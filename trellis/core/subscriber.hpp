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

namespace trellis {
namespace core {

using DynamicSubscriberClass = eCAL::protobuf::CDynamicSubscriber;
using DynamicSubscriber = std::shared_ptr<DynamicSubscriberClass>;

template <typename T>
class SubscriberImpl {
 public:
  using Callback = std::function<void(const T&)>;
  SubscriberImpl(const std::string& topic, Callback callback) : ecal_sub_{topic} {
    // XXX(bsirang) consider passing time_ and clock_ to user
    auto callback_wrapper = [callback](const char* topic_name_, const T& msg_, long long time_, long long clock_,
                                       long long id_) { callback(msg_); };
    ecal_sub_.AddReceiveCallback(callback_wrapper);
  }

 private:
  eCAL::protobuf::CSubscriber<T> ecal_sub_;
};

template <typename T>
using Subscriber = std::shared_ptr<SubscriberImpl<T>>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SUBSCRIBER_HPP
