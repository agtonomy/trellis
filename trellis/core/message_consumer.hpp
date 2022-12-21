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

#ifndef TRELLIS_CORE_MESSAGE_CONSUMER_HPP_
#define TRELLIS_CORE_MESSAGE_CONSUMER_HPP_

#include <array>
#include <string>
#include <type_traits>
#include <utility>

#include "node.hpp"
#include "subscriber.hpp"
#include "trellis/containers/multi_fifo.hpp"

namespace trellis {
namespace core {

namespace {
// Helper to find the tuple index for a given type
// Borrowed from https://stackoverflow.com/questions/18063451/get-index-of-a-tuple-elements-type
template <class T, class Tuple>
struct Index;

template <class T, class... Types>
struct Index<T, std::tuple<T, Types...>> {
  static const std::size_t value = 0;
};

template <class T, class U, class... Types>
struct Index<T, std::tuple<U, Types...>> {
  static const std::size_t value = 1 + Index<T, std::tuple<Types...>>::value;
};
}  // namespace

/**
 * MessageConsumer a class to manage consumption of inbound messages from an arbitrary number of subscribers.
 *
 * This class uses variadic templates to specify an arbitrary number of message types for which subscribers will be
 * created. Furthermore, this class will manage FIFOs to queue up inbound messages. All user
 * callbacks will be invoked using the event loop handle used by the underlying subscribers. This
 * means that the user can interact with the messages freely (without threading concerns) from any message consumer
 * callbacks or any other callback running on the given event loop (such as timers)
 *
 * There are two high-level usage patterns for this module. The first pattern is to use a callback to consume messages
 * off of the underlying FIFO as they come in. The other pattern is to call Newest<MSG_T>() to access the most recent
 * message. For a given message type, these patterns should not be mixed.
 *
 * For applications that only care about the most recent messages at each cycle of execution, they should use FIFO_DEPTH
 * of one. Also using Newest<>() with FIFO_DEPTH greater than one doesn't generally make sense to do.
 *
 * @tparam FIFO_DEPTH the maximum depth of the underlying FIFOs.
 * @tparam Types variadic list of message types to consume
 */
template <size_t FIFO_DEPTH, typename... Types>
class MessageConsumer {
 public:
  template <typename T>
  using PointerType = SubscriberImpl<T>::PointerType;

  /**
   * @brief Structure to hold a timestamp and a message pointer
   *
   * Lightweight structure to hold a timestamp and a pointer to a message. This structure is used with the FIFOs
   *
   * @tparam T Message type to hold
   */
  template <typename T>
  struct StampedMessagePtr {
    time::TimePoint timestamp;
    PointerType<T> message;
  };

  /**
   * @brief Structure to hold a timestamp and a const ref message pointer
   *
   * Lightweight structure to hold a timestamp and a const reference to a message. This structure is passed to users.
   *
   * @tparam T Message type to hold
   * @see Newest<T>()
   */
  template <typename T>
  struct StampedMessage {
    StampedMessage(const StampedMessagePtr<T>& ptr) : timestamp{ptr.timestamp}, message{*(ptr.message)} {}
    time::TimePoint timestamp;
    const T& message;
  };

