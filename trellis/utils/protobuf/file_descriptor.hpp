/*
 * Copyright (C) 2023 Agtonomy
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

#ifndef TRELLIS_UTILS_PROTOBUF_FILE_DESCRIPTOR_HPP_
#define TRELLIS_UTILS_PROTOBUF_FILE_DESCRIPTOR_HPP_

#include <google/protobuf/descriptor.pb.h>

#include <queue>
#include <string>
#include <unordered_set>

namespace trellis {
namespace utils {
namespace protobuf {

/**
 * @brief Generates a FileDescriptorSet from a protobuf descriptor
 *
 * Includes all transitive children
 *
 * @param top_level_descriptor Descriptor of the top level protobuf type
 * @return google::protobuf::FileDescriptorSet the file descriptor set
 */
inline google::protobuf::FileDescriptorSet GenerateFileDescriptorSetFromTopLevelDescriptor(
    const google::protobuf::Descriptor* top_level_descriptor) {
  google::protobuf::FileDescriptorSet fd_set;
  std::queue<const google::protobuf::FileDescriptor*> to_add;
  to_add.push(top_level_descriptor->file());
  std::unordered_set<std::string> added;
  while (!to_add.empty()) {
    const google::protobuf::FileDescriptor* next = to_add.front();
    to_add.pop();
    // Ensure that the proto type wasn't already added by checking our map
    if (added.find(next->name()) == added.end()) {
      next->CopyTo(fd_set.add_file());
    }
    added.insert(next->name());
    // Iterate over all of the dependencies, and push them onto the queue
    for (int i = 0; i < next->dependency_count(); ++i) {
      const auto& dep = next->dependency(i);
      to_add.push(dep);
    }
  }
  return fd_set;
}

}  // namespace protobuf
}  // namespace utils
}  // namespace trellis

#endif  // TRELLIS_UTILS_PROTOBUF_FILE_DESCRIPTOR_HPP_
