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

#include "trellis/containers/multi_fifo.hpp"
#include "trellis/core/constraints.hpp"
#include "trellis/core/node.hpp"
#include "trellis/core/stamped_message.hpp"
#include "trellis/core/subscriber.hpp"

namespace trellis::core {

namespace detail {
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

// Helper to make default converters
template <class... T>
auto MakeIdentityConverters() {
  return std::make_tuple(((void)sizeof(T), std::identity())...);
}

}  // namespace detail

template <typename SerializableT, typename MsgT = SerializableT, typename ConverterT = std::identity>
  requires constraints::_IsDynamic<SerializableT, MsgT, ConverterT> ||
           constraints::_IsConverter<ConverterT, SerializableT, MsgT>
struct TypeTuple {
  using SerializableType = SerializableT;
  using MsgType = MsgT;
  using ConverterType = ConverterT;
};

template <typename T>
concept _IsTypeTuple = requires {
  typename T::SerializableType;
  typename T::MsgType;
  typename T::ConverterType;
};

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
 * off of the underlying FIFO as they come in. The other pattern is to call Newest<MsgT>() to access the most recent
 * message. For a given message type, these patterns should not be mixed.
 *
 * For applications that only care about the most recent messages at each cycle of execution, they should use FIFO_DEPTH
 * of one. Also using Newest<>() with FIFO_DEPTH greater than one doesn't generally make sense to do.
 *
 * This class supports opt-in automatic conversion from protobuf messages to native C++ types. To use this feature,
 * callers must specify the serializable, native, and converter types as template parameters in the `TypeTuple`s. A
 * tuple of concrete converters is passed as a constructor argument. Free functions or functors can be used; the type of
 * a free function `Foo` can be deduced easily via `decltype(Foo)`.
 *
 * @tparam FIFO_DEPTH the maximum depth of the underlying FIFOs.
 * @tparam Types variadic list of `TypeTuple` structs
 */
template <size_t FIFO_DEPTH, _IsTypeTuple... Types>
class MessageConsumer {
 public:
  /**
   * @brief Callback when a new message of a particular type is received
   *
   * @tparam MsgT the particular message type to receive; in spirit, this should align with the MsgT in the TypeTuple
   *
   * @param topic the topic that the message is received from (useful when multiple topics carry the same type)
   * @param msg the message object that was received
   * @param now the time at which the callback was dispatched
   * @param msgtime the time at which the publisher transmitted the message
   */
  template <typename MsgT>
  using NewMessageCallback = std::function<void(const std::string& topic, const MsgT& msg, const time::TimePoint& now,
                                                const time::TimePoint& msgtime)>;
  using NewMessageCallbacks = std::tuple<NewMessageCallback<typename Types::MsgType>...>;
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
  using ConverterTuple = std::tuple<typename Types::ConverterType...>;

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
   * @param converters A tuple of converters as defined by the `TypeTuple`s
   */
  MessageConsumer(Node& node, SingleTopicArray topics, UniversalUpdateCallback callback = {},
                  OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                  WatchdogCallbacksArray watchdog_callbacks = {}, OptionalMaxFrequencyArray max_frequencies_hz = {},
                  ConverterTuple converters = detail::MakeIdentityConverters<Types...>())
      : MessageConsumer(node, CreateTopicsArrayFromSingleTopicArray(topics), callback, watchdog_timeouts_ms,
                        watchdog_callbacks, max_frequencies_hz, converters) {}

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
   * @param converters A tuple of converters as defined by the `TypeTuple`s
   */
  MessageConsumer(Node& node, SingleTopicArray topics, NewMessageCallbacks callbacks,
                  OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                  WatchdogCallbacksArray watchdog_callbacks = {}, OptionalMaxFrequencyArray max_frequencies_hz = {},
                  ConverterTuple converters = detail::MakeIdentityConverters<Types...>())
      : MessageConsumer(node, CreateTopicsArrayFromSingleTopicArray(topics), callbacks, watchdog_timeouts_ms,
                        watchdog_callbacks, max_frequencies_hz, converters) {}

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of topics to subscribe to. The order of topics must match the order of message types in the
   * template arguments
   * @param callbacks A tuple of callbacks for each message type. The order of topics must match the order of message
   * types in the template arguments
   * @param converters A tuple of converters as defined by the `TypeTuple`s
   * @param watchdog_timeouts_ms an optional array of watchdog timeouts for each message type
   * @param watchdog_callbacks an array of optional watchdog callbacks
   * @param max_frequencies_hz an array of optional maximum frequencies (in Hz)
   */
  MessageConsumer(Node& node, SingleTopicArray topics, NewMessageCallbacks callbacks, ConverterTuple converters,
                  OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                  WatchdogCallbacksArray watchdog_callbacks = {}, OptionalMaxFrequencyArray max_frequencies_hz = {})
      : MessageConsumer(node, CreateTopicsArrayFromSingleTopicArray(topics), callbacks, watchdog_timeouts_ms,
                        watchdog_callbacks, max_frequencies_hz, converters) {}

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
   * @param converters A tuple of converters as defined by the `TypeTuple`s
   */
  explicit MessageConsumer(Node& node, TopicsArray topics, UniversalUpdateCallback callback = {},
                           OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                           WatchdogCallbacksArray watchdog_callbacks = {},
                           OptionalMaxFrequencyArray max_frequencies_hz = {},
                           ConverterTuple converters = detail::MakeIdentityConverters<Types...>())
      : topics_{topics},
        update_callback_{callback},
        new_message_callbacks_{},
        watchdog_timeouts_ms_{watchdog_timeouts_ms},
        watchdog_callbacks_{watchdog_callbacks},
        max_frequencies_hz_{max_frequencies_hz},
        converters_{std::move(converters)} {
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
   * @param converters A tuple of converters as defined by the `TypeTuple`s
   */
  explicit MessageConsumer(Node& node, TopicsArray topics, NewMessageCallbacks callbacks,
                           OptionalWatchdogTimeoutsArray watchdog_timeouts_ms = {},
                           WatchdogCallbacksArray watchdog_callbacks = {},
                           OptionalMaxFrequencyArray max_frequencies_hz = {},
                           ConverterTuple converters = detail::MakeIdentityConverters<Types...>())
      : topics_{topics},
        update_callback_{},
        new_message_callbacks_{callbacks},
        watchdog_timeouts_ms_{watchdog_timeouts_ms},
        watchdog_callbacks_{watchdog_callbacks},
        max_frequencies_hz_{max_frequencies_hz},
        converters_{std::move(converters)} {
    CreateSubscribers(node);
  }

  /**
   * @brief Size return the number of elements in the FIFO for the given type
   *
   * @tparam SerializableT the message type to retrieve;
   *                       in spirit, this should align with the SerializableT in the TypeTuple.

   * @return size_t the number of MsgT elements in the FIFO
   */
  template <typename SerializableT>
  size_t Size() {
    return fifos_.template Size<StampedMessagePtr<SerializableT>>();
  }

  /**
   * @brief Newest retrieve the newest (most recent) message for the given type
   *
   * Note: if no messages have been received a default-constructed MsgT is returned
   * Caution: Applications should only use the Newest API if they are not NewMessageCallback
   * functions since the FIFOs are drained when messages are passed to the new message callbacks
   *
   * @tparam MsgT the particular message type to receive; in spirit, this should align with the MsgT in the TypeTuple
   *
   * @return StampedMessage<MsgT>& A reference to a timestamped message of the given type
   */
  template <typename MsgT>
  StampedMessage<MsgT> Newest() {
    const std::size_t tuple_index = detail::Index<MsgT, std::tuple<typename Types::MsgType...>>::value;
    using SerializableType = std::tuple_element_t<tuple_index, std::tuple<typename Types::SerializableType...>>;
    // TODO (bsirang) look into evaluating this at compile time.
    const auto& new_message_callback = std::get<tuple_index>(new_message_callbacks_);
    if (new_message_callback) {
      throw std::runtime_error(
          "Invalid use of Newest<>() while a new message callback was given for the message type.");
    }
    return StampedMessage<MsgT>(
        std::get<tuple_index>(converters_)(fifos_.template Newest<StampedMessagePtr<SerializableType>>()));
  }

  /**
   * @brief  TimedOut determine if too much time has elapsed since the last reception of the given message type
   *
   * @tparam SerializableT the message type to check;
   *                       in spirit, this should align with the SerializableT in the TypeTuple.
   *
   * @param now A time point intended to represent the current time
   * @param timeout_ms The time duration in which the message is considered to be timed out
   *
   * @return true if the time elapsed since the last message reception is greater than timeout_ms
   */
  template <typename SerializableT>
  bool TimedOut(const time::TimePoint& now, unsigned timeout_ms) {
    const std::size_t tuple_index =
        detail::Index<SerializableT, std::tuple<typename Types::SerializableType...>>::value;
    const auto& latest_stamp = latest_timestamps_[tuple_index];
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time::Now() - latest_stamp);
    return elapsed_ms.count() > timeout_ms;
  }