  /**
   * @brief Callback when a new message of a particular type is received
   *
   * @tparam MSG_T the particular message type to receive
   * @param topic the topic that the message is received from (useful when multiple topics carry the same type)
   * @param msg the message object that was received
   * @param now the time at which the callback was dispatched
   * @param msgtime the time at which the publisher transmitted the message
   */
  template <typename MSG_T>
  using NewMessageCallback = std::function<void(const std::string& topic, const MSG_T& msg, const time::TimePoint& now,
                                                const time::TimePoint& msgtime)>;
  using NewMessageCallbacks = std::tuple<NewMessageCallback<Types>...>;
  using UniversalUpdateCallback = std::function<void(void)>;
  using SingleTopic = std::string;
  using TopicsList = std::vector<SingleTopic>;
  using SingleTopicArray = std::array<SingleTopic, sizeof...(Types)>;
  using TopicsArray = std::array<TopicsList, sizeof...(Types)>;
  using OptionalWatchdogTimeoutsArray = std::optional<std::array<unsigned, sizeof...(Types)>>;
  using WatchdogCallback = std::function<void(const std::string&, const time::TimePoint&)>;
  using WatchdogCallbacksArray = std::array<WatchdogCallback, sizeof...(Types)>;
  using OptionalMaxFrequencyArray = std::optional<std::array<double, sizeof...(Types)>>;
  using LatestTimestampArray = std::array<time::TimePoint, sizeof...(Types)>;

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of topics to subscribe to. The order of topics must match the order of message types in the
   * template arguments
   * @param callback A callback to call any time there's a new inbound message. The user is then responsible for
   * querying the data structures to understand what was updated.
   * @param watchdog_timeouts_ms an optional array of watchdog timeouts for each message type
   * @param watchdog_callbacks an array of optional watchdog callbacks
   * @param max_frequencies_hz an array of optional maximum frequencies (in Hz) for each subscriber, use 0.0 to skip
   * rate throttling for a particular message type
   */
  MessageConsumer(Node& node, SingleTopicArray topics, UniversalUpdateCallback callback = {},
                  OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                  WatchdogCallbacksArray watchdog_callbacks = {}, OptionalMaxFrequencyArray max_frequencies_hz = {})
      : MessageConsumer(node, CreateTopicsArrayFromSingleTopicArray(topics), callback, watchdog_timeouts_ms,
                        watchdog_callbacks) {}

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of topics to subscribe to. The order of topics must match the order of message types in the
   * template arguments
   * @param callbacks A tuple of callbacks for each message type. The order of topics must match the order of message
   * types in the template arguments
   * @param watchdog_timeouts_ms an optional array of watchdog timeouts for each message type
   * @param watchdog_callbacks an array of optional watchdog callbacks
   * @param max_frequencies_hz an array of optional maximum frequencies (in Hz)
   */
  MessageConsumer(Node& node, SingleTopicArray topics, NewMessageCallbacks callbacks,
                  OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                  WatchdogCallbacksArray watchdog_callbacks = {}, OptionalMaxFrequencyArray max_frequencies_hz = {})
      : MessageConsumer(node, CreateTopicsArrayFromSingleTopicArray(topics), callbacks, watchdog_timeouts_ms,
                        watchdog_callbacks) {}

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of list topics to subscribe to. The order of topics must match the order of message types in
   * the template arguments. Note this is useful in cases where there are multiple topics with the same message type
   * @param callback A callback to call any time there's a new inbound message. The user is then responsible for
   * querying the data structures to understand what was updated
   * @param watchdog_timeouts_ms an optional array of watchdog timeouts for each message type
   * @param watchdog_callbacks an array of optional watchdog callbacks
   * @param max_frequencies_hz an array of optional maximum frequencies (in Hz)
   */
  explicit MessageConsumer(Node& node, TopicsArray topics, UniversalUpdateCallback callback = {},
                           OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                           WatchdogCallbacksArray watchdog_callbacks = {},
                           OptionalMaxFrequencyArray max_frequencies_hz = {})
      : topics_{topics},
        update_callback_{callback},
        new_message_callbacks_{},
        watchdog_timeouts_ms_{watchdog_timeouts_ms},
        watchdog_callbacks_{watchdog_callbacks},
        max_frequencies_hz_{max_frequencies_hz} {
    CreateSubscribers(node);
  }

  /*
   * @brief  MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of list topics to subscribe to. The order of topics must match the order of message types in
   * the template arguments. Note this is useful in cases where there are multiple topics with the same message type
   * @param callbacks A tuple of callbacks for each message type. The order of topics must match the order of message
   * types in the template arguments
   * @param watchdog_timeouts_ms an optional array of watchdog timeouts for each message type
   * @param watchdog_callbacks an array of optional watchdog callbacks
   * @param max_frequencies_hz an array of optional maximum frequencies (in Hz)
   */
  explicit MessageConsumer(Node& node, TopicsArray topics, NewMessageCallbacks callbacks,
                           OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                           WatchdogCallbacksArray watchdog_callbacks = {},
                           OptionalMaxFrequencyArray max_frequencies_hz = {})
      : topics_{topics},
        update_callback_{},
        new_message_callbacks_{callbacks},
        watchdog_timeouts_ms_{watchdog_timeouts_ms},
        watchdog_callbacks_{watchdog_callbacks},
        max_frequencies_hz_{max_frequencies_hz} {
    CreateSubscribers(node);
  }

