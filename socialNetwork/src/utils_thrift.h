#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstddef>
#include <cstdlib>
#include <atomic>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <string>
#include <nlohmann/json.hpp>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSSLSocket.h>
#include <thrift/transport/TSSLServerSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/TTransportException.h>

#include "futex_wait.h"
#include "ingress.h"
#include "perf_counter.h"
#include "shm_log.h"

namespace social_network{
using json = nlohmann::json;
using apache::thrift::transport::TServerSocket;
using apache::thrift::transport::TSSLServerSocket;
using apache::thrift::transport::TSSLSocketFactory;
using apache::thrift::transport::TFramedTransport;
using apache::thrift::transport::TFDTransport;
using apache::thrift::transport::TMemoryBuffer;
using apache::thrift::TProcessor;
using apache::thrift::protocol::TTransport;
using apache::thrift::protocol::TProtocol;
using apache::thrift::transport::TTransportException;

std::shared_ptr<TServerSocket> get_server_socket(const json &config_json, const std::string &address, int port) {
  bool ssl_enabled = config_json["ssl"]["enabled"];
  if (ssl_enabled) {
    std::string cert_path = config_json["ssl"]["serverCertPath"];
    std::string key_path = config_json["ssl"]["serverKeyPath"];
    std::string ca_path = config_json["ssl"]["caPath"];
    std::string ciphers = config_json["ssl"]["ciphers"];

    std::shared_ptr<TSSLSocketFactory> ssl_socket_factory;
    ssl_socket_factory = std::make_shared<TSSLSocketFactory>();
    ssl_socket_factory->loadCertificate(cert_path.c_str());
    ssl_socket_factory->loadPrivateKey(key_path.c_str());
    ssl_socket_factory->ciphers(ciphers);
    // if (config_json["ssl"]["verifyClient"]) {
    //   ssl_socket_factory->loadTrustedCertificates(ca_path.c_str());
    //   ssl_socket_factory->authenticate(true);
    // }
    return std::make_shared<TSSLServerSocket>(address, port, ssl_socket_factory);
  }
  return std::make_shared<TServerSocket>(address, port);
};

// NOTE: TFramedTransport's per-message framing overhead is expensive relative
// to the tiny requests these services process -- A/B'd against an unframed
// mmap+TMemoryBuffer(OBSERVE) read path on UniqueIdService (100k
// ComposeUniqueId requests, -O3, task-scoped `perf stat -p`): framing alone
// accounted for a ~2.8x increase in cycles (833M -> 320M cycles removing it)
// and dropped IPC from 2.15 to 1.40 -- the dominant factor (far more than the
// also-tested removal of the unused `carrier` map field, which only moved
// cycles ~3%). So the read path (out=false) below mmaps the trace file and
// wraps it in a TMemoryBuffer(OBSERVE) instead of TFDTransport+
// TFramedTransport: zero read() syscalls after the initial mmap() (TBinaryProtocol
// messages are self-delimiting via readMessageBegin/T_STOP, so no framing
// layer is needed once the whole file is already addressable memory), and no
// length-prefix framing cost either. This means `name` must point to an
// UNFRAMED trace file (see tools/gen_uniqueid_trace.cpp/gen_media_trace.cpp,
// which write unframed output directly, and deframe.py-style stripping for
// any pre-existing framed trace). The write path (out=true) is unchanged --
// TFramedTransport over a real fd -- since it's for producing new files, not
// the hot read loop this was about.
std::shared_ptr<TTransport> openFileTransport(const char* name, bool out) {
	if (out) {
		int fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IXUSR);
		if (-1 == fd) {
			LOG(error) << ("ERROR: Open/create for write failed!\n");
			return nullptr;
		}
		std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
		std::shared_ptr<TFramedTransport> transport(new TFramedTransport(file));
		return transport;
	}

	int fd = open(name, O_RDONLY);
	if (-1 == fd) {
		LOG(error) << ("ERROR: Open for read failed!\n");
		return nullptr;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		LOG(error) << ("ERROR: fstat on input trace file failed!\n");
		close(fd);
		return nullptr;
	}
	void *mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED) {
		LOG(error) << ("ERROR: mmap on input trace file failed!\n");
		close(fd);
		return nullptr;
	}
	close(fd);  // mapping stays valid after the fd is closed.
	std::shared_ptr<TMemoryBuffer> transport(new TMemoryBuffer(
	    reinterpret_cast<uint8_t *>(mapped), st.st_size, TMemoryBuffer::OBSERVE));
	return transport;
}

