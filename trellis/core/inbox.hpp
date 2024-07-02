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
#include "trellis/containers/dynamic_ring_buffer.hpp"
#include "trellis/containers/ring_buffer.hpp"

namespace trellis::core {

/**
 * @brief Type for representing how to receive a single topic where we always get the latest message on that topic (if
 * not timed out).
 *
 * @tparam MSG_T the message type to receive.
 */
template <typename MSG_T>
struct Latest {
  using MessageType = MSG_T;
  using LatestTag = int;  // Add an arbitrary type tag so we know we are using this template.
};

/**
 * @brief Type for representing how to receive a single topic where we get the latest N messages on the topic that are
 * not timed out.
 *
 * @tparam MSG_T The message type to receive.
 * @tparam N The max number of latest messages to return.
 */
template <typename MSG_T, size_t N>
struct NLatest {
  using MessageType = MSG_T;
  static constexpr size_t kNLatest = N;
};

/**
 * @brief Type for representing how to receive a single topic where we get the latest messages on the topic that are not
 * timed out.
 *
 * Slightly more convenient than NLatest for the common case of getting all the latest messages, with the drawback that
 * there is no cap on how many messages are stored and returned.
 *
 * Also less efficient than NLatest as we have to copy out of the receiver's memory pool since we don't know the amount
 * of messages we may need to store.
 *
 * @tparam MSG_T The message type to receive.
 */
template <typename MSG_T>
struct AllLatest {
  using MessageType = MSG_T;
  using AllLatestTag = int;  // Add an arbitrary type tag so we know we are using this template.
};

/**
 * @brief Type for representing how to receive a single topic which is coming from the owner of the inbox so it does not
 * need to be received, only sent.
 *
 * @tparam MSG_T The message type.
 * @tparam SERIALIZED_T The type to serialize to (default is the same as MSG_T).
 * @tparam SERIALIZATION_FN_T The type of function to call to serialize the message (default is identity). For now the
 * Inbox only supports stateless functors (see example in unit tests).
 */
template <typename MSG_T, typename SERIALIZED_T = MSG_T, typename SERIALIZATION_FN_T = std::identity>
struct Loopback {
  using MessageType = MSG_T;
  using SerializedType = SERIALIZED_T;
  using SerializationFn = SERIALIZATION_FN_T;
};

/// @brief Concept for a type that derives from Latest.
template <typename R>
concept IsLatestReceiveType = requires { typename R::LatestTag; };

/// @brief Concept for a type that derives from NLatest.
template <typename R>
concept IsNLatestReceiveType = requires {
  { R::kNLatest } -> std::convertible_to<size_t>;
};

/// @brief Concept for a type that derives from AllLatest.
template <typename R>
concept IsAllLatestReceiveType = requires { typename R::AllLatestTag; };

/// @brief Concept for a type that derives from Loopback.
template <typename R>
concept IsLoopbackReceiveType = requires {
  typename R::SerializedType;
  typename R::SerializationFn;
};

/// @brief Concept for a type that derives from one of our receive types and is valid for use in the inbox.
template <typename R>
concept IsReceiveType = requires {
  typename R::MessageType;
} && (IsLatestReceiveType<R> || IsNLatestReceiveType<R> || IsAllLatestReceiveType<R> || IsLoopbackReceiveType<R>);

/**
 * @brief An inbox for getting the latest messages on various channels.
 *
 * @tparam ReceiveTypes the message receive types which should be IsReceiveTypes, which are specializations of the
 * templates Latest, NLatest, AllLatest, or Loopback.
 */
template <IsReceiveType... ReceiveTypes>
class Inbox {
 public:
  using MessageTimeouts = std::array<time::TimePoint::duration, sizeof...(ReceiveTypes)>;
  using TopicArray = std::array<std::string_view, sizeof...(ReceiveTypes)>;

  /**
   * @brief Construct a new Inbox object.
   *
   * @param node the node to get the subscribers from
   * @param topics the topic for each type to subscribe to
   * @param timeouts the timeout for each type
   */
  Inbox(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts)
      : receivers_{MakeReceivers(node, topics, timeouts)} {}

  template <typename R>
  struct InboxReturnType;

