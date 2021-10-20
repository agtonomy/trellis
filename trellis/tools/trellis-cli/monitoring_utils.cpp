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

#include "monitoring_utils.hpp"

#include <google/protobuf/descriptor.pb.h>

#include <algorithm>
#include <sstream>

namespace trellis {
namespace tools {
namespace cli {

MonitorUtil::MonitorUtil() { static_cast<void>(UpdateSnapshot()); }

const eCAL::pb::Monitoring& MonitorUtil::UpdateSnapshot() {
  std::string snapshot_raw;
  eCAL::pb::Monitoring snapshot;
  if (eCAL::Monitoring::GetMonitoring(snapshot_raw)) {
    snapshot.ParseFromString(snapshot_raw);
    snapshot_raw_ = snapshot_raw;
    snapshot_ = snapshot;
  }
  return snapshot_;
}

std::shared_ptr<google::protobuf::Message> MonitorUtil::GetMessageFromTopic(const std::string& topic) {
  std::string topic_type = eCAL::Util::GetTypeName(topic);
  topic_type = topic_type.substr(topic_type.find_first_of(':') + 1, topic_type.size());
  topic_type = topic_type.substr(topic_type.find_last_of('.') + 1, topic_type.size());

  if (topic_type.size() == 0) {
    std::stringstream msgstream;
    msgstream << "Could not retrieve topic type for " << topic << ". Are there any subscribers online?";
    throw std::runtime_error(msgstream.str());
  }

  std::string topic_description = eCAL::Util::GetDescription(topic);
  if (topic_description.size() == 0) {
    std::stringstream msgstream;
    msgstream << "Could not retrieve topic description for " << topic << ".";
    throw std::runtime_error(msgstream.str());
  }

  google::protobuf::FileDescriptorSet proto_desc;
  proto_desc.ParseFromString(topic_description);
  std::string error_s;

  auto message = std::shared_ptr<google::protobuf::Message>{
      decoder_.GetProtoMessageFromDescriptorSet(proto_desc, topic_type, error_s)};

  if (error_s.size() > 0) {
    throw std::runtime_error(error_s);
  }

  return message;
}

void MonitorUtil::PrintTopics(TopicFilterFunction filter) const {
  const auto& topics = snapshot_.topics();

  auto it = std::find_if(topics.begin(), topics.end(), filter);
  while (it != topics.end()) {
    // print topic details
    const auto& topic = *it;
    std::cout << "tname        : " << topic.tname() << std::endl;      // topic name
    std::cout << "ttype        : " << topic.ttype() << std::endl;      // topic type
    std::cout << "direction    : " << topic.direction() << std::endl;  // direction (publisher, subscriber)
    std::cout << "hname        : " << topic.hname() << std::endl;      // host name
    std::cout << "pid          : " << topic.pid() << std::endl;        // process id
    std::cout << "tid          : " << topic.tid() << std::endl;        // topic id
    std::cout << std::endl;
    ++it;
  }
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis