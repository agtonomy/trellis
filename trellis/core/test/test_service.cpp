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

class MySlowService : public test::TestService {
 public:
  void DoStuff(::google::protobuf::RpcController* /* controller_ */, const test::Test* request_,
               test::TestTwo* response_, ::google::protobuf::Closure* /* done_ */) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
};

namespace {
// We'll create a new service for each test so that their interactions are independent from each other.
// We'll use this global vectors so that the lifecycle of the service outlives the clients
trellis::core::ServiceServer<MyService> g_service{nullptr};
trellis::core::ServiceServer<MySlowService> g_slow_service{nullptr};

trellis::core::ServiceServer<MyService> CreateNewService(trellis::core::Node& node) {
  g_service = node.CreateServiceServer<MyService>(std::make_shared<MyService>());
  return g_service;
}

trellis::core::ServiceServer<MySlowService> CreateNewSlowService(trellis::core::Node& node) {
  g_slow_service = node.CreateServiceServer<MySlowService>(std::make_shared<MySlowService>());
  return g_slow_service;
}
}  // namespace

TEST_F(TrellisFixture, BasicService) {
  static unsigned response_count{0};
  auto service = CreateNewService(node_);
  static auto client = node_.CreateServiceClient<MyService>();
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

TEST_F(TrellisFixture, UseZeroForTimeout) {
  static unsigned response_count{0};
  auto service = CreateNewService(node_);
  static auto client = node_.CreateServiceClient<MyService>();
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
      0);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(response_count, 1);
}

TEST_F(TrellisFixture, ServiceCallTimeout) {
  static unsigned response_count{0};
  auto service = CreateNewSlowService(node_);
  static auto client = node_.CreateServiceClient<MySlowService>();
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
  auto service = CreateNewService(node_);
  static auto client = node_.CreateServiceClient<MyService>();
  static unsigned id = 0;
  StartRunnerThread();
  WaitForDiscovery();

  static test::Test request;
  static constexpr unsigned burst_count{20};

  // Chain a bunch of requests together until we hit our target burst count
  std::function<void(void)> request_fn = [&request_fn]() {
    request.set_id(id);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&request_fn](ServiceCallStatus status, const test::TestTwo* response) {
          ASSERT_EQ(status, ServiceCallStatus::kSuccess);
          ASSERT_EQ(id, static_cast<unsigned>(response->foo()));
          ++response_count;
          ++id;
          if (id < burst_count) {
            request_fn();
          }
        },
        100);
  };

  // Kick off the request chain
  request_fn();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(response_count, burst_count);
}
