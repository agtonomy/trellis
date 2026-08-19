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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "trellis/core/node.hpp"
#include "trellis/core/sim_clock.pb.h"
#include "trellis/core/sim_controller.hpp"

namespace trellis::core {
namespace {

using namespace std::chrono_literals;

constexpr unsigned kTimerIntervalMs = 100u;

// Loopback discovery keeps the test single-process; sim mode (follower) is on by default.
const char* const kSimConfig = R"(
trellis:
  publisher:
    attributes:
      num_buffers: 200
  discovery:
    interval: 50
    loopback_enabled: true
  simulated_clock:
    enabled: true
)";

time::TimePoint MsToTimePoint(uint64_t ms) { return time::NanosecondsToTimePoint(ms * 1000ULL * 1000ULL); }

class SimClockFixture : public ::testing::Test {
 protected:
  // Construct the node under test. role == nullopt resolves to follower via the config flag; tests pass
  // kPublisher to exercise the simulator role.
  void CreateNode(std::optional<Node::SimClockRole> role = std::nullopt) {
    // Use a short node name: it is embedded in the per-writer UNIX socket path under /tmp/trellis, which
    // is bound by the ~108 byte sun_path limit. The long gtest test name would overflow it.
    node_ = std::make_unique<Node>("sim_clock_test", Config(YAML::Load(kSimConfig)), role);
    // The node constructor has enabled the sim clock; establish a deterministic time base of zero.
    if (time::IsSimulatedClockEnabled()) {
      time::SetSimulatedTime(time::TimePoint{});
    }
  }

  void TearDown() override {
    if (node_) {
      node_->Stop();
    }
    if (runner_.joinable()) {
      runner_.join();
    }
    time::DisableSimulatedClock();
    node_.reset();
  }

  void StartRunner() {
    runner_ = std::thread([this]() { node_->Run(); });
  }

