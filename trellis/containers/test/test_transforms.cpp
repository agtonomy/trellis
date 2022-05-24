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

#include "trellis/containers/transforms.hpp"

using namespace trellis::core;

static constexpr std::chrono::milliseconds kTransformHistoryWindow(1000);

TEST(TrellisTransforms, HasTransformTestSuccessCase) {
  time::EnableSimulatedClock();
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(5000)));  // initial time
  trellis::containers::Transforms transforms;

  // Initially we don't have this transform
  ASSERT_FALSE(transforms.HasTransform("foo", "bar"));

  trellis::containers::Transforms::RigidTransform transform;
  // Even with a 0 ms validity we expect this to pass because our simulated clock didn't change
  transforms.UpdateTransform("foo", "bar", transform, std::chrono::milliseconds(0));
  ASSERT_TRUE(transforms.HasTransform("foo", "bar"));
}

TEST(TrellisTransforms, HasTransformTestTimeExpired) {
  time::EnableSimulatedClock();
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(5000)));  // initial time
  trellis::containers::Transforms transforms;

  // Initially we don't have this transform
  ASSERT_FALSE(transforms.HasTransform("foo", "bar"));

  trellis::containers::Transforms::RigidTransform transform;
  transforms.UpdateTransform("foo", "bar", transform, std::chrono::milliseconds(0));

  // One millisecond later and we should fail to retrieve the transform
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(5001)));
  ASSERT_FALSE(transforms.HasTransform("foo", "bar"));
}

TEST(TrellisTransforms, HasTransformStaticTransformCase) {
  time::EnableSimulatedClock();
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(0)));  // initial time
  trellis::containers::Transforms transforms;

  // Initially we don't have this transform
  ASSERT_FALSE(transforms.HasTransform("foo", "bar"));

  trellis::containers::Transforms::RigidTransform transform;
  transforms.UpdateTransform("foo", "bar", transform);

  // Jump forward as much as possible, we expect to be able to retrieve this transform
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds::max()));
  ASSERT_TRUE(transforms.HasTransform("foo", "bar"));
}

TEST(TrellisTransforms, RetrieveCorrectTransformGivenATime) {
  time::EnableSimulatedClock();
  auto now = time::TimePoint(std::chrono::milliseconds(1000));  // initial time
  constexpr auto validity_window = std::chrono::milliseconds(200);

  trellis::containers::Transforms transforms;
  trellis::containers::Transforms::RigidTransform transform;
  for (unsigned i = 0; i < 10U; ++i) {
    transform.translation.x = static_cast<double>(i + 1);
    time::SetSimulatedTime(now);  // initial time
    transforms.UpdateTransform("foo", "bar", transform, validity_window);
    now += std::chrono::milliseconds(50);  // 20 Hz update rate
  }
  // Now we have transforms with timestamps starting from 1000ms with 50ms increments

  {
    // We're back in time before the first transform by just within the validity window
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(800)));
    ASSERT_EQ(result.translation.x, 1.0);
  }
  {
    // We're right in between the 1000 and 1050 sample, older sample wins
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1025)));
    ASSERT_EQ(result.translation.x, 1.0);
  }
  {
    // Now we're closer to the 1050ms sample
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1026)));
    ASSERT_EQ(result.translation.x, 2.0);
  }
  {
    // Let's go near the 8th (1350ms) transform
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1326)));
    ASSERT_EQ(result.translation.x, 8.0);
  }
  {
    // Let's go just past the last one
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1451)));
    ASSERT_EQ(result.translation.x, 10.0);
  }

  // Just ahead of the validity window of the newest sample
  ASSERT_THROW(transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1450 + 200 + 1))),
               std::runtime_error);

  // Just before the validity window of the oldest sample
  ASSERT_THROW(transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1000 - 200 - 1))),
               std::runtime_error);
}

TEST(TrellisTransforms, OldTransformsShouldBePurged) {
  time::EnableSimulatedClock();
  auto now = time::TimePoint(std::chrono::milliseconds(1000));  // initial time
  constexpr auto validity_window = std::chrono::milliseconds(200);

  trellis::containers::Transforms transforms(5);  // 5 transforms max
  trellis::containers::Transforms::RigidTransform transform;
  for (unsigned i = 0; i < 10U; ++i) {
    transform.translation.x = static_cast<double>(i + 1);
    time::SetSimulatedTime(now);  // initial time
    transforms.UpdateTransform("foo", "bar", transform, validity_window);
    now += std::chrono::milliseconds(50);  // 20 Hz update rate
  }

  {
    // The oldest transform should now be at 1250ms, so we'll go back to 1250ms - 200ms validity window
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1050)));
    ASSERT_EQ(result.translation.x, 6.0);  // 1 - 5 should have been truncated
  }
}