// Response output no longer touches disk during the request loop: replies go
// to an in-memory buffer (pre-sized to avoid reallocation mid-run) instead of
// a file, so per-request write() syscalls don't show up in IPC/cycle counts.
std::shared_ptr<TMemoryBuffer> openMemoryTransport(uint32_t size_hint = 32 * 1024 * 1024) {
	return std::make_shared<TMemoryBuffer>(size_hint);
}

// Dumps the memory buffer's accumulated bytes to `name` in one write, for
// after-the-fact correctness checks (e.g. `strings out-file | grep -c ...`).
// Call once, after serve() returns -- never from the request-handling loop.
void dumpMemoryTransportToFile(const std::shared_ptr<TMemoryBuffer> &mem, const char* name) {
	std::string data = mem->getBufferAsString();
	int fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IXUSR);
	if (-1 == fd) {
		LOG(error) << "could not open " << name << " to dump memory transport";
		return;
	}
	ssize_t written = write(fd, data.data(), data.size());
	if (written < 0 || static_cast<size_t>(written) != data.size()) {
		LOG(error) << "short/failed write dumping memory transport to " << name;
	}
	close(fd);
}

class TFileServer {
public:
	// `label` (e.g. "uid", "media") is only used for the shm timeline log
	// (see shm_log.h): if non-null, every request logs a "<label>_start" and
	// "<label>_end" event (SHM_LOG_NAME env var permitting; otherwise this
	// is all a no-op). Leave null to skip logging entirely.
	TFileServer(std::shared_ptr<TProcessor> processor, std::shared_ptr<TTransport> transportIn, std::shared_ptr<TProtocol> protocolIn, std::shared_ptr<TTransport> transportOut, std::shared_ptr<TProtocol> protocolOut, const char *label = nullptr) :
			 processor(processor),
			 transportIn(transportIn),
			 protocolIn(protocolIn),
			 transportOut(transportOut),
			 protocolOut(protocolOut)
			 	{
				if (label) {
					shm_log_ = MaybeOpenShmLog();
					label_ = std::string(label);
					label_start_ = label_ + "_start";
					label_end_ = label_ + "_end";
				}
			}
	void serve() {
		if (std::getenv("USE_DISPATCHER") != nullptr) {
			ServeWithDispatcher();
			return;
		}
		static const bool skip_yield = std::getenv("GHOST_SKIP_YIELD") != nullptr;
		// Bracket the WHOLE loop (one Read() before, one after), not each
		// individual request -- see perf_counter.h. A per-request bracket
		// (Read() around just processor->process()) was tried first and
		// dropped: it doubles the syscall count of a workload that's already
		// this syscall-cheap, and worse, injects a read() syscall right next
		// to the exact code region being measured on every single iteration,
		// perturbing cache/TLB state for the very thing under test. A
		// whole-loop bracket still isolates "just the worker loop actually
		// running" -- excludes setup (trace-file open, processor construction,
		// both above this point), the ghOSt agent thread, dispatch machinery,
		// and (cross-process/same-process) every other process/thread, since
		// this counter is self-scoped to the calling thread -- at the cost of
		// also including LogShmEvent (a no-op unless SHM_LOG_NAME is set) and
		// sched_yield() in the total, which per-request bracketing excluded.
		uint64_t instr_before = instr_counter_.Read();
		uint64_t cycles_before = cycle_counter_.Read();
		for (;;) {
				try {
					LogShmEvent(shm_log_, label_start_.c_str(), req_seq_);
					processor.get()->process(protocolIn, protocolOut, NULL);
					LogShmEvent(shm_log_, label_end_.c_str(), req_seq_);
					req_seq_++;
					if (!skip_yield) sched_yield();
				} catch (TTransportException& ttx) {
					if (ttx.getType() == TTransportException::TTransportExceptionType::END_OF_FILE) {
						LOG(info) << "ran out of data: " << ttx.what() << "\n";
						break;
					}
					LOG(error) << "breaking: " << ttx.what();
					break;
				}
		}
		instr_u_total_ = instr_counter_.Read() - instr_before;
		cycles_u_total_ = cycle_counter_.Read() - cycles_before;
		PrintInstructionStats();
	}
private:
	// Prints to stderr (not LOG(), so it's never suppressed by
	// QUIET_LOGGING -- same convention latency_stats.h's PrintLatencyStats
	// uses) regardless of which scenario this TFileServer is running under
	// (solo/cross-process/same-process all share this one serve() path).
	void PrintInstructionStats() {
		std::cerr << "=== instructions:u/cycles:u (whole worker loop"
		          << (label_.empty() ? "" : ", label=" + label_) << ") ===\n"
		          << "n=" << req_seq_
		          << " instructions_u_total=" << instr_u_total_
		          << " instructions_u_avg=" << (req_seq_ ? static_cast<double>(instr_u_total_) / req_seq_ : 0.0)
		          << " cycles_u_total=" << cycles_u_total_
		          << " cycles_u_avg=" << (req_seq_ ? static_cast<double>(cycles_u_total_) / req_seq_ : 0.0)
		          << "\n";
	}

