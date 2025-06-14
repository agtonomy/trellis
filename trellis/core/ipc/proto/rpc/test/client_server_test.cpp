/*
 * Copyright (C) 2025 Agtonomy
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

#include "trellis/core/ipc/proto/rpc/client.hpp"
#include "trellis/core/ipc/proto/rpc/server.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

namespace trellis::core::ipc::proto::rpc {

namespace {
class TestServiceHandler : public trellis::core::test::TestService {
 public:
  ~TestServiceHandler() override {}
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure*) override {
    response->set_foo(static_cast<float>(request->id()));
    response->set_bar("Echo: " + request->msg());
  }
};
static constexpr auto kServiceCallWaitTime = std::chrono::milliseconds{200};
}  // namespace

using namespace trellis::core::test;

TEST_F(TrellisFixture, BasicSingleServiceCall) {
  StartRunnerThread();

  auto client = node_.CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = node_.CreateServiceServer<TestServiceHandler>(handler);

  // Wait for some time so the client can find the server
  WaitForDiscovery();

  test::Test request;
  request.set_id(1337);
  request.set_msg("this is a test request");
  unsigned success_count{0};
  unsigned fail_count{0};
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   EXPECT_EQ(resp->foo(), 1337);
                                                   EXPECT_EQ(resp->bar(), "Echo: this is a test request");
                                                   ++success_count;
                                                 } else {
                                                   std::cout << "Call failed!" << std::endl;
                                                   ++fail_count;
                                                 }
                                               });

  std::this_thread::sleep_for(kServiceCallWaitTime);

  EXPECT_EQ(success_count, 1);
  EXPECT_EQ(fail_count, 0);
}

TEST_F(TrellisFixture, RepeatedServiceCallsInLoop) {
  StartRunnerThread();

  auto client = node_.CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = node_.CreateServiceServer<TestServiceHandler>(handler);

  WaitForDiscovery();

  unsigned success_count{0};
  unsigned fail_count{0};

  for (int i = 0; i < 5; ++i) {
    test::Test request;
    request.set_id(i);
    request.set_msg("repeat");
    client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                                 [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                   if (status == kSuccess) {
                                                     EXPECT_EQ(resp->foo(), i);
                                                     EXPECT_EQ(resp->bar(), "Echo: repeat");
                                                     ++success_count;
                                                   } else {
                                                     ++fail_count;
                                                   }
                                                 });
    std::this_thread::sleep_for(std::chrono::milliseconds{50});  // small delay to stagger calls
  }

  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(success_count, 5);
  EXPECT_EQ(fail_count, 0);
}

TEST_F(TrellisFixture, ServerRestartsBetweenCalls) {
  StartRunnerThread();

  auto client = node_.CreateServiceClient<trellis::core::test::TestService>();

  {
    auto handler = std::make_shared<TestServiceHandler>();
    auto server1 = node_.CreateServiceServer<TestServiceHandler>(handler);
    WaitForDiscovery();

    test::Test req1;
    req1.set_id(1);
    req1.set_msg("first");

    unsigned success_count = 0;
    client->CallAsync<test::Test, test::TestTwo>("DoStuff", req1,
                                                 [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                   if (status == kSuccess) {
                                                     EXPECT_EQ(resp->foo(), 1);
                                                     EXPECT_EQ(resp->bar(), "Echo: first");
                                                     ++success_count;
                                                   }
                                                 });

    std::this_thread::sleep_for(kServiceCallWaitTime);
    EXPECT_EQ(success_count, 1);
  }

  // Old server destructs here; now create a new one
  auto handler = std::make_shared<TestServiceHandler>();
  auto server2 = node_.CreateServiceServer<TestServiceHandler>(handler);
  WaitForDiscovery();
  std::this_thread::sleep_for(kServiceCallWaitTime);

  test::Test req2;
  req2.set_id(2);
  req2.set_msg("second");

  unsigned success_count = 0;
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", req2,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   EXPECT_EQ(resp->foo(), 2);
                                                   EXPECT_EQ(resp->bar(), "Echo: second");
                                                   ++success_count;
                                                 }
                                               });

  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(success_count, 1);
}

TEST_F(TrellisFixture, ClientRestartsBetweenCalls) {
  StartRunnerThread();

  auto handler = std::make_shared<TestServiceHandler>();
  auto server = node_.CreateServiceServer<TestServiceHandler>(handler);

  {
    auto client1 = node_.CreateServiceClient<trellis::core::test::TestService>();
    WaitForDiscovery();

    test::Test req1;
    req1.set_id(10);
    req1.set_msg("first client");

    unsigned success_count = 0;
    client1->CallAsync<test::Test, test::TestTwo>("DoStuff", req1,
                                                  [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                    if (status == kSuccess) {
                                                      EXPECT_EQ(resp->foo(), 10);
                                                      EXPECT_EQ(resp->bar(), "Echo: first client");
                                                      ++success_count;
                                                    }
                                                  });

    std::this_thread::sleep_for(kServiceCallWaitTime);
    EXPECT_EQ(success_count, 1);
  }

  // Old client destructs here; now create a new one
  auto client2 = node_.CreateServiceClient<trellis::core::test::TestService>();
  WaitForDiscovery();

  test::Test req2;
  req2.set_id(20);
  req2.set_msg("second client");

  unsigned success_count = 0;
  client2->CallAsync<test::Test, test::TestTwo>("DoStuff", req2,
                                                [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                  if (status == kSuccess) {
                                                    EXPECT_EQ(resp->foo(), 20);
                                                    EXPECT_EQ(resp->bar(), "Echo: second client");
                                                    ++success_count;
                                                  }
                                                });

  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(success_count, 1);
}

TEST_F(TrellisFixture, UnknownMethodReturnsFailure) {
  StartRunnerThread();

  auto client = node_.CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = node_.CreateServiceServer<TestServiceHandler>(handler);

  WaitForDiscovery();

  test::Test request;
  request.set_id(42);
  request.set_msg("invalid method");

  unsigned success_count = 0;
  unsigned fail_count = 0;

  client->CallAsync<test::Test, test::TestTwo>("UnknownMethod", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   ++success_count;
                                                 } else {
                                                   ++fail_count;
                                                 }
                                               });

  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(success_count, 0);
  EXPECT_EQ(fail_count, 1);
}

TEST_F(TrellisFixture, BackToBackCallFails) {
  StartRunnerThread();

  auto client = node_.CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = node_.CreateServiceServer<TestServiceHandler>(handler);

  // Wait for some time so the client can find the server
  WaitForDiscovery();

  test::Test request;
  request.set_id(1337);
  request.set_msg("this is a test request");
  unsigned success_count{0};
  unsigned fail_count{0};
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   EXPECT_EQ(resp->foo(), 1337);
                                                   EXPECT_EQ(resp->bar(), "Echo: this is a test request");
                                                   ++success_count;
                                                 } else {
                                                   std::cout << "Call failed!" << std::endl;
                                                   ++fail_count;
                                                 }
                                               });
  // Call again immediately... should fail
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   EXPECT_EQ(resp->foo(), 1337);
                                                   EXPECT_EQ(resp->bar(), "Echo: this is a test request");
                                                   ++success_count;
                                                 } else {
                                                   std::cout << "Call failed!" << std::endl;
                                                   ++fail_count;
                                                 }
                                               });

  std::this_thread::sleep_for(kServiceCallWaitTime);

  EXPECT_EQ(success_count, 1);
  EXPECT_EQ(fail_count, 1);
}

}  // namespace trellis::core::ipc::proto::rpc
