/*
 * Copyright (C) 2026 Agtonomy
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
#include <memory>
#include <stdexcept>
#include <thread>

#include "trellis/core/config.hpp"
#include "trellis/core/node.hpp"
#include "trellis/core/timer.hpp"
#include "trellis/core/timer_options.hpp"
#include "trellis/core/timer_options_config.hpp"

using trellis::core::Config;
using trellis::core::EventLoop;
using trellis::core::PeriodicTimerImpl;
using trellis::core::RearmPolicy;
using trellis::core::TimerKind;
using trellis::core::TimerOptions;
using trellis::core::TimerOptionsFromConfig;
using trellis::core::time::TimePoint;

namespace {

constexpr unsigned kIntervalMs{10};

// How many intervals the clock is advanced past an expiry to represent a timer having fallen behind
constexpr unsigned kSlotsBehind{10};

// Where these tests put simulated time once it starts moving. Replay drives the clock from message timestamps,
// which are nanoseconds since the Unix epoch, so simulated time there is vastly larger than the steady clock's
// uptime. Starting near the steady epoch instead would leave the two close together and pointing the wrong way:
// a subtraction that mixed them up would come out negative and be clamped to nothing rather than blowing up.
constexpr auto kSimulatedStartTime = std::chrono::hours(24 * 365 * 56);

/// Advance the simulated clock to `target` and let the node step whatever came due
void StepClockTo(trellis::core::Node& node, const TimePoint& target) {
  node.UpdateSimulatedClock(target);
  node.RunOnce();
}

/// A node whose loop carries the given policy, with its timers anchored ready to be stepped
class SimulatedNode {
 public:
  explicit SimulatedNode(RearmPolicy policy)
      : node_{"timer_rearm_test", trellis::core::Config{YAML::Load(ConfigFor(policy))}} {
    // The first advance re-anchors rather than fires, so get that out of the way and record where we landed.
    // It has to be the first, which is why the fixture leaves the clock at zero for this to move off.
    now_ = TimePoint{kSimulatedStartTime};
    StepClockTo(node_, now_);
  }

  trellis::core::Node& node() { return node_; }

  /// Advance the clock by `ms` and step the node
  void Advance(unsigned ms) {
    now_ += std::chrono::milliseconds(ms);
    StepClockTo(node_, now_);
  }

 private:
  static std::string ConfigFor(RearmPolicy policy) {
    return std::string{"trellis:\n  timers:\n    rearm_policy: "} +
           (policy == RearmPolicy::kSkipAligned ? "skip_aligned" : "catch_up") + "\n";
  }

  trellis::core::Node node_;
  TimePoint now_;
};

}  // namespace

// The simulated clock is process-wide state, so which fixture a test uses is part of what it asserts. Owning
// the enable and disable here rather than in each test body means no test can leave the clock on for whatever
// runs next, and a test that reads the clock wrongly fails rather than quietly measuring something else.
class TrellisTimerRearmSimulated : public ::testing::Test {
 protected:
  void SetUp() override {
    trellis::core::time::EnableSimulatedClock();
    trellis::core::time::SetSimulatedTime(TimePoint{});
  }

  void TearDown() override { trellis::core::time::DisableSimulatedClock(); }
};

/// For the tests asio drives, which need the simulated clock off or their timers are never created at all
class TrellisTimerRearmWallClock : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_FALSE(trellis::core::time::IsSimulatedClockEnabled()); }
};

// =============================================================================
// Rearm behavior, stepped by the simulated clock so the numbers are exact
// =============================================================================

TEST_F(TrellisTimerRearmSimulated, CatchUpReplaysEverySlotTheClockPassedOver) {
  SimulatedNode sim{RearmPolicy::kCatchUp};

  unsigned invocations{0};
  auto timer = sim.node().CreateTimer(kIntervalMs, [&invocations](const TimePoint&) { ++invocations; });
  const auto expiry_before = timer->GetExpiry();

  sim.Advance(kIntervalMs * kSlotsBehind);

  // Every slot the clock passed over is replayed, one callback each
  ASSERT_EQ(invocations, kSlotsBehind);
  ASSERT_EQ(timer->GetExpiry(), expiry_before + std::chrono::milliseconds(kIntervalMs * kSlotsBehind));
  // The same figure kSkipAligned reports below for the same advance. What the counter measures cannot depend
  // on the policy, or it cannot be compared across two apps that chose differently.
  ASSERT_EQ(timer->GetOverrunCount(), kSlotsBehind - 1);
}

TEST_F(TrellisTimerRearmSimulated, SkipAlignedFiresOnceAndDropsTheRest) {
  SimulatedNode sim{RearmPolicy::kSkipAligned};

  unsigned invocations{0};
  auto timer = sim.node().CreateTimer(kIntervalMs, [&invocations](const TimePoint&) { ++invocations; });
  const auto expiry_before = timer->GetExpiry();

  sim.Advance(kIntervalMs * kSlotsBehind);

  ASSERT_EQ(invocations, 1U);
  // Landed on the first slot past the advance, still a whole number of intervals from where it started
  ASSERT_EQ(timer->GetExpiry(), expiry_before + std::chrono::milliseconds(kIntervalMs * kSlotsBehind));
  // The dropped slots are still counted as missed, so the metric matches what kCatchUp would have reported
  ASSERT_EQ(timer->GetOverrunCount(), kSlotsBehind - 1);
}

TEST_F(TrellisTimerRearmSimulated, SkipAlignedKeepsItsPhaseAcrossRepeatedJumps) {
  SimulatedNode sim{RearmPolicy::kSkipAligned};

  auto timer = sim.node().CreateTimer(kIntervalMs, [](const TimePoint&) {});
  const auto expiry_before = timer->GetExpiry();

  // Jumps that are not multiples of the interval, so a rearm anchored on now rather than on the grid would
  // drift off phase
  for (const unsigned jump : {37u, 4u, 91u, 13u}) {
    sim.Advance(jump);
  }

  const auto offset = timer->GetExpiry() - expiry_before;
  ASSERT_GT(offset.count(), 0);
  ASSERT_EQ(offset % std::chrono::milliseconds(kIntervalMs), std::chrono::nanoseconds::zero());
}

TEST_F(TrellisTimerRearmSimulated, SkipAlignedCrossesAHugeJumpInOneRearm) {
  SimulatedNode sim{RearmPolicy::kSkipAligned};

  unsigned invocations{0};
  auto timer = sim.node().CreateTimer(1u, [&invocations](const TimePoint&) { ++invocations; });

  // A hundred thousand slots. Walking forward one at a time would take as many iterations; the closed form
  // takes one, and this test would not finish in reasonable time without it.
  sim.Advance(100000u);

  ASSERT_EQ(invocations, 1U);
  ASSERT_EQ(timer->GetOverrunCount(), 99999U);
}

TEST_F(TrellisTimerRearmSimulated, SimulatedOneShotWaitsItsDelayWhenBuiltAfterTheClockMoved) {
  SimulatedNode sim{RearmPolicy::kCatchUp};

  // Built after the clock has already moved, which is what any timer created in response to something
  // arriving does rather than at startup
  constexpr unsigned kDelayMs{50};
  unsigned invocations{0};
  auto timer = sim.node().CreateOneShotTimer(kDelayMs, [&invocations](const TimePoint&) { ++invocations; });

  sim.Advance(kDelayMs - 10);
  ASSERT_EQ(invocations, 0U);

  sim.Advance(20u);
  ASSERT_EQ(invocations, 1U);
}

TEST_F(TrellisTimerRearmSimulated, SimulatedPeriodicTimerStoppedFromItsOwnCallbackFiresOnce) {
  SimulatedNode sim{RearmPolicy::kCatchUp};

  unsigned invocations{0};
  trellis::core::Timer timer;
  timer = sim.node().CreateTimer(kIntervalMs, [&timer, &invocations](const TimePoint&) {
    ++invocations;
    timer->Stop();
    // Stopping leaves the expiry where it is, because the rearm is skipped for a cancelled timer. If the walk
    // does not notice, it re-queues this timer against that same expiry forever -- bounded here so that shows
    // up as a failure rather than as a test that never returns.
    if (invocations > 5) {
      throw std::runtime_error("simulated clock kept firing a stopped timer");
    }
  });

  sim.Advance(kIntervalMs * kSlotsBehind);

  ASSERT_EQ(invocations, 1U);
}

// A management timer keeps a real asio timer even under a simulated clock, so its expiry is a steady clock
// reading while time::Now() reports simulated time -- and by then the fixture has put simulated time an epoch
// apart from the steady clock. Anything offsetting from this timer's expiry has to notice which of the two it
// is holding.
namespace {

/// A management timer on the node's loop, built the way trellis's own housekeeping timers are
trellis::core::PeriodicTimer CreateManagementTimer(trellis::core::Node& node, RearmPolicy policy) {
  return std::make_shared<PeriodicTimerImpl>(
      node.GetEventLoop(), kIntervalMs, [](const TimePoint&) {}, 0u, TimerKind::kManagement, policy);
}

}  // namespace

TEST_F(TrellisTimerRearmSimulated, ManagementTimerRearmsOnItsOwnClockUnderASimulatedClock) {
  SimulatedNode sim{RearmPolicy::kSkipAligned};

  auto timer = CreateManagementTimer(sim.node(), RearmPolicy::kSkipAligned);
  ASSERT_FALSE(timer->IsSimulationDriven());
  const auto expiry_before = timer->GetExpiry();

  timer->Fire();

  // Measuring the gap to simulated time would read as decades of missed slots and rearm this timer out of reach
  const auto advance = timer->GetExpiry() - expiry_before;
  ASSERT_GT(advance.count(), 0);
  ASSERT_LE(advance, std::chrono::milliseconds(kIntervalMs * 2));
}

TEST_F(TrellisTimerRearmSimulated, ManagementTimerRestartsOnItsOwnClockUnderASimulatedClock) {
  SimulatedNode sim{RearmPolicy::kCatchUp};

  auto timer = CreateManagementTimer(sim.node(), RearmPolicy::kCatchUp);

  timer->Reset();

  // Restarting has to land one interval from the clock this timer runs on, not from simulated time, or the
  // deadline handed to asio belongs to an epoch it knows nothing about
  const auto remaining = timer->GetExpiry() - std::chrono::steady_clock::now();
  ASSERT_GT(remaining.count(), 0);
  ASSERT_LE(remaining, std::chrono::milliseconds(kIntervalMs * 2));
}

// One node per test rather than two in one body: a SimulatedNode starts the clock from where the fixture left
// it, and the second would be asking it to go backwards
TEST_F(TrellisTimerRearmSimulated, PerTimerSkipAlignedOverridesACatchUpLoop) {
  SimulatedNode sim{RearmPolicy::kCatchUp};

  unsigned invocations{0};
  auto timer = sim.node().CreateTimer(
      kIntervalMs, [&invocations](const TimePoint&) { ++invocations; }, 0u, TimerKind::kApplication,
      RearmPolicy::kSkipAligned);

  sim.Advance(kIntervalMs * kSlotsBehind);

  ASSERT_EQ(invocations, 1U);
}

TEST_F(TrellisTimerRearmSimulated, PerTimerCatchUpOverridesASkipAlignedLoop) {
  SimulatedNode sim{RearmPolicy::kSkipAligned};

  unsigned invocations{0};
  auto timer = sim.node().CreateTimer(
      kIntervalMs, [&invocations](const TimePoint&) { ++invocations; }, 0u, TimerKind::kApplication,
      RearmPolicy::kCatchUp);

  sim.Advance(kIntervalMs * kSlotsBehind);

  ASSERT_EQ(invocations, kSlotsBehind);
}

TEST_F(TrellisTimerRearmWallClock, ZeroIntervalIsRejected) {
  EventLoop loop{nullptr, TimerOptions{.rearm_policy = RearmPolicy::kSkipAligned}};

  // A periodic timer with no period has no expiry to advance to. Under a real clock that spins on a
  // deadline already in the past; under a simulated clock it wedges the walk that steps timers forward,
  // because the expiry it is re-queued against never moves. Neither is worth supporting.
  ASSERT_THROW(std::make_shared<PeriodicTimerImpl>(loop, 0u, [](const TimePoint&) {}), std::invalid_argument);
}

// =============================================================================
// The same behavior under a real clock, which is the part simulation cannot prove
// =============================================================================

/// A callback that stalls the loop once, on its first invocation, then returns immediately
class StallOnce {
 public:
  explicit StallOnce(std::chrono::milliseconds stall) : stall_{stall} {}

  void operator()(const TimePoint&) {
    ++invocations;
    if (!stalled_) {
      stalled_ = true;
      std::this_thread::sleep_for(stall_);
    }
  }

  unsigned invocations{0};

 private:
  std::chrono::milliseconds stall_;
  bool stalled_{false};
};

// These two time a real sleep, so they assert direction rather than exact counts. A stall of 100ms on a
// 10ms timer is ten slots; the window is generous enough that an overshooting sleep cannot end the run
// before the rearm happens, and the bounds sit clear of both the honest and the broken value.

// Deliberately not a policy comparison. Separating the two on a real clock needs a window short enough that
// the replayed slots still dominate the count, and a sleep that overshoots by more than that window would then
// fail the test on a loaded host rather than a broken one. What this covers is that an asio dispatch reaches
// the shared rearm at all; the exact per-policy counts belong to the simulated tests above, which get them
// without depending on when the host chose to run anything.
TEST_F(TrellisTimerRearmWallClock, AsioDispatchReachesTheRearmAndKeepsFiring) {
  EventLoop loop{nullptr, TimerOptions{.rearm_policy = RearmPolicy::kCatchUp}};
  StallOnce callback{std::chrono::milliseconds(100)};
  auto timer = std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, std::ref(callback));

  loop.RunFor(std::chrono::milliseconds(400));

  ASSERT_GE(callback.invocations, 9U);
}

TEST_F(TrellisTimerRearmWallClock, AsioDispatchSkipsMissedSlotsUnderSkipAligned) {
  EventLoop loop{nullptr, TimerOptions{.rearm_policy = RearmPolicy::kSkipAligned}};
  StallOnce callback{std::chrono::milliseconds(100)};
  auto timer = std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, std::ref(callback));

  // Kept just past the stall: widening this window would add on-time fires and shrink the gap from kCatchUp
  loop.RunFor(std::chrono::milliseconds(100 + kIntervalMs * 2));

  ASSERT_LE(callback.invocations, 6U);
  ASSERT_GE(timer->GetOverrunCount(), 9U);
}

// =============================================================================
// Reset() re-anchors on now, under either policy
// =============================================================================

class TimerResetTest : public TrellisTimerRearmSimulated, public ::testing::WithParamInterface<RearmPolicy> {};

TEST_P(TimerResetTest, ResetAfterALongIdleFiresOnceRatherThanPerMissedInterval) {
  SimulatedNode sim{GetParam()};

  unsigned invocations{0};
  auto timer = sim.node().CreateTimer(
      kIntervalMs, [&invocations](const TimePoint&) { ++invocations; }, 0u, TimerKind::kApplication, GetParam());

  // Stop the timer and let a long stretch of time pass, which leaves its expiry far behind
  timer->Stop();
  sim.Advance(kIntervalMs * 100);
  invocations = 0;

  timer->Reset();
  sim.Advance(kIntervalMs);

  // Anchoring the restart on the stale expiry would replay one callback per interval of idle time
  ASSERT_EQ(invocations, 1U);
}

TEST_P(TimerResetTest, ResetDoesNotPushTheExpiryFurtherIntoTheFuture) {
  SimulatedNode sim{GetParam()};

  auto timer = sim.node().CreateTimer(kIntervalMs, [](const TimePoint&) {}, 0u, TimerKind::kApplication, GetParam());

  // Reset far more often than the interval, the way a watchdog fed by a faster stream of messages does.
  // Adding an interval to the previous expiry each time would ratchet the expiry away from now until the
  // timer stopped firing at all.
  for (unsigned i = 0; i < 20; ++i) {
    timer->Reset();
    sim.Advance(1u);
  }

  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(timer->GetExpiry() - trellis::core::time::Now());
  ASSERT_LE(remaining.count(), static_cast<int64_t>(kIntervalMs));
}

INSTANTIATE_TEST_SUITE_P(BothPolicies, TimerResetTest,
                         ::testing::Values(RearmPolicy::kCatchUp, RearmPolicy::kSkipAligned));

// =============================================================================
// Config parsing
// =============================================================================

TEST(TrellisTimerOptionsConfig, DefaultsToCatchUpWhenTheKeyIsAbsent) {
  const Config config{YAML::Load("trellis:\n  timers: {}\n")};
  ASSERT_EQ(TimerOptionsFromConfig(config).rearm_policy, RearmPolicy::kCatchUp);
}

TEST(TrellisTimerOptionsConfig, DefaultsToCatchUpWhenThereIsNoTrellisSection) {
  const Config config{YAML::Load("something_else: 1\n")};
  ASSERT_EQ(TimerOptionsFromConfig(config).rearm_policy, RearmPolicy::kCatchUp);
}

TEST(TrellisTimerOptionsConfig, ReadsBothPolicyNames) {
  const Config catch_up{YAML::Load("trellis:\n  timers:\n    rearm_policy: catch_up\n")};
  ASSERT_EQ(TimerOptionsFromConfig(catch_up).rearm_policy, RearmPolicy::kCatchUp);

  const Config skip_aligned{YAML::Load("trellis:\n  timers:\n    rearm_policy: skip_aligned\n")};
  ASSERT_EQ(TimerOptionsFromConfig(skip_aligned).rearm_policy, RearmPolicy::kSkipAligned);
}

TEST(TrellisTimerOptionsConfig, PolicyNamesAreCaseInsensitive) {
  const Config config{YAML::Load("trellis:\n  timers:\n    rearm_policy: Skip_Aligned\n")};
  ASSERT_EQ(TimerOptionsFromConfig(config).rearm_policy, RearmPolicy::kSkipAligned);
}

TEST(TrellisTimerOptionsConfig, ThrowsOnAnUnrecognizedPolicy) {
  const Config config{YAML::Load("trellis:\n  timers:\n    rearm_policy: nonsense\n")};

  // A policy name that is not one of the two is a mistake in the configuration, not something to run
  // past with a default the author did not choose
  ASSERT_THROW(TimerOptionsFromConfig(config), std::invalid_argument);
}
