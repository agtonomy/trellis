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

  // There may be a lot of jitter depending on system load, so we're not concerned with a precise count
  ASSERT_THAT(fire_count, testing::AllOf(testing::Gt(0), testing::Le(6)));
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
}
