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
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "trellis/core/ipc/proto/rpc/client.hpp"
#include "trellis/core/ipc/proto/rpc/server.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

namespace trellis::core::ipc::proto::rpc {

namespace {

static constexpr unsigned kLargeMessageTestSize = 4194304u;
// How long a test waits for calls to finish. Generous, since a sanitizer slows them; a reply that comes ends the wait.
static constexpr auto kServiceCallDeadline = std::chrono::seconds{10};
static constexpr auto kTimeoutReconnectTime = std::chrono::milliseconds{400};

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

// Leaves DoStuff to the generated body, which reports "not implemented" through the controller and runs `done`.
class UnimplementedHandler : public trellis::core::test::TestService {};

// Runs `done` before returning, as a handler written against protobuf's callback-style API does.
class DoneRunningHandler : public TestServiceHandler {
 public:
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure* done) override {
    TestServiceHandler::DoStuff(controller, request, response, done);
    done->Run();
  }
};

// Fails request ids 1 to 3, each its own way, and answers the rest.
class FailingHandler : public TestServiceHandler {
 public:
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure* done) override {
    switch (request->id()) {
      case 1:
        throw std::runtime_error{"handler failed"};
      case 2:
        throw 2;  // not a std::exception
      case 3:
        controller->SetFailed("refused");
        return;
      default:
        TestServiceHandler::DoStuff(controller, request, response, done);
    }
  }
};

// Registers a NotifyOnCancel() callback that counts its runs, then throws for request id 1 and answers the rest.
class CancelNotifyingHandler : public TestServiceHandler {
 public:
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure* done) override {
    controller->NotifyOnCancel(google::protobuf::NewCallback(this, &CancelNotifyingHandler::CountCancelCallback));
    if (request->id() == 1) {
      throw std::runtime_error{"handler failed"};
    }
    TestServiceHandler::DoStuff(controller, request, response, done);
  }

  std::atomic<int> cancel_callbacks{0};

 private:
  void CountCancelCallback() { ++cancel_callbacks; }
};

// Calls DoStuff with `id` and returns the status and the response's foo, or nothing if no outcome arrives in time.
template <typename CLIENT_T>
std::optional<std::pair<ServiceCallStatus, float>> CallDoStuff(CLIENT_T& client, unsigned id) {
  auto result = std::make_shared<std::promise<std::pair<ServiceCallStatus, float>>>();
  test::Test request;
  request.set_id(id);
  client.template CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [result](ServiceCallStatus status, const test::TestTwo* response) {
        result->set_value({status, response != nullptr ? response->foo() : 0.0f});
      },
      /* timeout_ms = */ 2000);
  auto future = result->get_future();
  if (future.wait_for(std::chrono::seconds{3}) != std::future_status::ready) {
    return std::nullopt;
  }
  return future.get();
}

// As CallDoStuff(), but through the callback that is also told why a call failed.
template <typename CLIENT_T>
std::optional<std::pair<ServiceCallStatus, std::string>> CallDoStuffForError(CLIENT_T& client, unsigned id) {
  auto result = std::make_shared<std::promise<std::pair<ServiceCallStatus, std::string>>>();
  test::Test request;
  request.set_id(id);
  client.template CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [result](ServiceCallStatus status, const test::TestTwo*, std::string_view error) {
        result->set_value({status, std::string{error}});
      },
      /* timeout_ms = */ 2000);
  auto future = result->get_future();
  if (future.wait_for(std::chrono::seconds{3}) != std::future_status::ready) {
    return std::nullopt;
  }
  return future.get();
}

// Service handlers run on the server's own thread, so a call made inside a handler comes from off the event loop.
class NestedCallHandler : public TestServiceHandler {
 public:
  explicit NestedCallHandler(std::function<void()> on_outer_call) : on_outer_call_{std::move(on_outer_call)} {}
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure* done) override {
    if (request->id() == 1) {
      on_outer_call_();
    }
    TestServiceHandler::DoStuff(controller, request, response, done);
  }

 private:
  std::function<void()> on_outer_call_;
};

// Records that a handler started, so a test can tell a reply that is genuinely owed from a request that never got
// dispatched. Request id 2000 makes the base handler sleep, which is what holds the reply back.
class SlowSignallingHandler : public TestServiceHandler {
 public:
  void DoStuff(::google::protobuf::RpcController* controller, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure* done) override {
    entered = true;
    TestServiceHandler::DoStuff(controller, request, response, done);
  }

  std::atomic<bool> entered{false};
};

// Holds each call until `release`, and counts them. Given a loop, it holds that loop, as a service owning a Client
// does.
class BlockingHandler : public trellis::core::test::TestService {
 public:
  explicit BlockingHandler(std::optional<EventLoop> loop = std::nullopt) : loop_{std::move(loop)} {}

  void DoStuff(::google::protobuf::RpcController*, const ::trellis::core::test::Test* request,
               ::trellis::core::test::TestTwo* response, ::google::protobuf::Closure*) override {
    ++calls;
    entered = true;
    while (!release) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    response->set_foo(static_cast<float>(request->id()));
  }

  std::atomic<int> calls{0};
  std::atomic<bool> entered{false};
  std::atomic<bool> release{false};

 private:
  std::optional<EventLoop> loop_;
};

// Added to an io_context, sets `destroyed` when that io_context is destroyed.
class ContextDestroyedFlag : public asio::execution_context::service {
 public:
  static inline asio::execution_context::id id;

  ContextDestroyedFlag(asio::execution_context& context, std::shared_ptr<std::atomic<bool>> destroyed)
      : asio::execution_context::service{context}, destroyed_{std::move(destroyed)} {}
  ~ContextDestroyedFlag() override { *destroyed_ = true; }

 private:
  void shutdown() override {}

  std::shared_ptr<std::atomic<bool>> destroyed_;
};

// Polls `done` until it holds or `timeout` passes, and returns whether it held.
template <typename Pred>
bool WaitUntil(Pred done, std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!done()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  return true;
}

// A node of the test's own, run on its own thread, that tells when its loop is destroyed. A test uses one to see a
// handler thread let go of the loop, which the fixture's node would outlive.
struct OwnNode {
  OwnNode() { asio::make_service<ContextDestroyedFlag>(*node->GetEventLoop(), loop_destroyed); }
  ~OwnNode() { Stop(); }

  void Run() {
    runner = std::thread([node = node.get()]() { node->Run(); });
  }
  // Stops the loop and waits for the runner, if it is still running.
  void Stop() {
    if (runner.joinable()) {
      node->Stop();
      runner.join();
    }
  }

