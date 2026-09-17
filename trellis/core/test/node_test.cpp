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
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include "trellis/core/config.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"
#include "trellis/core/test/test_paths.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

constexpr auto kBaseFilename = "test_base_config.yml";

TEST_F(TrellisFixture, StartAndStopNode) {
  // Simply start the runner thread and then test that it will gracefully
  // stop without hanging
  StartRunnerThread();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  Stop();
}

TEST(TrellisNode, Name) {
  Config config(DataPath(kBaseFilename));
  Node node("name", config);
  ASSERT_EQ("name", node.GetName());
}

namespace {

// Loopback discovery with a short management interval, so the tests below see a registration within a few RunFor
// calls instead of waiting on the one-second default and a real UDP socket.
Config SharedContextConfig() {
  return Config(YAML::Load(R"(
    trellis:
      discovery:
        interval: 20
        sample_timeout: 2000
        loopback_enabled: true
    )"));
}

Node::SharedContext ContextOf(const Node& host) {
  return Node::SharedContext{host.GetEventLoop(), host.GetDiscovery()};
}

// Whether the loop can still do work, which is what a stray Stop() takes away: asio leaves a stopped io_context
// stopped until restart(), so handlers posted after that never run. EventLoop::Stopped() cannot answer this, since
// it also reports true for a loop that has simply not been run yet.
bool CanStillRunWork(Node& host) {
  bool ran = false;
  asio::post(*host.GetEventLoop(), [&ran]() { ran = true; });
  host.RunFor(std::chrono::milliseconds(20));
  return ran;
}

// Run the host's loop until `predicate` holds or the budget runs out, so tests assert on the outcome rather than on
// how long it took.
template <typename Predicate>
void RunUntil(Node& host, Predicate predicate) {
  for (int i = 0; i < 100 && !predicate(); ++i) {
    host.RunFor(std::chrono::milliseconds(20));
  }
}

std::set<std::string> ProcessNames(const Node& node) {
  std::set<std::string> names;
  for (const auto& sample : node.GetDiscovery()->GetProcessSamples()) {
    names.insert(sample.process().uname());
  }
  return names;
}

}  // namespace

TEST(TrellisNodeSharedContext, RunsOnTheHostsLoopAndDiscovery) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  EXPECT_EQ(guest.GetDiscovery(), host.GetDiscovery());
  EXPECT_EQ(&*guest.GetEventLoop(), &*host.GetEventLoop());
}

TEST(TrellisNodeSharedContext, DestroyingAGuestLeavesTheHostRunnable) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  { Node guest("guest", config, std::nullopt, ContextOf(host)); }

  // ~Node calls Stop(), and on the shared loop that would have ended the host along with the guest
  EXPECT_FALSE((*host.GetEventLoop()).stopped());
  EXPECT_TRUE(CanStillRunWork(host));
}

TEST(TrellisNodeSharedContext, GuestStopDoesNotStopTheHost) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  guest.Stop();

  EXPECT_FALSE((*host.GetEventLoop()).stopped());
  EXPECT_TRUE(CanStillRunWork(host));
}

TEST(TrellisNodeSharedContext, SchedLatencyStatsStayWithTheirOwner) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  unsigned host_fires = 0;
  auto host_timer = host.CreatePeriodicTimer(1, [&host_fires](const time::TimePoint&) { ++host_fires; });

  // Collect from the loop thread, the way each node's own metrics timer does
  unsigned guest_samples = 0;
  unsigned host_samples = 0;
  auto probe = guest.CreateOneShotTimer(30, [&](const time::TimePoint&) {
    guest_samples = guest.GetAndResetTimerSchedLatencyStats().count;
    host_samples = host.GetAndResetTimerSchedLatencyStats().count;
  });
  host.RunFor(std::chrono::milliseconds(60));

  ASSERT_GT(host_fires, 0u);
  // The guest neither reports the host's samples nor drains them out from under it
  EXPECT_EQ(guest_samples, 0u);
  EXPECT_GT(host_samples, 0u);
}

TEST(TrellisNodeSharedContext, OverrunsAreAttributedToTheirOwner) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  // A 10ms interval with a 25ms callback guarantees overruns
  auto host_timer = host.CreatePeriodicTimer(
      10, [](const time::TimePoint&) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); });
  host.RunFor(std::chrono::milliseconds(100));

  EXPECT_GT(host.GetTimerOverrunCount(), 0u);
  EXPECT_EQ(guest.GetTimerOverrunCount(), 0u);
}

TEST(TrellisNodeSharedContext, GuestAnnouncesItselfInDiscovery) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  RunUntil(host, [&host]() { return ProcessNames(host).contains("guest"); });

  const auto names = ProcessNames(host);
  EXPECT_TRUE(names.contains("host")) << "host is missing from discovery";
  EXPECT_TRUE(names.contains("guest")) << "a hosted node is invisible to `trellis node list`";
}

TEST(TrellisNodeSharedContext, AddSignalHandlerIsRefused) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  bool called = false;
  guest.AddSignalHandler([&called](int) { called = true; });

  // The handler is dropped rather than stored on a node that waits on no signal and would never call it
  EXPECT_TRUE(CanStillRunWork(host));
  EXPECT_FALSE(called);
}

TEST(TrellisNodeSharedContext, RunIsRefused) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  // Returns a failure code instead of blocking on a loop the guest's own Stop() cannot end
  EXPECT_NE(0, guest.Run());
  EXPECT_TRUE(CanStillRunWork(host));
}

TEST(TrellisNodeSharedContext, RejectsAnIncompleteContext) {
  const auto config = SharedContextConfig();
  Node host("host", config);

  EXPECT_THROW(
      { Node bad("bad", config, std::nullopt, Node::SharedContext{host.GetEventLoop(), nullptr}); },
      std::invalid_argument);
  // A default constructed loop carries no timer registry, so its timers would go untracked and a clock step would
  // skip them
  EXPECT_THROW(
      { Node bad("bad", config, std::nullopt, Node::SharedContext{EventLoop{}, host.GetDiscovery()}); },
      std::invalid_argument);
}

TEST(TrellisNodeSharedContext, StillValidatesItsOwnTimerConfig) {
  const auto config = SharedContextConfig();
  Node host("host", config);

  YAML::Node root = YAML::Clone(config.Root());
  root["trellis"]["timers"]["rearm_policy"] = "not_a_policy";
  const Config bad_config(root);

  // The host's policy is the one that applies, but a policy name that does not exist is a mistake, and it is worth
  // reporting where that mistake was made
  EXPECT_THROW({ Node guest("guest", bad_config, std::nullopt, ContextOf(host)); }, std::invalid_argument);
}

TEST(TrellisNodeSharedContext, RefusesToFlipTheProcessClockUnderTheHost) {
  const auto config = SharedContextConfig();
  Node host("host", config);  // clock disabled, so the host's timers are already asio-driven
  ASSERT_FALSE(time::IsSimulatedClockEnabled());

  YAML::Node root = YAML::Clone(config.Root());
  root["trellis"]["simulated_clock"]["enabled"] = true;
  const Config sim_config(root);

  EXPECT_THROW({ Node guest("guest", sim_config, std::nullopt, ContextOf(host)); }, std::runtime_error);
  EXPECT_FALSE(time::IsSimulatedClockEnabled());
}

TEST(TrellisNodeSharedContext, GuestPublisherReachesAHostSubscriber) {
  const auto config = SharedContextConfig();
  Node host("host", config);
  Node guest("guest", config, std::nullopt, ContextOf(host));

  unsigned received = 0;
  auto sub = host.CreateSubscriber<test::Test>("shared_context_topic",
                                               [&received](const time::TimePoint&, const time::TimePoint&,
                                                           SubscriberImpl<test::Test>::MsgTypePtr) { ++received; });
  auto pub = guest.CreatePublisher<test::Test>("shared_context_topic");

  test::Test msg;
  msg.set_id(1);
  msg.set_msg("hello");
  RunUntil(host, [&]() {
    pub->Send(msg);
    return received > 0;
  });
  EXPECT_GT(received, 0u);
}

// RunUntilIdle is the conductor's quiescence step: it must drain a whole cascade of posted work, however deep, not
// stop at a fixed count the way RunN does.
TEST(TrellisNode, RunUntilIdleDrainsAFullCascade) {
  Config config(DataPath(kBaseFilename));
  Node node("node", config);

  unsigned ran{0};
  // Each handler posts the next, forming a chain deeper than RunN's old fixed 10000-handler cap in replay_apps.
  static constexpr unsigned kDepth{15000};
  std::function<void()> step = [&]() {
    ++ran;
    if (ran < kDepth) {
      asio::post(*node.GetEventLoop(), step);
    }
  };
  asio::post(*node.GetEventLoop(), step);

  EXPECT_TRUE(node.RunUntilIdle());
  EXPECT_EQ(ran, kDepth);
  // The loop is now idle, so a second drain runs nothing more.
  EXPECT_TRUE(node.RunUntilIdle());
  EXPECT_EQ(ran, kDepth);
}

TEST(TrellisNode, RunUntilIdleStopsAtTheSafetyCap) {
  Config config(DataPath(kBaseFilename));
  Node node("node", config);

  unsigned ran{0};
  // A handler that reposts itself unconditionally never lets the loop go idle; the cap must stop it rather than spin.
  std::function<void()> forever = [&]() {
    ++ran;
    asio::post(*node.GetEventLoop(), forever);
  };
  asio::post(*node.GetEventLoop(), forever);

  static constexpr unsigned kCap{100};
  EXPECT_TRUE(node.RunUntilIdle(kCap));
  EXPECT_EQ(ran, kCap);
}