  /// @brief Defines what a NLatest or AllLatest receive type will return in GetMessages.
  template <typename R>
    requires IsNLatestReceiveType<R> || IsAllLatestReceiveType<R>
  struct InboxReturnType<R> {
    using type = std::vector<StampedMessage<typename R::MessageType>>;
  };

  /// @brief Defines what a Latest or Loopback receive type will return in GetMessages.
  template <typename R>
    requires IsLatestReceiveType<R> || IsLoopbackReceiveType<R>
  struct InboxReturnType<R> {
    using type = std::optional<StampedMessage<typename R::MessageType>>;
  };

  /// @brief A convenient helper for InboxReturnType.
  template <IsReceiveType R>
  using InboxReturnType_t = typename InboxReturnType<R>::type;

  /// @brief The return type for GetMessages.
  using Messages = std::tuple<InboxReturnType_t<ReceiveTypes>...>;

  /**
   * @brief Gets the messages for each topic that are not expired (past the corresponding timeout) according to the
   * receive type.
   *
   * @param time the current time at which to check the inbox
   * @return Messages for each topic.
   */
  Messages GetMessages(const time::TimePoint& time) const {
    return std::apply([&time](const auto&... receivers) { return std::make_tuple(Receive(time, receivers)...); },
                      receivers_);
  }

  /**
   * @brief Gets the index of the loopback receiver for MSG_T.
   *
   * @tparam MSG_T the message type to find the looback receiver for
   */
  template <class MSG_T>
  struct LoobackIndex {
    static constexpr std::size_t value = []() {
      constexpr std::array<bool, sizeof...(ReceiveTypes)> matches{
          {(IsLoopbackReceiveType<ReceiveTypes> && std::is_same_v<typename ReceiveTypes::MessageType, MSG_T>)...}};
      // As we are in constant expression, we will get a compilation error not a runtime expection.
      if (std::ranges::count(matches, true) != 1) {
        throw std::runtime_error("Expected exactly 1 loopback receiver of the given type.");
      }
      return std::distance(matches.begin(), std::ranges::find(matches, true));
    }();
  };

  /**
   * @brief Sends a loopback message, which also stores it for getting in GetMessages.
   *
   * Similar in interface to PublisherImpl::Send.
   *
   * @tparam MSG_T the message type
   * @param msg the message, encouraged to move into this function as we store the message
   * @param time the send time, default is the current time
   * @return time::TimePoint the time the message was sent
   */
  template <typename MSG_T>
  time::TimePoint Send(MSG_T msg, const time::TimePoint& time = trellis::core::time::Now()) {
    auto& receiver = std::get<LoobackIndex<MSG_T>::value>(receivers_);
    const auto send_time = receiver.publisher->Send(receiver.serializer(msg), time);
    receiver.latest.emplace(send_time, std::move(msg));
    return send_time;
  }

 private:
  template <typename R>
  struct Receiver;

  /// @brief Struct to hold the state required for receiving the latest message.
  /// @tparam R the ReceiveType to follow.
  template <IsLatestReceiveType R>
  struct Receiver<R> {
    using ReceiveType = R;
    using MessageType = ReceiveType::MessageType;

    Subscriber<MessageType> subscriber;
    // We use a unique_ptr so we can pass capture in the sub callback safely, even if this receiver moves around. This
    // ptr should always point to the same value. We use `latest->message != nullptr` to check if there is indeed a
    // valid message here. We could wrap it in an optional, but that is not necessary.
    std::unique_ptr<StampedMessagePtr<MessageType>> latest;
    time::TimePoint::duration timeout;
  };

  /// @brief Struct to hold the state required for receiving the N latest messages.
  /// @tparam R the ReceiveType to follow.
  template <IsNLatestReceiveType R>
  struct Receiver<R> {
    using ReceiveType = R;
    using MessageType = ReceiveType::MessageType;

    // Increase subscriber memory pool so it can fill the buffer plus the default padding. The buffer may own at most
    // kNLatest + 2 messages, but a little extra padding allows the inbox thread to get slightly behind the subscriber
    // thread.
    Subscriber<MessageType, ReceiveType::kNLatest + containers::kDefaultSlotSize> subscriber;
    // We use a unique_ptr so we can pass capture in the sub callback safely, even if this receiver moves around. This
    // ptr should always point to the same value.
    std::unique_ptr<containers::RingBuffer<StampedMessagePtr<MessageType>, ReceiveType::kNLatest>> buffer;
    time::TimePoint::duration timeout;
  };

