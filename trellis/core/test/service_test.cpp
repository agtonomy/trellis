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

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

namespace {

class MyService : public test::TestService {
 public:
  void DoStuff(::google::protobuf::RpcController* /* controller_ */, const test::Test* request_,
               test::TestTwo* response_, ::google::protobuf::Closure* /* done_ */) override {
    response_->set_foo(static_cast<float>(request_->id()));
    response_->set_bar("success");
  }
};

// Never answers within the caller's timeout, so a call against it times out.
class MySlowService : public test::TestService {
 public:
  void DoStuff(::google::protobuf::RpcController* /* controller_ */, const test::Test* /* request_ */,
               test::TestTwo* /* response_ */, ::google::protobuf::Closure* /* done_ */) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
};

// Poll until the predicate holds, or give up at the deadline. These tests used to budget a flat 100ms for work whose
// cost depends on the machine, which is what made them fail intermittently on CI. Waiting on the result instead costs
// nothing when it arrives quickly and does not fail when it does not.
template <typename PRED_T>
bool WaitFor(PRED_T&& predicate, std::chrono::milliseconds timeout = std::chrono::seconds{10}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  return predicate();
}

}  // namespace

TEST_F(TrellisFixture, BasicService) {
  StartRunnerThread();
  // Declared before the client: ~Client() runs the callback that references it.
  std::atomic<unsigned> response_count{0};
  auto service = GetNode().CreateServiceServer<MyService>(std::make_shared<MyService>());
  auto client = GetNode().CreateServiceClient<MyService>();
  WaitForDiscovery();

  test::Test request;
  request.set_id(123);
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [&response_count, request](ServiceCallStatus status, const test::TestTwo* response) {
        EXPECT_EQ(status, ServiceCallStatus::kSuccess);
        EXPECT_EQ(request.id(), static_cast<unsigned>(response->foo()));
        ++response_count;
      },
      /* timeout_ms = */ 1000);

  EXPECT_TRUE(WaitFor([&] { return response_count.load() == 1u; }));
  StopAndJoinRunnerThread();  // the callback references locals of this function
  EXPECT_EQ(response_count.load(), 1u);
}

TEST_F(TrellisFixture, UseZeroForTimeout) {
  StartRunnerThread();
  std::atomic<unsigned> response_count{0};
  auto service = GetNode().CreateServiceServer<MyService>(std::make_shared<MyService>());
  auto client = GetNode().CreateServiceClient<MyService>();
  WaitForDiscovery();

  test::Test request;
  request.set_id(123);
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [&response_count, request](ServiceCallStatus status, const test::TestTwo* response) {
        EXPECT_EQ(status, ServiceCallStatus::kSuccess);
        EXPECT_EQ(request.id(), static_cast<unsigned>(response->foo()));
        ++response_count;
      },
      /* timeout_ms = */ 0);

  EXPECT_TRUE(WaitFor([&] { return response_count.load() == 1u; }));
  StopAndJoinRunnerThread();
  EXPECT_EQ(response_count.load(), 1u);
}

TEST_F(TrellisFixture, ServiceCallTimeout) {
  StartRunnerThread();
  std::atomic<unsigned> response_count{0};
  auto service = GetNode().CreateServiceServer<MySlowService>(std::make_shared<MySlowService>());
  auto client = GetNode().CreateServiceClient<MySlowService>();
  WaitForDiscovery();

  test::Test request;
  request.set_id(123);
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [&response_count](ServiceCallStatus status, const test::TestTwo* /* response */) {
        EXPECT_EQ(status, ServiceCallStatus::kTimedOut);
        ++response_count;
      },
      /* timeout_ms = */ 10);  // the handler takes 100ms, so this expires first

  EXPECT_TRUE(WaitFor([&] { return response_count.load() == 1u; }));
  StopAndJoinRunnerThread();
  EXPECT_EQ(response_count.load(), 1u);
}

TEST_F(TrellisFixture, BurstServiceCalls) {
  static constexpr unsigned kBurstCount{20};

  StartRunnerThread();
  // Both are referenced by the callback, so both outlive the client.
  std::atomic<unsigned> response_count{0};
  std::atomic<unsigned> failure_count{0};
  std::function<void(unsigned)> request_fn;

  auto service = GetNode().CreateServiceServer<MyService>(std::make_shared<MyService>());
  auto client = GetNode().CreateServiceClient<MyService>();
  WaitForDiscovery();

  // Each reply issues the next call, so the burst is a chain rather than a fan-out.
  request_fn = [&](unsigned request_id) {
    test::Test request;
    request.set_id(request_id);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&, request_id](ServiceCallStatus status, const test::TestTwo* response) {
          if (status != ServiceCallStatus::kSuccess) {
            ++failure_count;
            return;  // stop the chain rather than call back into a client that is going away
          }
          EXPECT_EQ(request_id, static_cast<unsigned>(response->foo()));
          ++response_count;
          if (request_id + 1 < kBurstCount) {
            request_fn(request_id + 1);
          }
        },
        /* timeout_ms = */ 1000);
  };

  request_fn(0);

  EXPECT_TRUE(WaitFor([&] { return response_count.load() + failure_count.load() >= kBurstCount; }));
  StopAndJoinRunnerThread();
  EXPECT_EQ(response_count.load(), kBurstCount);
  EXPECT_EQ(failure_count.load(), 0u);
}
