/*
 * Copyright (C) 2023 Agtonomy
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

#ifndef TRELLIS_CONTAINERS_DYNAMIC_RING_BUFFER_HPP_
#define TRELLIS_CONTAINERS_DYNAMIC_RING_BUFFER_HPP_

#include <cstddef>
#include <memory>
namespace trellis::containers {

/**
 * @brief A dynamically sized ring buffer.
 *
 * Dynamically allocated but never deallocates. Only ever allocates to the maximum concurrently stored values. When the
 * buffer is full, the capacity is doubled.
 *
 * Uses unmasked indices for simplicity, meaning we never have to bound our indices, they naturally underflow and
 * overflow appropriately. This works since capacity is always a power of 2, so the overlow value is always divisible by
 * capacity, so wrapping around and then masking with capacity always gives the next/previous element.
 *
 * @tparam T the type to store
 */
template <typename T>
class DynamicRingBuffer {
 public:
  using value_type = T;

  DynamicRingBuffer() = default;

  ~DynamicRingBuffer() {
    for (auto i = begin_; i != end_; ++i) std::destroy_at(&data_[Mask(i)]);
    std::allocator<T>{}.deallocate(data_, capacity_);
  }

  DynamicRingBuffer(const DynamicRingBuffer& other) = delete;
  DynamicRingBuffer& operator=(const DynamicRingBuffer&) = delete;

  DynamicRingBuffer(DynamicRingBuffer&& other) noexcept {
    data_ = other.data_;
    capacity_ = other.capacity_;
    begin_ = other.begin_;
    end_ = other.end_;
    other.data_ = nullptr;
    other.capacity_ = 0;
    other.begin_ = 0;
    other.end_ = 0;
  }

  DynamicRingBuffer& operator=(DynamicRingBuffer&& other) noexcept {
    for (auto i = begin_; i != end_; ++i) std::destroy_at(&data_[Mask(i)]);
    std::allocator<T>{}.deallocate(data_, capacity_);
    data_ = other.data_;
    capacity_ = other.capacity_;
    begin_ = other.begin_;
    end_ = other.end_;
    other.data_ = nullptr;
    other.capacity_ = 0;
    other.begin_ = 0;
    other.end_ = 0;
    return *this;
  }

  size_t size() const { return end_ - begin_; }  // Overflow handles correctly.
  bool empty() const { return end_ == begin_; }

  void push_back(T t) {
    if (size() == capacity_) IncreaseCapacity();
    std::construct_at(&data_[Mask(end_)], std::move(t));
    ++end_;
  }

  void pop_front() {
    std::destroy_at(&data_[Mask(begin_)]);
    ++begin_;
  }

  class ConstIterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using pointer = const T*;
    using reference = const T&;
    using difference_type = std::ptrdiff_t;

    // Constructors.
    ConstIterator() = default;
    ConstIterator(const T* data, size_t capacity, size_t index) : data_{data}, capacity_{capacity}, index_{index} {}

    // Pointer like operators.
    reference operator*() const { return data_[Mask(index_)]; }
    pointer operator->() const { return &data_[Mask(index_)]; }
    reference operator[](const difference_type offset) const { return data_[Mask(index_ + offset)]; }

    // Increment / Decrement
    ConstIterator& operator++() {
      ++index_;
      return *this;
    }

    ConstIterator& operator--() {
      --index_;
      return *this;
    }

    // Arithmetic
    ConstIterator& operator+=(const difference_type offset) {
      index_ += offset;
      return *this;
    }

    ConstIterator operator+(const difference_type offset) const { return {data_, capacity_, index_ + offset}; }

    friend ConstIterator operator+(const difference_type offset, const ConstIterator& right) {
      return {right.data_, right.capacity_, right.index_ + offset};
    }

    ConstIterator& operator-=(const difference_type offset) {
      index_ -= offset;
      return *this;
    }

    ConstIterator operator-(const difference_type offset) const { return {data_, capacity_, index_ - offset}; }

    difference_type operator-(const ConstIterator& right) const { return index_ - right.index_; }

   private:
    // Comparison operators
    friend bool operator==(const ConstIterator&, const ConstIterator&) = default;
    friend bool operator!=(const ConstIterator&, const ConstIterator&) = default;

    // Go from unmasked index to masked index (can be used to access data).
    size_t Mask(const size_t index) const { return index & (capacity_ - 1); }

    const T* data_ = nullptr;
    size_t capacity_ = 0;
    size_t index_ = 0;  // Unmasked, integer overflow works appropriately since capacity is a power of 2.
  };

  ConstIterator begin() const { return {data_, capacity_, begin_}; }
  ConstIterator end() const { return {data_, capacity_, end_}; }

 private:
  // Go from unmasked index to masked index (can be used to access data).
  size_t Mask(const size_t index) const { return index & (capacity_ - 1); }

  void IncreaseCapacity() {
    const auto new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
    IncreaseCapacity(new_capacity);
  }

  void IncreaseCapacity(const size_t new_capacity) {
    const auto new_data = allocator_.allocate(new_capacity);
    const auto size = this->size();
    for (auto new_i = size_t{}, old_i = begin_; old_i != end_; ++new_i, ++old_i) {
      std::construct_at(&new_data[new_i], std::move(data_[Mask(old_i)]));
      std::destroy_at(&data_[Mask(old_i)]);
    }
    allocator_.deallocate(data_, capacity_);
    data_ = new_data;
    capacity_ = new_capacity;
    begin_ = 0;
    end_ = size;
  }

  T* data_ = nullptr;
  size_t capacity_ = 0;  // Always a power of 2 (or 0).
  size_t begin_ = 0;     // Unmasked, integer overflow works appropriately since capacity is a power of 2.
  size_t end_ = 0;       // Unmasked, integer overflow works appropriately since capacity is a power of 2.
  std::allocator<T> allocator_ = {};
};

}  // namespace trellis::containers

#endif  // TRELLIS_CONTAINERS_DYNAMIC_RING_BUFFER_HPP_