  /// @brief Struct to hold the state required for receiving the all-latest messages.
  /// @tparam R the ReceiveType to follow.
  template <IsAllLatestReceiveType R>
  struct Receiver<R> {
    using ReceiveType = R;
    using MessageType = ReceiveType::MessageType;

    // We use the default subscriber memory pool size since we will copy messages out of the subscriber since we don't
    // know how large to size it.
    Subscriber<MessageType> subscriber;
    // We use a unique_ptr so we can pass capture in the sub callback safely, even if this receiver moves around. This
    // ptr should always point to the same value.
    std::unique_ptr<containers::DynamicRingBuffer<std::pair<time::TimePoint, MessageType>>> buffer;
    time::TimePoint::duration timeout;
  };

  /// @brief Struct to hold the state required for receiving the loopback message.
  /// @tparam R the ReceiveType to follow.
  template <IsLoopbackReceiveType R>
  struct Receiver<R> {
    using ReceiveType = R;
    using MessageType = ReceiveType::MessageType;
    using SerializedType = ReceiveType::SerializedType;

    struct TimedMessage {
      time::TimePoint timestamp;
      MessageType message;
    };

    // TODO(matt): Should we support non-simple funtors?
    ReceiveType::SerializationFn serializer = {};
    Publisher<SerializedType> publisher;
    std::optional<TimedMessage> latest;
    time::TimePoint::duration timeout;
  };

  /// @brief Make a latest receiver for topic at position Index in the ReceiveTypes, topics, and timeouts.
  template <size_t Index>
  static auto MakeLatestReceiver(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    using ReceiveType = std::tuple_element_t<Index, std::tuple<ReceiveTypes...>>;
    using MessageType = ReceiveType::MessageType;

    // Not const to allow move.
    auto latest = std::make_unique<StampedMessagePtr<MessageType>>();

    // Not const to allow move.
    auto subscriber = node.CreateSubscriber<MessageType>(
        topics[Index], [&latest = *latest](const time::TimePoint&, const time::TimePoint& msgtime,
                                           MessagePointer<MessageType> msg) { latest = {msgtime, std::move(msg)}; });

    return Receiver<ReceiveType>{std::move(subscriber), std::move(latest), timeouts[Index]};
  }

  /// @brief Make an n-latest receiver for topic at position Index in the ReceiveTypes, topics, and timeouts.
  template <size_t Index>
  static auto MakeNLatestReceiver(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    using ReceiveType = std::tuple_element_t<Index, std::tuple<ReceiveTypes...>>;
    using MessageType = ReceiveType::MessageType;

    // Not const to allow move.
    auto buffer = std::make_unique<containers::RingBuffer<StampedMessagePtr<MessageType>, ReceiveType::kNLatest>>();

    // Not const to allow move.
    auto subscriber = node.CreateSubscriber<MessageType, ReceiveType::kNLatest + containers::kDefaultSlotSize>(
        topics[Index],
        [&buffer = *buffer](const time::TimePoint&, const time::TimePoint& msgtime, MessagePointer<MessageType> msg) {
          buffer.push_back(StampedMessagePtr<MessageType>{msgtime, std::move(msg)});
        });

    return Receiver<ReceiveType>{std::move(subscriber), std::move(buffer), timeouts[Index]};
  }

  /// @brief Make an all-latest receiver for topic at position Index in the ReceiveTypes, topics, and timeouts.
  template <size_t Index>
  static auto MakeAllLatestReceiver(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    using ReceiveType = std::tuple_element_t<Index, std::tuple<ReceiveTypes...>>;
    using MessageType = ReceiveType::MessageType;

    // Not const to allow move.
    auto buffer = std::make_unique<containers::DynamicRingBuffer<std::pair<time::TimePoint, MessageType>>>();

    // Not const to allow move.
    auto subscriber = node.CreateSubscriber<MessageType>(
        topics[Index],
        [&buffer = *buffer](const time::TimePoint&, const time::TimePoint& msgtime, MessagePointer<MessageType> msg) {
          buffer.push_back({msgtime, *msg});  // Copies the message out of the subscriber memory pool.
        });

    return Receiver<ReceiveType>{std::move(subscriber), std::move(buffer), timeouts[Index]};
  }

