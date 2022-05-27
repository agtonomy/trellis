/*
 * Copyright (C) 2022 Agtonomy
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
  transforms.UpdateTransform("foo", "bar", transform);
  ASSERT_TRUE(transforms.HasTransform("foo", "bar"));
}

TEST(TrellisTransforms, RetrieveCorrectTransformGivenATime) {
  time::EnableSimulatedClock();
  auto now = time::TimePoint(std::chrono::milliseconds(1000));  // initial time

  trellis::containers::Transforms transforms;
  trellis::containers::Transforms::RigidTransform transform;
  for (unsigned i = 0; i < 10U; ++i) {
    transform.translation.x() = static_cast<double>(i + 1);
    time::SetSimulatedTime(now);  // initial time
    transforms.UpdateTransform("foo", "bar", transform);
    now += std::chrono::milliseconds(50);  // 20 Hz update rate
  }
  // Now we have transforms with timestamps starting from 1000ms with 50ms increments

  {
    // We're back in time before the first transform by just within the validity window
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(800)));
    ASSERT_EQ(result.translation.x(), 1.0);
  }
  {
    // We're right in between the 1000 and 1050 sample, older sample wins
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1025)));
    ASSERT_EQ(result.translation.x(), 1.0);
  }
  {
    // Now we're closer to the 1050ms sample
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1026)));
    ASSERT_EQ(result.translation.x(), 2.0);
  }
  {
    // Let's go near the 8th (1350ms) transform
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1326)));
    ASSERT_EQ(result.translation.x(), 8.0);
  }
  {
    // Let's go just past the last one
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1451)));
    ASSERT_EQ(result.translation.x(), 10.0);
  }
}

TEST(TrellisTransforms, OldTransformsShouldBePurged) {
  time::EnableSimulatedClock();
  auto now = time::TimePoint(std::chrono::milliseconds(1000));  // initial time

  trellis::containers::Transforms transforms(5);  // 5 transforms max
  trellis::containers::Transforms::RigidTransform transform;
  for (unsigned i = 0; i < 10U; ++i) {
    transform.translation.x() = static_cast<double>(i + 1);
    time::SetSimulatedTime(now);
    transforms.UpdateTransform("foo", "bar", transform);
    now += std::chrono::milliseconds(50);  // 20 Hz update rate
  }

  {
    // The oldest transform should now be at 1250ms, so we'll go back to 1250ms - 200ms validity window
    const auto& result = transforms.GetTransform("foo", "bar", time::TimePoint(std::chrono::milliseconds(1050)));
    ASSERT_EQ(result.translation.x(), 6.0);  // 1 - 5 should have been truncated
  }
}

TEST(TrellisTransforms, InverseTransformIsInserted) {
  time::EnableSimulatedClock();
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(5000)));  // initial time
  trellis::containers::Transforms transforms;

  // Initially we don't have this transform
  ASSERT_FALSE(transforms.HasTransform("foo", "bar"));
  ASSERT_FALSE(transforms.HasTransform("bar", "foo"));

  trellis::containers::Transforms::RigidTransform transform;
  transform.translation.x() = 1.0;
  transform.translation.y() = 2.0;
  transform.translation.z() = 3.0;
  transform.rotation.w() = 0.70710678118;
  transform.rotation.x() = 0.0;
  transform.rotation.y() = 0.0;
  transform.rotation.z() = 0.70710678118;
  transforms.UpdateTransform("foo", "bar", transform);
  ASSERT_TRUE(transforms.HasTransform("foo", "bar"));
  ASSERT_TRUE(transforms.HasTransform("bar", "foo"));  // inverse also should exist

  const auto transform_out = transforms.GetTransform("foo", "bar").GetAffineRepresentation();
  const auto transform_inv_out = transforms.GetTransform("bar", "foo").GetAffineRepresentation();

  const Eigen::Vector3d vec(1.0, 2.5, 10.0);

  // We transformed and then back via the inverse, expect to be equal essentially
  const Eigen::Vector3d rotated = transform_inv_out * (transform_out * vec);
  EXPECT_NEAR(vec.x(), rotated.x(), 0.0000001);
  EXPECT_NEAR(vec.y(), rotated.y(), 0.0000001);
  EXPECT_NEAR(vec.z(), rotated.z(), 0.0000001);
}

TEST(TrellisTransforms, InvalidTransformNamesThrows) {
  time::EnableSimulatedClock();
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(5000)));  // initial time
  trellis::containers::Transforms transforms;

  // using the delimiter in the name is invalid
  ASSERT_THROW(transforms.HasTransform("fo|o", "bar"), std::invalid_argument);
  ASSERT_THROW(transforms.HasTransform("fo|o", "bar|"), std::invalid_argument);
}
