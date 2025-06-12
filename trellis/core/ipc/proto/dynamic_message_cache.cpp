/*
 * Copyright (C) 2025 Agtonomy
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

#include "trellis/core/ipc/proto/dynamic_message_cache.hpp"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <sstream>

namespace trellis::core::ipc::proto {

using namespace google::protobuf;

DynamicMessageCache::DynamicMessageCache(const std::string& descriptor_set_data) : factory_(&pool_) {
  FileDescriptorSet file_descriptor_set;
  if (!file_descriptor_set.ParseFromString(descriptor_set_data)) {
    throw std::runtime_error("Failed to parse FileDescriptorSet");
  }

  for (int i = 0; i < file_descriptor_set.file_size(); ++i) {
    const FileDescriptorProto& file_proto = file_descriptor_set.file(i);
    if (pool_.FindFileByName(file_proto.name()) != nullptr) {
      continue;  // already added
    }

    if (pool_.BuildFile(file_proto) == nullptr) {
      throw std::runtime_error("Failed to build file descriptor: " + file_proto.name());
    }
  }
}

std::unique_ptr<Message> DynamicMessageCache::Create(const std::string& type_name) {
  if (message_prototype_ == nullptr) {
    const Descriptor* descriptor = pool_.FindMessageTypeByName(type_name);
    if (!descriptor) {
      throw std::runtime_error("Message type not found in descriptor pool: " + type_name);
    }

    const Message* prototype = factory_.GetPrototype(descriptor);
    if (!prototype) {
      throw std::runtime_error("Failed to get prototype for type: " + type_name);
    }

    message_prototype_ = std::unique_ptr<Message>(prototype->New());
  }

  return std::unique_ptr<Message>(message_prototype_->New());
}

std::unique_ptr<Message> DynamicMessageCache::Get() { return std::unique_ptr<Message>(message_prototype_->New()); }

}  // namespace trellis::core::ipc::proto