  std::shared_ptr<std::atomic<bool>> loop_destroyed{std::make_shared<std::atomic<bool>>(false)};
  std::unique_ptr<Node> node{std::make_unique<Node>(
      "own_node", Config(YAML::Load(test::CreateConfig(test::kNumPubBuffers, test::kTestDiscoveryInterval,
                                                       test::kTestDiscoveryTimeout))))};
  std::thread runner;
};

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
  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> fail_count{0};
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

  EXPECT_TRUE(WaitUntil([&]() { return success_count + fail_count > 0; }, kServiceCallDeadline)) << "no reply";

  EXPECT_EQ(success_count.load(), 1u);
  EXPECT_EQ(fail_count.load(), 0u);
}

TEST_F(TrellisFixture, RepeatedServiceCallsInLoop) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  WaitForDiscovery();

  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> fail_count{0};

  for (int i = 0; i < 5; ++i) {
    test::Test request;
    request.set_id(i);
    request.set_msg("repeat");
    client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                                 [&, i](ServiceCallStatus status, const test::TestTwo* resp) {
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

  EXPECT_TRUE(WaitUntil([&]() { return success_count + fail_count == 5; }, kServiceCallDeadline)) << "no reply";
  EXPECT_EQ(success_count.load(), 5u);
  EXPECT_EQ(fail_count.load(), 0u);
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

    std::atomic<unsigned> success_count{0};
    client->CallAsync<test::Test, test::TestTwo>("DoStuff", req1,
                                                 [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                   if (status == kSuccess) {
                                                     EXPECT_EQ(resp->foo(), 1);
                                                     EXPECT_EQ(resp->bar(), "Echo: first");
                                                     ++success_count;
                                                   }
                                                 });

    EXPECT_TRUE(WaitUntil([&]() { return success_count == 1; }, kServiceCallDeadline)) << "no reply";
    EXPECT_EQ(success_count.load(), 1u);
  }

  // Old server destructs here; now create a new one
  auto handler = std::make_shared<TestServiceHandler>();
  auto server2 = GetNode().CreateServiceServer<TestServiceHandler>(handler);
  WaitForDiscovery();
  // The old server's sample goes stale after kTestDiscoveryTimeout, but the purge waits for the next discovery tick.
  std::this_thread::sleep_for(std::chrono::milliseconds{200});

  test::Test req2;
  req2.set_id(2);
  req2.set_msg("second");

  std::atomic<unsigned> success_count{0};
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", req2,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   EXPECT_EQ(resp->foo(), 2);
                                                   EXPECT_EQ(resp->bar(), "Echo: second");
                                                   ++success_count;
                                                 }
                                               });

  EXPECT_TRUE(WaitUntil([&]() { return success_count == 1; }, kServiceCallDeadline)) << "no reply";
  EXPECT_EQ(success_count.load(), 1u);
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

    std::atomic<unsigned> success_count{0};
    client1->CallAsync<test::Test, test::TestTwo>("DoStuff", req1,
                                                  [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                    if (status == kSuccess) {
                                                      EXPECT_EQ(resp->foo(), 10);
                                                      EXPECT_EQ(resp->bar(), "Echo: first client");
                                                      ++success_count;
                                                    }
                                                  });

    EXPECT_TRUE(WaitUntil([&]() { return success_count == 1; }, kServiceCallDeadline)) << "no reply";
    EXPECT_EQ(success_count.load(), 1u);
  }

  // Old client destructs here; now create a new one
  auto client2 = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  WaitForDiscovery();

  test::Test req2;
  req2.set_id(20);
  req2.set_msg("second client");

  std::atomic<unsigned> success_count{0};
  client2->CallAsync<test::Test, test::TestTwo>("DoStuff", req2,
                                                [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                  if (status == kSuccess) {
                                                    EXPECT_EQ(resp->foo(), 20);
                                                    EXPECT_EQ(resp->bar(), "Echo: second client");
                                                    ++success_count;
                                                  }
                                                });

  EXPECT_TRUE(WaitUntil([&]() { return success_count == 1; }, kServiceCallDeadline)) << "no reply";
  EXPECT_EQ(success_count.load(), 1u);
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

  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> fail_count{0};

  client->CallAsync<test::Test, test::TestTwo>("UnknownMethod", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess) {
                                                   ++success_count;
                                                 } else {
                                                   ++fail_count;
                                                 }
                                               });

  EXPECT_TRUE(WaitUntil([&]() { return success_count + fail_count > 0; }, kServiceCallDeadline)) << "no reply";
  EXPECT_EQ(success_count.load(), 0u);
  EXPECT_EQ(fail_count.load(), 1u);
}

// The generated body of an unimplemented method dereferences the controller and `done`, which were null.
TEST_F(TrellisFixture, UnimplementedMethodReturnsFailure) {
  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto server = GetNode().CreateServiceServer<UnimplementedHandler>(std::make_shared<UnimplementedHandler>());
  WaitForDiscovery();

  const auto result = CallDoStuff(*client, 7);
  ASSERT_TRUE(result.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(result->first, kFailure);

  const auto again = CallDoStuff(*client, 8);
  ASSERT_TRUE(again.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(again->first, kFailure) << "the server keeps serving";
}

// `done` was null, so a handler that ran it crashed the server.
TEST_F(TrellisFixture, HandlerMayRunDone) {
  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto server = GetNode().CreateServiceServer<DoneRunningHandler>(std::make_shared<DoneRunningHandler>());
  WaitForDiscovery();

  const auto result = CallDoStuff(*client, 7);
  ASSERT_TRUE(result.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(result->first, kSuccess);
  EXPECT_EQ(result->second, 7.0f);
}

// A throw used to leave the handler thread and terminate the process.
TEST_F(TrellisFixture, ThrowingHandlerReturnsFailureAndTheServerKeepsServing) {
  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto server = GetNode().CreateServiceServer<FailingHandler>(std::make_shared<FailingHandler>());
  WaitForDiscovery();

  const auto failed = CallDoStuff(*client, 1);
  ASSERT_TRUE(failed.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(failed->first, kFailure);

  const auto served = CallDoStuff(*client, 7);
  ASSERT_TRUE(served.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(served->first, kSuccess);
  EXPECT_EQ(served->second, 7.0f);
}

// RpcController requires a NotifyOnCancel() callback to run exactly once when an uncanceled call completes, and a
// self-deleting one leaks if it never does. It runs before the reply goes out, whether the handler answered or threw.
TEST_F(TrellisFixture, CancelCallbackRunsOnceTheHandlerReturns) {
  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<CancelNotifyingHandler>();
  auto server = GetNode().CreateServiceServer<CancelNotifyingHandler>(handler);
  WaitForDiscovery();

  const auto served = CallDoStuff(*client, 7);
  ASSERT_TRUE(served.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(served->first, kSuccess);
  EXPECT_EQ(handler->cancel_callbacks.load(), 1);

  const auto failed = CallDoStuff(*client, 1);
  ASSERT_TRUE(failed.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(failed->first, kFailure);
  EXPECT_EQ(handler->cancel_callbacks.load(), 2);
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
  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> fail_count{0};
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

  EXPECT_TRUE(WaitUntil([&]() { return success_count + fail_count == 2; }, kServiceCallDeadline)) << "no reply";

  EXPECT_EQ(success_count.load(), 2u);
  EXPECT_EQ(fail_count.load(), 0u);
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
  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> fail_count{0};
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

  EXPECT_TRUE(WaitUntil([&]() { return success_count + fail_count > 0; }, kServiceCallDeadline)) << "no reply";

  EXPECT_EQ(success_count.load(), 1u);
  EXPECT_EQ(fail_count.load(), 0u);
}

// A request whose length prefix is over the limit closes the connection instead of waiting for a body that size.
TEST_F(TrellisFixture, ServerClosesAConnectionWhoseRequestIsTooLong) {
  StartRunnerThread();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);
  WaitForDiscovery();

  std::promise<uint16_t> port_promise;
  asio::post(*GetNode().GetEventLoop(), [this, &port_promise]() {
    for (const auto& sample : GetNode().GetDiscovery()->GetServiceSamples()) {
      if (sample.service().sname() == TestService::descriptor()->full_name()) {
        port_promise.set_value(sample.service().tcp_port());
        return;
      }
    }
    port_promise.set_value(0);
  });
  const uint16_t port = port_promise.get_future().get();
  ASSERT_NE(port, 0);

  asio::io_context io;
  asio::ip::tcp::socket socket{io};
  socket.connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});
  const timeval receive_timeout{.tv_sec = 2, .tv_usec = 0};
  ASSERT_EQ(::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)),
            0);
  const uint32_t too_long = kMaxFrameSize + 1;
  asio::write(socket, asio::buffer(&too_long, sizeof(too_long)));
  std::array<uint8_t, 1> byte{};
  trellis::core::error_code ec;
  socket.read_some(asio::buffer(byte), ec);
  EXPECT_EQ(ec, asio::error::eof) << ec.message();

  StopAndJoinRunnerThread();  // the server must not be destroyed while the loop can still accept on it
}

// A response whose length prefix is over the limit fails the call at once instead of waiting out its timeout.
TEST_F(TrellisFixture, ClientFailsACallWhoseResponseIsTooLong) {
  StartRunnerThread();

  static constexpr uint32_t kTooLong = kMaxFrameSize + 1;
  std::vector<network::TCP> accepted;  // held so the connections stay open
  network::TCPServer oversized_server(
      GetNode().GetEventLoop(), /* port = */ 0, [&accepted](const trellis::core::error_code& ec, network::TCP socket) {
        if (!ec) {
          socket.AsyncSendAll(&kTooLong, sizeof(kTooLong), [](const trellis::core::error_code&, size_t) {});
          accepted.push_back(std::move(socket));
        }
      });
  const auto handle = GetNode().GetDiscovery()->RegisterServiceServer(
      std::string{TestService::descriptor()->full_name()}, oversized_server.GetPort(), MethodsMap{});
  std::atomic<std::optional<ServiceCallStatus>> status;  // declared before client, whose destructor runs callbacks
  auto client = GetNode().CreateServiceClient<TestService>();
  WaitForDiscovery();

  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", test::Test{}, [&status](ServiceCallStatus result, const test::TestTwo*) { status = result; },
      /* timeout_ms = */ 5000);
  ASSERT_TRUE(WaitUntil([&status]() { return status.load().has_value(); }));
  EXPECT_EQ(status.load(), kFailure);

  StopAndJoinRunnerThread();
  GetNode().GetDiscovery()->Unregister(handle);
}

// Bytes no message parses: field 2 claims 5 bytes, and only 2 follow.
const std::string kTruncatedField{
    "\x12\x05"
    "ab",
    4};

// Writes one length-prefixed frame on a raw socket and reads the reply frame, as a client would.
discovery::Response ExchangeFrame(asio::ip::tcp::socket& socket, const std::string& body) {
  const uint32_t length = body.size();
  asio::write(socket, std::array{asio::buffer(&length, sizeof(length)), asio::buffer(body)});
  uint32_t reply_length = 0;
  asio::read(socket, asio::buffer(&reply_length, sizeof(reply_length)));
  std::string reply(reply_length, '\0');
  asio::read(socket, asio::buffer(reply));
  discovery::Response response;
  EXPECT_TRUE(response.ParseFromString(reply));
  return response;
}

// A request body the method's type can't parse reached the handler as a partial message, and was answered as executed.
// It is now failed, and the connection stays usable: the frame's length kept the stream in step.
TEST_F(TrellisFixture, ServerFailsARequestThatDoesNotParse) {
  StartRunnerThread();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(std::make_shared<TestServiceHandler>());
  WaitForDiscovery();

  std::promise<uint16_t> port_promise;
  asio::post(*GetNode().GetEventLoop(), [this, &port_promise]() {
    for (const auto& sample : GetNode().GetDiscovery()->GetServiceSamples()) {
      if (sample.service().sname() == TestService::descriptor()->full_name()) {
        port_promise.set_value(sample.service().tcp_port());
        return;
      }
    }
    port_promise.set_value(0);
  });
  const uint16_t port = port_promise.get_future().get();
  ASSERT_NE(port, 0);

  asio::io_context io;
  asio::ip::tcp::socket socket{io};
  socket.connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});
  const timeval receive_timeout{.tv_sec = 2, .tv_usec = 0};
  ASSERT_EQ(::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)),
            0);

  discovery::Request request;
  request.mutable_header()->set_mname("DoStuff");
  request.set_request(kTruncatedField);
  EXPECT_EQ(ExchangeFrame(socket, request.SerializeAsString()).header().status(), discovery::ServiceHeader::failed)
      << "a request body that does not parse";
  EXPECT_EQ(ExchangeFrame(socket, kTruncatedField).header().status(), discovery::ServiceHeader::failed)
      << "an envelope that does not parse";

  test::Test valid;
  valid.set_id(7);
  request.set_request(valid.SerializeAsString());
  const discovery::Response answered = ExchangeFrame(socket, request.SerializeAsString());
  EXPECT_EQ(answered.header().status(), discovery::ServiceHeader::executed);
  test::TestTwo response;
  ASSERT_TRUE(response.ParseFromString(answered.response()));
  EXPECT_EQ(response.foo(), 7.0f);

  StopAndJoinRunnerThread();  // the server must not be destroyed while the loop can still accept on it
}

// A caller sees only kFailure, so the text a failing handler is answered with is checked on the wire.
TEST_F(TrellisFixture, ServerAnswersAFailingHandlerWithItsText) {
  StartRunnerThread();
  auto server = GetNode().CreateServiceServer<FailingHandler>(std::make_shared<FailingHandler>());
  WaitForDiscovery();

  std::promise<uint16_t> port_promise;
  asio::post(*GetNode().GetEventLoop(), [this, &port_promise]() {
    for (const auto& sample : GetNode().GetDiscovery()->GetServiceSamples()) {
      if (sample.service().sname() == TestService::descriptor()->full_name()) {
        port_promise.set_value(sample.service().tcp_port());
        return;
      }
    }
    port_promise.set_value(0);
  });
  const uint16_t port = port_promise.get_future().get();
  ASSERT_NE(port, 0);

  asio::io_context io;
  asio::ip::tcp::socket socket{io};
  socket.connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port});
  const timeval receive_timeout{.tv_sec = 2, .tv_usec = 0};
  ASSERT_EQ(::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)),
            0);

  const auto call = [&socket](unsigned id) {
    test::Test rpc_request;
    rpc_request.set_id(id);
    discovery::Request request;
    request.mutable_header()->set_mname("DoStuff");
    request.set_request(rpc_request.SerializeAsString());
    return ExchangeFrame(socket, request.SerializeAsString()).header();
  };
  const auto threw = call(1);
  EXPECT_EQ(threw.status(), discovery::ServiceHeader::failed);
  EXPECT_EQ(threw.error(), "handler failed");
  const auto threw_other = call(2);
  EXPECT_EQ(threw_other.status(), discovery::ServiceHeader::failed);
  EXPECT_EQ(threw_other.error(), "handler threw a non-std::exception");
  const auto set_failed = call(3);
  EXPECT_EQ(set_failed.status(), discovery::ServiceHeader::failed);
  EXPECT_EQ(set_failed.error(), "refused");
  EXPECT_EQ(call(7).status(), discovery::ServiceHeader::executed) << "the connection is kept";

  StopAndJoinRunnerThread();  // the server must not be destroyed while the loop can still accept on it
}

// Answers a call with `reply` as its one response frame, and returns the status the call reports. Runs the loop for
// the call and stops it before anything here is destroyed.
class ResponseParseTest : public TrellisFixture {
 protected:
  ServiceCallStatus StatusOfACallAnsweredWith(const std::string& reply) {
    StartRunnerThread();
    auto frame = std::make_shared<std::string>(sizeof(uint32_t), '\0');
    const uint32_t length = reply.size();
    std::memcpy(frame->data(), &length, sizeof(length));
    frame->append(reply);
    std::vector<network::TCP> accepted;  // held so the connections stay open
    network::TCPServer fake_server(GetNode().GetEventLoop(), /* port = */ 0,
                                   [&accepted, frame](const trellis::core::error_code& ec, network::TCP socket) {
                                     if (!ec) {
                                       socket.AsyncSendAll(frame->data(), frame->size(),
                                                           [frame](const trellis::core::error_code&, size_t) {});
                                       accepted.push_back(std::move(socket));
                                     }
                                   });
    const auto handle = GetNode().GetDiscovery()->RegisterServiceServer(
        std::string{TestService::descriptor()->full_name()}, fake_server.GetPort(), MethodsMap{});
    std::atomic<std::optional<ServiceCallStatus>> status;  // declared before client, whose destructor runs callbacks
    auto client = GetNode().CreateServiceClient<TestService>();
    WaitForDiscovery();

    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", test::Test{}, [&status](ServiceCallStatus result, const test::TestTwo*) { status = result; },
        /* timeout_ms = */ 5000);
    EXPECT_TRUE(WaitUntil([&status]() { return status.load().has_value(); }));
    StopAndJoinRunnerThread();
    GetNode().GetDiscovery()->Unregister(handle);
    return status.load().value_or(kTimedOut);
  }
};

// A reply that did not parse was reported as kSuccess with an empty response.
TEST_F(ResponseParseTest, ClientFailsACallWhoseResponseEnvelopeDoesNotParse) {
  EXPECT_EQ(StatusOfACallAnsweredWith(kTruncatedField), kFailure);
}

TEST_F(ResponseParseTest, ClientFailsACallWhoseResponseBodyDoesNotParse) {
  discovery::Response response;
  response.mutable_header()->set_status(discovery::ServiceHeader::executed);
  response.set_response(kTruncatedField);
  EXPECT_EQ(StatusOfACallAnsweredWith(response.SerializeAsString()), kFailure);
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
  std::atomic<unsigned> callback_count{0};
  client->CallAsync<test::Test, test::TestTwo>(
      "DoStuff", request,
      [&](ServiceCallStatus status, const test::TestTwo* resp) {
        EXPECT_EQ(status, kTimedOut);
        ++callback_count;
      },
      /* timeout_ms = */ 100);

  EXPECT_TRUE(WaitUntil([&]() { return callback_count == 1; }, kServiceCallDeadline)) << "no reply";
  EXPECT_EQ(callback_count.load(), 1u);

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
  EXPECT_TRUE(WaitUntil([&]() { return callback_count == 1; }, kServiceCallDeadline)) << "no reply";
  EXPECT_EQ(callback_count.load(), 1u);
}

TEST_F(TrellisFixture, QueuedCallsWithTimeouts) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  WaitForDiscovery();

  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> timeout_count{0};
  std::atomic<unsigned> fail_count{0};

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

  EXPECT_EQ(success_count.load(), 3u);  // all three calls should succeed
  EXPECT_EQ(timeout_count.load(), 0u);
  EXPECT_EQ(fail_count.load(), 0u);
}

// The second call goes out while the timed-out call's reply is still owed. Sharing a socket, that late reply arrived
// after the second request and was read as the second call's response. The handler holds the first reply until the
// second request has gone out, so the reply can't arrive first and be skipped.
TEST_F(TrellisFixture, LateReplyToATimedOutCallIsNotDeliveredToTheNextCall) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<BlockingHandler>();
  auto server = GetNode().CreateServiceServer<BlockingHandler>(handler);
  absl::Cleanup release = [&handler]() { handler->release = true; };  // or ~Server() waits on the handler
  WaitForDiscovery();

  std::promise<ServiceCallStatus> first;
  RunOnLoop([&]() {
    test::Test request;
    request.set_id(2000);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request, [&first](ServiceCallStatus status, const test::TestTwo*) { first.set_value(status); },
        /* timeout_ms = */ 100);
  });
  ASSERT_TRUE(WaitUntil([&handler]() { return handler->entered.load(); }));
  auto first_status = first.get_future();
  ASSERT_EQ(first_status.wait_for(std::chrono::seconds{2}), std::future_status::ready);
  ASSERT_EQ(first_status.get(), kTimedOut);

  std::promise<std::pair<ServiceCallStatus, float>> second;
  RunOnLoop([&]() {
    test::Test request;
    request.set_id(9999);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&second](ServiceCallStatus status, const test::TestTwo* response) {
          second.set_value({status, response == nullptr ? 0.0f : response->foo()});
        },
        /* timeout_ms = */ 2000);
  });
  handler->release = true;  // the first reply, then the second's

  auto second_result = second.get_future();
  ASSERT_EQ(second_result.wait_for(std::chrono::seconds{3}), std::future_status::ready);
  const auto [status, foo] = second_result.get();
  EXPECT_EQ(status, kSuccess);
  EXPECT_EQ(foo, 9999.0f) << "the second call got the timed-out call's reply";
  StopAndJoinRunnerThread();  // The promises are locals.
}

// ~Server() drops the requests that have not started. It has closed their sockets, so no reply could arrive.
TEST_F(TrellisFixture, ServerDestroyedWithAQueuedRequestDoesNotRunIt) {
  StartRunnerThread();
  auto handler = std::make_shared<BlockingHandler>();
  auto server = GetNode().CreateServiceServer<BlockingHandler>(handler);
  // One call in flight per client, so a second client queues a request behind the running one.
  auto running_client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto queued_client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  // On any exit, let the handler finish, so ~Server() does not wait for it forever.
  absl::Cleanup release = [&handler]() { handler->release = true; };
  WaitForDiscovery();

  asio::post(*GetNode().GetEventLoop(), [running_client, queued_client]() {
    test::Test request;
    request.set_id(1);
    running_client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                                         [](ServiceCallStatus, const test::TestTwo*) {});
    request.set_id(2);
    queued_client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                                        [](ServiceCallStatus, const test::TestTwo*) {});
  });
  ASSERT_TRUE(WaitUntil([&handler]() { return handler->entered.load(); }));
  // For the second request to be queued. Nothing here sees the handler queue, so the check on `calls` below is only
  // as good as this wait.
  WaitForSendReceive();

  // ~Server() waits for the running handler, so it is let go from another thread once ~Server() has dropped the rest.
  std::thread releaser([&handler]() {
    WaitForSendReceive();
    handler->release = true;
  });
  server.reset();
  releaser.join();
  EXPECT_EQ(handler->calls, 1) << "a request queued behind the running one ran after the server was gone";
  StopAndJoinRunnerThread();  // before the clients go
}

// A service commonly holds its loop, through a Client of its own, say. A handler finishing after the loop has stopped
// must not queue the service on that loop with its reply: the two would keep each other alive for good.
TEST_F(TrellisFixture, HandlerFinishingAfterTheLoopStopsDoesNotLeakAServiceThatHoldsTheLoop) {
  OwnNode own;
  auto handler = std::make_shared<BlockingHandler>(own.node->GetEventLoop());
  auto server = own.node->CreateServiceServer<BlockingHandler>(handler);
  auto client = own.node->CreateServiceClient<trellis::core::test::TestService>();
  // On any exit, let the handler finish, and stop the loop before the locals it uses go.
  absl::Cleanup stop_first = [&own, &handler]() {
    if (handler) {
      handler->release = true;
    }
    own.Stop();
  };
  own.Run();
  WaitForDiscovery();

  asio::post(*own.node->GetEventLoop(), [client]() {
    test::Test request;
    request.set_id(1);
    client->CallAsync<test::Test, test::TestTwo>("DoStuff", request, [](ServiceCallStatus, const test::TestTwo*) {});
  });
  ASSERT_TRUE(WaitUntil([&handler]() { return handler->entered.load(); }));

  own.Stop();
  handler->release = true;
  server.reset();  // off the loop's thread, so this waits for the handler
  client.reset();
  handler.reset();
  own.node.reset();
  EXPECT_TRUE(WaitUntil([&own]() { return own.loop_destroyed->load(); }))
      << "the service was queued on the stopped loop it holds, so neither was destroyed";
}

// A raw caller sends a request whose 4MB reply takes several writes, and a second request right behind it. The
// server used to start the second reply while the first was still being written, interleaving their bytes, so the
// caller read a length from inside the first reply's payload.
TEST_F(TrellisFixture, PipelinedRepliesAreWrittenOneAfterAnother) {
  constexpr int kConnections{10};
  StartRunnerThread();

  std::atomic<uint16_t> port{0};
  const auto discovery_handle = GetNode().GetDiscovery()->AsyncReceiveServices(
      [&port](discovery::Discovery::EventType event, const discovery::Sample& sample) {
        if (event == discovery::Discovery::EventType::kNewRegistration &&
            sample.service().sname() == trellis::core::test::TestService::descriptor()->full_name()) {
          port = static_cast<uint16_t>(sample.service().tcp_port());
        }
      });
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);
  WaitForDiscovery();
  ASSERT_NE(port.load(), 0);

  for (int connection = 0; connection < kConnections; ++connection) {
    trellis::core::EventLoop caller_loop;
    network::TCP caller{caller_loop, "127.0.0.1", port.load()};
    const auto send_exactly = [&caller](const void* buffer, size_t size) {
      for (size_t sent = 0; sent < size;) {
        sent += caller.Send(static_cast<const uint8_t*>(buffer) + sent, size - sent);
      }
    };
    const auto send_request = [&send_exactly](unsigned id, std::string msg) {
      test::Test payload;
      payload.set_id(id);
      payload.set_msg(std::move(msg));
      discovery::Request request;
      request.mutable_header()->set_mname("DoStuff");
      payload.SerializeToString(request.mutable_request());
      const std::string wire = request.SerializeAsString();
      const uint32_t size = wire.size();
      send_exactly(&size, sizeof(size));
      send_exactly(wire.data(), wire.size());
    };
    const auto receive_exactly = [&caller](void* buffer, size_t size) {
      for (size_t received = 0; received < size;) {
        received += caller.Receive(static_cast<uint8_t*>(buffer) + received, size - received);
      }
    };

    send_request(100, std::string(kLargeMessageTestSize, 'B'));  // the handler answers id 100 with 4MB
    send_request(7, "small");

    for (const float expected_foo : {100.0f, 7.0f}) {
      uint32_t length = 0;
      receive_exactly(&length, sizeof(length));
      ASSERT_LE(length, 2 * kLargeMessageTestSize) << "connection " << connection << " read a length from mid-reply";
      std::vector<uint8_t> frame(length);
      receive_exactly(frame.data(), frame.size());
      discovery::Response response;
      test::TestTwo reply;
      ASSERT_TRUE(response.ParseFromArray(frame.data(), frame.size()) && reply.ParseFromString(response.response()))
          << "connection " << connection << " got a garbled reply";
      EXPECT_EQ(reply.foo(), expected_foo) << "connection " << connection;
    }
    caller.Close();
  }

  StopAndJoinRunnerThread();
  GetNode().GetDiscovery()->StopReceive(discovery_handle);
}

