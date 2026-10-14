#include <fcntl.h>
#include <fmt/core.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "trellis/core/config.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/shm/shm_writer.hpp"

namespace trellis::core::ipc::shm {
namespace {

void CreateShmObject(const std::string& name) {
  const int fd = ::shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
  ASSERT_GE(fd, 0) << name;
  ::close(fd);
}

bool Exists(const std::string& name) { return std::filesystem::exists(std::filesystem::path{"/dev/shm"} / name); }

pid_t ExitedPid() {
  const pid_t child = ::fork();
  if (child == 0) {
    ::_exit(0);
  }
  ::waitpid(child, nullptr, 0);
  return child;
}

}  // namespace

// The cleanup runs once per process, before its first writer, so this binary holds this one test.
TEST(ShmWriterCleanup, RemovesOnlyObjectsOfExitedWriters) {
  const std::string node = fmt::format("cleanup_{}", ::getpid());
  const pid_t exited = ExitedPid();
  const std::vector<std::string> removed = {
      fmt::format("trellis_{}_{}_1_000", node, exited),
      fmt::format("trellis_{}_{}_1_000_mtx", node, exited),
      // This process has no writer yet, so its pid on an object means an earlier process with the same pid left it
      fmt::format("trellis_{}_{}_1_000", node, ::getpid()),
  };
  const std::vector<std::string> kept = {
      fmt::format("trellis_{}_{}_1_000", node, ::getppid()),
      fmt::format("trellis_{}_{}_1_000_mtx", node, ::getppid()),
      // An exited writer of a node named <node>_1
      fmt::format("trellis_{}_1_{}_1_000", node, exited),
  };
  for (const auto& name : removed) {
    CreateShmObject(name);
  }
  for (const auto& name : kept) {
    CreateShmObject(name);
  }

  const trellis::core::Config config;
  trellis::core::EventLoop loop;
  const ShmWriter writer{node, loop, ::getpid(), /* num_buffers = */ 1, /* buffer_size = */ 64, config};

  for (const auto& name : removed) {
    EXPECT_FALSE(Exists(name)) << name;
  }
  for (const auto& name : kept) {
    EXPECT_TRUE(Exists(name)) << name;
    ::shm_unlink(name.c_str());
  }
}

}  // namespace trellis::core::ipc::shm
