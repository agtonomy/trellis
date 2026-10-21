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
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "trellis/core/node.hpp"

TEST(TrellisSimulatedClock, UpdateSimulatedClockTicksTimers) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned timer1_interval{1000u};
  const unsigned timer2_interval{333u};
  const unsigned timer3_interval{220u};
  static constexpr unsigned time_jump_ms{10500};

  unsigned timer1_ticks{0};
  unsigned timer2_ticks{0};
  unsigned timer3_ticks{0};
  trellis::core::time::TimePoint last_now;

  auto timer1 =
      node.CreateTimer(timer1_interval, [&timer1_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer1_ticks;
        ASSERT_EQ(trellis::core::time::TimePointToMilliseconds(now), (timer1_ticks * timer1_interval) + time_jump_ms);
        ASSERT_TRUE(trellis::core::time::TimePointToMilliseconds(now) >
                    trellis::core::time::TimePointToMilliseconds(last_now));
        last_now = now;
      });

  auto timer2 =
      node.CreateTimer(timer2_interval, [&timer2_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer2_ticks;
        ASSERT_EQ(trellis::core::time::TimePointToMilliseconds(now), (timer2_ticks * timer2_interval) + time_jump_ms);
        ASSERT_TRUE(trellis::core::time::TimePointToMilliseconds(now) >
                    trellis::core::time::TimePointToMilliseconds(last_now));
        last_now = now;
      });

  auto timer3 =
      node.CreateTimer(timer3_interval, [&timer3_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer3_ticks;
        ASSERT_EQ(trellis::core::time::TimePointToMilliseconds(now), (timer3_ticks * timer3_interval) + time_jump_ms);
        ASSERT_TRUE(trellis::core::time::TimePointToMilliseconds(now) >
                    trellis::core::time::TimePointToMilliseconds(last_now));
        last_now = now;
      });

  // The first time we update the time, we're essentially resetting all the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(time_jump_ms)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(timer1_ticks, 0);
  ASSERT_EQ(timer2_ticks, 0);
  ASSERT_EQ(timer3_ticks, 0);

  // Now we're moving forward in time, so our timers should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // We jumped forward in time 10500 milliseconds, so...
  // our 1000ms timer should have fired 10 times
  // our 333ms timer should have fired 31 times
  // our 220ms timer should have fired 47 times
  ASSERT_EQ(timer1_ticks, 10);
  ASSERT_EQ(timer2_ticks, 31);
  ASSERT_EQ(timer3_ticks, 47);
}

TEST(TrellisSimulatedClock, UpdateSimulatedClockTicksTimersCreatedDirectlyOnTheLoop) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned timer_interval{1000u};
  static constexpr unsigned time_jump_ms{10500};

  unsigned ticks{0};
  // Registration follows the loop, so a timer built directly against it is stepped like any other
  auto timer = std::make_shared<trellis::core::PeriodicTimerImpl>(
      node.GetEventLoop(), timer_interval, [&ticks](const trellis::core::time::TimePoint&) { ++ticks; });

  // The first time we update the time, we're essentially resetting all the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(time_jump_ms)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(ticks, 0);

  // Now we're moving forward in time, so our timer should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // We jumped forward 10500 milliseconds, so our 1000ms timer should have fired 10 times
  ASSERT_EQ(ticks, 10);
}

TEST(TrellisSimulatedClock, TimersSharingAnExpiryFireInCreationOrder) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned timer_interval{100u};
  static constexpr unsigned kTimerCount{6};

  // Churn some registrations first so the live timers' handles are not the lowest ones. Entries reach the walk in the
  // registry map's iteration order, which tracks the hash of the handle rather than the order of creation, so a run
  // starting from a used counter is what would expose an ordering that depends on the map.
  for (unsigned i = 0; i < 12; ++i) {
    auto churn = node.CreateTimer(timer_interval, [](const trellis::core::time::TimePoint&) {});
  }

  std::vector<unsigned> fire_order;
  std::vector<trellis::core::Timer> timers;
  for (unsigned i = 0; i < kTimerCount; ++i) {
    timers.push_back(node.CreateTimer(
        timer_interval, [&fire_order, i](const trellis::core::time::TimePoint&) { fire_order.push_back(i); }));
  }

  // The first update resets every timer, so they all end up sharing one expiry
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(1000)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  // Advance by exactly one interval so all of them come due on the same step
  time += std::chrono::milliseconds(timer_interval);
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  const std::vector<unsigned> expected{0, 1, 2, 3, 4, 5};
  ASSERT_EQ(fire_order, expected);
}

