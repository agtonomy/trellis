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

#include "time.hpp"
#include "timer.hpp"

namespace trellis {
namespace core {

template <typename MSG_T, typename ECAL_SUB_T = eCAL::protobuf::CSubscriber<MSG_T>>
class SubscriberImpl {
 public:
  using Callback = std::function<void(const MSG_T&)>;
  using WatchdogCallback = std::function<void(void)>;

  /**
   * Construct a subscriber for a given topic
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   */
  SubscriberImpl(const std::string& topic, Callback callback) : ecal_sub_{topic} {
    SetCallbackWithoutWatchdog(callback);
  }

  /**
   * Construct a subscriber for a given topic with a rate throttle
   *
   * @param topic the topic string to subscribe to
   * @param callback the callback function to receive messages on
   * @param max_frequency the maximum frequency in which the callback may be called
   */
  SubscriberImpl(const std::string& topic, Callback callback, double max_frequency) : SubscriberImpl(topic, callback) {
    SetMaxFrequencyThrottle(max_frequency);
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
                 WatchdogCallback watchdog_callback, EventLoop event_loop)
      : ecal_sub_{topic} {
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
   * @param max_frequency the maximum frequency in which the callback may be called
   */
  SubscriberImpl(const std::string& topic, Callback callback, unsigned watchdog_timeout_ms,
                 WatchdogCallback watchdog_callback, EventLoop event_loop, double max_frequency)
      : SubscriberImpl(topic, callback, watchdog_timeout_ms, watchdog_callback, event_loop) {
    SetMaxFrequencyThrottle(max_frequency);
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
  void SetMaxFrequencyThrottle(double frequency) {
    if (frequency != 0.0) {
      const unsigned interval_ms = static_cast<unsigned>(1000 / frequency);

      if (interval_ms != 0) {
        rate_throttle_interval_ms_ = interval_ms;
      }
    }
  }

 private:
  using RawCallback =
      std::function<void(const char* topic_name_, const MSG_T& msg_, long long time_, long long clock_, long long id_)>;

  /*
   * To support both dynamic subscribers (specialized with `google::protobuf::Message`) as well as specific message
   * types, we need to use SFINAE (a. la. `std::enable_if_t`) to allow the compiler to select the correct
   * `SetCallbackWithoutWatchdog` and `SetCallbackWithWatchdog` overloads based on whether or not we're using the
   * `google::protobuf::Message` type. This is because unfortunately there's a special case because eCAL's dynamic
   * subscriber callbacks use a slightly different function signature.
   */
  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithoutWatchdog(Callback callback) {
    auto callback_wrapper = [this, callback](const char* topic_name_, const MSG_T& msg_, long long time_,
                                             long long clock_, long long id_) { CallbackWrapperLogic(msg_, callback); };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithoutWatchdog(Callback callback) {
    auto callback_wrapper = [this, callback](const char* topic_name_, const MSG_T& msg_, long long time_) {
      CallbackWrapperLogic(msg_, callback);
    };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithWatchdog(Callback callback, WatchdogCallback watchdog_callback, unsigned watchdog_timeout_ms,
                               EventLoop event_loop) {
    Timer watchdog_timer{nullptr};
    auto callback_wrapper = [this, callback, watchdog_callback, watchdog_timer, watchdog_timeout_ms, event_loop](
                                const char* topic_name_, const MSG_T& msg_, long long time_, long long clock_,
                                long long id_) mutable {
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

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithWatchdog(Callback callback, WatchdogCallback watchdog_callback, unsigned watchdog_timeout_ms,
                               EventLoop event_loop) {
    Timer watchdog_timer{nullptr};
    auto callback_wrapper = [this, callback, watchdog_callback, watchdog_timer, watchdog_timeout_ms, event_loop](
                                const char* topic_name_, const MSG_T& msg_, long long time_) mutable {
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

  void CallbackWrapperLogic(const MSG_T& msg, const Callback& callback) {
    if (rate_throttle_interval_ms_) {
      // throttle callback
      const bool enough_time_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(time::now() - last_sent_).count() >
          rate_throttle_interval_ms_;
      if (enough_time_elapsed) {
        callback(msg);
        last_sent_ = trellis::core::time::now();
      }
    } else {
      callback(msg);
    }
  }

  ECAL_SUB_T ecal_sub_;
  EventLoop ev_loop_;
  std::atomic<unsigned> rate_throttle_interval_ms_{0};
  trellis::core::time::TimePoint last_sent_{};
};

template <typename MSG_T, typename ECAL_SUB_T = eCAL::protobuf::CSubscriber<MSG_T>>
using Subscriber = std::shared_ptr<SubscriberImpl<MSG_T, ECAL_SUB_T>>;

using DynamicSubscriberClass = SubscriberImpl<google::protobuf::Message, eCAL::protobuf::CDynamicSubscriber>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberClass>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP
