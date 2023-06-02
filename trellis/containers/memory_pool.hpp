/*
 * Copyright (C) 2022 Agtonomy
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

#ifndef TRELLIS_CONTAINERS_MEMORY_POOL_HPP_
#define TRELLIS_CONTAINERS_MEMORY_POOL_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_set>
#include <vector>

namespace trellis {
namespace containers {

constexpr size_t kDefaultSlotSize = 60U;

/**
 * @brief A simple memory pool implementation for allocating memory for a particlar object type.
 *
 * This implementation is simple but meets the current needs.
 *
 * Future improvement: adhere to various standard interfaces such as allocator traits.
 *
 * @tparam T A data type to allocate memory for
 * @tparam NUM_SLOTS the number of slots to reserve for the memory pool. This equals the maximum number of objects that
 * can be allocated
 */
template <class T, size_t NUM_SLOTS = kDefaultSlotSize>
class MemoryPool {
 public:
  using SharedPtr = std::shared_ptr<T>;

  // For std::unique_ptr the deleter type has to be specified as a template arg
  using UniquePtr = std::unique_ptr<T, std::function<void(T*)>>;

  /**
   * @brief Construct a memory pool object, dynamically allocating the underlying memory buffer
   *
   */
  MemoryPool() = default;

  /**
   * @brief Allocate a new buffer of size equal to sizeof(T)
   *
   * @return void* a pointer to the new buffer
   * @throws std::bad_alloc if the pool is exhausted
   */
  void* Allocate() {
    std::lock_guard<std::mutex> lock(free_slot_mutex_);
    const auto slot = FindAnUnusedSlot();
    if (slot) {
      free_slots_[*slot] = false;
    }
    if (!slot) {
      throw std::bad_alloc();
    }
    return GetPointerForSlotNumber(*slot);
  }

  /**
   * @brief Destruct the object pointed to before freeing the memory
   *
   * @param ptr the object to destruct
   */
  void Destruct(T* ptr) {
    if (ptr != nullptr) {
      ptr->~T();
    }
    Free(ptr);
  }

  /**
   * @brief Free the given buffer
   *
   * @param ptr a pointer to the buffer returned by Allocate()
   * @see Allocate()
   * @throws std::runtime_error if ptr is not valid or was already free'd
   */
  void Free(void* ptr) {
    if (ptr != nullptr) {
      const auto slot = GetSlotNumberFromPointer(ptr);
      std::lock_guard<std::mutex> lock(free_slot_mutex_);
      if (free_slots_[slot] == true) {
        // double free!
        throw std::runtime_error("double free detected on slot " + std::to_string(slot));
      } else {
        free_slots_[slot] = true;
      }
    }
  }

  /**
   * @brief Return the number of memory slots that are still free (unused)
   *
   * @return size_t the number of slots
   */
  size_t FreeSlotsRemaining() {
    std::lock_guard<std::mutex> lock(free_slot_mutex_);
    unsigned count = NUM_SLOTS;
    for (size_t i = 0; i < NUM_SLOTS; ++i) {
      if (free_slots_[i] == false) {
        --count;
      }
    }
    return count;
  }

  /**
   * @brief Return a shared pointer to a newly allocated and constructed object
   *
   * This method uses placement new to construct a new object in a memory region allocated by this memory pool.
   *
   * @tparam Args variadic list of argument types to forward to the constructor
   * @param args variadic list of arguments to forward to the constructor
   * @return SharedPtr a newly constructed shared pointer
   */
  template <typename... Args>
  SharedPtr ConstructSharedPointer(Args&&... args) {
    return SharedPtr(new (Allocate()) T(std::forward<Args>(args)...), [this](T* ptr) mutable { Destruct(ptr); });
  }

  /**
   * @brief Return a unique pointer to a newly allocated and constructed object
   *
   * This method uses placement new to construct a new object in a memory region allocated by this memory pool.
   *
   * @tparam Args variadic list of argument types to forward to the constructor
   * @param args variadic list of arguments to forward to the constructor
   * @return UniquePtr a newly constructed unique pointer
   */
  template <typename... Args>
  UniquePtr ConstructUniquePointer(Args&&... args) {
    return UniquePtr(new (Allocate()) T(std::forward<Args>(args)...), [this](T* ptr) mutable { Destruct(ptr); });
  }

 private:
  static constexpr size_t kSlotSizeBytes = sizeof(T);
  static constexpr size_t kPoolSizeBytes = NUM_SLOTS * kSlotSizeBytes;

  using FreeSlotsContainer = std::array<bool, NUM_SLOTS>;

  /**
   * @brief Get the Byte Offset For Slot object
   *
   * @param slot the slot number
   * @return size_t the byte offset from the base for the given slot
   */
  static size_t GetByteOffsetForSlot(size_t slot) { return slot * kSlotSizeBytes; }

  /**
   * @brief Get the Pointer For Slot Number
   *
   * @param slot the slot number
   * @return T* a pointer to the head of the given slot
   */
  void* GetPointerForSlotNumber(size_t slot) {
    return reinterpret_cast<void*>(&byte_buffer_[GetByteOffsetForSlot(slot)]);
  }

  /**
   * @brief Get the Slot Number From Pointer
   *
   * @param ptr the pointer, which must point to the head of a slot
   * @return size_t the slot number which the pointer represents
   * @throws std::invalid_argument if the pointer is out of range or misaligned
   * @throws std::logic_error if there is an internal miscalculation
   */
  size_t GetSlotNumberFromPointer(const void* const ptr) const {
    // Casts for byte-based pointer arithmetic
    const std::byte* byte_ptr = reinterpret_cast<const std::byte*>(ptr);
    const std::byte* base_ptr = byte_buffer_.data();
    const intptr_t distance = static_cast<intptr_t>(byte_ptr - base_ptr);

    if (distance < 0 || static_cast<size_t>(distance) >= kPoolSizeBytes) {
      throw std::invalid_argument("GetSlotNumberFromPointer - given pointer is out of range!");
    }
    if (distance % kSlotSizeBytes != 0) {
      throw std::invalid_argument("GetSlotNumberFromPointer - given pointer is not aligned to a slot boundary!");
    }
    return distance / kSlotSizeBytes;
  }

  /**
   * @brief Find an Unused Slot
   *
   * @return std::optional<size_t> an unused slot or std::nullopt_t of there exists no unsed slots
   */
  std::optional<size_t> FindAnUnusedSlot() {
    // No mutex needed since this is a private method the synchronization will be handled by the caller
    for (size_t i = 0; i < NUM_SLOTS; ++i) {
      if (free_slots_[i] == true) {
        return std::optional<size_t>(i);
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Construct the initial set of unused slots
   *
   * @return FreeSlotsContainer the initial set of unused slots
   */
  static FreeSlotsContainer InitializeFreeSlotContainer() {
    FreeSlotsContainer free_slots;
    free_slots.fill(true);
    return free_slots;
  }

  std::vector<std::byte> byte_buffer_ = std::vector<std::byte>(kPoolSizeBytes);
  FreeSlotsContainer free_slots_{InitializeFreeSlotContainer()};
  std::mutex free_slot_mutex_;
};

}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_MEMORY_POOL_HPP_