TEST(TrellisSimulatedClock, TimerDestroyedByAnotherCallbackDuringTheSameJumpIsSkipped) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  static constexpr unsigned time_jump_ms{1000};
  unsigned killer_ticks{0};
  unsigned victim_ticks{0};

  // The victim expires later than the killer, so it is still queued when the killer's callback releases it
  auto victim = node.CreateTimer(200u, [&victim_ticks](const trellis::core::time::TimePoint&) { ++victim_ticks; });
  auto killer = node.CreateTimer(100u, [&killer_ticks, &victim](const trellis::core::time::TimePoint&) {
    ++killer_ticks;
    victim.reset();
  });

  // The first update just resets the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(time_jump_ms)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  ASSERT_GT(killer_ticks, 0U);
  ASSERT_EQ(victim, nullptr);
  // Released before its own expiry came up, and the walk must not have touched it afterwards
  ASSERT_EQ(victim_ticks, 0U);
}

TEST(TrellisSimulatedClock, TimerCreatedByACallbackFiresInTheSameUpdate) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  trellis::core::OneShotTimer armed_by_callback;
  std::optional<trellis::core::time::TimePoint> second_fired_at;
  auto first = node.CreateOneShotTimer(120u, [&](const trellis::core::time::TimePoint&) {
    armed_by_callback = node.CreateOneShotTimer(
        100u, [&second_fired_at](const trellis::core::time::TimePoint& now) { second_fired_at = now; });
  });

  // The first update just resets the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(1000)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  node.UpdateSimulatedClock(time + std::chrono::milliseconds(280));
  node.RunOnce();

  ASSERT_EQ(second_fired_at, time + std::chrono::milliseconds(220));
}

TEST(TrellisSimulatedClock, ManagementTimersRunOnWallTimeUnderASimulatedClock) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  unsigned ticks{0};
  auto timer = std::make_shared<trellis::core::PeriodicTimerImpl>(
      node.GetEventLoop(), 5u, [&ticks](const trellis::core::time::TimePoint&) { ++ticks; }, 0u,
      trellis::core::TimerKind::kManagement);

  ASSERT_FALSE(timer->IsSimulationDriven());

  // Run the loop for real time without ever advancing the simulated clock
  node.RunFor(std::chrono::milliseconds(50));

  ASSERT_GT(ticks, 0U);
}

TEST(TrellisSimulatedClock, TimersCreatedBeforeTheClockWasEnabledAreNotStepped) {
  trellis::core::time::DisableSimulatedClock();

  // An application is free to construct its node before enabling the simulated clock. Timers built in that window hold
  // a real asio::steady_timer, so their expiry is a steady_clock reading rather than a simulated one, and the two are
  // not comparable even though time::TimePoint gives them the same type.
  trellis::core::Node node("test_clock_domain", {});

  const unsigned timer_interval{100u};
  unsigned ticks{0};
  // The initial delay keeps asio from firing it during the microseconds this test takes, so any tick below can only
  // have come from the simulated-clock walk
  auto timer =
      node.CreateTimer(timer_interval, [&ticks](const trellis::core::time::TimePoint&) { ++ticks; }, timer_interval);
  ASSERT_FALSE(timer->IsSimulationDriven());

  trellis::core::time::EnableSimulatedClock();
  // An hour past the steady_clock reading the timer's expiry was taken from. A caller seeding the clock from absolute
  // timestamps instead, as log playback does, puts the two domains ~1.7e9 seconds apart, which at this interval is
  // ~1e10 catch-up iterations rather than the 36000 an hour costs.
  const auto sim_start = std::chrono::steady_clock::now() + std::chrono::hours{1};
  trellis::core::time::SetSimulatedTime(sim_start);

  node.UpdateSimulatedClock(sim_start + std::chrono::milliseconds(timer_interval));
  node.RunOnce();

  // The timer is driven by asio, not by the walk, so the walk must leave it alone. Before this was guarded the walk
  // caught it up one interval at a time across the gap between the two clocks, firing it 36000 times here.
  ASSERT_EQ(ticks, 0);

  trellis::core::time::DisableSimulatedClock();
}

