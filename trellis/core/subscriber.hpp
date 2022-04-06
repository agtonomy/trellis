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

#ifndef TRELLIS_CORE_SUBSCRIBER_HPP
#define TRELLIS_CORE_SUBSCRIBER_HPP

#include <ecal/msg/protobuf/dynamic_subscriber.h>
#include <ecal/msg/protobuf/subscriber.h>

#include "monitor_interface.hpp"
#include "proto_utils.hpp"
#include "time.hpp"
#include "timer.hpp"

namespace trellis {
namespace core {

template <typename MSG_T>
class SubscriberImpl {
 public:
  using Callback = std::function<void(const time::TimePoint&, const MSG_T&)>;
  using UpdateSimulatedClockFunction = std::function<void(const time::TimePoint&)>;
  using WatchdogCallback = std::function<void(void)>;

  /**
   * Construct a subscriber for a given topic
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   */
  SubscriberImpl(const std::string& topic, Callback callback, UpdateSimulatedClockFunction update_sim_fn)
      : ecal_sub_{topic}, ecal_sub_raw{CreateRawTopicSubscriber(topic)}, update_sim_fn_{update_sim_fn} {
    SetCallbackWithoutWatchdog(callback);
  }

  /**
   * Construct a subscriber for a given topic with a rate throttle
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   * @param max_frequency the maximum frequency (in Hz) in which the callback may be called
   */
  SubscriberImpl(const std::string& topic, Callback callback, double max_frequency_hz,
                 UpdateSimulatedClockFunction update_sim_fn)
      : SubscriberImpl(topic, callback, update_sim_fn) {
    SetMaxFrequencyThrottle(max_frequency_hz);
  }

  /**
   * Construct a subscriber for a given topic with a watchdog timer
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   * @param watchdog_timeout_ms the amount of time in milliseconds in between messages that will trigger the watchdog
   * @param watchdog_callback the function to call when the watchdog fires
   * @param event_loop the event loop handle in which to run the watchdog on
   */
  SubscriberImpl(const std::string& topic, Callback callback, unsigned watchdog_timeout_ms,
                 WatchdogCallback watchdog_callback, EventLoop event_loop, UpdateSimulatedClockFunction update_sim_fn)
      : ecal_sub_{topic}, ecal_sub_raw{CreateRawTopicSubscriber(topic)}, update_sim_fn_{update_sim_fn} {
    SetCallbackWithWatchdog(callback, watchdog_callback, watchdog_timeout_ms, event_loop);
  }

  /**
   * Construct a subscriber for a given topic with a watchdog timer and rate throttle
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   * @param watchdog_timeout_ms the amount of time in milliseconds in between messages that will trigger the watchdog
   * @param watchdog_callback the function to call when the watchdog fires
   * @param event_loop the event loop handle in which to run the watchdog on
   * @param max_frequency the maximum frequency (in Hz) in which the callback may be called
   */
  SubscriberImpl(const std::string& topic, Callback callback, unsigned watchdog_timeout_ms,
                 WatchdogCallback watchdog_callback, EventLoop event_loop, double max_frequency_hz,
                 UpdateSimulatedClockFunction update_sim_fn)
      : SubscriberImpl(topic, callback, watchdog_timeout_ms, watchdog_callback, event_loop, update_sim_fn) {
    SetMaxFrequencyThrottle(max_frequency_hz);
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
  void SetCallbackWithoutWatchdog(Callback callback) {
    auto callback_wrapper = [this, callback](const char* topic_name_, const trellis::core::TimestampedMessage& msg_,
                                             long long time_, long long clock_,
                                             long long id_) { CallbackWrapperLogic(msg_, callback); };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  void SetCallbackWithWatchdog(Callback callback, WatchdogCallback watchdog_callback, unsigned watchdog_timeout_ms,
                               EventLoop event_loop) {
    Timer watchdog_timer{nullptr};
    auto callback_wrapper = [this, callback, watchdog_callback, watchdog_timer, watchdog_timeout_ms, event_loop](
                                const char* topic_name_, const trellis::core::TimestampedMessage& msg_, long long time_,
                                long long clock_, long long id_) mutable {
      if (watchdog_timer == nullptr) {
        // create one shot watchdog timer which automatically loads the timer too
        watchdog_timer = std::make_shared<TimerImpl>(event_loop, TimerImpl::Type::kOneShot, watchdog_callback, 0,
                                                     watchdog_timeout_ms);
      } else {
        watchdog_timer->Reset();
      }

      CallbackWrapperLogic(msg_, callback);
    };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  void CallbackWrapperLogic(const trellis::core::TimestampedMessage& msg, const Callback& callback) {
    if (user_msg_ == nullptr) {
      user_msg_ = CreateUserMessage(msg.payload().type_url());
    }

    if (user_msg_ != nullptr) {
      msg.payload().UnpackTo(&(*user_msg_));
      CallbackHelperLogic(msg, callback);
    }
  }

  // Common logic between dynamic and non-dynamic case
  void CallbackHelperLogic(const trellis::core::TimestampedMessage& msg, const Callback& callback) {
    const unsigned interval_ms = rate_throttle_interval_ms_.load();
    const trellis::core::time::TimePoint msgtime{trellis::core::time::TimePointFromFromTimestampedMessage(msg)};
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
      callback(msgtime, *user_msg_);
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
  std::shared_ptr<MSG_T> CreateUserMessage(const std::string&) {
    return std::make_shared<MSG_T>();
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  std::shared_ptr<MSG_T> CreateUserMessage(const std::string& type_url) {
    monitor_.UpdateSnapshot();
    // This will throw on failure
    return monitor_.GetMessageFromTypeString(proto_utils::GetTypeFromURL(type_url));
  }

  eCAL::protobuf::CSubscriber<trellis::core::TimestampedMessage> ecal_sub_;

  std::shared_ptr<eCAL::protobuf::CSubscriber<MSG_T>>
      ecal_sub_raw;  // exists to provide MSG_T metadata on the monitoring layer

  UpdateSimulatedClockFunction update_sim_fn_;

  std::atomic<unsigned> rate_throttle_interval_ms_{0};
  trellis::core::time::TimePoint last_sent_{};

  // Used for dynamic subscribers
  trellis::core::MonitorInterface monitor_;

  // Cache the message sent to the user, using a shared pointer here since
  // it's useful in the dynamic case where MSG_T = google::protobuf::Message
  std::shared_ptr<MSG_T> user_msg_{nullptr};
};

template <typename MSG_T>
using Subscriber = std::shared_ptr<SubscriberImpl<MSG_T>>;

using DynamicSubscriberImpl = SubscriberImpl<google::protobuf::Message>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberImpl>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP
