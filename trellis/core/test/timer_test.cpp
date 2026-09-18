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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>
#include <string>

#include "trellis/core/test/test_fixture.hpp"

using trellis::core::test::TrellisFixture;

TEST_F(TrellisFixture, OneShotTimerFires) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = GetNode().CreateOneShotTimer(10, [](const trellis::core::time::TimePoint&) { ++fire_count; });
  ASSERT_EQ(timer->Expired(), false);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_EQ(timer->Expired(), true);
  ASSERT_EQ(fire_count, 1U);
  // One-shot timers always return 0 for overrun count
  ASSERT_EQ(timer->GetOverrunCount(), 0U);
}

TEST_F(TrellisFixture, OneShotTimerCancelsWithoutFiring) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = GetNode().CreateOneShotTimer(10, [](const trellis::core::time::TimePoint&) { ++fire_count; });
  ASSERT_EQ(timer->Expired(), false);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  timer->Stop();  // cancel before timer is set to expire
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_EQ(timer->Expired(), true);
  ASSERT_EQ(fire_count, 0U);
}

TEST_F(TrellisFixture, OneShotTimerReset) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = GetNode().CreateOneShotTimer(200, [](const trellis::core::time::TimePoint&) { ++fire_count; });
  ASSERT_EQ(timer->Expired(), false);

  // Now it should still fire once after we wait
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  ASSERT_EQ(fire_count, 1U);
  ASSERT_EQ(timer->Expired(), true);

  // Keep rapidly resetting the timer and let more time pass than the timer was
  // originally set for
  for (unsigned i = 0; i < 1000; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    timer->Reset();
  }

  ASSERT_EQ(timer->Expired(), false);
  // Now it should still fire once more after we wait
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  ASSERT_EQ(fire_count, 2U);
  ASSERT_EQ(timer->Expired(), true);
}

TEST_F(TrellisFixture, PeriodicTimerFiresMultipleTimes) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = GetNode().CreateTimer(10, [](const trellis::core::time::TimePoint&) { ++fire_count; });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  // This timer is still armed, so join before it goes out of scope
  StopAndJoinRunnerThread();

  // There may be a lot of jitter depending on system load, so we're not concerned with a precise count
  ASSERT_THAT(fire_count, testing::AllOf(testing::Gt(0), testing::Le(6)));
  // No overruns expected since callback is trivial
  ASSERT_EQ(timer->GetOverrunCount(), 0U);
}

TEST_F(TrellisFixture, PeriodicTimerStopsProperly) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = GetNode().CreateTimer(10, [](const trellis::core::time::TimePoint&) { ++fire_count; });
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  timer->Stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Periodic timer fires immediately, so we expect it fired once before we stopped it
  ASSERT_EQ(fire_count, 1U);
  // No overruns expected since callback is trivial
  ASSERT_EQ(timer->GetOverrunCount(), 0U);
}

TEST_F(TrellisFixture, PeriodicTimerStopsWithinCallback) {
  static unsigned fire_count{0};
  StartRunnerThread();

  std::shared_ptr<trellis::core::TimerImpl> timer =
      GetNode().CreateTimer(10, [&timer](const trellis::core::time::TimePoint&) {
        if (++fire_count == 5) {
          timer->Stop();
        }
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Periodic timer fires immediately, so we expect it fired 5 times before we stopped it
  ASSERT_EQ(fire_count, 5);
  // No overruns expected since callback is trivial
  ASSERT_EQ(timer->GetOverrunCount(), 0U);
}

TEST_F(TrellisFixture, PeriodicTimerOverrunDetection) {
  static unsigned fire_count{0};
  StartRunnerThread();

  // Create a timer with a 10ms interval, but the callback sleeps for 25ms
  // This should cause overruns to be detected
  auto timer = GetNode().CreateTimer(10, [](const trellis::core::time::TimePoint&) {
    ++fire_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  });

  // Wait enough time for several firings
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // The callback sleeps for longer than the interval, so the loop is always inside this timer's handler here. Join
  // before the timer goes out of scope, or the handler resumes on a destroyed timer.
  StopAndJoinRunnerThread();

  // We should have detected overruns since callback takes longer than interval
  ASSERT_GT(timer->GetOverrunCount(), 0U);
  ASSERT_GT(fire_count, 0U);
}

// A callback may drop the last reference to its own timer (the RPC client's timeout handler does) and keep running
// afterwards, so its closure has to outlive the timer.
TEST_F(TrellisFixture, CallbackMayDestroyItsOwnTimer) {
  static unsigned fire_count{0};
  static bool closure_outlived_the_timer{false};
  StartRunnerThread();

  // The 10ms delay matters. At the default of zero the timer is due the moment its constructor arms it, and the
  // callback would race this thread's assignment to `timer`.
  std::shared_ptr<trellis::core::TimerImpl> timer = GetNode().CreateTimer(
      10,
      // Too big for a small-object buffer, so it lives wherever the callback does. Reading it after the reset is
      // the point.
      [&timer, sentinel = std::string(64, 'x')](const trellis::core::time::TimePoint&) {
        ++fire_count;
        timer.reset();
        closure_outlived_the_timer = sentinel.size() == 64;
      },
      10u);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  StopAndJoinRunnerThread();

  ASSERT_EQ(fire_count, 1U);
  ASSERT_EQ(timer, nullptr);
  ASSERT_TRUE(closure_outlived_the_timer);
}

// Block the loop past both deadlines and asio queues killer and victim together, in expiry order. Killer destroys
// victim after victim's completion is committed, which cancel() cannot undo.
//
// That batching is current asio behaviour, not a promise, so the extra counters are here to fail loudly rather than
// pass empty if the blocker or the killer never ran.
TEST_F(TrellisFixture, DestroyedTimerDropsAnAlreadyQueuedCompletion) {
  static unsigned blocker_fire_count{0};
  static unsigned killer_fire_count{0};
  static unsigned victim_fire_count{0};
  StartRunnerThread();

  auto blocker = GetNode().CreateOneShotTimer(5, [](const trellis::core::time::TimePoint&) {
    ++blocker_fire_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  });
  auto victim = GetNode().CreateOneShotTimer(20, [](const trellis::core::time::TimePoint&) { ++victim_fire_count; });
  auto killer = GetNode().CreateOneShotTimer(10, [&victim](const trellis::core::time::TimePoint&) {
    ++killer_fire_count;
    victim.reset();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  StopAndJoinRunnerThread();

  ASSERT_EQ(blocker_fire_count, 1U);
  ASSERT_EQ(killer_fire_count, 1U);
  ASSERT_EQ(victim, nullptr);  // the killer really did drop the last reference
  ASSERT_EQ(victim_fire_count, 0U);
}

// Same queued completion, stopped instead of destroyed. Stop() cannot recall it either, so the timer would fire
// once more after Stop() returned.
TEST_F(TrellisFixture, StoppedTimerDropsAnAlreadyQueuedCompletion) {
  static unsigned stopper_fire_count{0};
  static unsigned target_fire_count{0};
  StartRunnerThread();

  auto blocker = GetNode().CreateOneShotTimer(
      5, [](const trellis::core::time::TimePoint&) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
  auto target = GetNode().CreateOneShotTimer(20, [](const trellis::core::time::TimePoint&) { ++target_fire_count; });
  auto stopper = GetNode().CreateOneShotTimer(10, [&target](const trellis::core::time::TimePoint&) {
    ++stopper_fire_count;
    target->Stop();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  StopAndJoinRunnerThread();

  ASSERT_EQ(stopper_fire_count, 1U);
  ASSERT_EQ(target_fire_count, 0U);
  ASSERT_TRUE(target->Expired());
}

// Reset() re-arms through Stop(), so the same queued completion is waiting for it, and firing on it would report a
// deadline the caller just moved. Hence the assertion on when the timer fired, not that it did.
TEST_F(TrellisFixture, ResetTimerDropsAnAlreadyQueuedCompletion) {
  static constexpr auto kDelayMs = std::chrono::milliseconds(20);
  static unsigned target_fire_count{0};
  static bool fired_before_the_new_deadline{false};
  static trellis::core::time::TimePoint reset_time{};
  StartRunnerThread();

  auto blocker = GetNode().CreateOneShotTimer(
      5, [](const trellis::core::time::TimePoint&) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
  auto target = GetNode().CreateOneShotTimer(20, [](const trellis::core::time::TimePoint& now) {
    ++target_fire_count;
    // A stale completion lands the instant the loop unblocks, a full delay early
    fired_before_the_new_deadline = (now - reset_time) < (kDelayMs - std::chrono::milliseconds(5));
  });
  auto resetter = GetNode().CreateOneShotTimer(10, [&target](const trellis::core::time::TimePoint& now) {
    reset_time = now;
    target->Reset();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  StopAndJoinRunnerThread();

  ASSERT_NE(reset_time, trellis::core::time::TimePoint{});  // the resetter ran, so reset_time is real
  ASSERT_EQ(target_fire_count, 1U);
  ASSERT_FALSE(fired_before_the_new_deadline);
}
