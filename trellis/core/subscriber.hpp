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

 private:
  using RawCallback =
      std::function<void(const char* topic_name_, const MSG_T& msg_, long long time_, long long clock_, long long id_)>;

  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithoutWatchdog(Callback callback) {
    auto callback_wrapper = [callback](const char* topic_name_, const MSG_T& msg_, long long time_, long long clock_,
                                       long long id_) { callback(msg_); };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithoutWatchdog(Callback callback) {
    auto callback_wrapper = [callback](const char* topic_name_, const google::protobuf::Message& msg_,
                                       long long time_) { callback(msg_); };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithWatchdog(Callback callback, WatchdogCallback watchdog_callback, unsigned watchdog_timeout_ms,
                               EventLoop event_loop) {
    Timer watchdog_timer{nullptr};
    auto callback_wrapper = [callback, watchdog_callback, watchdog_timer, watchdog_timeout_ms, event_loop](
                                const char* topic_name_, const MSG_T& msg_, long long time_, long long clock_,
                                long long id_) mutable {
      if (watchdog_timer == nullptr) {
        // create one shot watchdog timer which automatically loads the timer too
        watchdog_timer = std::make_shared<TimerImpl>(event_loop, TimerImpl::Type::kOneShot, watchdog_callback, 0,
                                                     watchdog_timeout_ms);
      } else {
        watchdog_timer->Reset();
      }

      callback(msg_);
    };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  void SetCallbackWithWatchdog(Callback callback, WatchdogCallback watchdog_callback, unsigned watchdog_timeout_ms,
                               EventLoop event_loop) {
    Timer watchdog_timer{nullptr};
    auto callback_wrapper = [callback, watchdog_callback, watchdog_timer, watchdog_timeout_ms, event_loop](
                                const char* topic_name_, const google::protobuf::Message& msg_,
                                long long time_) mutable {
      if (watchdog_timer == nullptr) {
        // create one shot watchdog timer which automatically loads the timer too
        watchdog_timer = std::make_shared<TimerImpl>(event_loop, TimerImpl::Type::kOneShot, watchdog_callback, 0,
                                                     watchdog_timeout_ms);
      } else {
        watchdog_timer->Reset();
      }

      callback(msg_);
    };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  ECAL_SUB_T ecal_sub_;
  EventLoop ev_loop_;
};

template <typename MSG_T, typename ECAL_SUB_T = eCAL::protobuf::CSubscriber<MSG_T>>
using Subscriber = std::shared_ptr<SubscriberImpl<MSG_T, ECAL_SUB_T>>;

using DynamicSubscriberClass = SubscriberImpl<google::protobuf::Message, eCAL::protobuf::CDynamicSubscriber>;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberClass>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP
