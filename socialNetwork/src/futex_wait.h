#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_FUTEX_WAIT_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_FUTEX_WAIT_H_

#include <atomic>
#include <cerrno>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace social_network {

// Port of ghost-userspace's experiments/shared/thread_wait.cc kFutex path
// (itself built on lib/base.h's Futex::Wait/Wake) to a raw syscall(2), since
// this codebase doesn't link against ghost-userspace's lib. Single-slot
// version (one dispatcher, one worker) -- MarkRunnable/MarkIdle are plain
// atomic writes; WaitUntilRunnable is a real futex(2) sleep, so the worker
// genuinely blocks (visible to the kernel/ghOSt as TASK_BLOCKED) rather than
// spinning, unlike PrioTable's WaitUntilRunnable.
class FutexWait {
 public:
  void MarkRunnable() {
    runnable_.store(1, std::memory_order_release);
    Futex(FUTEX_WAKE, 1);
  }

  void MarkIdle() { runnable_.store(0, std::memory_order_release); }

  void WaitUntilRunnable() {
    while (true) {
      long rc = Futex(FUTEX_WAIT, 0);
      if (rc == 0) {
        if (runnable_.load(std::memory_order_acquire) != 0) return;
        // Spurious wakeup; go back to sleep.
      } else {
        if (errno == EAGAIN) return;  // Value already changed; no need to wait.
        // EINTR: retry. Anything else shouldn't happen; retry rather than
        // spin-loop-forever on an unexpected errno.
      }
    }
  }

 private:
  long Futex(int op, int val) {
    return syscall(SYS_futex, reinterpret_cast<int *>(&runnable_), op, val, nullptr);
  }

  std::atomic<int> runnable_{0};
};

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_FUTEX_WAIT_H_
