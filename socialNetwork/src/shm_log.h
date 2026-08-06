#ifndef SOCIAL_NETWORK_MICROSERVICES_SHM_LOG_H
#define SOCIAL_NETWORK_MICROSERVICES_SHM_LOG_H

// A minimal shared-memory event log: UniqueIdService/MediaService/
// CombinedService (the "producers") each append one record per request
// start/end, and a separate Logger process (the "consumer", tools/Logger)
// reads them live and prints a timeline -- which core each request actually
// ran on and when, independent of and external to the services themselves.
// This is a verification tool: it doesn't participate in scheduling or
// request handling, just observes it.
//
// Layout: a fixed-capacity array of fixed-size records plus one atomic
// write cursor, all in one mmap'd region. Producers only ever
// fetch_add-and-write their own slot (never read, never coordinate with
// each other), so this works across multiple processes with no locking:
// std::atomic<uint32_t> in shared memory is lock-free on x86_64, and two
// processes never write the same slot since fetch_add is atomic across
// them too. The Logger owns creation/sizing/cleanup (shm_open with
// O_CREAT); producers only ever open an existing region by name.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace social_network {

struct ShmLogRecord {
  uint64_t timestamp_ns;  // CLOCK_MONOTONIC -- comparable across processes
                           // on the same host, immune to wall-clock jumps.
  int32_t pid;
  int32_t tid;
  int32_t cpu;   // sched_getcpu() at the moment of the call
  uint32_t seq;  // caller-assigned (e.g. request index)
  char label[16];  // e.g. "uid_start", "uid_end", "media_start", "media_end"
  // Published (via __atomic_store_n(..., __ATOMIC_RELEASE)) as the LAST
  // write once every field above is filled in; readers must
  // __atomic_load_n(..., __ATOMIC_ACQUIRE) this and see 1 before trusting
  // the rest of the record -- see the NOTE in LogShmEvent. Plain uint32_t
  // (not std::atomic<uint32_t>) so ShmLogRecord stays a POD/trivially
  // copyable type, since callers store it by value in std::vector<ShmLogRecord>.
  uint32_t ready;
};

struct ShmLogRegion {
  std::atomic<uint32_t> next_index;
  uint32_t capacity;
  ShmLogRecord records[];  // flexible array member -- region is sized to
                            // sizeof(ShmLogRegion) + capacity * sizeof(ShmLogRecord)
};

inline size_t ShmLogRegionSize(uint32_t capacity) {
  return sizeof(ShmLogRegion) + static_cast<size_t>(capacity) * sizeof(ShmLogRecord);
}

