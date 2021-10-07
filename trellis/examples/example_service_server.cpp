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

#include "trellis/core/logging.hpp"
#include "trellis/core/node.hpp"
#include "trellis/examples/addition_service.pb.h"

using namespace trellis::examples;
using namespace trellis::core;

class AdditionServiceImpl : public AdditionService {
  void Add(::google::protobuf::RpcController* /* controller_ */, const AdditionRequest* request_,
           AdditionResponse* response_, ::google::protobuf::Closure* /* done_ */) override {
    const auto arg1 = request_->arg1();
    const auto arg2 = request_->arg2();
    const auto result = arg1 + arg2;
    Log::Info("Got request for {} + {}, sum is {}", arg1, arg2, result);
    response_->set_sum(result);
  }
};

int main(int argc, char** argv) {
  Node node("Hello World Protobuf Service Server");
  auto server = node.CreateServiceServer<AdditionService>(std::make_shared<AdditionServiceImpl>());

  return node.Run();
}