// The handler thread used to start before the TCP server and the registration, so a throw from either left it
// joinable while the members unwound, and std::thread's destructor terminated the process.
TEST_F(TrellisFixture, ServerThatFailsToConstructThrows) {
  // Caps descriptors at the next free one, so the server's listening socket is the first to fail (EMFILE).
  const int next_fd = ::dup(0);
  ASSERT_GE(next_fd, 0);
  ::close(next_fd);
  rlimit original{};
  ASSERT_EQ(::getrlimit(RLIMIT_NOFILE, &original), 0);
  rlimit capped = original;
  capped.rlim_cur = static_cast<rlim_t>(next_fd);
  ASSERT_EQ(::setrlimit(RLIMIT_NOFILE, &capped), 0);
  const absl::Cleanup restore = [&original]() { ::setrlimit(RLIMIT_NOFILE, &original); };

  EXPECT_THROW(GetNode().CreateServiceServer<TestServiceHandler>(std::make_shared<TestServiceHandler>()),
               std::system_error);
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
    while (fail_count.load() < 3 && std::chrono::steady_clock::now() < deadline) {
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

TEST_F(TrellisFixture, CallFromServiceHandler) {
  StartRunnerThread();

  std::atomic<unsigned> outer_success_count{0};
  std::atomic<unsigned> nested_success_count{0};
  std::atomic<unsigned> fail_count{0};
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<NestedCallHandler>([&]() {
    test::Test nested;
    nested.set_id(2);
    nested.set_msg("nested");
    // Usually queues behind the outer call, which is still pending. It goes straight out if the outer reply lands
    // first; either way it must be answered.
    client->CallAsync<test::Test, test::TestTwo>("DoStuff", nested,
                                                 [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                   if (status == kSuccess && resp->bar() == "Echo: nested") {
                                                     ++nested_success_count;
                                                   } else {
                                                     ++fail_count;
                                                   }
                                                 });
  });
  auto server = GetNode().CreateServiceServer<NestedCallHandler>(handler);
  WaitForDiscovery();

  test::Test outer;
  outer.set_id(1);
  outer.set_msg("outer");
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", outer,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess && resp->bar() == "Echo: outer") {
                                                   ++outer_success_count;
                                                 } else {
                                                   ++fail_count;
                                                 }
                                               });
  EXPECT_TRUE(
      WaitUntil([&]() { return outer_success_count + nested_success_count + fail_count >= 2; }, kServiceCallDeadline))
      << "no reply";
  StopAndJoinRunnerThread();  // the callbacks reference locals of this function

  EXPECT_EQ(outer_success_count.load(), 1u);
  EXPECT_EQ(nested_success_count.load(), 1u);
  EXPECT_EQ(fail_count.load(), 0u);
}

// A call made from another thread still gets exactly one callback if the client is destroyed before the loop picks
// the call up.
TEST_F(TrellisFixture, FailsHandedOffCallsOnDestruction) {
  std::atomic<unsigned> fail_count{0};
  std::atomic<unsigned> other_count{0};
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();

  // The loop is not running, so this call counts as coming from another thread and waits for the loop to pick it up.
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", test::Test{},
                                               [&](ServiceCallStatus status, const test::TestTwo*) {
                                                 if (status == kFailure) {
                                                   ++fail_count;
                                                 } else {
                                                   ++other_count;
                                                 }
                                               });
  EXPECT_EQ(fail_count.load(), 0u) << "a call from off the loop should not be answered before the loop sees it";

  client.reset();
  EXPECT_EQ(fail_count.load(), 1u);
  EXPECT_EQ(other_count.load(), 0u);
}

// Retrying from a failure callback is ordinary, and during teardown the retry arrives from the destroying thread
// after ~Client() has already closed the hand-off list. Nothing will drain that list again, so the CallAsync() making
// the retry has to answer it inline; that is the only thing keeping the one-callback guarantee once the list closes.
TEST_F(TrellisFixture, AnswersRetriesMadeDuringDestruction) {
  static constexpr unsigned kRetries = 3;
  std::atomic<unsigned> fail_count{0};
  std::atomic<unsigned> other_count{0};
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  // reset() nulls the shared_ptr before ~Client() runs, so the retry needs its own handle on the client.
  auto* const raw_client = client.get();

  std::function<void(ServiceCallStatus, const test::TestTwo*)> on_response = [&](ServiceCallStatus status,
                                                                                 const test::TestTwo*) {
    if (status == kFailure) {
      ++fail_count;
    } else {
      ++other_count;
    }
    if (fail_count.load() < kRetries) {
      raw_client->CallAsync<test::Test, test::TestTwo>("DoStuff", test::Test{}, on_response);
    }
  };

  // The loop is not running, so this call waits for the loop to pick it up and ~Client() is what fails it.
  raw_client->CallAsync<test::Test, test::TestTwo>("DoStuff", test::Test{}, on_response);
  ASSERT_EQ(fail_count.load(), 0u);

  client.reset();
  EXPECT_EQ(fail_count.load(), kRetries);
  EXPECT_EQ(other_count.load(), 0u);
}

// When called on the loop's thread with no connection, CallAsync() invokes the callback with kFailure before it
// returns.
// A handler's reason for failing a call reaches the caller, not just kFailure.
TEST_F(TrellisFixture, CallerSeesWhyAHandlerFailed) {
  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto server = GetNode().CreateServiceServer<FailingHandler>(std::make_shared<FailingHandler>());
  WaitForDiscovery();

  const std::vector<std::pair<unsigned, std::pair<ServiceCallStatus, std::string>>> cases{
      {1, {kFailure, "handler failed"}},
      {2, {kFailure, "handler threw a non-std::exception"}},
      {3, {kFailure, "refused"}},
      {7, {kSuccess, ""}},
  };
  for (const auto& [id, expected] : cases) {
    const auto result = CallDoStuffForError(*client, id);
    ASSERT_TRUE(result.has_value()) << "call " << id << " never reported an outcome";
    EXPECT_EQ(*result, expected) << "call " << id;
  }
}

// A failure the client reports itself says why too.
TEST_F(TrellisFixture, CallerSeesWhyTheClientFailedACall) {
  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();  // no server

  const auto result = CallDoStuffForError(*client, 7);
  ASSERT_TRUE(result.has_value()) << "the call never reported an outcome";
  EXPECT_EQ(*result, std::make_pair(kFailure, std::string{"not connected"}));
}

TEST_F(TrellisFixture, CallOnLoopWithoutConnectionFailsInline) {
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  std::promise<bool> failed_inline;
  asio::post(*GetNode().GetEventLoop(), [&]() {
    bool failed = false;
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", test::Test{},
        [&failed](ServiceCallStatus status, const test::TestTwo*) { failed = status == kFailure; });
    failed_inline.set_value(failed);
  });

  auto future = failed_inline.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::seconds{1}), std::future_status::ready);
  EXPECT_TRUE(future.get());
}

