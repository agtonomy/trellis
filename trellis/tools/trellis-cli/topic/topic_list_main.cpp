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
#include "trellis/core/discovery/discovery.hpp"
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
  options.add_options()("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  // Delay to give time for discovery
  trellis::core::EventLoop loop;
  trellis::core::discovery::Discovery discovery("trellis-cli", loop, trellis::core::Config{});
  loop.RunFor(std::chrono::milliseconds(monitor_delay_ms));

  const auto pubsub_samples = discovery.GetPubSubSamples();

  std::map<std::string, TopicInfo> topic_map;

  for (const auto& sample : pubsub_samples) {
    const bool is_publisher = sample.type() == trellis::core::discovery::publisher_registration;
    const auto& topic = sample.topic();
    TopicInfo& topic_info = topic_map[topic.tname()];
    if (is_publisher) {
      ++topic_info.publisher_count;
      topic_info.hostnames.insert(topic.hname());
      topic_info.pub_count = topic.dclock();
      topic_info.pub_freq = topic.dfreq() / 1000.0;
      topic_info.types.insert(topic.tdatatype().name());
    } else {
      ++topic_info.subscriber_count;
    }
  }

  VariadicTable<std::string, int, int, double, int, std::string> vt(
      {"Topic", "Num Pub", "Num Sub", "Freq (Hz)", "Tx Count", "Type"});
  for (const auto& [topic, info] : topic_map) {
    vt.addRow(topic, info.publisher_count, info.subscriber_count, info.pub_freq, info.pub_count,
              StringifySet(info.types));
  }
  vt.print(std::cout);

  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
