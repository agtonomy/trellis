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

#include <cxxopts.hpp>
#include <thread>

#include "VariadicTable.h"
#include "trellis/core/monitor_interface.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

struct TopicInfo {
  unsigned publisher_count{0};
  unsigned subscriber_count{0};
  std::unordered_set<std::string> types{};
  std::unordered_set<std::string> hostnames;
  double pub_freq{0.0};
  unsigned pub_count{0};
};

std::string StringifySet(const std::unordered_set<std::string>& set) {
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

bool IsRawTopic(const std::string& topic) {
  // check if name ends with "/raw"
  if (topic.size() >= 4) {
    return topic.substr(topic.size() - 4, topic.size()) == "/raw";
  }

  return false;
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

  if ((result.count("raw"))) {
    mutil.PrintTopics();
  } else {
    const auto& snapshot = mutil.UpdateSnapshot();
    std::map<std::string, TopicInfo> topic_map;

    for (const auto& topic : snapshot.topics()) {
      const bool is_publisher = (topic.direction() == "publisher");
      std::string topic_name = topic.tname();
      if (IsRawTopic(topic.tname())) {
        if (is_publisher) {
          // We grab the real type from the raw topic
          const std::string actual_topic_name = topic.tname().substr(0, topic.tname().size() - 4);
          topic_map[actual_topic_name].types.insert(topic.ttype());
        }
      } else {
        TopicInfo& topic_info = topic_map[topic.tname()];
        if (is_publisher) {
          ++topic_info.publisher_count;
          topic_info.hostnames.insert(topic.hname());
          topic_info.pub_count = topic.dclock();
          topic_info.pub_freq = topic.dfreq() / 1000.0;
        } else {
          ++topic_info.subscriber_count;
        }
      }
    }

    VariadicTable<std::string, int, int, double, int, std::string> vt(
        {"Topic", "Num Pub", "Num Sub", "Freq (Hz)", "Tx Count", "Type"});
    for (const auto& [topic, info] : topic_map) {
      vt.addRow(topic, info.publisher_count, info.subscriber_count, info.pub_freq, info.pub_count,
                StringifySet(info.types));
    }
    vt.print(std::cout);
  }

  eCAL::Finalize();
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
