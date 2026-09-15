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

#include <atomic>

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

// The loop stops before the client is released, as it does when an app shuts down.
TEST_F(TrellisFixture, FailsCallsOnDestruction) {
  StartRunnerThread();

  std::vector<network::TCP> accepted;  // held so the connections stay open
  network::TCPServer silent_server(GetNode().GetEventLoop(), /* port = */ 0,
                                   [&accepted](const trellis::core::error_code& ec, network::TCP socket) {
                                     if (!ec) {
                                       accepted.push_back(std::move(socket));
                                     }
                                   });
  const auto handle = GetNode().GetDiscovery()->RegisterServiceServer(
      std::string{TestService::descriptor()->full_name()}, silent_server.GetPort(), MethodsMap{});
  std::atomic<unsigned> fail_count{0};  // declared before client: ~Client() runs the callbacks that reference it
  auto client = GetNode().CreateServiceClient<TestService>();
  WaitForDiscovery();

  for (int i = 0; i < 3; ++i) {
    test::Test request;
    request.set_id(i);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&fail_count](ServiceCallStatus status, const test::TestTwo*) {
          if (status == kFailure) ++fail_count;
        },
        /* timeout_ms = */ 0);
  }
  WaitForSendReceive();
  ASSERT_EQ(fail_count.load(), 0u) << "calls should still be outstanding before the client is destroyed";

  StopAndJoinRunnerThread();
  client.reset();
  EXPECT_EQ(fail_count.load(), 3u) << "one in flight and two queued, all should be failed by ~Client()";

  GetNode().GetDiscovery()->Unregister(handle);
}

// One call in flight and two queued when the registration goes away. Every one of them has to fail.
class UnregisterTest : public TrellisFixture {
 protected:
  void ExpectAllCallsFail(unsigned timeout_ms) {
    StartRunnerThread();

    std::vector<network::TCP> accepted;  // held so the connections stay open
    network::TCPServer silent_server(GetNode().GetEventLoop(), /* port = */ 0,
                                     [&accepted](const trellis::core::error_code& ec, network::TCP socket) {
                                       if (!ec) {
                                         accepted.push_back(std::move(socket));
                                       }
                                     });
    const auto handle = GetNode().GetDiscovery()->RegisterServiceServer(
        std::string{TestService::descriptor()->full_name()}, silent_server.GetPort(), MethodsMap{});
    // Declared before client: on a failing run ~Client() runs the callbacks that reference these.
    std::atomic<unsigned> success_count{0};
    std::atomic<unsigned> fail_count{0};
    std::atomic<unsigned> timeout_count{0};
    auto client = GetNode().CreateServiceClient<TestService>();
    WaitForDiscovery();

    for (int i = 0; i < 3; ++i) {
      test::Test request;
      request.set_id(i);
      client->CallAsync<test::Test, test::TestTwo>(
          "DoStuff", request,
          [&success_count, &fail_count, &timeout_count](ServiceCallStatus status, const test::TestTwo* resp) {
            if (status == kSuccess) {
              ++success_count;
            } else if (status == kFailure) {
              ++fail_count;
            } else {
              ++timeout_count;
            }
          },
          timeout_ms);
    }
    WaitForSendReceive();
    EXPECT_EQ(success_count.load(), 0u);
    EXPECT_EQ(fail_count.load(), 0u);
    EXPECT_EQ(timeout_count.load(), 0u);

    // The client only learns of this when discovery's stale-sample scan runs, up to one interval plus one timeout
    // later, and the failures then cascade through posted handlers.
    const auto unregistered_at = std::chrono::steady_clock::now();
    GetNode().GetDiscovery()->Unregister(handle);
    const auto deadline = unregistered_at + std::chrono::seconds{2};
    while (fail_count < 3 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const auto elapsed = std::chrono::steady_clock::now() - unregistered_at;

    // Run past the timeout so a request timer that was never cancelled still gets seen.
    if (timeout_ms > 0) {
      EXPECT_LT(elapsed, std::chrono::milliseconds{timeout_ms}) << "calls should fail on unregistration, not time out";
      std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms} - elapsed + kSendReceiveTime);
    }
    StopAndJoinRunnerThread();  // the accept handler and the call callbacks reference locals of this function

    EXPECT_EQ(success_count.load(), 0u);
    EXPECT_EQ(fail_count.load(), 3u);
    EXPECT_EQ(timeout_count.load(), 0u);
  }
};

// With no timer, closing the connection is the only thing that can complete the calls.
TEST_F(UnregisterTest, FailsCallsWithoutTimeout) { ExpectAllCallsFail(/* timeout_ms = */ 0); }

// The timeout outlasts discovery's purge, so the calls fail before it fires. Unregister() only stops the broadcast,
// so the client hears about it once the sample goes stale: kTestDiscoveryTimeout to expire, plus up to one
// kTestDiscoveryInterval before the management tick that scans for it. 1000 ms is comfortably clear of that 300 ms
// worst case without making a loaded machine flaky.
TEST_F(UnregisterTest, FailsCallsBeforeTimeout) { ExpectAllCallsFail(/* timeout_ms = */ 1000); }

}  // namespace trellis::core::ipc::proto::rpc
