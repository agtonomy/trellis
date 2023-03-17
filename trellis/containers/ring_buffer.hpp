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

#ifndef TRELLIS_CONTAINERS_RING_BUFFER_HPP_
#define TRELLIS_CONTAINERS_RING_BUFFER_HPP_

#include <array>
#include <cstddef>

namespace trellis::containers {

/**
 * @brief A statically sized ring buffer.
 *
 * Backed by a std::array of one element more than capacity, to avoid ambiguity of an empty and full buffer (and so that
 * for a full buffer we can use index equality to determine when we have iterated through the buffer once).
 *
 * @tparam T the type to store
 * @tparam capacity the max amount of elements to store
 */
template <typename T, size_t capacity>
class RingBuffer {
 public:
  using value_type = T;

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  void push_back(T t) {
    data_[end_] = std::move(t);
    IncrementEnd();
    if (size_ > capacity) IncrementBegin();
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
    ConstIterator(const T* data, size_t index) : data_{data}, index_{index} {}

    // Pointer like operators.
    reference operator*() const { return data_[index_]; }
    pointer operator->() const { return &data_[index_]; }
    reference operator[](const difference_type offset) const { return data_[Offset(offset)]; }

    // Increment / Decrement
    ConstIterator& operator++() {
      IncrementAndWrap(index_);
      return *this;
    }

    ConstIterator& operator--() {
      if (index_ == 0) index_ = kArraySize;
      --index_;
      return *this;
    }

    // Arithmetic
    ConstIterator& operator+=(const difference_type offset) {
      index_ = Offset(offset);
      return *this;
    }

    ConstIterator operator+(const difference_type offset) const { return {data_, Offset(offset)}; }

    friend ConstIterator operator+(const difference_type offset, const ConstIterator& right) {
      return {right.data_, right.Offset(offset)};
    }

    ConstIterator& operator-=(const difference_type offset) {
      index_ = Offset(-offset);
      return *this;
    }

    ConstIterator operator-(const difference_type offset) const { return {data_, Offset(-offset)}; }

    difference_type operator-(const ConstIterator& right) const {
      return static_cast<difference_type>(index_) - static_cast<difference_type>(right.index_);
    }

   private:
    // Comparison operators
    friend bool operator==(const ConstIterator&, const ConstIterator&) = default;
    friend bool operator!=(const ConstIterator&, const ConstIterator&) = default;

    size_t Offset(const difference_type offset) const {
      auto mod = (static_cast<difference_type>(index_) + offset) % static_cast<difference_type>(kArraySize);
      if (mod < 0) mod += kArraySize;
      return mod;
    }

    const T* data_ = nullptr;
    size_t index_ = 0;
  };

  ConstIterator begin() const { return {data_.data(), begin_}; }
  ConstIterator end() const { return {data_.data(), end_}; }

 private:
  static constexpr size_t kArraySize = capacity + 1;

  static void IncrementAndWrap(size_t& index) {
    ++index;
    if (index == kArraySize) index = 0;
  }

  void IncrementEnd() {
    IncrementAndWrap(end_);
    ++size_;
  }

  void IncrementBegin() {
    IncrementAndWrap(begin_);
    --size_;
  }

  std::array<T, kArraySize> data_;
  size_t begin_ = 0;
  size_t end_ = 0;
  size_t size_ = 0;
};

}  // namespace trellis::containers

#endif  // TRELLIS_CONTAINERS_RING_BUFFER_HPP_
