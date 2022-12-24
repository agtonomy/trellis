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

#include <google/protobuf/util/json_util.h>

#include <cxxopts.hpp>
#include <set>
#include <thread>

#include "VariadicTable.h"
#include "trellis/core/monitor_interface.hpp"
#include "trellis/core/proto_utils.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

struct TopicInfo {
  unsigned publisher_count{0};
  unsigned subscriber_count{0};
  std::set<std::string> types{};
  std::set<std::string> hostnames{};
  double pub_freq{0.0};
  unsigned pub_count{0};
  std::set<std::string> transports{};
};

std::string StringifySet(const std::set<std::string>& set) {
  std::string result;
  for (const auto& item : set) {
    if (result.size() > 0) {
      result += "," + item;
    } else {
      result = item;
    }
  }
  return result;
}

int topic_list_main(int argc, char* argv[]) {
  cxxopts::Options options(topic_list_command.data(), topic_list_command_desc.data());
  options.add_options()("h,help", "print usage")("r,raw", "raw dump");

  auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  eCAL::Initialize(0, nullptr, root_command.data(), eCAL::Init::All);

  // Delay to give time for discovery
  std::this_thread::sleep_for(std::chrono::milliseconds(monitor_delay_ms));

  trellis::core::MonitorInterface mutil;
  const auto& snapshot = mutil.UpdateSnapshot();
  if ((result.count("raw"))) {
    google::protobuf::util::JsonPrintOptions json_options;
    json_options.add_whitespace = true;
    json_options.always_print_primitive_fields = true;
    json_options.always_print_enums_as_ints = false;
    json_options.preserve_proto_field_names = false;
    std::string json_raw;
    for (const auto& topic : snapshot.topics()) {
      auto status = google::protobuf::util::MessageToJsonString(topic, &json_raw, json_options);
      if (status.ok()) {
        std::cout << json_raw << std::endl
                  << "================================================================================================="
                     "=============================="
                  << std::endl;
      }
    }
  } else {
    std::map<std::string, TopicInfo> topic_map;

    for (const auto& topic : snapshot.topics()) {
      const bool is_publisher = (topic.direction() == "publisher");
      if (trellis::core::proto_utils::IsRawTopic(topic.tname())) {
        if (is_publisher) {
          // We grab the real type from the raw topic
          const std::string actual_topic_name = core::proto_utils::GetTopicFromRawTopic(topic.tname());
          topic_map[actual_topic_name].types.insert(topic.ttype());
        }
      } else {
        TopicInfo& topic_info = topic_map[topic.tname()];
        if (is_publisher) {
          ++topic_info.publisher_count;
          topic_info.hostnames.insert(topic.hname());
          topic_info.pub_count = topic.dclock();
          topic_info.pub_freq = topic.dfreq() / 1000.0;
          for (const auto& layer : topic.tlayer()) {
            topic_info.transports.insert(eCAL::pb::eTLayerType_Name(layer.type()));
          }
        } else {
          ++topic_info.subscriber_count;
        }
      }
    }

    VariadicTable<std::string, int, int, double, int, std::string, std::string> vt(
        {"Topic", "Num Pub", "Num Sub", "Freq (Hz)", "Tx Count", "Type", "Transports"});
    for (const auto& [topic, info] : topic_map) {
      vt.addRow(topic, info.publisher_count, info.subscriber_count, info.pub_freq, info.pub_count,
                StringifySet(info.types), StringifySet(info.transports));
    }
    vt.print(std::cout);
  }

  eCAL::Finalize();
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
