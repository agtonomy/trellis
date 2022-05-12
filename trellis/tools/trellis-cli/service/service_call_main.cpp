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
#include <thread>

#include "trellis/core/monitor_interface.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

int service_call_main(int argc, char* argv[]) {
  cxxopts::Options options(service_info_command.data(), service_info_command_desc.data());
  options.add_options()("s,service", "service name", cxxopts::value<std::string>())(
      "w,whitespace", "add whitespace to output")("m,method", "method name", cxxopts::value<std::string>())(
      "r,request", "request message body in JSON", cxxopts::value<std::string>())("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("service")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  const std::string service_name = result["service"].as<std::string>();
  const std::string method_name = result["method"].as<std::string>();
  const std::string request_json = result["request"].as<std::string>();
  const bool add_whitespace = result.count("whitespace");

  eCAL::Initialize(0, nullptr, root_command.data(), eCAL::Init::All);

  // Delay to give time for discovery
  std::this_thread::sleep_for(std::chrono::milliseconds(monitor_delay_ms));

  eCAL::CServiceClient service_client(service_name);
  while (!service_client.IsConnected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "Waiting for the service .." << std::endl;
  }

  trellis::core::MonitorInterface mutil;
  // mutil.PrintServiceInfo(service_name);

  auto [req, resp] = mutil.GetRequestResponseMessageFromServiceMethod(service_name, method_name);

  google::protobuf::util::JsonParseOptions json_options;
  json_options.ignore_unknown_fields = false;
  json_options.case_insensitive_enum_parsing = false;
  // auto r = google::protobuf::util::JsonStringToMessage(request_json, req.get(), json_options);

  const std::string serialized_request = req->SerializeAsString();

  // Issue service call
  eCAL::ServiceResponseVecT service_response_vec;
  std::cout << "Calling service!" << std::endl;
  if (service_client.Call(method_name, serialized_request, -1, &service_response_vec)) {
    std::cout << "Call succeeded" << std::endl;
    for (auto service_response : service_response_vec) {
      switch (service_response.call_state) {
        // service successful executed
        case call_state_executed: {
          if (!resp->ParseFromString(service_response.response)) {
            std::cerr << "Could not parse google message content from string." << std::endl;
          } else {
            std::string json_raw;
            google::protobuf::util::JsonPrintOptions json_options;
            json_options.add_whitespace = add_whitespace;
            json_options.always_print_primitive_fields = true;
            json_options.always_print_enums_as_ints = false;
            json_options.preserve_proto_field_names = false;
            auto status = google::protobuf::util::MessageToJsonString(*resp, &json_raw, json_options);
            if (!status.ok()) {
              std::cerr << "Got response, but failed to convert it to a JSON string" << std::endl;
            } else {
              std::cout << json_raw << std::endl;
            }
          }
        } break;
        // service execution failed
        case call_state_failed:
          std::cout << "Received error             : " << service_response.error_msg;
          break;
        default:
          break;
      }
    }

  } else {
    // failed
    std::cerr << "Failed to issue service call" << std::endl;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  eCAL::Finalize();
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
