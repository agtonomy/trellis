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
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "trellis/core/ipc/borrowed_payload.hpp"
#include "trellis/core/ipc/shm/shm_read_write_lock.hpp"
#include "trellis/core/ipc/shm/shm_reader.hpp"
#include "trellis/core/ipc/shm/shm_writer.hpp"

namespace trellis::core::ipc::shm {
namespace {

constexpr size_t kInitialBufferSize = 256;

std::string SlotHandle(const std::string& prefix, size_t index) { return fmt::format("{}_{:03}", prefix, index); }

/// @brief The reader half of a single shared memory slot: takes the read lock just long enough to snapshot a borrow.
class SlotReader {
 public:
  SlotReader(const std::string& handle, const trellis::core::Config& config)
      : file_{handle, /* owner = */ false, 0, config},
        lock_{ShmReadWriteLock::GenerateLockName(handle), /* owner = */ false, config} {}

  ShmFile::BorrowInfo Info() {
    if (!lock_.TryLockRead()) {
      throw std::runtime_error("SlotReader failed to take the read lock");
    }
    ShmReadWriteLock::UnlockGuard guard{lock_};
    return file_.Borrow();
  }

  /// @brief Deliberately skips the read lock, which is what a caller violating the borrow precondition looks like.
  ShmFile::BorrowInfo InfoWithoutLock() { return file_.Borrow(); }

  /// @return An empty optional if the writer currently holds this slot.
  std::optional<BorrowedPayload> TryBorrow() {
    if (!lock_.TryLockRead()) {
      return std::nullopt;
    }
    ShmReadWriteLock::UnlockGuard guard{lock_};
    return file_.Borrow().payload;
  }

  BorrowedPayload Borrow() {
    auto payload = TryBorrow();
    if (!payload.has_value()) {
      throw std::runtime_error("SlotReader failed to take the read lock");
    }
    return std::move(*payload);
  }

