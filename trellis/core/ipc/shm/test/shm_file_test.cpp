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

#include "trellis/core/ipc/shm/shm_file.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace trellis::core::ipc::shm {

namespace {

size_t CountOpenFds() {
  size_t count = 0;
  for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    ++count;
  }
  return count;
}

std::string UniqueHandle(const std::string& suffix) {
  const std::string handle = "shm_file_test_" + std::to_string(::getpid()) + "_" + suffix;
  // A crashed prior run with a reused pid would leave the segment behind and fail the O_EXCL create
  ::shm_unlink(handle.c_str());
  return handle;
}

constexpr size_t kRequestedSize = 1024;

}  // namespace

TEST(ShmFile, OwnerClosesFdOnDestruction) {
  const trellis::core::Config config;
  const auto baseline = CountOpenFds();
  {
    ShmFile file(UniqueHandle("owner"), true, kRequestedSize, config);
    ASSERT_TRUE(file.IsInitialized());
    EXPECT_EQ(CountOpenFds(), baseline + 1);
  }
  EXPECT_EQ(CountOpenFds(), baseline);
}

TEST(ShmFile, ReaderClosesFdOnDestruction) {
  const trellis::core::Config config;
  const auto handle = UniqueHandle("reader");
  ShmFile owner(handle, true, kRequestedSize, config);
  const auto baseline = CountOpenFds();
  {
    ShmFile reader(handle, false, 0, config);
    ASSERT_TRUE(reader.IsInitialized());
    EXPECT_EQ(CountOpenFds(), baseline + 1);
  }
  EXPECT_EQ(CountOpenFds(), baseline);
}

TEST(ShmFile, MovedFromDoesNotCloseFd) {
  const trellis::core::Config config;
  const auto baseline = CountOpenFds();
  {
    auto file = std::make_unique<ShmFile>(UniqueHandle("move"), true, kRequestedSize, config);
    ShmFile moved{std::move(*file)};
    // The moved-from destructor must not close the fd now owned by the moved-to object
    file.reset();
    ASSERT_TRUE(moved.IsInitialized());
    EXPECT_EQ(CountOpenFds(), baseline + 1);
    // The fd must remain usable for remapping after the moved-from object is destroyed
    moved.Resize(kRequestedSize * 2);
    EXPECT_NE(moved.GetWriteInfo().data, nullptr);
  }
  EXPECT_EQ(CountOpenFds(), baseline);
}

TEST(ShmFile, ResizeShrinkThrows) {
  const trellis::core::Config config;
  ShmFile file(UniqueHandle("shrink"), true, kRequestedSize, config);
  ASSERT_TRUE(file.IsInitialized());
  EXPECT_THROW(file.Resize(kRequestedSize / 2), std::logic_error);
  // A rejected shrink must leave the existing mapping intact and usable
  auto write_info = file.GetWriteInfo();
  EXPECT_NE(write_info.data, nullptr);
  EXPECT_EQ(write_info.size, kRequestedSize);
}

TEST(ShmFile, ConstructionFailureReleasesFdAndSegment) {
  const trellis::core::Config config;
  const auto handle = UniqueHandle("ctor_fail");
  const auto baseline = CountOpenFds();
  // Too large to map, so construction fails after the segment is created and opened
  constexpr size_t kImplausibleSize = size_t{1} << 62;
  EXPECT_THROW(ShmFile(handle, true, kImplausibleSize, config), std::system_error);
  EXPECT_EQ(CountOpenFds(), baseline);
  EXPECT_FALSE(std::filesystem::exists("/dev/shm/" + handle));
}

TEST(ShmFile, MissingSegmentHoldsNoFd) {
  const trellis::core::Config config;
  const auto baseline = CountOpenFds();
  ShmFile reader(UniqueHandle("missing"), false, 0, config);
  EXPECT_FALSE(reader.IsInitialized());
  EXPECT_EQ(CountOpenFds(), baseline);
}

}  // namespace trellis::core::ipc::shm
