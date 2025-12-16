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

static constexpr unsigned kLargeMessageTestSize = 4194304u;

class TestServiceHandler : public trellis::core::test::TestService {
 public:
  ~TestServiceHandler() override {}
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure*) override {
    response->set_foo(static_cast<float>(request->id()));
    if (request->id() == 100) {
      ASSERT_EQ(request->msg().size(), kLargeMessageTestSize);
      response->set_bar(std::string(kLargeMessageTestSize, 'A'));  // 4 megabyte string
    } else {
      if (request->id() == 2000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
      response->set_bar("Echo: " + request->msg());
    }
  }
};
static constexpr auto kServiceCallWaitTime = std::chrono::milliseconds{200};
static constexpr auto kTimeoutReconnectTime = std::chrono::milliseconds{400};
}  // namespace

using namespace trellis::core::test;

TEST_F(TrellisFixture, BasicSingleServiceCall) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

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

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

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
    std::this_thread::sleep_for(std::chrono::milliseconds{200});  // small delay to stagger calls
  }

  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(success_count, 5);
  EXPECT_EQ(fail_count, 0);
}

TEST_F(TrellisFixture, ServerRestartsBetweenCalls) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();

  {
    auto handler = std::make_shared<TestServiceHandler>();
    auto server1 = GetNode().CreateServiceServer<TestServiceHandler>(handler);
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
  auto server2 = GetNode().CreateServiceServer<TestServiceHandler>(handler);
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
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  {
    auto client1 = GetNode().CreateServiceClient<trellis::core::test::TestService>();
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
  auto client2 = GetNode().CreateServiceClient<trellis::core::test::TestService>();
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

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

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

TEST_F(TrellisFixture, BackToBackCallSucceeds) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

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
  // Call again immediately... should be enqueued
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

  EXPECT_EQ(success_count, 2);
  EXPECT_EQ(fail_count, 0);
}

TEST_F(TrellisFixture, LargeRequestResponse) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  // Wait for some time so the client can find the server
  WaitForDiscovery();

  test::Test request;
  request.set_id(100);  // special value to return a large message
  request.set_msg(std::string(kLargeMessageTestSize, 'X'));
  unsigned success_count{0};
  unsigned fail_count{0};
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   EXPECT_EQ(resp->foo(), 100);
                                                   //  EXPECT_EQ(resp->bar(), std::string(4194304, 'A'));
                                                   EXPECT_EQ(resp->bar().size(), 4194304);
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

TEST_F(TrellisFixture, LongRunningCallTimeout) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  // Wait for some time so the client can find the server
  WaitForDiscovery();

  test::Test request;
  request.set_id(2000);
  request.set_msg("this is a test request");
  unsigned callback_count{0};
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [&](ServiceCallStatus status, const test::TestTwo* resp) {
        EXPECT_EQ(status, kTimedOut);
        ++callback_count;
      },
      /* timeout_ms = */ 100);

  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(callback_count, 1);

  // Wait some additional time for the underlying socket to reconnect
  std::this_thread::sleep_for(kTimeoutReconnectTime);

  // Now do another call that should succeed
  callback_count = 0;
  request.set_id(10);  // Call again and see that we succeed
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [&](ServiceCallStatus status, const test::TestTwo* resp) {
        EXPECT_EQ(status, kSuccess);
        ++callback_count;
      },
      /* timeout_ms = */ 100);
  std::this_thread::sleep_for(kServiceCallWaitTime);
  EXPECT_EQ(callback_count, 1);
}

TEST_F(TrellisFixture, QueuedCallsWithTimeouts) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  WaitForDiscovery();

  unsigned success_count{0};
  unsigned timeout_count{0};
  unsigned fail_count{0};

  // Make multiple calls rapidly with varying response times and timeouts. they should all succeed, even though the
  // first one takes longer
  for (int i = 0; i < 3; ++i) {
    test::Test request;
    request.set_id(i == 0 ? 2000 : i);  // First request triggers 500ms delay
    request.set_msg("queued_call_" + std::to_string(i));

    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&, i](ServiceCallStatus status, const test::TestTwo* resp) {
          if (status == kSuccess) {
            ++success_count;
          } else if (status == kTimedOut) {
            ++timeout_count;
          } else {
            ++fail_count;
          }
        },
        i == 1 ? 100 : 0);  // second request times out after 100ms, others have no timeout
  }

  // Wait longer to account for the 500ms delay plus processing time
  std::this_thread::sleep_for(std::chrono::milliseconds{800});

  EXPECT_EQ(success_count, 3);  // all three calls should succeed
  EXPECT_EQ(timeout_count, 0);
  EXPECT_EQ(fail_count, 0);
}

TEST_F(TrellisFixture, TimeoutResponseCorrelation) {
  // This test verifies that after a timeout, the next call receives its own response
  // and not a stale response from the timed-out call.
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  WaitForDiscovery();

  unsigned timeout_count{0};
  unsigned success_count{0};
  unsigned correlation_errors{0};

  // First call: will timeout but the server will eventually complete and send a response
  test::Test request1;
  request1.set_id(2000);  // triggers 500ms delay in handler
  request1.set_msg("timeout_request");

  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request1,
      [&](ServiceCallStatus status, const test::TestTwo* resp) {
        if (status == kTimedOut) {
          ++timeout_count;
        }
      },
      /* timeout_ms = */ 100);

  // Wait for both the client timeout (100ms) and the server handler (500ms) to complete.
  // This ensures the stale response is sitting in the socket buffer before the second call.
  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  EXPECT_EQ(timeout_count, 1);

  // Second call: should receive its own response, not the stale one from the first call
  test::Test request2;
  request2.set_id(9999);  // unique ID to verify correlation
  request2.set_msg("follow_up_request");

  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request2,
      [&](ServiceCallStatus status, const test::TestTwo* resp) {
        if (status == kSuccess) {
          ++success_count;
          // The response foo field should match request2's id (9999), not request1's id (2000)
          if (resp->foo() != 9999.0f) {
            std::cout << "CORRELATION ERROR: Expected foo=9999, got foo=" << resp->foo() << std::endl;
            ++correlation_errors;
          }
          // The response bar should echo request2's message
          if (resp->bar() != "Echo: follow_up_request") {
            std::cout << "CORRELATION ERROR: Expected 'Echo: follow_up_request', got '" << resp->bar() << "'"
                      << std::endl;
            ++correlation_errors;
          }
        }
      },
      /* timeout_ms = */ 500);

  std::this_thread::sleep_for(std::chrono::milliseconds{300});

  EXPECT_EQ(timeout_count, 1);
  EXPECT_EQ(success_count, 1);
  EXPECT_EQ(correlation_errors, 0) << "Response did not match the request - possible stale response received";
}

}  // namespace trellis::core::ipc::proto::rpc
