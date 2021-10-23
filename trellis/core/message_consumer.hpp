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

/**
 * MessageConsumer a class to manage consumption of inbound messages from an arbitrary number of subscribers.
 *
 * This class uses variadic templates to specify an arbitrary number of message types for which subscribers will be
 * created. Furthermore, this class will manage FIFOs to queue up inbound messages and deal with thread-safety. All user
 * callbacks will be invoked using the event loop handle, and all thread-synchronization is handled internally. This
 * means that the user can interact with the messages freely (without threading concerns) from any message consumer
 * callbacks or any other callback running on the given event loop (such as timers)
 *
 * @tparam FIFO_DEPTH the maximum depth of the underlying FIFOs.
 * @tparam Types variadic list of message types to consume
 */
template <size_t FIFO_DEPTH, typename... Types>
class MessageConsumer {
 public:
  template <typename T>
  struct StampedMessage {
    time::TimePoint timestamp;
    T message;
  };
  template <typename MSG_T>
  using NewMessageCallback = std::function<void(const MSG_T&, const time::TimePoint&)>;
  using NewMessageCallbacks = std::tuple<NewMessageCallback<Types>...>;
  using UniversalUpdateCallback = std::function<void(void)>;
  using SingleTopic = std::string;
  using TopicsList = std::vector<SingleTopic>;
  using SingleTopicArray = std::array<SingleTopic, sizeof...(Types)>;
  using TopicsArray = std::array<TopicsList, sizeof...(Types)>;

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of topics to subscribe to. The order of topics must match the order of message types in the
   * template arguments
   * @param callback A callback to call any time there's a new inbound message. The user is then responsible for
   * querying the data structures to understand what was updated.
   */
  MessageConsumer(const Node& node, SingleTopicArray topics, UniversalUpdateCallback callback = {})
      : topics_{CreateTopicsArrayFromSingleTopicArray(topics)},
        update_callback_{callback},
        new_message_callbacks_{},
        loop_{node.GetEventLoop()} {
    CreateSubscribers(node);
  }

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of topics to subscribe to. The order of topics must match the order of message types in the
   * template arguments
   * @param callbacks A tuple of callbacks for each message type. The order of topics must match the order of message
   * types in the template arguments
   */
  MessageConsumer(const Node& node, SingleTopicArray topics, NewMessageCallbacks callbacks)
      : topics_{CreateTopicsArrayFromSingleTopicArray(topics)},
        update_callback_{},
        new_message_callbacks_{callbacks},
        loop_{node.GetEventLoop()} {
    CreateSubscribers(node);
  }

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of list topics to subscribe to. The order of topics must match the order of message types in
   * the template arguments. Note this is useful in cases where there are multiple topics with the same message type
   * @param callback A callback to call any time there's a new inbound message. The user is then responsible for
   * querying the data structures to understand what was updated
   */
  MessageConsumer(const Node& node, TopicsArray topics, UniversalUpdateCallback callback = {})
      : topics_{topics}, update_callback_{callback}, new_message_callbacks_{}, loop_{node.GetEventLoop()} {
    CreateSubscribers(node);
  }

  /*
   * MessageConsumer constructor
   *
   * @param node A node instance to create subscriptions with
   * @param topics A list of list topics to subscribe to. The order of topics must match the order of message types in
   * the template arguments. Note this is useful in cases where there are multiple topics with the same message type
   * @param callbacks A tuple of callbacks for each message type. The order of topics must match the order of message
   * types in the template arguments
   */
  MessageConsumer(const Node& node, TopicsArray topics, NewMessageCallbacks callbacks)
      : topics_{topics}, update_callback_{}, new_message_callbacks_{callbacks}, loop_{node.GetEventLoop()} {
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

  /**
   * Newest retrieve a reference to the newest message for the given type
   *
   * @tparam MSG_T the message type to retrieve
   * @param updated an optional user-supplied pointer to a bool which is true if the message was just updated
   * @return A reference to a timestamped message of the given type
   */
  template <typename MSG_T>
  const StampedMessage<MSG_T>& Newest(bool* updated = nullptr) {
    bool updated_msg;
    bool* updated_ptr = (updated == nullptr) ? &updated_msg : updated;
    return fifos_.template Newest<StampedMessage<MSG_T>>(*updated_ptr);
  }

  /**
   * TimedOut determine if too much time has elapsed since the last reception of the given message type
   *
   * @tparam the message type to check
   * @param now A time point intended to represent the current time
   * @param timeout_ms The time duration in which the message is considered to be timed out
   *
   * @return true if the time elapsed since the last message reception is greater than timeout_ms
   */
  template <typename MSG_T>
  bool TimedOut(const time::TimePoint& now, unsigned timeout_ms) {
    const auto& newest_stamp = Newest<MSG_T>().timestamp;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time::now() - newest_stamp);
    return elapsed_ms.count() > timeout_ms;
  }

 private:
  template <typename MSG_T>
  void NewMessage(const MSG_T& msg) {
    fifos_.template Push<StampedMessage<MSG_T>>(std::move(StampedMessage<MSG_T>{time::now(), msg}));

    // Check if we have a callback to signal an update
    if (update_callback_) {
      // XXX(bsirang): are there any thread-safety issues here?
      // According to https://think-async.com/Asio/asio-1.18.2/doc/asio/overview/core/threads.html
      // asio::post() seems to be thread-safe
      auto cb = update_callback_;
      asio::post(*loop_, [cb]() { cb(); });
    }

    // Check if we have a callback to directly ingest a message of this particular type
    const auto& new_message_callback = std::get<NewMessageCallback<MSG_T>>(new_message_callbacks_);
    if (new_message_callback) {
      auto cb = new_message_callback;
      const auto& newest = Newest<MSG_T>();
      asio::post(*loop_, [cb, &newest]() { cb(newest.message, newest.timestamp); });
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
  const EventLoop loop_;
  std::tuple<std::vector<Subscriber<Types>>...> subscribers_;
  trellis::containers::MultiFifo<FIFO_DEPTH, StampedMessage<Types>...> fifos_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_MESSAGE_CONSUMER_HPP