 private:
  ShmFile file_;
  ShmReadWriteLock lock_;
};

/// @brief Test fixture that owns a writer and lazily attaches readers to its slots.
class ShmBorrowTest : public ::testing::Test {
 protected:
  void CreateWriter(size_t num_buffers) {
    readers_.clear();
    // The pid keeps concurrent runs of the same test apart: ShmWriter unlinks every segment sharing its node name
    // prefix, and the fixture's readers open their slots lazily
    writer_ = std::make_unique<ShmWriter>(
        fmt::format("{}_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name(), ::getpid()), loop_,
        ::getpid(), num_buffers, kInitialBufferSize, config_);
  }

  ShmWriter& Writer() { return *writer_; }

  SlotReader& Reader(size_t index) {
    auto it = readers_.find(index);
    if (it == readers_.end()) {
      it = readers_
               .emplace(index, std::make_unique<SlotReader>(SlotHandle(writer_->GetMemoryFilePrefix(), index), config_))
               .first;
    }
    return *it->second;
  }

  void Write(std::string_view bytes) {
    auto info = writer_->GetWriteAccess(bytes.size());
    ASSERT_GE(info.size, bytes.size());
    std::memcpy(info.data, bytes.data(), bytes.size());
    writer_->ReleaseWriteAccess(trellis::core::time::Now(), bytes.size(), /* success = */ true);
  }

  /// @brief Mimics a failed serialize: partially clobber a slot, then abandon it.
  void WriteAndAbandon(std::string_view garbage) {
    auto info = writer_->GetWriteAccess(garbage.size());
    std::memcpy(info.data, garbage.data(), garbage.size());
    writer_->ReleaseWriteAccess(trellis::core::time::Now(), /* bytes_written = */ 0, /* success = */ false);
  }

  static std::string BytesOf(const BorrowedPayload& payload) {
    return std::string{reinterpret_cast<const char*>(payload.data()), payload.size()};
  }

  trellis::core::EventLoop loop_;
  trellis::core::Config config_;
  std::unique_ptr<ShmWriter> writer_;
  std::unordered_map<size_t, std::unique_ptr<SlotReader>> readers_;
};

TEST_F(ShmBorrowTest, LappedBorrowIsInvalid) {
  CreateWriter(/* num_buffers = */ 1);
  Write("first payload");

  auto payload = Reader(0).Borrow();
  EXPECT_EQ(BytesOf(payload), "first payload");
  EXPECT_TRUE(payload.IsStillValid());

  Write("second payload");
  EXPECT_FALSE(payload.IsStillValid());
}

TEST_F(ShmBorrowTest, BorrowSurvivesWritesToOtherSlots) {
  constexpr size_t kNumBuffers = 4;
  CreateWriter(kNumBuffers);
  Write("slot zero");

  auto payload = Reader(0).Borrow();
  ASSERT_TRUE(payload.IsStillValid());

  // Fill every slot but the borrowed one
  for (size_t i = 1; i < kNumBuffers; ++i) {
    Write("some other slot");
    EXPECT_TRUE(payload.IsStillValid());
  }

  EXPECT_EQ(BytesOf(payload), "slot zero");
  EXPECT_TRUE(payload.IsStillValid());
}

TEST_F(ShmBorrowTest, BorrowIsInvalidDuringTornWriteWindow) {
  CreateWriter(/* num_buffers = */ 1);
  Write("stable payload");

  auto payload = Reader(0).Borrow();
  ASSERT_TRUE(payload.IsStillValid());

  // Write in progress but not committed: the generation is odd
  const auto info = Writer().GetWriteAccess(kInitialBufferSize);
  ASSERT_NE(info.data, nullptr);
  EXPECT_FALSE(payload.IsStillValid());

  Writer().ReleaseWriteAccess(trellis::core::time::Now(), 0, /* success = */ false);
  EXPECT_FALSE(payload.IsStillValid());
}

TEST_F(ShmBorrowTest, BorrowTakenMidWriteIsNeverValid) {
  CreateWriter(/* num_buffers = */ 1);
  Write("stable payload");

  const auto info = Writer().GetWriteAccess(kInitialBufferSize);
  ASSERT_NE(info.data, nullptr);

  // Borrowing without the read lock while the write window is open is a precondition violation. The snapshot is odd,
  // and an odd snapshot must not be trusted: comparing equal against it would report a torn payload as valid.
  auto borrow_info = Reader(0).InfoWithoutLock();
  ASSERT_EQ(borrow_info.header.generation % 2, 1U);
  const BorrowedPayload payload = std::move(borrow_info.payload);
  EXPECT_FALSE(payload.IsStillValid());

  Writer().ReleaseWriteAccess(trellis::core::time::Now(), 0, /* success = */ false);
  EXPECT_FALSE(payload.IsStillValid());
}

TEST_F(ShmBorrowTest, GrowRetryOnTheSameSlotInvalidatesTheBorrowItClobbers) {
  CreateWriter(/* num_buffers = */ 1);
  Write(std::string(64, 'a'));

  auto payload = Reader(0).Borrow();
  ASSERT_TRUE(payload.IsStillValid());
  const auto generation_before = Reader(0).Info().header.generation;

  // A failed serialize abandons the slot, then the next send re-locks that same slot at a larger size and publishes.
  // The abandoned attempt clobbers the borrowed bytes without ever publishing, so the borrow has to be invalidated by
  // the abandon alone.
  {
    auto info = Writer().GetWriteAccess(kInitialBufferSize);
    ASSERT_GE(info.size, kInitialBufferSize);
    std::memset(info.data, 'b', info.size);
    Writer().ReleaseWriteAccess(trellis::core::time::Now(), /* bytes_written = */ 0, /* success = */ false);
  }
  EXPECT_FALSE(payload.IsStillValid());

  const std::string oversized(kInitialBufferSize * 4, 'c');
  Write(oversized);

  // One abandoned cycle plus one published cycle
  EXPECT_EQ(Reader(0).Info().header.generation, generation_before + 4);
  EXPECT_EQ(BytesOf(Reader(0).Borrow()), oversized);
}

TEST_F(ShmBorrowTest, AbandonedWriteInvalidatesBorrowAndLeavesEvenParity) {
  CreateWriter(/* num_buffers = */ 1);
  Write("original");

  const auto after_write = Reader(0).Info();
  EXPECT_EQ(after_write.header.generation % 2, 0U);

  auto payload = Reader(0).Borrow();
  ASSERT_TRUE(payload.IsStillValid());

  WriteAndAbandon("clobbered by a partial serialize");
  EXPECT_FALSE(payload.IsStillValid());

  const auto after_abandon = Reader(0).Info();
  EXPECT_EQ(after_abandon.header.generation % 2, 0U);
  EXPECT_EQ(after_abandon.header.generation, after_write.header.generation + 2);
  // The abandoned write publishes nothing and zeroes the sequence, so a reader draining a stale event for this slot
  // drops it instead of delivering the clobbered bytes
  EXPECT_EQ(after_abandon.header.sequence, 0U);

  // A subsequent successful write still behaves normally
  Write("after abandon");
  auto fresh = Reader(0).Borrow();
  EXPECT_EQ(BytesOf(fresh), "after abandon");
  EXPECT_TRUE(fresh.IsStillValid());
  Write("laps the fresh borrow");
  EXPECT_FALSE(fresh.IsStillValid());
}

TEST_F(ShmBorrowTest, BorrowStaysReadableAcrossRemap) {
  CreateWriter(/* num_buffers = */ 1);
  const std::string small(32, 'a');
  Write(small);

  auto payload = Reader(0).Borrow();
  ASSERT_TRUE(payload.IsStillValid());
  ASSERT_EQ(payload.size(), small.size());

  // Force the writer to grow the slot well past the reader's current mapping
  const std::string large(kInitialBufferSize * 16, 'b');
  Write(large);

  // Reading here makes the reader remap; the borrow must keep its own mapping alive
  auto grown = Reader(0).Borrow();
  EXPECT_EQ(grown.size(), large.size());
  EXPECT_EQ(BytesOf(grown), large);

  // The pinned mapping is still addressable even though the reader remapped, and stays page-coherent with the file:
  // the lapping write is visible through it
  std::vector<uint8_t> copied(payload.size());
  std::memcpy(copied.data(), payload.data(), payload.size());
  EXPECT_EQ(copied, std::vector<uint8_t>(small.size(), 'b'));
  EXPECT_FALSE(payload.IsStillValid());

  // Dropping the last borrow releases the pin and actually unmaps the old region
  const auto page_mask = ~(static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE)) - 1);
  void* pinned_page = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(payload.data()) & page_mask);
  unsigned char resident = 0;
  EXPECT_EQ(::mincore(pinned_page, 1, &resident), 0);
  payload = BorrowedPayload{};
  EXPECT_EQ(::mincore(pinned_page, 1, &resident), -1);
  EXPECT_EQ(errno, ENOMEM);
}

