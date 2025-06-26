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

#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace trellis {
namespace containers {

class MemoryPoolBadAlloc : public std::bad_alloc {
  std::string msg;

 public:
  explicit MemoryPoolBadAlloc(std::string_view msg) : msg(msg) {}
  const char* what() const noexcept override { return msg.c_str(); }
};

/**
 * @brief A simple memory pool implementation for allocating memory for a particlar object type.
 *
 * This implementation is simple but meets the current needs.
 *
 * Future improvement: adhere to various standard interfaces such as allocator traits.
 *
 * @tparam T A data type to allocate memory for
 * can be allocated
 */
class DynamicMemoryPool {
 private:
  struct Deleter {
    Deleter() : pool_{nullptr} {};

    Deleter(DynamicMemoryPool* pool) noexcept : pool_{pool} {}
    void operator()(void* ptr) const {
      if (pool_ && ptr) {
        pool_->Free(ptr);
      }
    }

    Deleter(Deleter&& other) noexcept : pool_{other.pool_} { other.pool_ = nullptr; }

    Deleter& operator=(Deleter&& other) noexcept {
      if (this != &other) {
        pool_ = other.pool_;
        other.pool_ = nullptr;
      }
      return *this;
    }

    Deleter(const Deleter& other) = delete;
    Deleter& operator=(const Deleter& other) = delete;

    DynamicMemoryPool* pool_;
  };

 public:
  /**
   * @brief Construct a memory pool object, dynamically allocating the underlying memory buffer
   * @param num_slots the number of fixed-size memory slots to allocate
   *
   */
  DynamicMemoryPool(size_t num_slots, size_t slot_size) : num_slots_{num_slots}, slot_size_{slot_size} {}

  DynamicMemoryPool(const DynamicMemoryPool&) = delete;
  DynamicMemoryPool& operator=(const DynamicMemoryPool&) = delete;
  DynamicMemoryPool(DynamicMemoryPool&&) = delete;
  DynamicMemoryPool& operator=(DynamicMemoryPool&&) = delete;

  using SharedPtr = std::shared_ptr<std::byte[]>;

  // For std::unique_ptr the deleter type has to be specified as a template arg
  using UniquePtr = std::unique_ptr<std::byte[], Deleter>;

  /**
   * @brief Allocate a new buffer of size equal to sizeof(T)
   *
   * @return void* a pointer to the new buffer
   * @throws MemoryPoolBadAlloc if the pool is exhausted
   */
  void* Allocate() {
    const auto slot = FindAnUnusedSlot();
    if (slot >= num_slots_) {
      throw MemoryPoolBadAlloc(fmt::format("Failed to find an unused memory slot ({} slots)", num_slots_));
    }
    free_slots_[slot] = false;
    return GetPointerForSlotNumber(slot);
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
  size_t FreeSlotsRemaining() const {
    size_t count = num_slots_;
    for (size_t i = 0; i < num_slots_; ++i) {
      if (free_slots_[i] == false) {
        --count;
      }
    }
    return count;
  }

  /**
   * @brief Return a shared pointer to a newly allocated slot
   *
   * @return SharedPtr a newly constructed shared pointer
   */
  SharedPtr ConstructSharedPointer() {
    return SharedPtr(reinterpret_cast<std::byte*>(Allocate()), [this](std::byte* ptr) mutable { Free(ptr); });
  }

  /**
   * @brief Return a unique pointer to a newly allocated slot
   *
   * @return UniquePtr a newly constructed unique pointer
   */
  UniquePtr ConstructUniquePointer() { return UniquePtr(reinterpret_cast<std::byte*>(Allocate()), Deleter(this)); }

 private:
  using FreeSlotsContainer = std::vector<bool>;

  /**
   * @brief Get the Byte Offset For Slot object
   *
   * @param slot the slot number
   * @return size_t the byte offset from the base for the given slot
   */
  size_t GetByteOffsetForSlot(size_t slot) { return slot * slot_size_; }

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
    auto base_ptr = reinterpret_cast<uintptr_t>(byte_buffer_.data());
    auto target_ptr = reinterpret_cast<uintptr_t>(ptr);
    auto distance = target_ptr - base_ptr;

    if (static_cast<size_t>(distance) >= pool_size_bytes_) {
      throw std::invalid_argument("GetSlotNumberFromPointer - given pointer is out of range!");
    }
    if (distance % slot_size_ != 0) {
      throw std::invalid_argument("GetSlotNumberFromPointer - given pointer is not aligned to a slot boundary!");
    }
    return distance / slot_size_;
  }

  /**
   * @brief Find an Unused Slot
   *
   * @return an unused slot or num_slots_ of there exists no unsed slots
   */
  size_t FindAnUnusedSlot() {
    // No mutex needed since this is a private method the synchronization will be handled by the caller
    for (size_t i = 0; i < num_slots_; ++i) {
      if (free_slots_[i] == true) {
        return i;
      }
    }
    return num_slots_;
  }

  const size_t num_slots_;
  const size_t slot_size_;
  const size_t pool_size_bytes_{num_slots_ * slot_size_};
  std::vector<std::byte> byte_buffer_ =
      std::vector<std::byte>(pool_size_bytes_);  // use a vector to put the memory on the heap
  FreeSlotsContainer free_slots_ = FreeSlotsContainer(num_slots_, true);
};

template <class T>
class MemoryPool : public DynamicMemoryPool {
 private:
  struct Deleter {
    Deleter() : pool_{nullptr} {};

    Deleter(MemoryPool<T>* pool) noexcept : pool_{pool} {}
    void operator()(T* ptr) const {
      if (pool_ && ptr) {
        pool_->Destruct(ptr);
      }
    }

    Deleter(Deleter&& other) noexcept : pool_{other.pool_} { other.pool_ = nullptr; }

    Deleter& operator=(Deleter&& other) noexcept {
      if (this != &other) {
        pool_ = other.pool_;
        other.pool_ = nullptr;
      }
      return *this;
    }

    Deleter(const Deleter& other) = delete;
    Deleter& operator=(const Deleter& other) = delete;

    MemoryPool<T>* pool_;
  };

 public:
  // Guarantee that T has proper padding to be properly aligned
  static constexpr std::size_t kAlignment = alignof(T);
  static constexpr std::size_t kSlotSizeBytes = (sizeof(T) + kAlignment - 1) / kAlignment * kAlignment;

  static_assert(kSlotSizeBytes % kAlignment == 0, "Slot size must be a multiple of alignof(T)");
  static_assert(kSlotSizeBytes >= sizeof(T), "Slot size must not truncate the object");

  MemoryPool(size_t num_slots) : DynamicMemoryPool(num_slots, kSlotSizeBytes) {}

  using SharedPtr = std::shared_ptr<T>;

  // For std::unique_ptr the deleter type has to be specified as a template arg
  using UniquePtr = std::unique_ptr<T, Deleter>;

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
    return UniquePtr(new (Allocate()) T(std::forward<Args>(args)...), Deleter(this));
  }

  /**
   * @brief Destruct the object pointed to before freeing the memory
   *
   * @param ptr the object to destruct
   */
  void Destruct(T* ptr) {
    if (ptr != nullptr) {
      ptr->~T();
      Free(ptr);
    }
  }
};

}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_MEMORY_POOL_HPP_
