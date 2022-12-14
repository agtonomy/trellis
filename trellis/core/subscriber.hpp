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

#ifndef TRELLIS_CORE_SUBSCRIBER_HPP_
#define TRELLIS_CORE_SUBSCRIBER_HPP_

#include <ecal/msg/protobuf/dynamic_subscriber.h>
#include <ecal/msg/protobuf/subscriber.h>

#include "monitor_interface.hpp"
#include "proto_utils.hpp"
#include "time.hpp"
#include "timer.hpp"
#include "trellis/containers/memory_pool.hpp"

namespace trellis {
namespace core {

template <typename MSG_T>
class SubscriberImpl {
 public:
  /**
   * @brief Message receive callback
   * @param now the time at which the callback was dispatched
   * @param msgtime the time at which the publisher transmitted the message
   * @param msg the message object that was received
   *
   */
  // using UniquePtr = std::unique_ptr<MSG_T>;
  using MessagePool = containers::MemoryPool<MSG_T>;
  using PointerType = MessagePool::UniquePtr;
  using Callback = std::function<void(const time::TimePoint& now, const time::TimePoint& msgtime, PointerType msg)>;
  using UpdateSimulatedClockFunction = std::function<void(const time::TimePoint&)>;

  /**
   * @brief Construct a subscriber for a given topic
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   * @param update_sim_fn the function to update sim time on receive
   */
  SubscriberImpl(EventLoop ev, std::string topic, Callback callback, UpdateSimulatedClockFunction update_sim_fn)
      : ev_{ev},
        topic_{std::move(topic)},
        ecal_sub_{topic_},
        ecal_sub_raw_{CreateRawTopicSubscriber(topic_)},
        update_sim_fn_{std::move(update_sim_fn)} {
    auto callback_wrapper = [this, callback = std::move(callback)](
                                const char* topic_name_, const trellis::core::TimestampedMessage& msg_, long long time_,
                                long long clock_, long long id_) { CallbackWrapperLogic(msg_, callback); };
    ecal_sub_.AddReceiveCallback(std::move(callback_wrapper));
  }

  /**
   * @brief Construct a subscriber for a given topic with a watchdog timer
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   * @param update_sim_fn the function to update sim time on receive
   * @param watchdog_create_fn the function to create a watchdog timer
   */
  SubscriberImpl(EventLoop ev, std::string topic, Callback callback, UpdateSimulatedClockFunction update_sim_fn,
                 auto watchdog_create_fn)
      : ev_{ev},
        topic_{std::move(topic)},
        ecal_sub_{topic_},
        ecal_sub_raw_{CreateRawTopicSubscriber(topic_)},
        update_sim_fn_{std::move(update_sim_fn)} {
    auto callback_wrapper = [this, callback = std::move(callback), watchdog_create_fn = std::move(watchdog_create_fn)](
                                const char* topic_name_, const trellis::core::TimestampedMessage& msg_, long long time_,
                                long long clock_, long long id_) mutable {
      if (watchdog_timer_ == nullptr) {
        watchdog_timer_ = watchdog_create_fn();
      } else {
        watchdog_timer_->Reset();
      }

      CallbackWrapperLogic(msg_, callback);
    };
    ecal_sub_.AddReceiveCallback(std::move(callback_wrapper));
  }

  /**
   * SetMaxFrequencyThrottle set the maximum callback frequency for this subscriber
   *
   * This is useful in cases where the subscriber wants to process inbound messages
   * at a rate slower than the nominal publish rate. This rate can be changed
   * dynamically, which can be helpful in use cases where a downstream client
   * may want to request data at a specified rate at runtime.
   *
   * @param frequency The upper limit on receive frequency (in Hz)
   */
  void SetMaxFrequencyThrottle(double frequency_hz) {
    if (frequency_hz != 0.0) {
      const unsigned interval_ms = static_cast<unsigned>(1000 / frequency_hz);

      if (interval_ms != 0) {
        rate_throttle_interval_ms_ = interval_ms;
      }
    }
  }