TEST(TrellisSimulatedClock, UpdateSimulatedClockTicksOneShotTimersOnce) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned timer1_interval{1000u};
  const unsigned timer2_interval{333u};
  const unsigned timer3_interval{220u};
  static constexpr unsigned time_jump_ms{1000u};  // matches longest interval

  unsigned timer1_ticks{0};
  unsigned timer2_ticks{0};
  unsigned timer3_ticks{0};
  trellis::core::time::TimePoint last_now;

  auto timer1 =
      node.CreateOneShotTimer(timer1_interval, [&timer1_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer1_ticks;
        ASSERT_GT(now, last_now);
        last_now = now;
      });

  auto timer2 =
      node.CreateOneShotTimer(timer2_interval, [&timer2_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer2_ticks;
        ASSERT_GT(now, last_now);
        last_now = now;
      });

  auto timer3 =
      node.CreateOneShotTimer(timer3_interval, [&timer3_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer3_ticks;
        ASSERT_GT(now, last_now);
        last_now = now;
      });

  // The first time we update the time, we're essentially resetting all the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(time_jump_ms)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(timer1_ticks, 0);
  ASSERT_EQ(timer2_ticks, 0);
  ASSERT_EQ(timer3_ticks, 0);

  // Now we're moving forward in time, so our timers should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // One shot timers should have fired once
  ASSERT_EQ(timer1_ticks, 1);
  ASSERT_EQ(timer2_ticks, 1);
  ASSERT_EQ(timer3_ticks, 1);

  // Now we're moving forward in time, but our one shot timers are expired
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(timer1_ticks, 1);
  ASSERT_EQ(timer2_ticks, 1);
  ASSERT_EQ(timer3_ticks, 1);

  // Reset the timers
  timer1->Reset();
  timer2->Reset();
  timer3->Reset();

  // Now we're moving forward in time and our timers are reset, so our timers should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // One shot timers should have fired once more
  ASSERT_EQ(timer1_ticks, 2);
  ASSERT_EQ(timer2_ticks, 2);
  ASSERT_EQ(timer3_ticks, 2);
}

TEST(TrellisSimulatedClock, ResetOnFirstJumpDoesNotReviveStoppedTimers) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time to 0

  trellis::core::Node node("test_simtime_reset", {});

  unsigned ticks{0};
  auto timer = node.CreateTimer(1000u, [&ticks](const trellis::core::time::TimePoint&) { ++ticks; });

  // Stop the timer before any time jump. The first jump from time 0 takes the reset branch, which must not
  // revive an explicitly-stopped (expired) timer.
  timer->Stop();

  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(1000)};
  node.UpdateSimulatedClock(time);  // first jump -> reset branch
  node.RunOnce();

  // Advance well past several intervals; a correctly-skipped timer never fires.
  time += std::chrono::milliseconds(5000);
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  ASSERT_EQ(ticks, 0u);
}

TEST(TrellisSimulatedClock, SkipAlignedDropsMissedSlotsUnderASimulatedClock) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned interval_ms{10u};
  unsigned ticks{0};
  auto timer = node.CreateTimer(
      interval_ms, [&ticks](const trellis::core::time::TimePoint&) { ++ticks; }, 0u,
      trellis::core::TimerKind::kApplication, trellis::core::RearmPolicy::kSkipAligned);

  // The first update only re-anchors the timers, so step once to get onto a known grid
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(1000)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();
  ASSERT_EQ(ticks, 0);

  const auto expiry_before = timer->GetExpiry();

  // Jump ten intervals in one step. Every one of those slots came due, but the policy asks for the missed
  // ones to be dropped rather than replayed, so exactly one callback should run.
  time += std::chrono::milliseconds(interval_ms * 10);
  node.UpdateSimulatedClock(time);
  node.RunOnce();

  ASSERT_EQ(ticks, 1);
  // Landed on the first slot past the jump, still on the original phase grid
  ASSERT_EQ(timer->GetExpiry(), expiry_before + std::chrono::milliseconds(interval_ms * 10));
  // The nine slots that were dropped are still counted as missed
  ASSERT_EQ(timer->GetOverrunCount(), 9U);
}

// Called off the loop, a shared-context guest's UpdateSimulatedClock is queued on the host's loop. The guest can be
// gone before the host runs it, which used to step the clock through the destroyed guest (the ASAN build reports it).
TEST(TrellisSimulatedClock, GuestClockUpdateStillStepsAfterTheGuestIsDestroyed) {
  trellis::core::Node host("test_simtime_host", {}, trellis::core::Node::SimClockRole::kPublisher);
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  const trellis::core::time::TimePoint target{std::chrono::milliseconds(1000)};
  {
    trellis::core::Node guest("test_simtime_guest", {}, trellis::core::Node::SimClockRole::kPublisher,
                              trellis::core::Node::SharedContext{host.GetEventLoop(), host.GetDiscovery()});
    guest.UpdateSimulatedClock(target);  // not on the loop's thread, so this is queued
  }
  host.RunUntilIdle();

  ASSERT_EQ(trellis::core::time::Now(), target);
}

