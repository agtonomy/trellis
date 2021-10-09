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

namespace trellis {
namespace core {

template <typename T>
using PublisherClass = eCAL::protobuf::CPublisher<T>;

template <typename T>
using Publisher = std::shared_ptr<PublisherClass<T>>;

/**
 * A variant of the protobuf publisher which operates on the abstract
 * google::protobuf::Message. This is useful for publishing messages of
 * types determined at runtime
 *
 * This was adapted from eCAL's CPublisher<T>
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
      : CMsgPublisher<google::protobuf::Message>(topic_name, GetTypeName(msg.get()), GetDescription(msg.get())),
        msg_{msg_} {}

  DynamicPublisherImpl(const std::string& topic_name, const std::string& proto_type_name)
      : CMsgPublisher<google::protobuf::Message>(topic_name, GetTypeName(CreateMessageByName(proto_type_name).get()),
                                                 GetDescription(CreateMessageByName(proto_type_name).get())),
        msg_{CreateMessageByName(proto_type_name)} {}

  DynamicPublisherImpl(const DynamicPublisherImpl&) = delete;
  DynamicPublisherImpl& operator=(const DynamicPublisherImpl&) = delete;
  DynamicPublisherImpl(DynamicPublisherImpl&&) = default;
  DynamicPublisherImpl& operator=(DynamicPublisherImpl&&) = default;

  static std::string GetTypeName(const google::protobuf::Message* msg) { return ("proto:" + msg->GetTypeName()); }
  std::string GetTypeName() const override { return GetTypeName(msg_.get()); }

  static std::shared_ptr<google::protobuf::Message> CreateMessageByName(const std::string& type_name) {
    const google::protobuf::Descriptor* desc =
        google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);

    if (desc == nullptr) {
      std::stringstream errmsg;
      errmsg << "Unable to find message type " << type_name << std::endl;
      throw std::runtime_error(errmsg.str());
    }
    std::shared_ptr<google::protobuf::Message> message{
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(desc)->New()};

    if (message == nullptr) {
      std::stringstream errmsg;
      errmsg << "Unable to create new message from type " << type_name << std::endl;
      throw std::runtime_error(errmsg.str());
    }
    return message;
  }

 private:
  static std::string GetDescription(const google::protobuf::Message* msg) {
    const google::protobuf::Descriptor* desc = msg->GetDescriptor();
    google::protobuf::FileDescriptorSet pset;
    if (eCAL::protobuf::GetFileDescriptor(desc, pset)) {
      std::string desc_s = pset.SerializeAsString();
      return (desc_s);
    }
    return ("");
  }

  std::string GetDescription() const override { return GetDescription(msg_.get()); }
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