 private:
  void CallbackWrapperLogic(const trellis::core::TimestampedMessage& msg, const Callback& callback) {
    if (!did_receive_) {
      first_receive_time_ = time::Now();
      did_receive_ = true;
    }

    PointerType user_msg{nullptr};
    try {
      user_msg = CreateUserMessage();
    } catch (const std::runtime_error& e) {
      // This is a dynamic subscriber, and we need to wait some time for the monitoring layer to settle after a
      // publisher comes online and before we can retrieve the message schema
      if (std::chrono::duration_cast<std::chrono::milliseconds>(time::Now() - first_receive_time_).count() >
          kMonitorSettlingTime) {
        // Only throw if enough time has passed since our first message was received
        throw e;
      }
    }

    if (user_msg != nullptr) {
      // Here's the handoff from the serialization layer. After this point, the message shall not be copied on its way
      // back to the user
      user_msg->ParseFromString(msg.payload());
      CallbackHelperLogic(trellis::core::time::TimePointFromTimestampedMessage(msg), callback, std::move(user_msg));
    }
  }

  // Common logic between dynamic and non-dynamic case
  void CallbackHelperLogic(trellis::core::time::TimePoint msgtime, const Callback& callback, PointerType user_msg) {
    const unsigned interval_ms = rate_throttle_interval_ms_.load();
    // Update simulated clock if necessary
    update_sim_fn_(msgtime);
    bool should_callback = (interval_ms == 0);
    if (interval_ms) {
      // throttle callback
      const bool enough_time_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(msgtime - last_sent_).count() > interval_ms;
      if (enough_time_elapsed) {
        should_callback = true;
        last_sent_ = msgtime;
      }
    }

    if (should_callback) {
      auto now = time::Now();
      asio::post(*ev_, [now = std::move(now), msgtime = std::move(msgtime), user_msg = std::move(user_msg),
                        callback]() mutable { callback(now, msgtime, std::move(user_msg)); });
    }
  }

  /*
   * To support both dynamic subscribers (specialized with `google::protobuf::Message`) as well as specific message
   * types, we need to use SFINAE (a. la. `std::enable_if_t`) to allow the compiler to select the correct overloads
   * based on whether or not we're using the `google::protobuf::Message` type.
   */
  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  static std::shared_ptr<eCAL::protobuf::CSubscriber<MSG_T>> CreateRawTopicSubscriber(const std::string& topic) {
    return std::make_shared<eCAL::protobuf::CSubscriber<MSG_T>>(proto_utils::GetRawTopicString(topic));
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  static std::shared_ptr<eCAL::protobuf::CSubscriber<MSG_T>> CreateRawTopicSubscriber(const std::string& topic) {
    return nullptr;  // unused for dynamic subscribers
  }

  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  PointerType CreateUserMessage() {
    return pool_.ConstructUniquePointer();
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  PointerType CreateUserMessage() {
    if (dynamic_message_prototype_ == nullptr) {
      monitor_.UpdateSnapshot();
      // This call is expensive, let's cache this message so we can reuse it from here on out.
      dynamic_message_prototype_ =
          monitor_.GetMessageFromTopic(proto_utils::GetRawTopicString(topic_));  // will throw on failure
    }
    return std::unique_ptr<MSG_T>(dynamic_message_prototype_->New());
  }

  // For dynamic subscribers, how long before we give up on metadata from the monitor layer
  static constexpr unsigned kMonitorSettlingTime{1000U};

  EventLoop ev_;
  std::string topic_;

  eCAL::protobuf::CSubscriber<trellis::core::TimestampedMessage> ecal_sub_;

  std::shared_ptr<eCAL::protobuf::CSubscriber<MSG_T>>
      ecal_sub_raw_;  // exists to provide MSG_T metadata on the monitoring layer

  UpdateSimulatedClockFunction update_sim_fn_;

  containers::MemoryPool<MSG_T> pool_{};
  std::atomic<unsigned> rate_throttle_interval_ms_{0};
  trellis::core::time::TimePoint last_sent_{};

  // Used for dynamic subscribers
  trellis::core::MonitorInterface monitor_;

  // Used to know how long to wait for the monitor layer
  time::TimePoint first_receive_time_{};
  bool did_receive_{false};
  Timer watchdog_timer_{nullptr};
  PointerType dynamic_message_prototype_{nullptr};
};

template <typename MSG_T>
using Subscriber = std::shared_ptr<SubscriberImpl<MSG_T>>;

using DynamicSubscriberImpl = SubscriberImpl<google::protobuf::Message>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberImpl>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP_
