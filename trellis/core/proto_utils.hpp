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

#ifndef TRELLIS_CORE_PROTO_UTILS_HPP
#define TRELLIS_CORE_PROTO_UTILS_HPP

#include <ecal/protobuf/ecal_proto_dyn.h>

namespace trellis {
namespace core {
namespace proto_utils {

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

static std::string GetDescription(const google::protobuf::Message* msg) {
  const google::protobuf::Descriptor* desc = msg->GetDescriptor();
  google::protobuf::FileDescriptorSet pset;
  if (eCAL::protobuf::GetFileDescriptor(desc, pset)) {
    std::string desc_s = pset.SerializeAsString();
    return (desc_s);
  }
  return ("");
}

static std::string GetTypeName(const google::protobuf::Message* msg) { return ("proto:" + msg->GetTypeName()); }

static std::string GetTypeFromURL(const std::string& type_url) {
  // Example string: type.googleapis.com/trellis.examples.proto.HelloWorld
  return "proto:" + type_url.substr(type_url.find_first_of('/') + 1, type_url.size());
}

}  // namespace proto_utils
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_PROTO_UTILS_HPP
