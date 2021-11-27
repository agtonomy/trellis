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
  SubscriberImpl(const std::string& topic, Callback callback) : ecal_sub_{topic} {
    SetCallbackWithoutWatchdog(callback);
  }

  SubscriberImpl(const std::string& topic, Callback callback, unsigned watchdog_timeout_ms,
                 WatchdogCallback watchdog_callback, EventLoop event_loop)
      : ecal_sub_{topic} {
    SetCallbackWithWatchdog(callback, watchdog_callback, watchdog_timeout_ms, event_loop);
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
    const unsigned interval_ms = static_cast<unsigned>(1000 / frequency);
    *rate_throttle_interval_ms_ = interval_ms;
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
    if (!rate_throttle_interval_ms_) {
      callback(msg);
    } else {
      // throttle callback
      const bool enough_time_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(time::now() - last_sent_).count() >
          *rate_throttle_interval_ms_;
      if (enough_time_elapsed) {
        callback(msg);
        last_sent_ = trellis::core::time::now();
      }
    }
  }

  ECAL_SUB_T ecal_sub_;
  EventLoop ev_loop_;
  std::optional<std::atomic<unsigned>> rate_throttle_interval_ms_;
  trellis::core::time::TimePoint last_sent_;
};

template <typename MSG_T, typename ECAL_SUB_T = eCAL::protobuf::CSubscriber<MSG_T>>
using Subscriber = std::shared_ptr<SubscriberImpl<MSG_T, ECAL_SUB_T>>;

using DynamicSubscriberClass = SubscriberImpl<google::protobuf::Message, eCAL::protobuf::CDynamicSubscriber>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberClass>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP
