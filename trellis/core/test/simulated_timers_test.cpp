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

#include "trellis/core/simulated_timers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "trellis/core/event_loop.hpp"
#include "trellis/core/timer.hpp"

namespace trellis::core {
namespace {

using std::chrono::milliseconds;
using time::Now;
using time::TimePoint;

class StepSimulatedTimersTest : public testing::Test {
 protected:
  // Away from the epoch, which Node::StepSimulatedClock reads as a clock that has never been set.
  StepSimulatedTimersTest() {
    time::EnableSimulatedClock();
    time::SetSimulatedTime(TimePoint{std::chrono::seconds{1}});
  }

  ~StepSimulatedTimersTest() override { time::DisableSimulatedClock(); }

  void StepBy(milliseconds duration) { StepSimulatedTimers(*registry_, Now() + duration); }

  std::shared_ptr<TimerRegistry> registry_{std::make_shared<TimerRegistry>()};
  EventLoop loop_{registry_};
};

TEST_F(StepSimulatedTimersTest, AOneShotFiresAtItsExpiryAndNotBefore) {
  const auto start = Now();
  std::optional<TimePoint> fired_at;
  const OneShotTimerImpl timer{loop_, [&fired_at](const TimePoint& now) { fired_at = now; }, 100};

  StepBy(milliseconds{99});
  EXPECT_FALSE(fired_at.has_value());

  StepBy(milliseconds{1});
  ASSERT_TRUE(fired_at.has_value());
  EXPECT_EQ(*fired_at, start + milliseconds{100});
}

TEST_F(StepSimulatedTimersTest, ACallbackSeesItsOwnExpiryAndTheClockEndsAtTheTarget) {
  const auto start = Now();
  std::optional<TimePoint> now_in_callback;
  const OneShotTimerImpl timer{loop_, [&now_in_callback](const TimePoint&) { now_in_callback = Now(); }, 30};

  StepBy(milliseconds{500});

  EXPECT_EQ(now_in_callback, start + milliseconds{30});
  EXPECT_EQ(Now(), start + milliseconds{500});
}

TEST_F(StepSimulatedTimersTest, ATimerCreatedByACallbackFiresInTheSameStepWhenDue) {
  const auto start = Now();
  std::unique_ptr<OneShotTimerImpl> armed_by_callback;
  std::optional<TimePoint> second_fired_at;
  const OneShotTimerImpl first{loop_,
                               [&](const TimePoint&) {
                                 armed_by_callback = std::make_unique<OneShotTimerImpl>(
                                     loop_, [&](const TimePoint& now) { second_fired_at = now; }, 100);
                               },
                               120};

  StepBy(milliseconds{280});

  EXPECT_EQ(second_fired_at, start + milliseconds{220});
}

TEST_F(StepSimulatedTimersTest, ATimerResetByACallbackFiresAtItsNewExpiryInTheSameStep) {
  const auto start = Now();
  std::vector<TimePoint> fires;
  OneShotTimerImpl reset_one{loop_, [&fires](const TimePoint& now) { fires.push_back(now); }, 100};
  const OneShotTimerImpl resetter{loop_, [&reset_one](const TimePoint&) { reset_one.Reset(); }, 50};

  StepBy(milliseconds{200});

  EXPECT_EQ(fires, (std::vector<TimePoint>{start + milliseconds{150}}));
}

TEST_F(StepSimulatedTimersTest, APeriodicTimerFiresOncePerIntervalPassed) {
  const auto start = Now();
  std::vector<TimePoint> fires;
  const PeriodicTimerImpl timer{loop_, [&fires](const TimePoint& now) { fires.push_back(now); }, 100};

  const auto fired = StepSimulatedTimers(*registry_, start + milliseconds{350});

  EXPECT_EQ(fires,
            (std::vector<TimePoint>{start + milliseconds{100}, start + milliseconds{200}, start + milliseconds{300}}));
  EXPECT_EQ(fired, 3U);
}

TEST_F(StepSimulatedTimersTest, TimersFireInExpiryOrderAndTiesInCreationOrder) {
  std::vector<std::string> order;
  const OneShotTimerImpl late{loop_, [&order](const TimePoint&) { order.push_back("late"); }, 50};
  const OneShotTimerImpl tie_first{loop_, [&order](const TimePoint&) { order.push_back("tie_first"); }, 20};
  const OneShotTimerImpl tie_second{loop_, [&order](const TimePoint&) { order.push_back("tie_second"); }, 20};

  StepBy(milliseconds{100});

  EXPECT_EQ(order, (std::vector<std::string>{"tie_first", "tie_second", "late"}));
}

TEST_F(StepSimulatedTimersTest, AStoppedTimerDoesNotFire) {
  bool fired{false};
  OneShotTimerImpl timer{loop_, [&fired](const TimePoint&) { fired = true; }, 10};
  timer.Stop();

  StepBy(milliseconds{100});

  EXPECT_FALSE(fired);
}

TEST_F(StepSimulatedTimersTest, AOneShotFiresOnlyOnceAcrossSteps) {
  int fires{0};
  const OneShotTimerImpl timer{loop_, [&fires](const TimePoint&) { ++fires; }, 10};

  StepBy(milliseconds{100});
  StepBy(milliseconds{100});

  EXPECT_EQ(fires, 1);
}

TEST_F(StepSimulatedTimersTest, ThrowsInsteadOfSpinningOnATimerThatKeepsFallingDue) {
  int fires{0};
  std::unique_ptr<OneShotTimerImpl> timer;
  timer = std::make_unique<OneShotTimerImpl>(
      loop_,
      [&](const TimePoint&) {
        ++fires;
        timer->Reset();
      },
      0);

  EXPECT_THROW(StepSimulatedTimers(*registry_, Now() + milliseconds{10}, 100), std::runtime_error);
  EXPECT_EQ(fires, 100);
}

TEST_F(StepSimulatedTimersTest, ACallbackThatStepsPastTheTargetLeavesTheClockThere) {
  const auto start = Now();
  std::optional<TimePoint> later_fired_at;
  const OneShotTimerImpl stepper{
      loop_, [&](const TimePoint&) { StepSimulatedTimers(*registry_, start + milliseconds{500}); }, 100};
  const OneShotTimerImpl later{loop_, [&later_fired_at](const TimePoint& now) { later_fired_at = now; }, 400};

  StepBy(milliseconds{300});

  EXPECT_EQ(later_fired_at, start + milliseconds{400});
  EXPECT_EQ(Now(), start + milliseconds{500}) << "the outer step moved the clock back to its own target";
}

// A periodic timer is mid-fire while its callback steps the clock, so that inner step cannot fire it. It used to rearm
// against the outer step's target and be left due behind the clock, firing late on every later step.
TEST_F(StepSimulatedTimersTest, ASkipAlignedTimerThatStepsTheClockFromItsCallbackRearmsAheadOfIt) {
  const auto start = Now();
  std::vector<TimePoint> fires;
  const PeriodicTimerImpl timer{loop_,
                                [&](const TimePoint& now) {
                                  fires.push_back(now);
                                  if (fires.size() == 1) {
                                    StepSimulatedTimers(*registry_, start + milliseconds{500});
                                  }
                                },
                                100,
                                0,
                                TimerKind::kApplication,
                                RearmPolicy::kSkipAligned};

  StepBy(milliseconds{300});

  EXPECT_EQ(fires, (std::vector<TimePoint>{start + milliseconds{100}}));
  EXPECT_EQ(timer.GetExpiry(), start + milliseconds{600});
  EXPECT_EQ(timer.GetOverrunCount(), 4U);  // the slots at 200 to 500, all passed over while its callback ran
  EXPECT_EQ(Now(), start + milliseconds{500});
}

// The same, under kCatchUp: the passed-over slots are replayed, late, within the step that passed them, and leave the
// timer ahead of the clock.
TEST_F(StepSimulatedTimersTest, ACatchUpTimerThatStepsTheClockFromItsCallbackReplaysItsSlotsInTheSameStep) {
  const auto start = Now();
  std::vector<TimePoint> fires;
  const PeriodicTimerImpl timer{loop_,
                                [&](const TimePoint& now) {
                                  fires.push_back(now);
                                  if (fires.size() == 1) {
                                    StepSimulatedTimers(*registry_, start + milliseconds{500});
                                  }
                                },
                                100,
                                0,
                                TimerKind::kApplication,
                                RearmPolicy::kCatchUp};

  StepBy(milliseconds{300});

  const auto stepped_to = start + milliseconds{500};
  EXPECT_EQ(fires, (std::vector<TimePoint>{start + milliseconds{100}, stepped_to, stepped_to, stepped_to, stepped_to}));
  EXPECT_EQ(timer.GetExpiry(), start + milliseconds{600});
  EXPECT_EQ(timer.GetOverrunCount(), 4U);  // as under kSkipAligned: the policies count the same slots
  EXPECT_EQ(Now(), stepped_to);
}

TEST_F(StepSimulatedTimersTest, ATimerWhoseExpiryTheClockHasPassedFiresLateRatherThanRewindingIt) {
  const auto start = Now();
  std::optional<TimePoint> fired_at;
  const OneShotTimerImpl timer{loop_, [&fired_at](const TimePoint& now) { fired_at = now; }, 200};
  // The clock is process-global, so stepping another registry moves it past this one's timer without firing it
  const auto other_registry = std::make_shared<TimerRegistry>();
  StepSimulatedTimers(*other_registry, start + milliseconds{500});

  StepBy(milliseconds{100});

  EXPECT_EQ(fired_at, start + milliseconds{500});
  EXPECT_EQ(Now(), start + milliseconds{600});
}

TEST_F(StepSimulatedTimersTest, MovingBackwardsThrows) {
  EXPECT_THROW(StepSimulatedTimers(*registry_, Now() - milliseconds{1}), std::invalid_argument);
}

// Another loop's thread can move the process-global clock between a caller reading it and stepping, so on a shared
// clock a target the clock has passed is not an error: what is due by now fires, and the clock stays where it is.
TEST_F(StepSimulatedTimersTest, OnASharedClockATargetTheClockHasPassedFiresWhatIsDueByNowWithoutMovingTheClock) {
  const auto start = Now();
  std::optional<TimePoint> fired_at;
  const OneShotTimerImpl timer{loop_, [&fired_at](const TimePoint& now) { fired_at = now; }, 100};
  time::AdvanceSimulatedTime(start + milliseconds{300});

  EXPECT_NO_THROW(StepSimulatedTimersOnSharedClock(*registry_, start + milliseconds{50}));

  EXPECT_EQ(fired_at, start + milliseconds{300});
  EXPECT_EQ(Now(), start + milliseconds{300});
}

TEST_F(StepSimulatedTimersTest, ThrowsWithoutTheSimulatedClock) {
  time::DisableSimulatedClock();

  EXPECT_THROW(StepSimulatedTimers(*registry_, Now()), std::logic_error);
  EXPECT_THROW(StepSimulatedTimersOnSharedClock(*registry_, Now()), std::logic_error);
}

}  // namespace
}  // namespace trellis::core
