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

static unsigned arg1 = 0;
static unsigned arg2 = 0;

using namespace trellis::examples;
using namespace trellis::core;

static void ResponseCallback(const AdditionResponse* resp) {
  if (resp) {
    Log::Info("Received response {}", resp->sum());
  } else {
    Log::Info("Request failed!");
  }
}

int main(int argc, char** argv) {
  Node node("Hello World Protobuf Service Client");
  auto client = node.CreateServiceClient<AdditionService>();

  auto timer = node.CreateTimer(
      500,
      [client]() {
        AdditionRequest req;
        req.set_arg1(arg1);
        req.set_arg2(arg2);

        Log::Info("Sending request for {} + {}", arg1, arg2);
        arg1 += 2;
        arg2 += 3;
        client->CallAsync<AdditionRequest, AdditionResponse>("Add", req, ResponseCallback);
      },
      1000);

  return node.Run();
}