// A registration can name a server that has since gone, such as a stale sample. Connecting to it is refused, and
// that refusal used to throw out of the discovery callback and end Node::Run(). The client must instead stay up and
// connect once discovery reports a server it can reach. The call at the end only goes through if the loop survived.
TEST_F(TrellisFixture, RegistrationForAServerThatIsGoneDoesNotEndTheLoop) {
  // The port of a listener that has been destroyed, which closed it, so a connect to it is refused.
  const auto closed_port = [this]() {
    const network::TCPServer gone(GetNode().GetEventLoop(), /* port = */ 0,
                                  [](const trellis::core::error_code&, network::TCP) {});
    return gone.GetPort();
  }();
  // Declared before the client: ~Client() runs the callback that references it.
  std::promise<std::pair<ServiceCallStatus, float>> result;
  StartRunnerThread();

  const auto stale = GetNode().GetDiscovery()->RegisterServiceServer(
      std::string{TestService::descriptor()->full_name()}, closed_port, MethodsMap{});
  auto client = GetNode().CreateServiceClient<TestService>();
  WaitForDiscovery();

  GetNode().GetDiscovery()->Unregister(stale);
  WaitForDiscovery();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(std::make_shared<TestServiceHandler>());
  WaitForDiscovery();
  asio::post(*GetNode().GetEventLoop(), [&client, &result]() {
    test::Test request;
    request.set_id(7);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&result](ServiceCallStatus status, const test::TestTwo* response) {
          result.set_value({status, status == kSuccess ? response->foo() : 0.0f});
        },
        /* timeout_ms = */ 1000);
  });
  auto reply = result.get_future();
  ASSERT_EQ(reply.wait_for(std::chrono::seconds{2}), std::future_status::ready)
      << "no reply: the refused connect may have ended Node::Run()";
  EXPECT_EQ(reply.get(), std::make_pair(kSuccess, 7.0f));
  StopAndJoinRunnerThread();
}

// A registration for a server that has gone keeps arriving, once a heartbeat, while the client is connected to a live
// server. Each one names another port, so the client tries to move to it. The connect is refused, and the client must
// keep the connection it has rather than drop it and fail the call on it.
TEST_F(TrellisFixture, RegistrationForAServerThatIsGoneKeepsTheLiveConnection) {
  static constexpr int kCalls = 20;
  const auto closed_port = [this]() {
    const network::TCPServer gone(GetNode().GetEventLoop(), /* port = */ 0,
                                  [](const trellis::core::error_code&, network::TCP) {});
    return gone.GetPort();
  }();
  // Declared before the client: ~Client() runs the callbacks that reference these.
  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> other_count{0};
  StartRunnerThread();

  auto client = GetNode().CreateServiceClient<TestService>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(std::make_shared<TestServiceHandler>());
  WaitForDiscovery();
  const auto stale = GetNode().GetDiscovery()->RegisterServiceServer(
      std::string{TestService::descriptor()->full_name()}, closed_port, MethodsMap{});

  // One call at a time, spread over several heartbeats so the stale registration arrives between them.
  for (int i = 0; i < kCalls; ++i) {
    const auto answered = success_count.load() + other_count.load();
    test::Test request;
    request.set_id(i);
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", request,
        [&success_count, &other_count](ServiceCallStatus status, const test::TestTwo*) {
          ++(status == kSuccess ? success_count : other_count);
        },
        /* timeout_ms = */ 1000);
    ASSERT_TRUE(WaitUntil([&]() { return success_count.load() + other_count.load() > answered; }))
        << "call " << i << " was never answered";
    std::this_thread::sleep_for(std::chrono::milliseconds{kTestDiscoveryInterval / 2});
  }
  StopAndJoinRunnerThread();  // the callbacks reference locals of this function

  EXPECT_EQ(success_count.load(), static_cast<unsigned>(kCalls));
  EXPECT_EQ(other_count.load(), 0u) << "a stale registration cost the client its live connection";
  GetNode().GetDiscovery()->Unregister(stale);
}

// The old server goes away while its registration lingers, as a crashed server's does until its sample times out, and
// the peer's reset leaves the client's socket with no remote port. The new server's registration must replace that
// socket at once; it was ignored until the old sample went, here never.
TEST_F(TrellisFixture, NewRegistrationReplacesAConnectionThePeerReset) {
  // Declared before the client: ~Client() runs the callbacks that reference it.
  std::atomic<unsigned> successes{0};
  std::atomic<unsigned> answered{0};
  std::vector<network::TCP> accepted;
  StartRunnerThread();

  auto old_server = std::make_unique<network::TCPServer>(
      GetNode().GetEventLoop(), /* port = */ 0, [&accepted](const trellis::core::error_code& ec, network::TCP socket) {
        if (!ec) {
          accepted.push_back(std::move(socket));
        }
      });
  const auto stale = GetNode().GetDiscovery()->RegisterServiceServer(
      std::string{TestService::descriptor()->full_name()}, old_server->GetPort(), MethodsMap{});
  auto client = GetNode().CreateServiceClient<TestService>();
  ASSERT_TRUE(WaitUntil([&]() { return RunOnLoop([&]() { return !accepted.empty(); }); }));

  RunOnLoop([&]() {
    accepted.clear();  // closes the client's connection from the server's end
    old_server.reset();
  });
  const auto call = [&]() {
    const auto before = answered.load();
    client->CallAsync<test::Test, test::TestTwo>(
        "DoStuff", test::Test{},
        [&](ServiceCallStatus status, const test::TestTwo*) {
          successes += status == kSuccess ? 1 : 0;
          ++answered;
        },
        /* timeout_ms = */ 1000);
    return WaitUntil([&]() { return answered.load() > before; });
  };
  // The first call reads the server's close; its send draws the reset the second one fails on.
  ASSERT_TRUE(call());
  ASSERT_TRUE(call());
  ASSERT_EQ(successes.load(), 0u);

  auto server = GetNode().CreateServiceServer<TestServiceHandler>(std::make_shared<TestServiceHandler>());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{10 * kTestDiscoveryInterval};
  while (successes.load() == 0 && std::chrono::steady_clock::now() < deadline) {
    ASSERT_TRUE(call());
    std::this_thread::sleep_for(std::chrono::milliseconds{kTestDiscoveryInterval / 4});
  }
  StopAndJoinRunnerThread();  // the callbacks reference locals of this function
  EXPECT_GT(successes.load(), 0u) << "the client kept the dead connection while the old registration lingered";
  GetNode().GetDiscovery()->Unregister(stale);
}