// A timer callback that steps the clock further than the step running it. The outer step used to end by setting the
// clock to its own, earlier target, moving time backwards.
TEST(TrellisSimulatedClock, StepFromATimerCallbackPastTheOuterTargetDoesNotRewind) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  // The first update just resets the timers
  const trellis::core::time::TimePoint start{trellis::core::time::Now() + std::chrono::milliseconds(1000)};
  node.UpdateSimulatedClock(start);
  node.RunOnce();

  std::vector<std::pair<char, int64_t>> fires;
  const auto record = [&fires, start](char timer, const trellis::core::time::TimePoint& now) {
    fires.emplace_back(timer, std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
  };
  auto stepper = node.CreateOneShotTimer(100u, [&](const trellis::core::time::TimePoint& now) {
    record('A', now);
    node.UpdateSimulatedClock(start + std::chrono::milliseconds(500));  // on the loop's thread, so it runs inline
  });
  auto periodic = node.CreateTimer(200u, [&](const trellis::core::time::TimePoint& now) { record('B', now); });
  auto one_shot = node.CreateOneShotTimer(250u, [&](const trellis::core::time::TimePoint& now) { record('C', now); });

  node.UpdateSimulatedClock(start + std::chrono::milliseconds(300));
  node.RunOnce();

  const std::vector<std::pair<char, int64_t>> expected{{'A', 100}, {'B', 200}, {'C', 250}, {'B', 400}};
  EXPECT_EQ(fires, expected);
  EXPECT_EQ(trellis::core::time::Now(), start + std::chrono::milliseconds(500)) << "the outer step rewound the clock";
}

// With several loops in one process, another loop can set the clock before this one's first step. This loop's timers,
// armed while the clock was unset, are re-based by that first step rather than firing as if long overdue.
TEST(TrellisSimulatedClock, ASecondLoopReBasesItsTimersWhenAnotherLoopSetTheClockFirst) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time to 0

  trellis::core::Node first("test_simtime_first_loop", {});
  trellis::core::Node second("test_simtime_second_loop", {});
  const trellis::core::time::TimePoint start{std::chrono::milliseconds(1000)};
  std::vector<int64_t> fires;
  auto timer = second.CreateTimer(100u, [&fires, start](const trellis::core::time::TimePoint& now) {
    fires.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
  });

  first.UpdateSimulatedClock(start);
  first.RunOnce();
  // The second loop's first step carries an older time than the clock already shows.
  second.UpdateSimulatedClock(start - std::chrono::milliseconds(500));
  second.RunOnce();
  EXPECT_TRUE(fires.empty());

  second.UpdateSimulatedClock(start + std::chrono::milliseconds(250));
  second.RunOnce();
  EXPECT_EQ(fires, (std::vector<int64_t>{100, 200}));
}

// Nodes with loops of their own, each run by its own thread, all stepping the one process-global clock at once. A
// step whose target another thread has already passed must not throw, and the clock ends at the latest target.
TEST(TrellisSimulatedClock, LoopsOnTheirOwnThreadsStepTheSharedClockConcurrently) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time to 0
  constexpr int kLoops = 4;
  constexpr int kSteps = 2000;
  constexpr int kStepMs = 10;

  std::vector<std::unique_ptr<trellis::core::Node>> nodes;
  std::vector<std::unique_ptr<std::atomic<int>>> fires;
  std::vector<trellis::core::Timer> timers;
  for (int k = 0; k < kLoops; ++k) {
    nodes.push_back(std::make_unique<trellis::core::Node>("test_simtime_threaded_loop_" + std::to_string(k),
                                                          trellis::core::Config{}));
    fires.push_back(std::make_unique<std::atomic<int>>(0));
    timers.push_back(nodes.back()->CreateTimer(100u, [count = fires.back().get()](const auto&) { ++*count; }));
  }

  std::vector<std::thread> threads;
  for (int k = 0; k < kLoops; ++k) {
    threads.emplace_back([node = nodes[k].get()] {
      for (int i = 1; i <= kSteps; ++i) {
        node->UpdateSimulatedClock(trellis::core::time::TimePoint{std::chrono::milliseconds(i * kStepMs)});
        node->RunUntilIdle();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  const trellis::core::time::TimePoint end{std::chrono::milliseconds(kSteps * kStepMs)};
  EXPECT_EQ(trellis::core::time::Now(), end);

  // A loop whose thread started late may have been re-based against a clock the others had already run ahead, so how
  // often each fired during the race depends on scheduling. Past the end, every loop's timer is due whatever happened.
  const auto past_end = end + std::chrono::milliseconds(200);
  for (int k = 0; k < kLoops; ++k) {
    nodes[k]->UpdateSimulatedClock(past_end);
    nodes[k]->RunUntilIdle();
  }
  for (int k = 0; k < kLoops; ++k) {
    EXPECT_GT(*fires[k], 0) << "loop " << k << "'s timer never fired";
    EXPECT_LE(*fires[k], (kSteps * kStepMs + 200) / 100)
        << "loop " << k << "'s timer fired more often than its period allows";
  }
  timers.clear();
}
