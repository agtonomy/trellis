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

#include <fmt/core.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "trellis/core/ipc/shm/shm_file.hpp"
#include "trellis/core/ipc/shm/shm_writer.hpp"

namespace trellis::core::ipc::shm {
namespace {

constexpr size_t kInitialBufferSize = 256;

size_t PageSize() { return static_cast<size_t>(::sysconf(_SC_PAGESIZE)); }

/// @brief A payload that outgrows a slot mapped for `kInitialBufferSize`, forcing a resize.
///
/// Mappings are page granular, so such a slot also hands back the rest of its first page; spanning two pages exceeds
/// that capacity whatever the page size is.
std::string OversizedPayload(char fill) { return std::string(2 * PageSize(), fill); }

std::string SlotHandle(const std::string& prefix, size_t index) { return fmt::format("{}_{:03}", prefix, index); }

/// @brief Test fixture that owns a writer and lazily attaches reader-side `ShmFile`s to its slots.
class ShmGenerationTest : public ::testing::Test {
 protected:
  void CreateWriter(size_t num_buffers) {
    slots_.clear();
    writer_ = std::make_unique<ShmWriter>(::testing::UnitTest::GetInstance()->current_test_info()->name(), loop_,
                                          ::getpid(), num_buffers, kInitialBufferSize, config_);
  }

  ShmWriter& Writer() { return *writer_; }

  /// @brief A reader's view of one slot, attached on first use.
  ShmFile& Slot(size_t index) {
    auto it = slots_.find(index);
    if (it == slots_.end()) {
      it = slots_
               .emplace(index, std::make_unique<ShmFile>(SlotHandle(writer_->GetMemoryFilePrefix(), index),
                                                         /* owner = */ false, 0, config_))
               .first;
    }
    return *it->second;
  }

  uint64_t GenerationOf(size_t index) { return Slot(index).GetFileHeader().generation; }

  void Write(std::string_view bytes) {
    auto info = writer_->GetWriteAccess(bytes.size());
    ASSERT_NE(info.data, nullptr);
    ASSERT_GE(info.size, bytes.size());
    std::memcpy(info.data, bytes.data(), bytes.size());
    writer_->ReleaseWriteAccess(trellis::core::time::Now(), bytes.size(), /* success = */ true);
  }

  /// @brief Mimics a failed serialize: partially clobber a slot, then abandon it without publishing.
  void WriteAndAbandon(std::string_view garbage) {
    auto info = writer_->GetWriteAccess(garbage.size());
    ASSERT_NE(info.data, nullptr);
    std::memcpy(info.data, garbage.data(), garbage.size());
    writer_->ReleaseWriteAccess(trellis::core::time::Now(), /* bytes_written = */ 0, /* success = */ false);
  }

  trellis::core::EventLoop loop_;
  trellis::core::Config config_;
  std::unique_ptr<ShmWriter> writer_;
  std::unordered_map<size_t, std::unique_ptr<ShmFile>> slots_;
};

TEST_F(ShmGenerationTest, SuccessfulWriteAdvancesGenerationByTwo) {
  CreateWriter(/* num_buffers = */ 1);
  EXPECT_EQ(GenerationOf(0), 0U);

  Write("payload");

  EXPECT_EQ(GenerationOf(0), 2U);
}

TEST_F(ShmGenerationTest, GenerationIsOddWhileWriteIsInProgress) {
  CreateWriter(/* num_buffers = */ 1);
  Write("first");
  const auto at_rest = GenerationOf(0);
  ASSERT_EQ(at_rest % 2, 0U);

  const auto info = Writer().GetWriteAccess(kInitialBufferSize);
  ASSERT_NE(info.data, nullptr);
  EXPECT_EQ(GenerationOf(0), at_rest + 1);
  EXPECT_EQ(GenerationOf(0) % 2, 1U);

  Writer().ReleaseWriteAccess(trellis::core::time::Now(), /* bytes_written = */ 5, /* success = */ true);
  EXPECT_EQ(GenerationOf(0), at_rest + 2);
}

TEST_F(ShmGenerationTest, AbandonedWriteAdvancesGenerationWithoutPublishing) {
  CreateWriter(/* num_buffers = */ 1);
  Write("original");
  const auto after_write = Slot(0).GetFileHeader();

  WriteAndAbandon("clobbered by a partial serialize");

  const auto after_abandon = Slot(0).GetFileHeader();
  EXPECT_EQ(after_abandon.generation, after_write.generation + 2);
  EXPECT_EQ(after_abandon.generation % 2, 0U);
  // The abandoned write publishes nothing, so the committed metadata is untouched even though the bytes are not
  EXPECT_EQ(after_abandon.sequence, after_write.sequence);
  EXPECT_EQ(after_abandon.data_size, after_write.data_size);
}

TEST_F(ShmGenerationTest, GenerationSurvivesGrowingTheSlot) {
  CreateWriter(/* num_buffers = */ 1);
  Write("small");
  const auto at_rest = GenerationOf(0);
  ASSERT_EQ(at_rest % 2, 0U);

  // Outgrowing the slot's mapping forces GetWriteAccess through Resize (ftruncate + fresh mmap) before it opens the
  // seqlock window; the counter must carry over rather than reset
  const std::string big = OversizedPayload('x');
  Write(big);

  EXPECT_EQ(GenerationOf(0), at_rest + 2);
  EXPECT_EQ(Slot(0).GetFileHeader().data_size, big.size());
}

TEST_F(ShmGenerationTest, AbandonThenRetryReusesTheSlotAndKeepsParityEven) {
  CreateWriter(/* num_buffers = */ 2);
  Write("first");
  const auto slot_zero = GenerationOf(0);
  const auto slot_one = GenerationOf(1);

  // An abandoned write leaves the ring index in place, so the next send re-acquires the same slot at a larger size
  WriteAndAbandon("partial serialize");
  const std::string retry = OversizedPayload('y');
  Write(retry);

  EXPECT_EQ(GenerationOf(1), slot_one + 4);
  EXPECT_EQ(GenerationOf(1) % 2, 0U);
  EXPECT_EQ(Slot(1).GetFileHeader().data_size, retry.size());
  // The abandoned write must not advance the ring: the retry lands on the slot it clobbered
  EXPECT_EQ(GenerationOf(0), slot_zero);
}

TEST_F(ShmGenerationTest, WriteOnlyAdvancesTheSlotItTouches) {
  constexpr size_t kNumBuffers = 3;
  CreateWriter(kNumBuffers);
  Write("slot zero");
  const auto slot_zero = GenerationOf(0);

  for (size_t i = 1; i < kNumBuffers; ++i) {
    Write("another slot");
    EXPECT_EQ(GenerationOf(0), slot_zero);
  }

  // Wrapping the ring back around to slot zero does advance it
  Write("laps back to slot zero");
  EXPECT_EQ(GenerationOf(0), slot_zero + 2);
}

}  // namespace
}  // namespace trellis::core::ipc::shm