  std::unique_ptr<Node> node_;
  std::thread runner_;
};

// The config flag must enable the process-global simulated clock (follower role), and timers created on the
// node must be sim-driven (fired by UpdateSimulatedClock) just like the direct-driver behavior in
// simtime_test. This also guards the constructor ordering: discovery is constructed before sim is enabled,
// user timers after.
TEST_F(SimClockFixture, ConfigEnablesSimulatedClockAndTimersAreSimDriven) {
  CreateNode();
  ASSERT_TRUE(time::IsSimulatedClockEnabled());

  unsigned ticks{0};
  auto timer = node_->CreateTimer(kTimerIntervalMs, [&ticks](const time::TimePoint&) { ++ticks; });

  // The first advance only resets the timers; the second advances one second -> ten 100 ms ticks.
  auto t = time::Now();
  t += 1000ms;
  node_->UpdateSimulatedClock(t);
  node_->RunOnce();
  EXPECT_EQ(ticks, 0u);

  t += 1000ms;
  node_->UpdateSimulatedClock(t);
  node_->RunOnce();
  EXPECT_EQ(ticks, 10u);
}

// A follower must advance its clock and fire its timers purely from received SimClock messages. This drives
// the receive path with a raw publisher (not BroadcastSimulatedClock, which would also advance the local
// clock directly) so that the only thing moving time is the subscriber trigger reacting to send_time.
TEST_F(SimClockFixture, FollowerAdvancesFromReceivedClock) {
  CreateNode();  // follower (from config)

  std::mutex mtx;
  std::condition_variable cv;
  unsigned ticks{0};
  uint64_t last_now_ms{0};
  auto timer = node_->CreateTimer(kTimerIntervalMs, [&](const time::TimePoint& now) {
    std::lock_guard<std::mutex> lock(mtx);
    ++ticks;
    last_now_ms = time::TimePointToMilliseconds(now);
    cv.notify_all();
  });

  // Stand in for an external simulator. Created before the runner starts so discovery can connect it to the
  // node's SimController subscriber during the wait below. send_time is set to target_time, which is the
  // signal the follower advances to.
  auto clock_pub = node_->CreatePublisher<SimClock>(std::string{SimController::kDefaultClockTopic});
  const auto publish = [&clock_pub](uint64_t epoch, uint64_t target_ms) {
    SimClock msg;
    msg.set_epoch(epoch);
    const auto target = MsToTimePoint(target_ms);
    *msg.mutable_target_time() = time::TimePointToTimestamp(target);
    clock_pub->Send(msg, target);
  };

  StartRunner();
  std::this_thread::sleep_for(300ms);  // let loopback discovery connect publisher <-> subscriber

  publish(1, 1000);  // first advance resets the timers
  publish(2, 2000);  // second advance fires ten 100 ms ticks

  std::unique_lock<std::mutex> lock(mtx);
  const bool fired = cv.wait_for(lock, 2s, [&ticks]() { return ticks >= 10u; });
  EXPECT_TRUE(fired) << "observed timer ticks: " << ticks;
  EXPECT_GE(last_now_ms, 2000u);
}

// A publisher must advance its OWN clock (and fire its own timers) when it broadcasts, so its Now() tracks
// the time it hands out. This is deterministic: BroadcastSimulatedClock defers the local advance to the
// event loop, so we pump the loop with RunN after each broadcast.
TEST_F(SimClockFixture, PublisherBroadcastAdvancesOwnClock) {
  CreateNode(Node::SimClockRole::kPublisher);
  ASSERT_TRUE(time::IsSimulatedClockEnabled());

  unsigned ticks{0};
  auto timer = node_->CreateTimer(kTimerIntervalMs, [&ticks](const time::TimePoint&) { ++ticks; });

  node_->BroadcastSimulatedClock(1, MsToTimePoint(1000));  // first advance resets the timers
  node_->RunN(1000);
  EXPECT_EQ(ticks, 0u);

  node_->BroadcastSimulatedClock(2, MsToTimePoint(2000));  // second advance fires ten 100 ms ticks
  node_->RunN(1000);
  EXPECT_EQ(ticks, 10u);
  EXPECT_GE(time::TimePointToMilliseconds(time::Now()), 2000u);
}

// In sim mode the clock must advance to a message's send_time BEFORE the callback runs, so the callback
// observes now == msgtime (and latency is non-negative) rather than a receive_time stuck in the past. This
// uses an ordinary data topic (not the clock topic) so the advance comes from the generic subscriber path.
TEST_F(SimClockFixture, ReceiveTimeMatchesSendTimeInSimMode) {
  CreateNode();  // follower, sim enabled
  time::SetSimulatedTime(MsToTimePoint(500));

  std::mutex mtx;
  std::condition_variable cv;
  bool received{false};
  uint64_t now_ms{0};
  uint64_t msg_ms{0};
  int64_t latency_us{-1};
  auto sub = node_->CreateSubscriber<SimClock>(
      "sim_clock_test_data",
      [&](const time::TimePoint& now, const time::TimePoint& msgtime, std::unique_ptr<SimClock>) {
        std::lock_guard<std::mutex> lock(mtx);
        now_ms = time::TimePointToMilliseconds(now);
        msg_ms = time::TimePointToMilliseconds(msgtime);
        latency_us = std::chrono::duration_cast<std::chrono::microseconds>(now - msgtime).count();
        received = true;
        cv.notify_all();
      });
  auto pub = node_->CreatePublisher<SimClock>("sim_clock_test_data");

  StartRunner();
  std::this_thread::sleep_for(300ms);  // let loopback discovery connect publisher <-> subscriber

  SimClock msg;
  msg.set_epoch(0);
  pub->Send(msg, MsToTimePoint(1500));  // send_time is in the future relative to the base time of 500 ms

  std::unique_lock<std::mutex> lock(mtx);
  ASSERT_TRUE(cv.wait_for(lock, 2s, [&received]() { return received; }));
  EXPECT_EQ(now_ms, 1500u);   // clock advanced to send_time before delivery
  EXPECT_EQ(now_ms, msg_ms);  // receive_time == send_time
  EXPECT_GE(latency_us, 0);   // no longer negative
}

// Affirms that a timer callback may create a timer (like in the RPC response callback).
TEST_F(SimClockFixture, TimerCallbackCanCreateTimer) {
  CreateNode();

  unsigned ticks{0};
  Timer nested;
  auto timer = node_->CreateTimer(kTimerIntervalMs, [&](const time::TimePoint&) {
    ++ticks;
    if (!nested) {
      nested = node_->CreateOneShotTimer(kTimerIntervalMs, [](const time::TimePoint&) {});
    }
  });

  auto t = time::Now();
  t += 1000ms;
  node_->UpdateSimulatedClock(t);  // first advance only rebases the timers
  node_->RunOnce();

  t += 1000ms;
  node_->UpdateSimulatedClock(t);
  node_->RunOnce();
  EXPECT_EQ(ticks, 10u);
  EXPECT_TRUE(nested);
}

}  // namespace
}  // namespace trellis::core
