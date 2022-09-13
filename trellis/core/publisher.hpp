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

#ifndef TRELLIS_CORE_PUBLISHER_HPP
#define TRELLIS_CORE_PUBLISHER_HPP

#include <ecal/msg/protobuf/publisher.h>

#include "proto_utils.hpp"
#include "time.hpp"
#include "trellis/core/timestamped_message.pb.h"

namespace trellis {
namespace core {

template <typename MSG_T>
class PublisherClass {
 public:
  PublisherClass(const std::string& topic) : PublisherClass(topic, false) {}

  PublisherClass(const std::string& topic, bool enable_zero_copy)
      : ecal_pub_(topic), ecal_pub_raw_(CreateRawPublisher(topic)) {
    if (enable_zero_copy) {
      ecal_pub_.ShmEnableZeroCopy(true);
      ecal_pub_.ShmSetBufferCount(3);
    }
  }
  void Send(const MSG_T& msg) {
    const auto now = trellis::core::time::Now();
    Send(msg, now);
  }
  void Send(const MSG_T& msg, const time::TimePoint& tp) {
    trellis::core::TimestampedMessage tsmsg;
    auto timestamp = tsmsg.mutable_timestamp();
    *timestamp = time::TimePointToTimestamp(tp);
    auto any = tsmsg.mutable_payload();
    any->PackFrom(msg);
    ecal_pub_.Send(tsmsg);
  }

 private:
  template <class FOO = MSG_T, std::enable_if_t<!std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  static std::shared_ptr<eCAL::protobuf::CPublisher<MSG_T>> CreateRawPublisher(const std::string& topic) {
    return std::make_shared<eCAL::protobuf::CPublisher<MSG_T>>(proto_utils::GetRawTopicString(topic));
  }

  template <class FOO = MSG_T, std::enable_if_t<std::is_same<FOO, google::protobuf::Message>::value>* = nullptr>
  static std::shared_ptr<eCAL::protobuf::CPublisher<MSG_T>> CreateRawPublisher(const std::string& topic) {
    return nullptr;  // unused in dynamic publisher case
  }

  eCAL::protobuf::CPublisher<trellis::core::TimestampedMessage> ecal_pub_;

  // This enables us to get our MSG_T schema into the monitoring layer
  std::shared_ptr<eCAL::protobuf::CPublisher<MSG_T>> ecal_pub_raw_;
};

template <typename T>
using Publisher = std::shared_ptr<PublisherClass<T>>;

using DynamicPublisher = std::shared_ptr<PublisherClass<google::protobuf::Message>>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_PUBLISHER_HPP
