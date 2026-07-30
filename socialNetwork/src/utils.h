#ifndef SOCIAL_NETWORK_MICROSERVICES_UTILS_H
#define SOCIAL_NETWORK_MICROSERVICES_UTILS_H

#include <string>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

#include "logger.h"

namespace social_network{
using json = nlohmann::json;

int load_config_file(const std::string &file_name, json *config_json) {
  std::ifstream json_file;
  json_file.open(file_name);
  if (json_file.is_open()) {
    json_file >> *config_json;
    json_file.close();
    return 0;
  }
  else {
    LOG(error) << "Cannot open service-config.json";
    return -1;
  }
};

// Self-enrolls the calling thread into a ghOSt enclave by writing "0" (self)
// to the enclave's "tasks" pseudo-file, if the GHOST_ENCLAVE_TASKS
// environment variable is set to that file's path. This must happen before
// any real work starts, since it's racy to enroll a thread from an external
// process after the fact if that thread might finish before the external
// enroller gets scheduled.
void MaybeJoinGhostEnclave() {
  const char *tasks_path = std::getenv("GHOST_ENCLAVE_TASKS");
  if (!tasks_path) return;

  int fd = open(tasks_path, O_WRONLY);
  if (fd < 0) {
    LOG(error) << "could not open ghost enclave tasks file " << tasks_path;
    return;
  }
  const char *self = "0";
  if (write(fd, self, 1) != 1) {
    LOG(error) << "could not enroll self into ghost enclave " << tasks_path;
  } else {
    LOG(info) << "enrolled into ghost enclave " << tasks_path;
  }
  close(fd);
}

// Lets an external `perf stat -t <tid1>,<tid2> --per-thread` (or -p <pid>)
// attach before real request processing starts, without racing it: prints
// this thread/process's TID and sleeps for STARTUP_DELAY_MS before
// returning, giving a reliable window to read the TID off stdout and attach
// perf while nothing's happening yet. Call this AFTER MaybeJoinGhostEnclave()
// so the announced TID is already a ghost task, matching steady-state
// conditions. A no-op if STARTUP_DELAY_MS isn't set (the default), so this
// is always safe to leave in a hot path.
void MaybeAnnounceAndDelay(const char *label) {
  const char *delay_env = std::getenv("STARTUP_DELAY_MS");
  if (!delay_env) return;
  int delay_ms = std::atoi(delay_env);
  printf("[startup_delay] %s tid=%ld\n", label, static_cast<long>(syscall(SYS_gettid)));
  fflush(stdout);
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
}

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_UTILS_H
