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

/**
 * GetTypeFromURL extracts the protobuf type string from a type URL
 *
 * This is useful for working with the type_url field from google.protobuf.Any messages
 */
inline std::string GetTypeFromURL(const std::string& type_url) {
  // Example string: type.googleapis.com/trellis.examples.proto.HelloWorld
  return "proto:" + type_url.substr(type_url.find_first_of('/') + 1, type_url.size());
}

inline std::string GetRawTopicString(const std::string& topic) { return topic + "/raw"; }

}  // namespace proto_utils
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_PROTO_UTILS_HPP
