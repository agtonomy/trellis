/*
 * Copyright (C) 2023 Agtonomy
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

#include "trellis/core/transforms.hpp"

TEST(TransformToAndFromMessage, SimpleRoundTripConversion) {
  // To message.
  const std::string from = "from";
  const std::string to = "to";
  const Eigen::Translation<double, 3> translation(1.0, 2.0, 3.0);
  const Eigen::Quaterniond rotation(Eigen::AngleAxisd(0.3, Eigen::Vector<double, 3>::UnitZ()) *
                                    Eigen::AngleAxisd(0.2, Eigen::Vector<double, 3>::UnitY()) *
                                    Eigen::AngleAxisd(0.1, Eigen::Vector<double, 3>::UnitX()));
  const trellis::containers::Transforms::RigidTransform transform(
      trellis::containers::Transforms::AffineTransform3D(translation * rotation));
  const auto proto = trellis::core::Transforms::CreateMessageFromTransform(from, to, transform);
  EXPECT_NEAR(proto.translation().x(), translation.x(), 1e-6);
  EXPECT_NEAR(proto.translation().y(), translation.y(), 1e-6);
  EXPECT_NEAR(proto.translation().z(), translation.z(), 1e-6);
  EXPECT_NEAR(proto.rotation().x(), rotation.x(), 1e-6);
  EXPECT_NEAR(proto.rotation().y(), rotation.y(), 1e-6);
  EXPECT_NEAR(proto.rotation().z(), rotation.z(), 1e-6);
  EXPECT_NEAR(proto.rotation().w(), rotation.w(), 1e-6);
  EXPECT_EQ(proto.frame_from(), from);
  EXPECT_EQ(proto.frame_to(), to);

  // To transform.
  const auto new_transform = trellis::core::Transforms::CreateTransformFromMessage(proto);
  EXPECT_EQ(new_transform, transform);
}

TEST(CreateTransformFromConfig, SimpleConversion) {
  // This corresponds to approximately the same transform as the TransformToAndFromMessage test.
  const std::string transform_config = R"foo(
translation:
  x: 1.0
  y: 2.0
  z: 3.0
rotation:
  x: 0.1435722
  y: 0.1060205
  z: 0.0342708
  w: 0.9833474
)foo";

  const trellis::core::Config config(YAML::Load(transform_config));
  const auto transform = trellis::core::Transforms::CreateTransformFromConfig(config);
  EXPECT_EQ(transform.translation.x(), config["translation"]["x"].as<double>());
  EXPECT_EQ(transform.translation.y(), config["translation"]["y"].as<double>());
  EXPECT_EQ(transform.translation.z(), config["translation"]["z"].as<double>());
  EXPECT_EQ(transform.rotation.w(), config["rotation"]["w"].as<double>());
  EXPECT_EQ(transform.rotation.x(), config["rotation"]["x"].as<double>());
  EXPECT_EQ(transform.rotation.y(), config["rotation"]["y"].as<double>());
  EXPECT_EQ(transform.rotation.z(), config["rotation"]["z"].as<double>());
}