  /**
   * @brief  SetMaxFrequencyThrottle throttle the frequency of a particular message type
   *
   * @tparam SerializableT the message type to throttle;
   *                       in spirit, this should align with the SerializableT in the TypeTuple.
   *
   * @param max_frequency the maximum frequency of message updates for each subscriber of
   * the given message type
   */
  template <typename SerializableT>
  void SetMaxFrequencyThrottle(double max_frequency) {
    auto& subscribers = std::get<std::vector<Subscriber<SerializableT>>>(subscribers_);
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
    using SerializableType = std::tuple_element_t<I, std::tuple<typename Types::SerializableType...>>;
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
                                                                 MessagePointer<SerializableType> msg) {
        latest_stamp = msgtime;
        NewMessage<I, SerializableType>(topic, now, msgtime, std::move(msg));
      };
      if (do_frequency_throttle && do_watchdog) {
        const auto& frequency_throttle_hz = (*max_frequencies_hz_)[I];
        const auto& watchdog_timeout = (*watchdog_timeouts_ms_)[I];
        auto watchdog_callback_wrapper = [topic, watchdog_callback](const time::TimePoint& now) {
          watchdog_callback(topic, now);
        };
        subscriber_list.emplace_back(node.CreateSubscriber<SerializableType>(
            topic, message_callback, watchdog_timeout, watchdog_callback_wrapper, frequency_throttle_hz));
      } else if (do_frequency_throttle && !do_watchdog) {
        const auto& frequency_throttle_hz = (*max_frequencies_hz_)[I];
        subscriber_list.emplace_back(
            node.CreateSubscriber<SerializableType>(topic, message_callback, {}, {}, frequency_throttle_hz));
      } else if (!do_frequency_throttle && do_watchdog) {
        const auto& watchdog_timeout = (*watchdog_timeouts_ms_)[I];
        auto watchdog_callback_wrapper = [topic, watchdog_callback](const time::TimePoint& now) {
          watchdog_callback(topic, now);
        };
        subscriber_list.emplace_back(node.CreateSubscriber<SerializableType>(topic, message_callback, watchdog_timeout,
                                                                             watchdog_callback_wrapper));
      } else {
        subscriber_list.emplace_back(node.CreateSubscriber<SerializableType>(topic, message_callback));
      }
    }

    CreateSubscribers<I + 1>(node);
  }

  template <std::size_t I, typename SerializableT>
  void NewMessage(const std::string& topic, const time::TimePoint& now, const time::TimePoint& msgtime,
                  MessagePointer<SerializableT> msg) {
    fifos_.template Push<StampedMessagePtr<SerializableT>>(StampedMessagePtr<SerializableT>{msgtime, std::move(msg)});

    // Check if we have a callback to signal an update
    if (update_callback_) {
      update_callback_();
    }

    // Check if we have a callback to directly ingest a message of this particular type
    const auto& new_message_callback = std::get<I>(new_message_callbacks_);
    if (new_message_callback) {
      auto next = fifos_.template Next<StampedMessagePtr<SerializableT>>();
      new_message_callback(topic, std::get<I>(converters_)(*next.message), now, next.timestamp);
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
  std::tuple<std::vector<Subscriber<typename Types::SerializableType>>...> subscribers_;
  trellis::containers::MultiFifo<FIFO_DEPTH, StampedMessagePtr<typename Types::SerializableType>...> fifos_;
  LatestTimestampArray latest_timestamps_;
  ConverterTuple converters_;
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_MESSAGE_CONSUMER_HPP_
