#ifndef SOCIAL_NETWORK_MICROSERVICES_UTILS_H
#define SOCIAL_NETWORK_MICROSERVICES_UTILS_H

#include <string>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

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

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_UTILS_H