	// USE_DISPATCHER (env var): instead of processing requests back-to-back,
	// spawn a second, CFS-pinned thread ("dispatcher") that runs a ported
	// version of ghost-userspace's Ingress Poisson-arrival generator
	// (experiments/rocksdb/ingress.h -- same algorithm, not the same repo/
	// build) and calls MarkRunnable() on each synthetic arrival. This thread
	// (the "worker", already ghOSt-enrolled by the time serve() runs) blocks
	// on a real futex (FutexWait, ported from experiments/shared/
	// thread_wait.cc's kFutex path) between requests instead of spinning or
	// running back-to-back -- a genuine TASK_BLOCKED/TASK_WAKEUP cycle per
	// request, visible to any ghOSt scheduler generically (no PrioTable
	// needed, unlike Shinjuku's MarkIdle/WaitUntilRunnable).
	//   THROUGHPUT (env var, default 10000): target requests/sec for Ingress.
	//   DISPATCHER_CPU (env var, default 3): CPU the dispatcher thread is
	//     pinned to. Keep this off whatever CPU(s) the ghOSt enclave owns.
	// NOTE: single-slot handoff, not a queue -- if Ingress signals a new
	// arrival before the worker has finished the previous request and called
	// MarkIdle() again, that arrival is coalesced (silently absorbed into the
	// next wakeup) rather than queued. Fine for sub-saturation throughputs;
	// would need real queueing (like GhostOrchestrator's WorkerWork/
	// num_requests) to be accurate above the worker's max service rate.
	struct DispatcherArgs {
		double throughput;
		int cpu;
		FutexWait *futex_wait;
		std::atomic<bool> *done;
		// Arrival logging (see shm_log.h): if shm_log is non-null, every
		// synthetic arrival Ingress generates logs one "<label>_arrival"
		// event BEFORE waking the worker -- distinct from the worker's own
		// "<label>_start"/"<label>_end" (serve()'s LogShmEvent calls below),
		// so a captured timeline can show "when did this request arrive"
		// separately from "when was it actually serviced". The gap between
		// the two is queueing/scheduling delay, not part of the arrival
		// process itself.
		ShmLogRegion *shm_log;
		std::string arrival_label;  // precomputed "<label>_arrival"
	};

	// pthread_create trampoline (not a lambda/std::thread): see the
	// PTHREAD_EXPLICIT_SCHED comment in ServeWithDispatcher() for why.
	static void *DispatcherThreadMain(void *arg) {
		DispatcherArgs *args = static_cast<DispatcherArgs *>(arg);

		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(args->cpu, &cpuset);
		pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

		Ingress ingress(args->throughput);
		ingress.Start();
		uint32_t arrival_seq = 0;
		while (!args->done->load(std::memory_order_acquire)) {
			if (ingress.HasNewArrival()) {
				LogShmEvent(args->shm_log, args->arrival_label.c_str(), arrival_seq++);
				args->futex_wait->MarkRunnable();
			}
		}
		return nullptr;
	}

