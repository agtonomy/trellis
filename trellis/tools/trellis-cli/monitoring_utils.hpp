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

#ifndef TRELLIS_TOOLS_TRELLIS_CLI_MONITORING_UTILS_HPP
#define TRELLIS_TOOLS_TRELLIS_CLI_MONITORING_UTILS_HPP

#include <ecal/ecal.h>
#include <ecal/protobuf/ecal_proto_dyn.h>

#include "ecal/pb/monitoring.pb.h"

namespace trellis {
namespace tools {
namespace cli {

class MonitorUtil {
 public:
  using TopicFilterFunction = std::function<bool(const eCAL::pb::Topic&)>;
  MonitorUtil();
  const eCAL::pb::Monitoring& UpdateSnapshot();
  std::shared_ptr<google::protobuf::Message> GetMessageFromTopic(const std::string& topic);
  void PrintTopics(TopicFilterFunction filter = [](const eCAL::pb::Topic&) { return true; }) const;

 private:
  eCAL::protobuf::CProtoDynDecoder decoder_;
  std::string snapshot_raw_;
  eCAL::pb::Monitoring snapshot_;
};

}  // namespace cli
}  // namespace tools
}  // namespace trellis

#endif  // TRELLIS_TOOLS_TRELLIS_CLI_MONITORING_UTILS_HPP
