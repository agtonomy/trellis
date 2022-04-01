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
  PublisherClass(const std::string& topic) : ecal_pub_(topic), ecal_pub_raw_(topic + "/raw") {}
  void Send(const MSG_T& msg) {
    trellis::core::TimestampedMessage tsmsg;
    tsmsg.mutable_timestamp()->set_seconds(trellis::core::time::NowInSeconds());
    auto any = tsmsg.mutable_payload();
    any->PackFrom(msg);
    ecal_pub_.Send(tsmsg);
  }

 private:
  eCAL::protobuf::CPublisher<trellis::core::TimestampedMessage> ecal_pub_;

  // This enables us to get our MSG_T schema into the monitoring layer
  eCAL::protobuf::CPublisher<MSG_T> ecal_pub_raw_;
};

template <typename T>
using Publisher = std::shared_ptr<PublisherClass<T>>;

/**
 * A variant of the protobuf publisher which operates on the abstract
 * google::protobuf::Message. This is useful for publishing messages of
 * types determined at runtime
 *
 * This was adapted from eCAL's CPublisher<T>
 *
 * TODO(bsirang): this functionality was integrated upstream...
 * wipe this version and use the upstream implementation after the next
 * version of eCAL is released.
 */
class DynamicPublisherImpl : public eCAL::CMsgPublisher<google::protobuf::Message> {
 public:
  /**
   * @brief  Constructor.
   *
   * @param topic_name  Unique topic name.
   * @param msg Protobuf message
   **/
  DynamicPublisherImpl(const std::string& topic_name, std::shared_ptr<google::protobuf::Message> msg)
      : CMsgPublisher<google::protobuf::Message>(topic_name, proto_utils::GetTypeName(msg.get()),
                                                 proto_utils::GetDescription(msg.get())),
        msg_{msg_} {}

  DynamicPublisherImpl(const std::string& topic_name, const std::string& proto_type_name)
      : CMsgPublisher<google::protobuf::Message>(
            topic_name, proto_utils::GetTypeName(proto_utils::CreateMessageByName(proto_type_name).get()),
            proto_utils::GetDescription(proto_utils::CreateMessageByName(proto_type_name).get())),
        msg_{proto_utils::CreateMessageByName(proto_type_name)} {}

  DynamicPublisherImpl(const DynamicPublisherImpl&) = delete;
  DynamicPublisherImpl& operator=(const DynamicPublisherImpl&) = delete;
  DynamicPublisherImpl(DynamicPublisherImpl&&) = default;
  DynamicPublisherImpl& operator=(DynamicPublisherImpl&&) = default;

  std::string GetTypeName() const override { return proto_utils::GetTypeName(msg_.get()); }

 private:
  std::string GetDescription() const override { return proto_utils::GetDescription(msg_.get()); }
  size_t GetSize(const google::protobuf::Message& msg) const { return ((size_t)msg.ByteSizeLong()); }
  bool Serialize(const google::protobuf::Message& msg, char* buffer_, size_t size_) const {
    return (msg.SerializeToArray((void*)buffer_, (int)size_));
  }

 private:
  std::shared_ptr<google::protobuf::Message> msg_;
};

using DynamicPublisher = std::shared_ptr<DynamicPublisherImpl>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_PUBLISHER_HPP