TEST_F(ShmBorrowTest, BorrowSurvivesWriterTeardownAndUnlink) {
  CreateWriter(/* num_buffers = */ 1);
  Write("written before teardown");

  auto payload = Reader(0).Borrow();
  ASSERT_TRUE(payload.IsStillValid());

  // Tear the writer down, which unlinks the shared memory files, and drop the reader's own mapping
  readers_.clear();
  writer_.reset();

  // The unlinked inode stays alive behind the pin, and its bytes are never written again
  EXPECT_EQ(BytesOf(payload), "written before teardown");
  EXPECT_TRUE(payload.IsStillValid());
}

TEST_F(ShmBorrowTest, BorrowOfANeverPublishedSlotIsEmpty) {
  CreateWriter(/* num_buffers = */ 2);
  Write("only slot zero gets published");

  // hdr_size is stamped when the slot is created rather than on first publish, so borrowing a slot that was never
  // written is not an error. It reports the zeroed sequence that makes a reader drop the slot, and no payload
  const auto info = Reader(1).Info();
  EXPECT_EQ(info.header.sequence, 0U);
  EXPECT_EQ(info.payload.size(), 0U);
}

TEST_F(ShmBorrowTest, ImmutableBorrowStaysValidWhileHeld) {
  auto bytes = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{'o', 'w', 'n', 'e', 'd'});
  auto payload = BorrowedPayload::Immutable({bytes, bytes->data()}, bytes->size(), /* sequence = */ 7);
  EXPECT_EQ(BytesOf(payload), "owned");
  EXPECT_EQ(payload.sequence(), 7U);
  EXPECT_TRUE(payload.IsStillValid());

  // The borrow is the only remaining owner of the bytes, which stay readable and valid
  const uint8_t* data = bytes->data();
  bytes.reset();
  EXPECT_EQ(payload.data(), data);
  EXPECT_TRUE(payload.IsStillValid());

  // Moving carries the immutable state along and leaves the source empty, hence invalid
  BorrowedPayload moved{std::move(payload)};
  EXPECT_TRUE(moved.IsStillValid());
  EXPECT_EQ(BytesOf(moved), "owned");
  EXPECT_FALSE(payload.IsStillValid());
  EXPECT_EQ(payload.data(), nullptr);
}

