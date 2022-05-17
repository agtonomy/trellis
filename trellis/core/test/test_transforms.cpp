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

#include "trellis/core/transforms.hpp"

using namespace trellis::core;

TEST(TrellisTransforms, BasicHasTransformTest) {
  time::EnableSimulatedClock();
  time::SetSimulatedTime(time::TimePoint(std::chrono::milliseconds(5000))); // initial time


  trellis::core::Transforms transforms(std::chrono::milliseconds(1000));

  // Initially we don't have this transform
  ASSERT_FALSE(transforms.HasTransform("foo", "bar"));

  trellis::core::Transforms::RigidTransform transform;
  transforms.UpdateTransform("foo", "bar", transform);

  ASSERT_TRUE(transforms.HasTransform("foo", "bar"));
}
