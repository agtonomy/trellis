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

// TODO (bsirang) these tests pass locally but fail often (but not always) in the CI environment
// These tests depend on multiple threads running and TCP sockets connecting, so there's plenty of system-level
// dependencies that get in the way of determinism. We'll keep them disabled for the time being until these issues are
// sorted out.
#if 0
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
  StartRunnerThread();
  static unsigned response_count{0};
  auto service = CreateNewService(GetNode());
  static auto client = GetNode().CreateServiceClient<MyService>();
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
  StartRunnerThread();
  static unsigned response_count{0};
  auto service = CreateNewService(GetNode());
  static auto client = GetNode().CreateServiceClient<MyService>();
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
  StartRunnerThread();
  static unsigned response_count{0};
  auto service = CreateNewSlowService(GetNode());
  static auto client = GetNode().CreateServiceClient<MySlowService>();
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
}

TEST_F(TrellisFixture, BurstServiceCalls) {
  StartRunnerThread();
  static unsigned response_count{0};
  auto service = CreateNewService(GetNode());
  static auto client = GetNode().CreateServiceClient<MyService>();
  WaitForDiscovery();

  static constexpr unsigned burst_count{20};

  // Chain a bunch of requests together until we hit our target burst count
  std::function<void(unsigned)> request_fn = [&request_fn](unsigned request_id) {
    test::Test request;
    request.set_id(request_id);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&request_fn, request_id](ServiceCallStatus status, const test::TestTwo* response) {
          ASSERT_EQ(status, ServiceCallStatus::kSuccess);
          ASSERT_EQ(request_id, static_cast<unsigned>(response->foo()));
          ++response_count;
          if (request_id < burst_count - 1) {
            request_fn(request_id + 1);
          }
        },
        100);
  };

  // Kick off the request chain
  request_fn(0);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_EQ(response_count, burst_count);
}
#endif
