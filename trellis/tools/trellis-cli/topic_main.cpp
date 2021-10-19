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

#include "command_handlers.hpp"
#include "trellis/core/node.hpp"

namespace trellis {
namespace tools {
namespace cli {

using namespace trellis::core;

int topic_publish_main(int argc, char* argv[]) {
  cxxopts::Options options("trellis-cli topic publish", "publish messages to a given topic");
  options.add_options()("t,topic", "topic name", cxxopts::value<std::string>())(
      "b,body", "message body in JSON", cxxopts::value<std::string>())("c,count", "message count",
                                                                       cxxopts::value<int>()->default_value("1"))(
      "d,delay", "discovery delay milliseconds", cxxopts::value<int>()->default_value("1000"))(
      "r,rate", "publish rate hz", cxxopts::value<int>()->default_value("1"))("h,help", "print usage");
  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("topic") || !result.count("body")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  const std::string topic = result["topic"].as<std::string>();
  const std::string body = result["body"].as<std::string>();
  const int count = result["count"].as<int>();
  const int rate = result["rate"].as<int>();
  const int delay_ms = result["delay"].as<int>();
  const int interval_ms = 1000 / rate;

  Node node("trellis-cli");

  // Delay to give time for discovery
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

  std::string topic_type = eCAL::Util::GetTypeName(topic);
  topic_type = topic_type.substr(topic_type.find_first_of(':') + 1, topic_type.size());
  topic_type = topic_type.substr(topic_type.find_last_of('.') + 1, topic_type.size());

  if (topic_type.size() == 0) {
    std::cerr << "Could not retrieve topic type for " << topic << ". Are there any subscribers online?" << std::endl;
    return 1;
  }

  std::string topic_description = eCAL::Util::GetDescription(topic);
  if (topic_description.size() == 0) {
    std::cerr << "Could not retrieve topic description for " << topic << "." << std::endl;
    return 1;
  }
  google::protobuf::FileDescriptorSet proto_desc;
  proto_desc.ParseFromString(topic_description);
  eCAL::protobuf::CProtoDynDecoder decoder;
  std::string error_s;

  std::shared_ptr<google::protobuf::Message> message{
      decoder.GetProtoMessageFromDescriptorSet(proto_desc, topic_type, error_s)};

  if (error_s.size() > 0 || message == nullptr) {
    std::cerr << "Could not get proto message from descriptor set. " << error_s << std::endl;
    return 1;
  }
  auto pub = node.CreateDynamicPublisher(topic, message);

  google::protobuf::util::JsonParseOptions json_options;
  json_options.ignore_unknown_fields = false;
  json_options.case_insensitive_enum_parsing = false;

  auto r = google::protobuf::util::JsonStringToMessage(body, message.get(), json_options);

  std::cout << "Echoing " << count << " messages at " << rate << " hz to topic " << topic << std::endl;

  // Delay to give time for connection(s) to establish
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

  for (int i = 0; i < count && node.RunOnce(); ++i) {
    pub->Send(*message);
    if (i < count - 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
  }

  return 0;
}

// TODO implement
int topic_echo_main(int argc, char* argv[]) {
  cxxopts::Options options("trellis-cli topic echo", "echo messages from a given topic");
  options.add_options()("t,topic", "topic name", cxxopts::value<std::string>())("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("topic")) {
    std::cout << options.help() << std::endl;
    return 1;
  }
  const std::string topic = result["topic"].as<std::string>();

  Node node("trellis-cli");
  auto sub = node.CreateDynamicSubscriber(topic, [](const google::protobuf::Message& msg) {
    // convert to JSON and print
    std::string json;
    google::protobuf::util::JsonPrintOptions json_options;
    json_options.add_whitespace = true;
    json_options.always_print_primitive_fields = true;
    json_options.always_print_enums_as_ints = false;
    json_options.preserve_proto_field_names = false;
    auto r = google::protobuf::util::MessageToJsonString(msg, &json, json_options);
    std::cout << json << std::endl;
  });

  std::cout << "Echoing messages on \"" << topic << "\"" << std::endl;

  node.Run();
  return 0;
}

int topic_main(int argc, char* argv[]) {
  const std::string subcommand = cli::ShiftCommand(argc, argv);
  HandlersMap handlers{
      {"publish", {"publish to a given topic", [argc, argv]() { return cli::topic_publish_main(argc, argv); }}},
      {"echo", {"echo from a given topic", [argc, argv]() { return cli::topic_echo_main(argc, argv); }}},
  };
  if (subcommand.empty()) {
    std::cout << "Must specify a subcommand... " << std::endl;
    cli::PrintCommandsHelp("topic", handlers);
    return 0;
  }
  return cli::RunCommand("topic", subcommand, handlers);
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