  /**
   * @brief Size return the number of elements in the FIFO for the given type
   *
   * @tparam MSG_T the message type to retrieve
   * @return size_t the number of MSG_T elements in the FIFO
   */
  template <typename MSG_T>
  size_t Size() {
    return fifos_.template Size<StampedMessagePtr<MSG_T>>();
  }

  /**
   * @brief Newest retrieve the newest (most recent) message for the given type
   *
   * Note: if no messages have been received a default-constructed MSG_T is returned
   * Caution: Applications should only use the Newest API if they are not NewMessageCallback
   * functions since the FIFOs are drained when messages are passed to the new message callbacks
   *
   * @tparam MSG_T the message type to retrieve
   * @return StampedMessage<MSG_T>& A reference to a timestamped message of the given type
   */
  template <typename MSG_T>
  StampedMessage<MSG_T> Newest() {
    // TODO (bsirang) look into evaluating this at compile time.
    const auto& new_message_callback = std::get<NewMessageCallback<MSG_T>>(new_message_callbacks_);
    if (new_message_callback) {
      throw std::runtime_error(
          "Invalid use of Newest<>() while a new message callback was given for the message type.");
    }
    return StampedMessage<MSG_T>(fifos_.template Newest<StampedMessagePtr<MSG_T>>());
  }

  /**
   * @brief  TimedOut determine if too much time has elapsed since the last reception of the given message type
   *
   * @tparam the message type to check
   * @param now A time point intended to represent the current time
   * @param timeout_ms The time duration in which the message is considered to be timed out
   *
   * @return true if the time elapsed since the last message reception is greater than timeout_ms
   */
  template <typename MSG_T>
  bool TimedOut(const time::TimePoint& now, unsigned timeout_ms) {
    const std::size_t tuple_index = Index<MSG_T, std::tuple<Types...>>::value;
    const auto& latest_stamp = latest_timestamps_[tuple_index];
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time::Now() - latest_stamp);
    return elapsed_ms.count() > timeout_ms;
  }

  /**
   * @brief  SetMaxFrequencyThrottle throttle the frequency of a particular message type
   *
   * @tparam the message type to throttle
   * @param max_frequency the maximum frequency of message updates for each subscriber of
   * the given message type
   */
  template <typename MSG_T>
  void SetMaxFrequencyThrottle(double max_frequency) {
    auto& subscribers = std::get<std::vector<Subscriber<MSG_T>>>(subscribers_);
    for (auto& subscriber : subscribers) {
      subscriber->SetMaxFrequencyThrottle(max_frequency);
    }
  }

 private:
  template <size_t I = 0>
  inline typename std::enable_if<I == sizeof...(Types), void>::type CreateSubscribers(Node& node) {}

