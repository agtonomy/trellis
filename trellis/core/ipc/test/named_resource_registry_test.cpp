/*
 * Copyright (C) 2026 Agtonomy
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

#include "trellis/core/ipc/named_resource_registry.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace trellis::core::ipc {
namespace {

struct TerminateOnDestruction {
  ~TerminateOnDestruction() { std::terminate(); }
};

// A file and a shared memory object, created here so the death test's child can register them and this process can
// check that they are gone once it dies. The child reruns the test from the start, so both names must come out the
// same in it: the shared memory name is keyed on the test's temporary directory, which is unique to the run, not on a
// pid.
struct TrackedResources {
  explicit TrackedResources(const std::string& test)
      : path{::testing::TempDir() + "named_resource_registry_" + test},
        shm{"/trellis_named_resource_registry_" + test + "_" +
            std::to_string(std::hash<std::string>{}(::testing::TempDir()))} {
    std::ofstream{path}.close();
    const int fd = ::shm_open(shm.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd >= 0) {
      ::close(fd);
    }
  }
  ~TrackedResources() {
    std::filesystem::remove(path);
    ::shm_unlink(shm.c_str());
  }
  void Insert() const {
    NamedResourceRegistry::Get().Insert(path);
    NamedResourceRegistry::Get().InsertShm(shm);
  }
  bool FileExists() const { return std::filesystem::exists(path); }
  bool ShmExists() const { return std::filesystem::exists("/dev/shm" + shm); }

  const std::string path;
  const std::string shm;
};

// A terminate during static destruction ran the handler on the registry's destroyed queues.
TEST(NamedResourceRegistryDeathTest, TerminateDuringStaticDestructionStillUnlinks) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");  // the child starts fresh, so the registry is built inside it
  const TrackedResources resources{"terminate"};
  ASSERT_TRUE(resources.FileExists());
  ASSERT_TRUE(resources.ShmExists());

  EXPECT_EXIT(
      {
        static TerminateOnDestruction late;  // built before the registry, so destroyed after it
        resources.Insert();
        std::exit(0);
      },
      ::testing::KilledBySignal(SIGABRT), "");
  EXPECT_FALSE(resources.FileExists()) << "the terminate handler did not unlink the tracked file";
  EXPECT_FALSE(resources.ShmExists()) << "the terminate handler did not unlink the tracked shared memory";
}

// The registry's handler cleans up first, then hands over to the handler it replaced, which decides how to die.
TEST(NamedResourceRegistryDeathTest, TerminateRunsTheHandlerItReplacedAfterUnlinking) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  constexpr int kPreviousHandlerExitCode = 42;
  const TrackedResources resources{"previous_handler"};

  EXPECT_EXIT(
      {
        std::set_terminate([]() { std::_Exit(kPreviousHandlerExitCode); });  // set before the registry replaces it
        resources.Insert();
        std::terminate();
      },
      ::testing::ExitedWithCode(kPreviousHandlerExitCode), "");
  EXPECT_FALSE(resources.FileExists()) << "the previous handler ran before the tracked file was unlinked";
  EXPECT_FALSE(resources.ShmExists()) << "the previous handler ran before the tracked shared memory was unlinked";
}

// Resources are inserted on whichever thread builds them, while UnlinkAll() runs on the loop. Without the lock, one
// inserter raced UnlinkAll() in only some runs; several also race each other, which fails every run.
TEST(NamedResourceRegistry, InsertAndUnlinkAllMayRunConcurrently) {
  constexpr int kInserters = 4;
  constexpr int kInsertsPerThread = 20000;
  auto& registry = NamedResourceRegistry::Get();
  std::atomic<int> running{kInserters};
  std::vector<std::thread> inserters;
  for (int t = 0; t < kInserters; ++t) {
    inserters.emplace_back([&registry, &running, t]() {
      for (int i = 0; i < kInsertsPerThread; ++i) {
        registry.Insert("/nonexistent/trellis_named_resource_registry_test_" + std::to_string(t) + "_" +
                        std::to_string(i));
      }
      --running;
    });
  }
  while (running > 0) {
    registry.UnlinkAll();
  }
  for (auto& inserter : inserters) {
    inserter.join();
  }
  registry.UnlinkAll();
}

// An owner that unlinks its resource itself removes the name, and UnlinkAll() then leaves that path alone (another
// object may have it by then) while still unlinking what is still tracked.
TEST(NamedResourceRegistry, UnlinkAllSkipsANameItsOwnerRemoved) {
  const TrackedResources removed{"removed"};
  const TrackedResources kept{"kept"};
  removed.Insert();
  kept.Insert();
  auto& registry = NamedResourceRegistry::Get();
  registry.Remove(removed.path);
  registry.RemoveShm(removed.shm);
  registry.UnlinkAll();
  EXPECT_TRUE(removed.FileExists());
  EXPECT_TRUE(removed.ShmExists());
  EXPECT_FALSE(kept.FileExists());
  EXPECT_FALSE(kept.ShmExists());
}

TEST(NamedResourceRegistry, RemoveForgetsOneRegistrationOfAName) {
  const TrackedResources twice{"twice"};
  auto& registry = NamedResourceRegistry::Get();
  registry.Insert(twice.path);
  registry.Insert(twice.path);
  registry.Remove(twice.path);
  registry.UnlinkAll();
  EXPECT_FALSE(twice.FileExists());
}

}  // namespace
}  // namespace trellis::core::ipc
