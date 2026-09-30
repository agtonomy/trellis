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
#include <unistd.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "trellis/core/config.hpp"

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

}  // namespace trellis::core::ipc::shm