	void ServeWithDispatcher() {
		const char *throughput_env = std::getenv("THROUGHPUT");
		double throughput = throughput_env ? std::atof(throughput_env) : 10000.0;
		const char *cpu_env = std::getenv("DISPATCHER_CPU");
		int dispatcher_cpu = cpu_env ? std::atoi(cpu_env) : 3;

		FutexWait futex_wait;
		std::atomic<bool> done{false};
		DispatcherArgs args{throughput, dispatcher_cpu, &futex_wait, &done,
		                    shm_log_, label_ + "_arrival"};

		// IMPORTANT: this is real pthread_create, not std::thread, because
		// POSIX's default (PTHREAD_INHERIT_SCHED) makes a new thread inherit
		// its *creator's* scheduling policy. Since this worker thread is
		// already self-enrolled into ghOSt (SCHED_GHOST) by the time serve()
		// runs, a std::thread-spawned dispatcher silently inherited
		// SCHED_GHOST too and landed on the same single-CPU enclave as the
		// worker -- with nothing to ever preempt it (a tight spin, no
		// syscalls), it starved the worker completely under plain FIFO,
		// hanging the whole process. PTHREAD_EXPLICIT_SCHED + SCHED_OTHER
		// forces the dispatcher to actually be what it's supposed to be: a
		// separate, CFS-scheduled thread pinned to its own CPU.
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
		struct sched_param param;
		param.sched_priority = 0;
		pthread_attr_setschedparam(&attr, &param);

		pthread_t dispatcher_thread;
		int rc = pthread_create(&dispatcher_thread, &attr, &DispatcherThreadMain, &args);
		pthread_attr_destroy(&attr);
		if (rc != 0) {
			LOG(error) << "could not create dispatcher thread, rc=" << rc;
			exit(EXIT_FAILURE);
		}

		// Same whole-loop bracket serve() uses (see the comment there) --
		// excludes dispatcher-thread setup above and the join/teardown below,
		// covers only the worker actually waiting for and processing
		// requests. Missing from this path until now: ServeWithDispatcher()
		// never populated instr_u_total_/cycles_u_total_ or called
		// PrintInstructionStats(), so USE_DISPATCHER runs silently never
		// printed the "instructions:u/cycles:u (whole worker loop, ...)"
		// line serve() always does.
		uint64_t instr_before = instr_counter_.Read();
		uint64_t cycles_before = cycle_counter_.Read();
		for (;;) {
			futex_wait.WaitUntilRunnable();
			try {
				LogShmEvent(shm_log_, label_start_.c_str(), req_seq_);
				processor.get()->process(protocolIn, protocolOut, NULL);
				LogShmEvent(shm_log_, label_end_.c_str(), req_seq_);
				req_seq_++;
				futex_wait.MarkIdle();
			} catch (TTransportException& ttx) {
				if (ttx.getType() == TTransportException::TTransportExceptionType::END_OF_FILE) {
					LOG(info) << "ran out of data: " << ttx.what() << "\n";
				} else {
					LOG(error) << "breaking: " << ttx.what();
				}
				break;
			}
		}
		instr_u_total_ = instr_counter_.Read() - instr_before;
		cycles_u_total_ = cycle_counter_.Read() - cycles_before;
		PrintInstructionStats();

		done.store(true, std::memory_order_release);
		pthread_join(dispatcher_thread, nullptr);
	}

		std::shared_ptr<TProcessor> processor;
		std::shared_ptr<TTransport> transportIn;
		std::shared_ptr<TProtocol> protocolIn;
		std::shared_ptr<TTransport> transportOut;
		std::shared_ptr<TProtocol> protocolOut;

		ShmLogRegion *shm_log_ = nullptr;
		std::string label_;
		std::string label_start_;
		std::string label_end_;
		uint32_t req_seq_ = 0;

		HardwareCounter instr_counter_{PERF_COUNT_HW_INSTRUCTIONS};
		HardwareCounter cycle_counter_{PERF_COUNT_HW_CPU_CYCLES};
		uint64_t instr_u_total_ = 0;
		uint64_t cycles_u_total_ = 0;
};

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
