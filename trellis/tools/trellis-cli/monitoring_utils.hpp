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

#include <ostream>

#include "ecal/pb/monitoring.pb.h"

namespace trellis {
namespace tools {
namespace cli {

std::ostream& operator<<(std::ostream&, const eCAL::pb::Host&);
std::ostream& operator<<(std::ostream&, const eCAL::pb::Process&);
std::ostream& operator<<(std::ostream&, const eCAL::pb::Service&);
std::ostream& operator<<(std::ostream&, const eCAL::pb::Topic&);

class MonitorUtil {
 public:
  using TopicFilterFunction = std::function<bool(const eCAL::pb::Topic&)>;
  using NodeFilterFunction = std::function<bool(const eCAL::pb::Process&)>;

  template <typename T>
  using FilterFunction = std::function<bool(const T&)>;
  MonitorUtil();
  const eCAL::pb::Monitoring& UpdateSnapshot();
  std::shared_ptr<google::protobuf::Message> GetMessageFromTopic(const std::string& topic);
  void PrintTopics() const;
  void PrintNodes() const;
  void PrintHosts() const;
  void PrintServices() const;

 private:
  template <typename T>
  void PrintEntries(
      const google::protobuf::RepeatedPtrField<T>& entries,
      FilterFunction<T> filter = [](const T&) { return true; }) const {
    unsigned entry_count{0};
    auto it = std::find_if(entries.begin(), entries.end(), filter);
    while (it != entries.end()) {
      ++entry_count;
      const auto& entry = *it;
      std::cout << std::endl;
      std::cout << "=============================================================" << std::endl;
      std::cout << entry;
      ++it;
    }
    std::cout << "=============================================================" << std::endl;
    std::cout << "Displayed " << entry_count << " entries." << std::endl;
  }
  eCAL::protobuf::CProtoDynDecoder decoder_;
  std::string snapshot_raw_;
  eCAL::pb::Monitoring snapshot_;
};

}  // namespace cli
}  // namespace tools
}  // namespace trellis

#endif  // TRELLIS_TOOLS_TRELLIS_CLI_MONITORING_UTILS_HPP
