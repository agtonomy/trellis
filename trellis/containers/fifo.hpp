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

#include <queue>

namespace trellis {
namespace containers {

template <typename T, size_t MAX_SIZE = 1U>
class Fifo {
 public:
  using value_type = typename std::queue<T>::value_type;

  size_t Size() const { return queue_.size(); }

  void Push(T x) {
    queue_.push(std::forward<T>(x));
    if (Size() > MAX_SIZE) {
      (void)Next();
    }
  }

  T Next() {
    if (queue_.empty()) {
      throw std::runtime_error("Attempt to pop empty queue!");
    }
    T top = std::move(queue_.front());
    queue_.pop();
    return top;
  }

  const T& Newest() const {
    if (queue_.empty()) {
      throw std::runtime_error("Attempt to access empty queue!");
    }
    return queue_.back();
  }

 private:
  std::queue<T> queue_;
};

}  // namespace containers
}  // namespace trellis

#endif  // TRELLIS_CONTAINERS_FIFO_HPP_
