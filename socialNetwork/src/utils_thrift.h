#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_

#include <iostream>
#include <cstring>
#include <fcntl.h>
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
// to the tiny requests these services process. A/B'd against an unframed
// TBufferedTransport read path on UniqueIdService (100k ComposeUniqueId
// requests, -O3, task-scoped `perf stat -p`): framing alone accounted for a
// ~2.8x increase in cycles (833M -> 320M cycles removing it) and dropped IPC
// from 2.15 to 1.40 -- the dominant factor (far more than the also-tested
// removal of the unused `carrier` map field, which only moved cycles ~3%).
// Left as TFramedTransport for now since that's what's actually deployed;
// worth revisiting if per-request overhead matters more than wire framing.
std::shared_ptr<TFramedTransport>  openFileTransport(const char* name, bool out) {
	int fd;
	if (out) {
		fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IXUSR);
	} else {
		fd = open(name, O_RDONLY);
	}
	if (-1 == fd)
	{
		LOG(error) << ("ERROR: Open/create for write failed!\n");
		return nullptr;
	}

	std::shared_ptr<TFDTransport> file(new TFDTransport(fd));
	std::shared_ptr<TFramedTransport> transport(new TFramedTransport(file));
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
	TFileServer(std::shared_ptr<TProcessor> processor, std::shared_ptr<TTransport> transportIn, std::shared_ptr<TProtocol> protocolIn, std::shared_ptr<TTransport> transportOut, std::shared_ptr<TProtocol> protocolOut) :
			 processor(processor), 
			 transportIn(transportIn),
			 protocolIn(protocolIn),
			 transportOut(transportOut),
			 protocolOut(protocolOut)
			 	{ }
	void serve() {
		if (std::getenv("USE_DISPATCHER") != nullptr) {
			ServeWithDispatcher();
			return;
		}
		static const bool skip_yield = std::getenv("GHOST_SKIP_YIELD") != nullptr;
		for (;;) {
				try {
					processor.get()->process(protocolIn, protocolOut, NULL);
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
	}
private:
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
		while (!args->done->load(std::memory_order_acquire)) {
			if (ingress.HasNewArrival()) {
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
		DispatcherArgs args{throughput, dispatcher_cpu, &futex_wait, &done};

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

		for (;;) {
			futex_wait.WaitUntilRunnable();
			try {
				processor.get()->process(protocolIn, protocolOut, NULL);
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
		done.store(true, std::memory_order_release);
		pthread_join(dispatcher_thread, nullptr);
	}

		std::shared_ptr<TProcessor> processor;
		std::shared_ptr<TTransport> transportIn;
		std::shared_ptr<TProtocol> protocolIn;
		std::shared_ptr<TTransport> transportOut;
		std::shared_ptr<TProtocol> protocolOut;
};

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_SRC_UTILS_THRIFT_H_