// Opens (never creates) an existing shared memory log region named by the
// SHM_LOG_NAME env var. Returns nullptr if SHM_LOG_NAME isn't set (the
// default -- always safe to call from a hot path) or the region doesn't
// exist yet. Capacity is read from the region itself, written by whichever
// process created it (see tools/Logger.cpp).
inline ShmLogRegion *MaybeOpenShmLog() {
  const char *name = std::getenv("SHM_LOG_NAME");
  if (!name) return nullptr;

  int fd = shm_open(name, O_RDWR, 0666);
  if (fd < 0) return nullptr;

  // Peek the header first to learn the real capacity/size before mapping
  // the whole thing -- the creator may have sized it larger or smaller than
  // any particular producer would guess.
  void *header = mmap(nullptr, sizeof(ShmLogRegion), PROT_READ, MAP_SHARED, fd, 0);
  if (header == MAP_FAILED) {
    close(fd);
    return nullptr;
  }
  uint32_t capacity = reinterpret_cast<ShmLogRegion *>(header)->capacity;
  munmap(header, sizeof(ShmLogRegion));

  void *addr = mmap(nullptr, ShmLogRegionSize(capacity), PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
  close(fd);
  if (addr == MAP_FAILED) return nullptr;
  return reinterpret_cast<ShmLogRegion *>(addr);
}

// Appends one record. A no-op if region is null (SHM_LOG_NAME unset) or the
// ring is full (drops rather than wraps/overwrites, so a slow consumer
// never corrupts data the producer thinks it already wrote).
//
// NOTE on the ready flag: fetch_add reserves a slot, but reserving isn't
// the same as the slot being safe to read -- a reader on a different core
// (this is always a cross-process reader) can observe the bumped
// next_index and start reading records[idx] before this function has
// finished writing its fields. An earlier version had no ready flag and
// used next_index itself as the "how much is safe to read" bound; it
// produced occasional torn reads (e.g. a fully-written pid/seq paired with
// a still-zero timestamp_ns), which showed up as an absurd
// (timestamp_ns_end - timestamp_ns_start) duration wrapping around as
// unsigned when the "start" side was the torn one. Every field is written
// here BEFORE the release-store to `ready`, so a reader that observes
// ready==1 via an acquire-load is guaranteed to see all of them.
inline void LogShmEvent(ShmLogRegion *region, const char *label, uint32_t seq) {
  if (!region) return;
  uint32_t idx = region->next_index.fetch_add(1, std::memory_order_relaxed);
  if (idx >= region->capacity) return;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  ShmLogRecord &rec = region->records[idx];
  rec.pid = getpid();
  rec.tid = static_cast<int32_t>(syscall(SYS_gettid));
  rec.cpu = sched_getcpu();
  rec.seq = seq;
  std::strncpy(rec.label, label, sizeof(rec.label) - 1);
  rec.label[sizeof(rec.label) - 1] = '\0';
  rec.timestamp_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
  __atomic_store_n(&rec.ready, 1u, __ATOMIC_RELEASE);
}

// --- Consumer side (shared by tools/Logger and tools/ScheduleLogger) ---
//
// The consumer owns the region: it creates it (shm_open with O_CREAT),
// collects records as producers append them, then unlinks it. Producers
// only ever open an existing region by name (MaybeOpenShmLog, above).

// Creates a fresh shared memory log region named `name` with room for
// `capacity` records, defensively unlinking any stale region left behind by
// a crashed prior run under the same name first. Aborts (returns nullptr)
// if the name is already in use by a live region.
inline ShmLogRegion *CreateShmLog(const char *name, uint32_t capacity) {
  shm_unlink(name);

  int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
  if (fd < 0) return nullptr;

  size_t region_size = ShmLogRegionSize(capacity);
  if (ftruncate(fd, static_cast<off_t>(region_size)) != 0) {
    close(fd);
    return nullptr;
  }
  void *addr = mmap(nullptr, region_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (addr == MAP_FAILED) return nullptr;

  ShmLogRegion *region = reinterpret_cast<ShmLogRegion *>(addr);
  region->next_index.store(0, std::memory_order_relaxed);
  region->capacity = capacity;
  return region;
}

// Polls `region` until `expected_events` records have been written or
// nothing new has shown up for `idle_timeout_sec` (whichever comes first --
// the latter is a safety net against a producer that crashed or never
// enrolled, so this can't hang forever), then returns everything collected
// so far in write order (not yet time-sorted -- callers that care about
// wall-clock order should sort by timestamp_ns themselves, since producers
// in different processes can interleave their fetch_adds out of timestamp
// order under contention).
//
// next_index is only used here as an upper bound on which slots have been
// RESERVED, not which are safe to read -- each slot's own `ready` flag
// (acquire-loaded, paired with LogShmEvent's release-store) is what
// actually gates inclusion. seen only advances over a contiguous run of
// ready slots, so a slot that's reserved but not yet fully written just
// pauses collection at that index until it becomes ready, rather than
// racing ahead and reading torn data.
inline std::vector<ShmLogRecord> CollectShmLogEvents(ShmLogRegion *region,
                                                       uint32_t expected_events,
                                                       double idle_timeout_sec) {
  std::vector<ShmLogRecord> records;
  uint32_t seen = 0;
  auto last_progress = std::chrono::steady_clock::now();
  while (seen < expected_events) {
    uint32_t reserved = region->next_index.load(std::memory_order_relaxed);
    if (reserved > region->capacity) reserved = region->capacity;

    bool progressed = false;
    while (seen < reserved) {
      ShmLogRecord &rec = region->records[seen];
      if (__atomic_load_n(&rec.ready, __ATOMIC_ACQUIRE) != 1) break;
      records.push_back(rec);
      seen++;
      progressed = true;
    }

    if (progressed) {
      last_progress = std::chrono::steady_clock::now();
    } else {
      double idle = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                    last_progress)
                        .count();
      if (idle > idle_timeout_sec) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  return records;
}

inline void DestroyShmLog(const char *name, ShmLogRegion *region, uint32_t capacity) {
  if (region) munmap(region, ShmLogRegionSize(capacity));
  shm_unlink(name);
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SHM_LOG_H
