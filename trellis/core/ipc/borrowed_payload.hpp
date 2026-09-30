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

#ifndef TRELLIS_CORE_IPC_BORROWED_PAYLOAD_HPP_
#define TRELLIS_CORE_IPC_BORROWED_PAYLOAD_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace trellis::core::ipc {

/**
 * @brief A read-only view of a received payload that can be held past the receive callback, with a check for whether
 * the bytes have been overwritten since.
 *
 * A shared memory borrow holds no lock, so a slow or crashed consumer never blocks the writer. It keeps the mapping
 * alive so the bytes stay addressable, and IsStillValid() compares the slot's seqlock counter against a snapshot taken
 * at borrow time. Immutable() wraps bytes that are never rewritten, such as an InProcessBus payload, and is always
 * valid, so both transports can deliver this one type.
 *
 * Reads after the callback are a data race in the strict C++ memory model, since the writer may rewrite the bytes
 * with plain stores while they are read. This is the trade every seqlock makes; the contract below is what makes it
 * safe in practice.
 *
 * Consumer contract for reads after the receive callback returns:
 * 1. Stay within [data(), data() + size()). Check any offset, length, or count read from the bytes against size()
 *    before using it, and bound loops by bytes consumed so a torn value cannot run away.
 * 2. Call IsStillValid() after the last read, and discard everything derived from the bytes if it returns false.
 * 3. Keep hold time well under num_buffers * publish_period, or most borrows will be invalidated.
 *
 * IsStillValid() means "not overwritten", not "fresh": a borrow from a publisher that has exited stays valid forever.
 */
class BorrowedPayload {
 public:
  /// @brief Constructs an empty borrow, which reports no data and is never valid.
  BorrowedPayload() = default;

  /**
   * @brief Constructs a borrow of the payload currently in a shared memory slot.
   *
   * Preconditions, which this constructor cannot check: the caller holds the slot's read lock, so the header and
   * generation it snapshots are stable, and `slot_generation` points into the same mapping that `data` pins, so it
   * stays addressable for this borrow's lifetime. ShmFile::Borrow() satisfies both; prefer it over calling this
   * directly. If the generation snapshot is odd, a write was in progress and the borrow is permanently invalid.
   *
   * @param data Pointer to the payload bytes, sharing ownership of the pinned mapping via the shared_ptr aliasing
   * constructor.
   * @param size Length of the payload in bytes.
   * @param sequence The transport sequence number of this payload.
   * @param slot_generation Pointer to the slot's seqlock counter, within the same mapping.
   */
  BorrowedPayload(std::shared_ptr<const uint8_t> data, size_t size, uint64_t sequence, const uint64_t* slot_generation)
      : data_{std::move(data)},
        size_{size},
        sequence_{sequence},
        slot_generation_{slot_generation},
        generation_{LoadGeneration(slot_generation, std::memory_order_acquire)} {
    if (generation_ % 2 != 0) {
      slot_generation_ = nullptr;
    }
  }

  /**
   * @brief Constructs a borrow of bytes that are never rewritten, such as an InProcessBus payload.
   *
   * The borrow is valid for as long as it is held.
   *
   * @param data Pointer to the payload bytes, sharing ownership of whatever keeps them alive. Build it with the
   * shared_ptr aliasing constructor from the owning handle, such as the InProcessBus payload buffer.
   * @param size Length of the payload in bytes.
   * @param sequence The transport sequence number of this payload.
   */
  static BorrowedPayload Immutable(std::shared_ptr<const uint8_t> data, size_t size, uint64_t sequence) {
    BorrowedPayload payload;
    payload.data_ = std::move(data);
    payload.size_ = size;
    payload.sequence_ = sequence;
    payload.immutable_ = true;
    return payload;
  }

  // Moving leaves the source empty rather than holding pointers into a mapping it no longer pins
  BorrowedPayload(BorrowedPayload&& other) noexcept { *this = std::move(other); }

  BorrowedPayload& operator=(BorrowedPayload&& other) noexcept {
    if (this != &other) {
      data_ = std::move(other.data_);  // a moved-from shared_ptr is already empty
      size_ = std::exchange(other.size_, 0);
      sequence_ = std::exchange(other.sequence_, 0);
      slot_generation_ = std::exchange(other.slot_generation_, nullptr);
      generation_ = std::exchange(other.generation_, 0);
      immutable_ = std::exchange(other.immutable_, false);
    }
    return *this;
  }

  BorrowedPayload(const BorrowedPayload&) = delete;
  BorrowedPayload& operator=(const BorrowedPayload&) = delete;

  /// @return Pointer to the borrowed payload bytes; may be nullptr when size() is 0.
  const uint8_t* data() const { return data_.get(); }

  /// @return Length of the borrowed payload in bytes.
  size_t size() const { return size_; }

  /// @return The transport sequence number of the payload that was borrowed.
  uint64_t sequence() const { return sequence_; }

  /**
   * @brief Reports whether the borrowed bytes are still the bytes that were borrowed.
   *
   * @return false if the writer has begun or completed any write to this slot since the borrow, if this borrow was
   * default-constructed or moved from, or if it was taken while a write was already in progress. Always true for an
   * immutable borrow, including one of zero bytes.
   */
  bool IsStillValid() const {
    if (immutable_) {
      return true;
    }
    if (slot_generation_ == nullptr) {
      return false;
    }
    // Seqlock reader check (Boehm, "Can Seqlocks Get Along with Programming Language Memory Models?"). Use an acquire
    // fence, not an acquire load: only the fence keeps the payload reads before it from moving past the check.
    std::atomic_thread_fence(std::memory_order_acquire);
    return LoadGeneration(slot_generation_, std::memory_order_relaxed) == generation_;
  }

 private:
  /// std::atomic_ref cannot bind to a const object until C++26, and the mapping may be read-only, so the load goes
  /// through a const_cast. It is a pure load of a lock-free word; nothing is ever written back.
  static uint64_t LoadGeneration(const uint64_t* slot_generation, std::memory_order order) {
    return std::atomic_ref<uint64_t>{*const_cast<uint64_t*>(slot_generation)}.load(order);
  }

  /// Points at the payload bytes and, through the aliasing constructor, shares ownership of what keeps them alive
  /// (the pinned mapping, or the InProcessBus buffer).
  std::shared_ptr<const uint8_t> data_;
  size_t size_{0};
  uint64_t sequence_{0};
  const uint64_t* slot_generation_{nullptr};  ///< Points into what `data_` pins; null for immutable/empty borrows.
  uint64_t generation_{0};                    ///< Snapshot of *slot_generation_ at borrow time (stable, even).
  bool immutable_{false};                     ///< True when nothing can rewrite the bytes; see Immutable().
};

}  // namespace trellis::core::ipc

#endif  // TRELLIS_CORE_IPC_BORROWED_PAYLOAD_HPP_
