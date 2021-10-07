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

#ifndef TRELLIS_CONTAINERS_FIFO_HPP
#define TRELLIS_CONTAINERS_FIFO_HPP

#include <mutex>
#include <queue>

namespace trellis {
namespace containers {
template <typename T, typename MUTEX_T = std::mutex, size_t MAX_SIZE = 1U>
class Fifo {
 public:
  using value_type = typename std::queue<T>::value_type;

  size_t Size() const { return queue_.size(); }

  void Push(T&& x) {
    std::lock_guard<MUTEX_T> lock(mutex_);
    Push_(std::forward<T>(x));
  }

  void Push(const T& x) {
    std::lock_guard<MUTEX_T> lock(mutex_);
    PushCopy_(x);
  }

  const T& Pop() {
    {
      std::lock_guard<MUTEX_T> lock(mutex_);
      Pop_();
    }
    return top_;
  }

  const T& Newest(bool& updated) {
    std::lock_guard<MUTEX_T> lock(mutex_);
    updated = Size() != 0;
    if (updated) {
      Pop_();
    }
    return top_;
  }

 private:
  void Push_(T&& x) {
    queue_.push(std::forward<T>(x));
    if (Size() > MAX_SIZE) {
      (void)Pop_();
    }
  }

  void PushCopy_(const T& x) {
    queue_.push(x);
    if (Size() > MAX_SIZE) {
      (void)Pop_();
    }
  }

  void Pop_() {
    top_ = std::move(queue_.front());
    queue_.pop();
  }
  std::queue<T> queue_;
  MUTEX_T mutex_;
  T top_;
};

}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_FIFO_HPP
