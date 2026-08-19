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

#include "trellis/core/ipc/shm/shm_writer.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstddef>
#include <system_error>

#include "trellis/core/config.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/time.hpp"

namespace trellis::core::ipc::shm {

namespace {

constexpr size_t kBufferSize = 1024;

// Large enough that ftruncate or mmap fails, so Resize throws while the write lock is held.
constexpr size_t kUnmappableSize = size_t{1} << 62;

void* AcquireAndRelease(ShmWriter& writer) {
  void* data = nullptr;
  EXPECT_NO_THROW({
    const auto write_info = writer.GetWriteAccess(kBufferSize);
    data = write_info.data;
    writer.ReleaseWriteAccess(trellis::core::time::Now(), /* bytes_written = */ 1, /* success = */ true);
  });
  return data;
}

}  // namespace

TEST(ShmWriter, ResizeFailureReleasesWriteLockAndRethrows) {
  const trellis::core::Config config;
  trellis::core::EventLoop loop;
  ShmWriter writer{"shm_writer_test", loop, ::getpid(), /* num_buffers = */ 1, kBufferSize, config};

  EXPECT_THROW(writer.GetWriteAccess(kUnmappableSize), std::system_error);

  EXPECT_NE(AcquireAndRelease(writer), nullptr);
}

TEST(ShmWriter, ResizeFailureRetiresNoBuffer) {
  const trellis::core::Config config;
  trellis::core::EventLoop loop;
  constexpr size_t kNumBuffers = 2;
  ShmWriter writer{"shm_writer_test", loop, ::getpid(), kNumBuffers, kBufferSize, config};

  EXPECT_THROW(writer.GetWriteAccess(kUnmappableSize), std::system_error);

  // Distinct mappings prove the round robin visited every buffer; a still-locked one would be skipped and the
  // mappings would repeat.
  void* first = nullptr;
  for (size_t i = 0; i < kNumBuffers; ++i) {
    void* const data = AcquireAndRelease(writer);
    EXPECT_NE(data, nullptr);
    if (i == 0) {
      first = data;
    } else {
      EXPECT_NE(data, first);
    }
  }

  EXPECT_EQ(AcquireAndRelease(writer), first);
}

}  // namespace trellis::core::ipc::shm