TEST_F(ShmBorrowTest, ImmutableBorrowOfAnEmptyPayloadIsValid) {
  // A proto3 message with every field at its default serializes to zero bytes, and an empty vector's data() may be null
  auto bytes = std::make_shared<const std::vector<uint8_t>>();
  const auto payload = BorrowedPayload::Immutable({bytes, bytes->data()}, bytes->size(), /* sequence = */ 1);
  EXPECT_EQ(payload.size(), 0U);
  EXPECT_TRUE(payload.IsStillValid());
}

TEST_F(ShmBorrowTest, BorrowRejectsACorruptDataSize) {
  ShmFile file{fmt::format("trellis_borrow_corrupt_data_size_{}", ::getpid()), /* owner = */ true, kInitialBufferSize,
               config_};

  // Stamp sizes straight into the slot header the way corruption would. The near-2^64 value is the regression case:
  // additive bounds arithmetic (`kCombinedHeaderSize + data_size`) wraps and would pass
  file.SetFileHeader(std::numeric_limits<size_t>::max() - sizeof(ShmFile::SMemFileHeader), /* sequence = */ 1,
                     trellis::core::time::Now(), /* writer_id = */ 1);
  EXPECT_THROW(file.Borrow(), std::runtime_error);

  // The bound is the mapping, which is rounded up past the requested size, so ask the file for its real capacity
  file.SetFileHeader(file.GetWriteInfo().size + 1, /* sequence = */ 2, trellis::core::time::Now(),
                     /* writer_id = */ 2);
  EXPECT_THROW(file.Borrow(), std::runtime_error);
}

/// @brief FNV-1a over the payload body, embedded in each message so a torn read is detectable.
uint64_t Checksum(const uint8_t* data, size_t size) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

// Payload layout: [8 bytes sequence][8 bytes checksum of the body][body]
constexpr size_t kStressHeaderSize = 16;