  template <size_t I = 0>
      inline typename std::enable_if < I<sizeof...(Types), void>::type CreateSubscribers(Node& node) {
    const auto& topics = topics_[I];
    const auto& watchdog_callback = watchdog_callbacks_[I];
    auto& latest_stamp = latest_timestamps_[I];
    using MessageType = std::tuple_element_t<I, std::tuple<Types...>>;
    auto& subscriber_list = std::get<I>(subscribers_);
    const bool do_watchdog = static_cast<bool>(watchdog_timeouts_ms_ && watchdog_callback);
    const bool do_frequency_throttle = static_cast<bool>(max_frequencies_hz_) && ((*max_frequencies_hz_)[I] != 0.0);
    for (const auto& topic : topics) {
      // We have 2 optional features we can turn on (watchdogs and frequency throttles), so we have 4 total cases
      // for creating subscribers based on these optional features. They are enumerated in this if/else chain.
      // XXX(bsirang): currently if there are multiple subscribers of the same message type, they will all share the
      // same rate limits and watchdog timeouts. This can be made to be more flexible in the future.
      const auto message_callback = [topic, this, &latest_stamp](const time::TimePoint& now,
                                                                 const time::TimePoint& msgtime,
                                                                 SubscriberImpl<MessageType>::PointerType msg) {
        latest_stamp = msgtime;
        NewMessage<MessageType>(topic, now, msgtime, std::move(msg));
      };
      if (do_frequency_throttle && do_watchdog) {
        const auto& frequency_throttle_hz = (*max_frequencies_hz_)[I];
        const auto& watchdog_timeout = (*watchdog_timeouts_ms_)[I];
        auto watchdog_callback_wrapper = [topic, watchdog_callback](const time::TimePoint& now) {
          watchdog_callback(topic, now);
        };
        subscriber_list.emplace_back(node.CreateSubscriber<MessageType>(
            topic, message_callback, watchdog_timeout, watchdog_callback_wrapper, frequency_throttle_hz));
      } else if (do_frequency_throttle && !do_watchdog) {
        const auto& frequency_throttle_hz = (*max_frequencies_hz_)[I];
        subscriber_list.emplace_back(
            node.CreateSubscriber<MessageType>(topic, message_callback, {}, {}, frequency_throttle_hz));
      } else if (!do_frequency_throttle && do_watchdog) {
        const auto& watchdog_timeout = (*watchdog_timeouts_ms_)[I];
        auto watchdog_callback_wrapper = [topic, watchdog_callback](const time::TimePoint& now) {
          watchdog_callback(topic, now);
        };
        subscriber_list.emplace_back(
            node.CreateSubscriber<MessageType>(topic, message_callback, watchdog_timeout, watchdog_callback_wrapper));
      } else {
        subscriber_list.emplace_back(node.CreateSubscriber<MessageType>(topic, message_callback));
      }
    }

    CreateSubscribers<I + 1>(node);
  }

  template <typename MSG_T>
  void NewMessage(const std::string& topic, const time::TimePoint& now, const time::TimePoint& msgtime,
                  SubscriberImpl<MSG_T>::PointerType msg) {
    fifos_.template Push<StampedMessagePtr<MSG_T>>(StampedMessagePtr<MSG_T>{msgtime, std::move(msg)});

    // Check if we have a callback to signal an update
    if (update_callback_) {
      update_callback_();
    }

    // Check if we have a callback to directly ingest a message of this particular type
    const auto& new_message_callback = std::get<NewMessageCallback<MSG_T>>(new_message_callbacks_);
    if (new_message_callback) {
      auto next = fifos_.template Next<StampedMessagePtr<MSG_T>>();
      new_message_callback(topic, *next.message, now, next.timestamp);
    }
  }

  static TopicsArray CreateTopicsArrayFromSingleTopicArray(SingleTopicArray single_topic_array) {
    TopicsArray output;
    for (unsigned i = 0; i < output.size(); ++i) {
      output[i].push_back(single_topic_array[i]);
    }
    return output;
  }
  const TopicsArray topics_;
  const UniversalUpdateCallback update_callback_;
  const NewMessageCallbacks new_message_callbacks_;
  const OptionalWatchdogTimeoutsArray watchdog_timeouts_ms_;
  const WatchdogCallbacksArray watchdog_callbacks_;
  const OptionalMaxFrequencyArray max_frequencies_hz_;
  std::tuple<std::vector<Subscriber<Types>>...> subscribers_;
  trellis::containers::MultiFifo<FIFO_DEPTH, StampedMessagePtr<Types>...> fifos_;
  LatestTimestampArray latest_timestamps_;

  static_assert(FIFO_DEPTH < containers::kDefaultSlotSize,
                "We use the default slot side for the subscribers, and the FIFO may hold up to FIFO_DEPTH+1 objects, "
                "so we need it to be strictly larger.");
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_MESSAGE_CONSUMER_HPP_
