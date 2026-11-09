/*
 * Copyright (C) 2025 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "trellis/core/config.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/named_resource_registry.hpp"
#include "trellis/core/ipc/unix/socket_event.hpp"

namespace {
std::atomic<bool> fail_chmod{false};
}  // namespace

// Defined in the test binary, so it takes the place of libc's for the SocketEvent under test. A binary of its own keeps
// this away from every other test.
extern "C" int chmod(const char* path, mode_t mode) noexcept {
  if (fail_chmod) {
    errno = EACCES;
    return -1;
  }
  return ::fchmodat(AT_FDCWD, path, mode, 0);
}

namespace {

using trellis::core::ipc::NamedResourceRegistry;
using trellis::core::ipc::unix::SocketEvent;

// The reader's chmod runs after bind has created the socket file, and no destructor runs when the constructor throws,
// so the file used to be left behind.
TEST(SocketEventBindFailure, ReaderThatFailsAfterBindLeavesNoSocketFile) {
  const std::string path = "/tmp/test_socket_event_bind_failure_" + std::to_string(::getpid()) + ".sock";
  std::filesystem::remove(path);
  trellis::core::EventLoop loop;

  fail_chmod = true;
  EXPECT_THROW((SocketEvent{loop, /* reader = */ true, path, trellis::core::Config{}}), std::system_error);
  fail_chmod = false;

  EXPECT_FALSE(std::filesystem::exists(path)) << "the bound socket file was left behind";
  std::ofstream{path}.close();  // someone else's file at the path
  NamedResourceRegistry::Get().UnlinkAll();
  EXPECT_TRUE(std::filesystem::exists(path)) << "a constructor that failed left its path registered";
  std::filesystem::remove(path);
}

}  // namespace