TEST_F(ShmBorrowTest, SeqlockDetectsEveryTornOrLappedBorrow) {
  constexpr size_t kNumBuffers = 8;
  constexpr size_t kMinBodySize = 1024;
  constexpr size_t kMaxBodySize = 64 * 1024;
  constexpr size_t kMaxHolds = 3;
  constexpr auto kDuration = std::chrono::seconds(3);
  // The writer is paced so that a slot is rewritten roughly every kNumBuffers * kWritePeriod. Holds are drawn from a
  // window straddling that, which keeps both outcomes (survived, lapped) common enough to assert on.
  constexpr auto kWritePeriod = std::chrono::microseconds(100);
  constexpr int kMaxHoldUs = 3000;

  CreateWriter(kNumBuffers);
  // Prime every slot so the readers always find a valid file header
  for (size_t i = 0; i < kNumBuffers; ++i) {
    std::vector<uint8_t> primer(kStressHeaderSize + kMinBodySize, 0);
    const uint64_t checksum = Checksum(primer.data() + kStressHeaderSize, kMinBodySize);
    std::memcpy(primer.data() + sizeof(uint64_t), &checksum, sizeof(checksum));
    Write(std::string_view{reinterpret_cast<const char*>(primer.data()), primer.size()});
  }

  std::atomic<uint64_t> writes{0};

  // An exception escaping the thread body calls std::terminate, so capture it and fail the test instead. A jthread
  // taking a stop_token is joined on unwind too, so an exception in the test body surfaces as a failure rather than
  // as a terminate from destroying a joinable thread.
  std::string writer_error;
  std::jthread writer_thread([&](std::stop_token stop_token) {
    try {
      std::mt19937 rng{0xC0FFEEU};
      std::uniform_int_distribution<size_t> size_dist{kMinBodySize, kMaxBodySize};
      uint64_t sequence = 0;
      while (!stop_token.stop_requested()) {
        const size_t body_size = size_dist(rng);
        const size_t total_size = kStressHeaderSize + body_size;
        auto info = Writer().GetWriteAccess(total_size);
        auto* bytes = static_cast<uint8_t*>(info.data);
        std::memset(bytes + kStressHeaderSize, static_cast<int>(sequence & 0xFF), body_size);
        const uint64_t checksum = Checksum(bytes + kStressHeaderSize, body_size);
        std::memcpy(bytes, &sequence, sizeof(sequence));
        std::memcpy(bytes + sizeof(sequence), &checksum, sizeof(checksum));
        Writer().ReleaseWriteAccess(trellis::core::time::Now(), total_size, /* success = */ true);
        ++sequence;
        writes.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(kWritePeriod);
      }
    } catch (const std::exception& ex) {
      writer_error = ex.what();
    }
  });

  struct Held {
    BorrowedPayload payload;
    std::chrono::steady_clock::time_point deadline;
  };

  uint64_t validated = 0;
  uint64_t invalidated = 0;
  uint64_t corrupt_while_valid = 0;
  std::vector<uint8_t> scratch;

  {
    std::mt19937 rng{0xBADC0DEU};
    std::uniform_int_distribution<int> hold_dist{0, kMaxHoldUs};
    std::vector<Held> holds;
    size_t next_slot = 0;
    const auto end = std::chrono::steady_clock::now() + kDuration;

    const auto validate = [&](const BorrowedPayload& payload) {
      // Read the bytes out first, exactly as a consumer would, and only then ask whether they were stable
      scratch.resize(payload.size());
      std::memcpy(scratch.data(), payload.data(), payload.size());
      const bool still_valid = payload.IsStillValid();

      bool matches = scratch.size() > kStressHeaderSize;
      if (matches) {
        uint64_t expected_checksum = 0;
        std::memcpy(&expected_checksum, scratch.data() + sizeof(uint64_t), sizeof(expected_checksum));
        matches = Checksum(scratch.data() + kStressHeaderSize, scratch.size() - kStressHeaderSize) == expected_checksum;
      }

      if (still_valid) {
        ++validated;
        if (!matches) {
          ++corrupt_while_valid;
        }
      } else {
        ++invalidated;
      }
    };

    while (std::chrono::steady_clock::now() < end) {
      if (holds.size() < kMaxHolds) {
        auto payload = Reader(next_slot).TryBorrow();
        next_slot = (next_slot + 1) % kNumBuffers;
        if (payload.has_value() && payload->size() > kStressHeaderSize) {
          holds.push_back(
              Held{.payload = std::move(*payload),
                   .deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(hold_dist(rng))});
        }
      }

      const auto now = std::chrono::steady_clock::now();
      for (auto it = holds.begin(); it != holds.end();) {
        if (it->deadline <= now) {
          validate(it->payload);
          it = holds.erase(it);
        } else {
          ++it;
        }
      }
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    writer_thread.request_stop();
    for (const auto& held : holds) {
      validate(held.payload);
    }
  }

  writer_thread.join();

  EXPECT_TRUE(writer_error.empty()) << "writer thread threw: " << writer_error;
  EXPECT_GT(writes.load(), 0U);
  EXPECT_GT(validated, 0U) << "no borrow ever survived; the test window is not exercising the valid case";
  EXPECT_GT(invalidated, 0U) << "no borrow was ever lapped; the test window is not exercising the invalid case";
  EXPECT_EQ(corrupt_while_valid, 0U) << "a borrow reported valid but its payload checksum did not match, out of "
                                     << validated << " validated and " << invalidated << " invalidated borrows";
}

// Short by necessity: the test name and reader id go into the notification socket path, capped at 108 bytes
TEST_F(ShmBorrowTest, ReaderBorrowOutlivesTheCallback) {
  constexpr size_t kNumBuffers = 3;
  constexpr int kMaxPolls = 200;
  const std::string reader_id = "r";
  CreateWriter(kNumBuffers);

  std::vector<std::string> handles;
  for (size_t i = 0; i < kNumBuffers; ++i) {
    handles.push_back(SlotHandle(Writer().GetMemoryFilePrefix(), i));
  }
  std::vector<BorrowedPayload> held;
  const auto reader = ShmReader::Create(
      loop_, reader_id, handles,
      [&held](ShmFile::SMemFileHeader, BorrowedPayload payload) { held.push_back(std::move(payload)); }, config_);
  ASSERT_TRUE(reader->IsInitialized());
  // The writer drops a reader whose notification socket is not bound yet, so the reader has to exist first
  Writer().AddReader(reader_id);

  // Drain after each write so every message is delivered before the next one can lap its slot
  for (size_t i = 0; i <= kNumBuffers; ++i) {
    Write(fmt::format("message {}", i));
    for (int poll = 0; poll < kMaxPolls && held.size() <= i; ++poll) {
      loop_.PollOne();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(held.size(), i + 1) << "message " << i << " was not delivered";
  }

  EXPECT_FALSE(held[0].IsStillValid()) << "message " << kNumBuffers << " rewrote slot 0 while its borrow was held";
  for (size_t i = 1; i <= kNumBuffers; ++i) {
    EXPECT_EQ(BytesOf(held[i]), fmt::format("message {}", i));
    EXPECT_TRUE(held[i].IsStillValid());
  }
}

}  // namespace
}  // namespace trellis::core::ipc::shm
