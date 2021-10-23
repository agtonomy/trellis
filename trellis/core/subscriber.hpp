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

using DynamicSubscriberClass = eCAL::protobuf::CDynamicSubscriber;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberClass>;

template <typename T>
class SubscriberImpl {
 public:
  using Callback = std::function<void(const T&)>;
  using WatchdogCallback = std::function<void(void)>;
  SubscriberImpl(const std::string& topic, Callback callback) : ecal_sub_{topic} {
    auto callback_wrapper = CreateCallbackWithoutWatchdog(callback);
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

  SubscriberImpl(const std::string& topic, Callback callback, unsigned watchdog_timeout_ms,
                 WatchdogCallback watchdog_callback, EventLoop event_loop)
      : ecal_sub_{topic} {
    auto callback_wrapper = CreateCallbackWithWatchdog(callback, watchdog_callback, watchdog_timeout_ms, event_loop);
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

 private:
  using RawCallback =
      std::function<void(const char* topic_name_, const T& msg_, long long time_, long long clock_, long long id_)>;

  static RawCallback CreateCallbackWithoutWatchdog(Callback callback) {
    // XXX(bsirang) consider passing time_ and clock_ to user
    return [callback](const char* topic_name_, const T& msg_, long long time_, long long clock_, long long id_) {
      callback(msg_);
    };
  }

  static RawCallback CreateCallbackWithWatchdog(Callback callback, WatchdogCallback watchdog_callback,
                                                unsigned watchdog_timeout_ms, EventLoop event_loop) {
    Timer watchdog_timer{nullptr};
    return [callback, watchdog_callback, watchdog_timer, watchdog_timeout_ms, event_loop](
               const char* topic_name_, const T& msg_, long long time_, long long clock_, long long id_) mutable {
      if (watchdog_timer == nullptr) {
        // create one shot watchdog timer which automatically loads the timer too
        watchdog_timer = std::make_shared<TimerImpl>(event_loop, TimerImpl::Type::kOneShot, watchdog_callback, 0,
                                                     watchdog_timeout_ms);
      } else {
        watchdog_timer->Reset();
      }

      callback(msg_);
    };
  }
  eCAL::protobuf::CSubscriber<T> ecal_sub_;
  EventLoop ev_loop_;
};

template <typename T>
using Subscriber = std::shared_ptr<SubscriberImpl<T>>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP
