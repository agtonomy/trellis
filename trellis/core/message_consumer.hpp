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

#ifndef TRELLIS_CORE_MESSAGE_CONSUMER_HPP
#define TRELLIS_CORE_MESSAGE_CONSUMER_HPP

#include <array>
#include <string>
#include <type_traits>
#include <utility>

#include "node.hpp"
#include "subscriber.hpp"
#include "trellis/containers/multi_fifo.hpp"

namespace trellis {
namespace core {

template <size_t FIFO_DEPTH, typename... Types>
class MessageConsumer {
 public:
  template <typename T>
  struct StampedMessage {
    time::TimePoint timestamp;
    T message;
  };
  using Callback = std::function<void(void)>;
  using TopicsArray = std::array<std::vector<std::string>, sizeof...(Types)>;
  MessageConsumer(const Node& node, TopicsArray topics, Callback callback = {})
      : topics_{topics}, callback_{callback}, loop_{node.GetEventLoop()} {
    CreateSubscribers(node);
  }

  template <size_t I = 0>
  inline typename std::enable_if<I == sizeof...(Types), void>::type CreateSubscribers(const Node& node) {}

  template <size_t I = 0>
      inline typename std::enable_if < I<sizeof...(Types), void>::type CreateSubscribers(const Node& node) {
    const auto& topics = topics_[I];
    using MessageType = std::tuple_element_t<I, std::tuple<Types...>>;
    auto& subscriber_list = std::get<I>(subscribers_);
    for (const auto& topic : topics) {
      subscriber_list.emplace_back(
          node.CreateSubscriber<MessageType>(topic, [this](const MessageType& msg) { NewMessage(msg); }));
    }

    CreateSubscribers<I + 1>(node);
  }

  template <typename MSG_T>
  void NewMessage(const MSG_T& msg) {
    fifos_.template Push<StampedMessage<MSG_T>>(std::move(StampedMessage<MSG_T>{time::now(), msg}));

    if (callback_) {
      // XXX(bsirang): are there any thread-safety issues here?
      // According to https://think-async.com/Asio/asio-1.18.2/doc/asio/overview/core/threads.html
      // asio::post() seems to be thread-safe
      auto cb = callback_;
      asio::post(*loop_, [cb]() { cb(); });
    }
  }

  template <typename MSG_T>
  const StampedMessage<MSG_T>& Newest(bool* updated = nullptr) {
    bool updated_msg;
    bool* updated_ptr = (updated == nullptr) ? &updated_msg : updated;
    return fifos_.template Newest<StampedMessage<MSG_T>>(*updated_ptr);
  }

  template <typename MSG_T>
  bool TimedOut(const time::TimePoint& now, unsigned timeout_ms) {
    const auto& newest_stamp = Newest<MSG_T>().timestamp;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time::now() - newest_stamp);
    return elapsed_ms.count() > timeout_ms;
  }

 private:
  const TopicsArray topics_;
  const Callback callback_;
  const EventLoop loop_;
  std::tuple<std::vector<Subscriber<Types>>...> subscribers_;
  trellis::containers::MultiFifo<FIFO_DEPTH, StampedMessage<Types>...> fifos_;
};
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_MESSAGE_CONSUMER_HPP
