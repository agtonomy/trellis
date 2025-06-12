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

/**
 * @file dynamic_message_cache.hpp
 * @brief Defines the DynamicMessageCache class for handling dynamic protobuf messages.
 *
 * This class allows for loading a serialized `FileDescriptorSet` and instantiating
 * protobuf messages dynamically by type name at runtime.
 */

#ifndef TRELLIS_CORE_IPC_PROTO_DYNAMIC_MESSAGE_CACHE_HPP_
#define TRELLIS_CORE_IPC_PROTO_DYNAMIC_MESSAGE_CACHE_HPP_

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace trellis::core::ipc::proto {

/**
 * @class DynamicMessageCache
 * @brief Caches dynamic protobuf message types for runtime instantiation.
 *
 * This class is initialized with a serialized `FileDescriptorSet`, which is parsed
 * and stored in an internal `DescriptorPool`. It provides functionality to create
 * message instances by name and reuse message prototypes efficiently.
 *
 * This is useful to cache message prototypes for purposes of dynamic publishers and subscribers.
 */
class DynamicMessageCache {
 public:
  /**
   * @brief Construct a DynamicMessageCache from a serialized FileDescriptorSet.
   * @param descriptor_set_data A string containing the serialized FileDescriptorSet.
   * @throws std::runtime_error If the descriptor set cannot be parsed or built.
   */
  explicit DynamicMessageCache(const std::string& descriptor_set_data);

  // Disable copy and move constructors and assignment operators
  DynamicMessageCache(const DynamicMessageCache&) = delete;
  DynamicMessageCache& operator=(const DynamicMessageCache&) = delete;

  /**
   * @brief Create a new protobuf message instance of the given type.
   *
   * This method looks up the type in the descriptor pool, retrieves its prototype,
   * and returns a new instance. It also caches the prototype for future calls to `Get()`.
   *
   * @param type_name The fully qualified name of the protobuf message type.
   * @return A unique pointer to a new message instance.
   * @throws std::runtime_error If the message type is not found or instantiation fails.
   */
  std::unique_ptr<google::protobuf::Message> Create(const std::string& type_name);

  /**
   * @brief Get a new instance of the last created message type.
   *
   * This uses the cached prototype set by the most recent call to `Create()`.
   *
   * @return A unique pointer to a new message instance.
   * @throws std::runtime_error If no prototype is cached.
   */
  std::unique_ptr<google::protobuf::Message> Get();

 private:
  google::protobuf::DescriptorPool pool_;            ///< Descriptor pool for storing parsed file descriptors.
  google::protobuf::DynamicMessageFactory factory_;  ///< Factory for creating dynamic message instances.
  std::unique_ptr<google::protobuf::Message> message_prototype_;  ///< Cached prototype for reuse in Get().
};

}  // namespace trellis::core::ipc::proto

#endif  // TRELLIS_CORE_IPC_PROTO_DYNAMIC_MESSAGE_CACHE_HPP_
