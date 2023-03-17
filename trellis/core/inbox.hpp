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

#ifndef TRELLIS_CORE_INBOX_HPP_
#define TRELLIS_CORE_INBOX_HPP_

#include "node.hpp"
#include "stamped_message.hpp"
#include "subscriber.hpp"

namespace trellis::core {

/**
 * @brief A simple wrapper around MessageConsumer that grabs the lastest unexpired message of each type.
 *
 * TODO(matt): Extend to support N latest messages instead of only the latest.
 *
 * @tparam Types the message types to receive
 */
template <typename... Types>
class Inbox {
 public:
  using MessageTimeouts = std::array<time::TimePoint::duration, sizeof...(Types)>;
  using TopicArray = std::array<std::string_view, sizeof...(Types)>;

  /**
   * @brief Construct a new Inbox object.
   *
   * @param node the node to get the subscribers from
   * @param topics the topic for each type to subscribe to
   * @param timeouts the timeout for each type
   */
  Inbox(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts)
      : receivers_{MakeReceivers(node, topics, timeouts)} {}

  using LatestMessages = std::tuple<std::optional<StampedMessage<Types>>...>;

  /**
   * @brief Gets the latest message of each type that is not expired (past the corresponding timeout).
   *
   * @param time the current time at which to check the inbox
   * @return Latest message of each type (if it exists)
   */
  LatestMessages GetLatestMessages(const time::TimePoint& time) const {
    return std::apply(
        [&time](const auto&... receivers) { return std::make_tuple(GetLatestValidMessage(time, receivers)...); },
        receivers_);
  }

 private:
  template <typename MSG_T>
  struct Receiver {
    Subscriber<MSG_T> subscriber;
    // We use a unique_ptr so we can pass capture in the sub callback safely, even if this receiver moves around. This
    // ptr should always point to the same value. We use `latest->message != nullptr` to check if there is indeed a
    // valid message here. We could wrap it in an optional, but that is not necessary.
    std::unique_ptr<StampedMessagePtr<MSG_T>> latest;
    time::TimePoint::duration timeout;
  };

  template <size_t Index>
  static auto MakeReceiver(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    using MSG_T = std::tuple_element_t<Index, std::tuple<Types...>>;

    // Not const to allow move.
    auto latest = std::make_unique<StampedMessagePtr<MSG_T>>();

    // Not const to allow move.
    auto subscriber = node.CreateSubscriber<MSG_T>(
        topics[Index],
        [&latest = *latest](const time::TimePoint&, const time::TimePoint& msgtime, MessagePointer<MSG_T> msg) {
          latest = {msgtime, std::move(msg)};
        });

    return Receiver<MSG_T>{std::move(subscriber), std::move(latest), timeouts[Index]};
  }

  template <size_t... Indices>
  static auto MakeReceivers(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts,
                            std::index_sequence<Indices...>) {
    return std::make_tuple(MakeReceiver<Indices>(node, topics, timeouts)...);
  }

  /**
   * An intermediate of MakeReceivers that creates an index sequence to iterate over the topic and message timeout
   * arrays.
   */
  static auto MakeReceivers(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    return MakeReceivers(node, topics, timeouts, std::make_index_sequence<sizeof...(Types)>{});
  }

  template <typename MSG_T>
  static std::optional<StampedMessage<MSG_T>> GetLatestValidMessage(const time::TimePoint& time,
                                                                    const Receiver<MSG_T>& receiver) {
    if (receiver.latest->message == nullptr) return std::nullopt;                   // No message received yet.
    if (receiver.latest->timestamp < time - receiver.timeout) return std::nullopt;  // Message too old.

    return StampedMessage<MSG_T>{*receiver.latest};
  }

  std::tuple<Receiver<Types>...> receivers_;
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_INBOX_HPP_
