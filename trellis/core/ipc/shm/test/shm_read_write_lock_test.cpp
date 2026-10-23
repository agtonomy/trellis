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

#include "trellis/core/ipc/shm/shm_read_write_lock.hpp"

#include <gtest/gtest.h>
#include <sys/resource.h>
#include <unistd.h>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "trellis/core/config.hpp"
#include "trellis/core/ipc/named_resource_registry.hpp"

namespace trellis::core::ipc::shm {

namespace {

// Carries this test's pid so parallel runs don't share a lock.
std::string LockName(std::string_view tag) {
  return "trellis_rwlocktest_" + std::to_string(::getpid()) + "_" + std::string{tag} + "_mtx";
}

bool Exists(const std::string& name) { return std::filesystem::exists("/dev/shm/" + name); }

}  // namespace

// A reader whose writer's lock is gone used to create a fresh one. It then locked a mutex the writer never used, and,
// not being the owner, left the file behind.
TEST(ShmReadWriteLock, NonOwnerDoesNotCreateAMissingLock) {
  const auto name = LockName("missing");
  {
    ShmReadWriteLock reader_lock{name, /* owner = */ false, Config{}};
    EXPECT_FALSE(reader_lock.IsInitialized());
  }
  EXPECT_FALSE(Exists(name));
}

TEST(ShmReadWriteLock, NonOwnerSharesTheOwnersLockAndLeavesItToTheOwner) {
  const auto name = LockName("shared");
  std::optional<ShmReadWriteLock> writer_lock{std::in_place, name, /* owner = */ true, Config{}};
  ASSERT_TRUE(writer_lock->IsInitialized());
  {
    ShmReadWriteLock reader_lock{name, /* owner = */ false, Config{}};
    ASSERT_TRUE(reader_lock.IsInitialized());

    ASSERT_TRUE(writer_lock->LockWrite());
    EXPECT_FALSE(reader_lock.TryLockRead()) << "the reader did not see the writer's lock";
    ASSERT_TRUE(writer_lock->Unlock());
    ASSERT_TRUE(reader_lock.TryLockRead());
    ASSERT_TRUE(reader_lock.Unlock());
  }
  EXPECT_TRUE(Exists(name)) << "the reader unlinked a lock it does not own";

  writer_lock.reset();
  EXPECT_FALSE(Exists(name));
}

// The owner creates the object before sizing it, and a throw leaves no ShmReadWriteLock to unlink it. A file size
// limit of 0 makes the sizing fail.
TEST(ShmReadWriteLock, OwnerThatFailsToCreateTheLockLeavesNoObject) {
  const auto name = LockName("unsized");
  rlimit saved{};
  ASSERT_EQ(::getrlimit(RLIMIT_FSIZE, &saved), 0);
  const auto old_handler = std::signal(SIGXFSZ, SIG_IGN);  // the failed ftruncate also raises it
  rlimit none = saved;
  none.rlim_cur = 0;
  ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &none), 0);
  bool threw = false;
  try {
    ShmReadWriteLock writer_lock{name, /* owner = */ true, Config{}};
  } catch (const std::system_error&) {
    threw = true;
  }
  ::setrlimit(RLIMIT_FSIZE, &saved);
  std::signal(SIGXFSZ, old_handler);

  EXPECT_TRUE(threw);
  EXPECT_FALSE(Exists(name)) << "the half-created lock was left behind";
}

// Once the owner has unlinked its lock, the name may belong to someone else, so UnlinkAll() must leave it alone.
TEST(ShmReadWriteLock, OwnerForgetsItsNameWhenDestroyed) {
  const auto name = LockName("forget");
  { const ShmReadWriteLock lock{name, /* owner = */ true, Config{}}; }
  std::ofstream{"/dev/shm/" + name}.close();  // a new lock under the released name
  NamedResourceRegistry::Get().UnlinkAll();
  EXPECT_TRUE(Exists(name)) << "UnlinkAll() unlinked a name its owner had released";
  ::shm_unlink(name.c_str());
}

}  // namespace trellis::core::ipc::shm