  /// @brief Make a loopback receiver for topic at position Index in the ReceiveTypes, topics, and timeouts.
  template <size_t Index>
  static auto MakeLoopbackReceiver(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    using ReceiveType = std::tuple_element_t<Index, std::tuple<ReceiveTypes...>>;
    using SerializedType = ReceiveType::SerializedType;

    return Receiver<ReceiveType>{.publisher = node.CreatePublisher<SerializedType>(std::string{topics[Index]}),
                                 .timeout = timeouts[Index]};
  }

  /// @brief Make the receiver for topic at position Index in the ReceiveTypes, topics, and timeouts.
  template <size_t Index>
  static auto MakeReceiver(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    using ReceiveType = std::tuple_element_t<Index, std::tuple<ReceiveTypes...>>;

    if constexpr (IsLatestReceiveType<ReceiveType>) {
      return MakeLatestReceiver<Index>(node, topics, timeouts);
    } else if constexpr (IsNLatestReceiveType<ReceiveType>) {
      return MakeNLatestReceiver<Index>(node, topics, timeouts);
    } else if constexpr (IsLoopbackReceiveType<ReceiveType>) {
      return MakeLoopbackReceiver<Index>(node, topics, timeouts);
    } else if constexpr (IsAllLatestReceiveType<ReceiveType>) {
      return MakeAllLatestReceiver<Index>(node, topics, timeouts);
    }
  }

  template <size_t... Indices>
  static auto MakeReceivers(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts,
                            std::index_sequence<Indices...>) {
    return std::make_tuple(MakeReceiver<Indices>(node, topics, timeouts)...);
  }

  /// @brief An intermediate of MakeReceivers that creates an index sequence to iterate over the topic and message
  /// timeout arrays.
  static auto MakeReceivers(Node& node, const TopicArray& topics, const MessageTimeouts& timeouts) {
    return MakeReceivers(node, topics, timeouts, std::make_index_sequence<sizeof...(ReceiveTypes)>{});
  }

  /// @brief Message return generation for receiving the latest message.
  template <IsLatestReceiveType R>
  static InboxReturnType_t<R> Receive(const time::TimePoint& time, const Receiver<R>& receiver) {
    if (receiver.latest->message == nullptr) return std::nullopt;                   // No message received yet.
    if (receiver.latest->timestamp < time - receiver.timeout) return std::nullopt;  // Message too old.
    return StampedMessage<typename R::MessageType>{*receiver.latest};
  }

  /// @brief Messages return generation for receiving the latest N messages.
  template <IsNLatestReceiveType R>
  static auto Receive(const time::TimePoint& time, const Receiver<R>& receiver) {
    auto ret = InboxReturnType_t<R>{};
    for (const auto& message : *receiver.buffer) {
      if (message.timestamp < time - receiver.timeout) continue;  // Message too old.
      ret.emplace_back(message);
    }
    return ret;
  }

  /// @brief Messages return generation for receiving the all-latest messages.
  template <IsAllLatestReceiveType R>
  static auto Receive(const time::TimePoint& time, const Receiver<R>& receiver) {
    // Clear out stale messages from the buffer. Single pops are very efficient in the ring buffer.
    while (!receiver.buffer->empty() && receiver.buffer->begin()->first < time - receiver.timeout) {
      receiver.buffer->pop_front();
    }

    auto ret = InboxReturnType_t<R>{};
    ret.reserve(receiver.buffer->size());
    for (const auto& [time, message] : *receiver.buffer) ret.emplace_back(time, message);
    return ret;
  }

  /// @brief Messages return generation for receiving the loopback messages.
  template <IsLoopbackReceiveType R>
  static InboxReturnType_t<R> Receive(const time::TimePoint& time, const Receiver<R>& receiver) {
    if (!receiver.latest.has_value()) return std::nullopt;
    if (receiver.latest->timestamp < time - receiver.timeout) return std::nullopt;  // Message too old.
    return StampedMessage<typename R::MessageType>{receiver.latest->timestamp, receiver.latest->message};
  }

  std::tuple<Receiver<ReceiveTypes>...> receivers_;
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_INBOX_HPP_
