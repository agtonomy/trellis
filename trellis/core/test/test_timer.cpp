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

#include <iostream>

#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

TEST_F(TrellisFixture, OneShotTimerFires) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = node_.CreateOneShotTimer(10, []() { ++fire_count; });
  ASSERT_EQ(timer->Expired(), false);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_EQ(timer->Expired(), true);
  ASSERT_EQ(fire_count, 1U);
}

TEST_F(TrellisFixture, OneShotTimerCancelsWithoutFiring) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = node_.CreateOneShotTimer(10, []() { ++fire_count; });
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

  auto timer = node_.CreateOneShotTimer(200, []() { ++fire_count; });
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

  auto timer = node_.CreateTimer(10, []() { ++fire_count; });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // There may be jitter, we're not concerned with a precise count
  bool expected_count = (fire_count == 5U || fire_count == 6U);

  ASSERT_EQ(expected_count, true);
}

TEST_F(TrellisFixture, PeriodicTimerStopsProperly) {
  static unsigned fire_count{0};
  StartRunnerThread();

  auto timer = node_.CreateTimer(10, []() { ++fire_count; });
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  timer->Stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Periodic timer fires immediately, so we expect it fired once before we stopped it
  ASSERT_EQ(fire_count, 1U);
}
