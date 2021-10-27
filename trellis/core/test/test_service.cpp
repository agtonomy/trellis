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

#include <gtest/gtest.h>

#include <iostream>

#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

class MyService : public test::TestService {
 public:
  void DoStuff(::google::protobuf::RpcController* /* controller_ */, const test::Test* request_,
               test::TestTwo* response_, ::google::protobuf::Closure* /* done_ */) override {
    response_->set_foo(request_->id());
    response_->set_bar("success");
  }
};

TEST_F(TrellisFixture, BasicService) {
  static unsigned response_count{0};
  auto server = std::make_shared<MyService>();
  auto service = node_.CreateServiceServer<MyService>(server);
  auto client = node_.CreateServiceClient<MyService>();
  StartRunnerThread();
  WaitForDiscovery();

  test::Test request;
  request.set_id(123);
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [request](ServiceCallStatus status, const test::TestTwo* response) {
        ++response_count;
        ASSERT_EQ(status, ServiceCallStatus::kSuccess);
        ASSERT_EQ(request.id(), static_cast<unsigned>(response->foo()));
      },
      100);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(response_count, 1);
}

class MySlowService : public test::TestService {
 public:
  void DoStuff(::google::protobuf::RpcController* /* controller_ */, const test::Test* request_,
               test::TestTwo* response_, ::google::protobuf::Closure* /* done_ */) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
};

TEST_F(TrellisFixture, ServiceCallTimeout) {
  static unsigned response_count{0};
  auto server = std::make_shared<MySlowService>();
  auto service = node_.CreateServiceServer<MySlowService>(server);
  auto client = node_.CreateServiceClient<MySlowService>();
  StartRunnerThread();
  WaitForDiscovery();

  test::Test request;
  request.set_id(123);
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [request](ServiceCallStatus status, const test::TestTwo* response) {
        ++response_count;
        ASSERT_EQ(status, ServiceCallStatus::kTimedOut);  // expect timeout
      },
      10);  // we expect to timeout before response
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(response_count, 1);
  Stop();
}

TEST_F(TrellisFixture, BurstServiceCalls) {
  static unsigned response_count{0};
  auto server = std::make_shared<MyService>();
  auto service = node_.CreateServiceServer<MyService>(server);
  auto client = node_.CreateServiceClient<MyService>();
  StartRunnerThread();
  WaitForDiscovery();

  static test::Test request;
  static constexpr unsigned burst_count{20};
  for (size_t i = 0; i < burst_count; ++i) {
    const unsigned id = i;
    request.set_id(id);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [id](ServiceCallStatus status, const test::TestTwo* response) {
          ++response_count;
          ASSERT_EQ(status, ServiceCallStatus::kSuccess);
          ASSERT_EQ(id, static_cast<unsigned>(response->foo()));
        },
        100);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(response_count, burst_count);
}