// A reply still owed when the event loop stops is lost, because the send is posted to the loop and a stopped loop
// never runs it. What keeps that from stranding the caller is ~Server() closing the caller's socket, which completes
// the receive the caller is sitting on with an error. Shutdown therefore has to destroy the Server, not just stop the
// loop -- a caller left with an open socket gets neither a reply nor an error. This pins the close, since it is the
// only thing standing between a dropped reply and a caller that waits forever.
TEST_F(TrellisFixture, ClosesCallerSocketsWhenAReplyIsLostToAStoppedLoop) {
  StartRunnerThread();

  std::atomic<uint16_t> port{0};
  const auto handle = GetNode().GetDiscovery()->AsyncReceiveServices(
      [&port](discovery::Discovery::EventType event, const discovery::Sample& sample) {
        if (sample.service().sname() == TestService::descriptor()->full_name() &&
            event == discovery::Discovery::EventType::kNewRegistration) {
          port = static_cast<uint16_t>(sample.service().tcp_port());
        }
      });
  auto handler = std::make_shared<SlowSignallingHandler>();
  auto server = GetNode().CreateServiceServer<SlowSignallingHandler>(handler);
  WaitForDiscovery();
  ASSERT_NE(port.load(), 0) << "never discovered the server's port";

  // A loop of its own, so stopping the node's loop leaves this socket alone. Stands in for a caller in another
  // process that is still running.
  trellis::core::EventLoop caller_loop;
  network::TCP caller{caller_loop, "127.0.0.1", port.load()};

  // Framed by hand rather than through Client, which shares the node's loop and so would stop along with it.
  test::Test payload;
  payload.set_id(2000);
  payload.set_msg("in flight");
  discovery::Request request;
  request.mutable_header()->set_mname("DoStuff");
  payload.SerializeToString(request.mutable_request());
  std::string wire;
  request.SerializeToString(&wire);
  const uint32_t size = wire.size();
  caller.Send(&size, sizeof(size));
  caller.Send(wire.data(), wire.size());

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!handler->entered.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  ASSERT_TRUE(handler->entered.load()) << "the request never reached a handler, so no reply was ever owed";

  StopAndJoinRunnerThread();  // the reply's send is now posted to a loop that will never run again
  server.reset();             // ~Server() is what has to release the caller

  trellis::core::error_code result;
  bool completed = false;
  uint8_t response_header[sizeof(uint32_t)];
  caller.AsyncReceiveAll(response_header, sizeof(response_header),
                         [&completed, &result](const trellis::core::error_code& ec, size_t) {
                           result = ec;
                           completed = true;
                         });
  caller_loop.RunFor(std::chrono::seconds{2});

  EXPECT_TRUE(completed) << "the caller was left waiting with neither a reply nor an error";
  EXPECT_TRUE(static_cast<bool>(result)) << "expected the closed socket to fail the receive";

  GetNode().GetDiscovery()->StopReceive(handle);
}

// The hand-off list and its mutex exist so that several threads can call at once, and nothing covered that: every
// other test in this file calls from one thread. Each call carries its own id and checks the reply against it, so a
// queue damaged or crossed by concurrent pushes shows up as a wrong answer rather than only as a missing one.
TEST_F(TrellisFixture, ConcurrentCallsFromSeveralThreads) {
  static constexpr unsigned kThreads = 4;
  static constexpr unsigned kCallsPerThread = 5;
  static constexpr unsigned kExpected = kThreads * kCallsPerThread;
  // Clear of the ids TestServiceHandler treats specially: 100 asks for a 4MB reply, 2000 sleeps.
  static constexpr int kIdBase = 10000;

  // Declared before the client: ~Client() runs the callbacks that reference these.
  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> mismatch_count{0};
  std::atomic<unsigned> other_count{0};

  StartRunnerThread();
  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);
  WaitForDiscovery();

  std::vector<std::thread> callers;
  for (unsigned t = 0; t < kThreads; ++t) {
    callers.emplace_back([&, t]() {
      for (unsigned i = 0; i < kCallsPerThread; ++i) {
        const int id = kIdBase + static_cast<int>(t * kCallsPerThread + i);
        test::Test request;
        request.set_id(id);
        request.set_msg("concurrent");
        client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                                     [&, id](ServiceCallStatus status, const test::TestTwo* resp) {
                                                       if (status != kSuccess) {
                                                         ++other_count;
                                                       } else if (static_cast<int>(resp->foo()) != id) {
                                                         ++mismatch_count;
                                                       } else {
                                                         ++success_count;
                                                       }
                                                     });
      }
    });
  }
  for (auto& caller : callers) {
    caller.join();
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (success_count.load() + mismatch_count.load() + other_count.load() < kExpected &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  StopAndJoinRunnerThread();  // the callbacks reference locals of this function

  EXPECT_EQ(success_count.load(), kExpected) << "every call should get exactly one successful callback";
  EXPECT_EQ(mismatch_count.load(), 0u) << "a reply was matched to the wrong request";
  EXPECT_EQ(other_count.load(), 0u);
}

// An app that drives the loop in steps with RunN() or RunUntilIdle(), rather than handing a thread to Run(), makes its
// calls between steps, while nothing is running the loop. Pins that such a call waits for the next step and is then
// sent and answered.
TEST_F(TrellisFixture, CallsMadeBetweenLoopStepsStillGoOut) {
  // Declared before the client: ~Client() runs the callback that references these.
  std::atomic<unsigned> success_count{0};
  std::atomic<unsigned> other_count{0};

  auto client = GetNode().CreateServiceClient<trellis::core::test::TestService>();
  auto handler = std::make_shared<TestServiceHandler>();
  auto server = GetNode().CreateServiceServer<TestServiceHandler>(handler);

  // Step the loop through discovery so the client has a connection before the call is made. Nothing here can observe
  // the connection directly, so step for several discovery periods rather than waiting on it.
  const auto connected_by = std::chrono::steady_clock::now() + std::chrono::milliseconds{5 * kTestDiscoveryTimeout};
  while (std::chrono::steady_clock::now() < connected_by) {
    GetNode().RunUntilIdle();
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  test::Test request;
  request.set_id(7);
  request.set_msg("stepped");
  client->CallAsync<test::Test, test::TestTwo>("DoStuff", request,
                                               [&](ServiceCallStatus status, const test::TestTwo* resp) {
                                                 if (status == kSuccess && resp->bar() == "Echo: stepped") {
                                                   ++success_count;
                                                 } else {
                                                   ++other_count;
                                                 }
                                               });
  EXPECT_EQ(success_count.load(), 0u) << "a call made between steps should wait for the next one";

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (success_count.load() + other_count.load() == 0 && std::chrono::steady_clock::now() < deadline) {
    GetNode().RunUntilIdle();
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  EXPECT_EQ(success_count.load(), 1u) << "stepping the loop should have sent the call and delivered its reply";
  EXPECT_EQ(other_count.load(), 0u);
}

}  // namespace trellis::core::ipc::proto::rpc
