/*
 * Copyright (C) 2021 Agtonomy
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

#ifndef TRELLIS_CONTAINERS_FIFO_HPP_
#define TRELLIS_CONTAINERS_FIFO_HPP_

#include <mutex>
#include <queue>

namespace trellis {
namespace containers {

template <typename T, typename MUTEX_T = std::mutex, size_t MAX_SIZE = 1U>
class Fifo {
 public:
  using value_type = typename std::queue<T>::value_type;

  size_t Size() {
    std::lock_guard<MUTEX_T> lock(mutex_);
    return Size_();
  }

  void Push(T x) {
    std::lock_guard<MUTEX_T> lock(mutex_);
    Push_(std::forward<T>(x));
  }

  T Next() {
    std::lock_guard<MUTEX_T> lock(mutex_);
    return Pop_();
  }

  T Newest() {
    std::lock_guard<MUTEX_T> lock(mutex_);
    return Back_();
  }

 private:
  void Push_(T x) {
    queue_.push(std::forward<T>(x));
    if (Size_() > MAX_SIZE) {
      (void)Pop_();
    }
  }

  T Pop_() {
    if (queue_.empty()) {
      throw std::runtime_error("Attempt to pop empty queue!");
    }
    T top = std::move(queue_.front());
    queue_.pop();
    return top;
  }

  T Back_() const {
    if (queue_.empty()) {
      throw std::runtime_error("Attempt to access empty queue!");
    }
    return queue_.back();
  }

  size_t Size_() { return queue_.size(); }

  std::queue<T> queue_;
  MUTEX_T mutex_;
};

}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_FIFO_HPP_
