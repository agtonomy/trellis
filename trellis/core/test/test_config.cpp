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

#include "trellis/core/config.hpp"

using namespace trellis::core;

TEST(TrellisConfigAPI, OverlaySmokeTest) {
  trellis::core::Config config("trellis/core/test/test_base_config.yml");

  // First make sure we loaded the base configuration correctly
  ASSERT_EQ(config["root_key"]["option1"]["string_value"].as<std::string>(), "foo_bar");

  const std::vector<bool> expected{true,  true,  false, false, false, true,  true, true,
                                   false, false, false, false, false, false, false};
  ASSERT_EQ(config["root_key"]["option1"]["boolean_vector"].as<std::vector<bool>>(), expected);
  ASSERT_EQ(config["root_key"]["option1"]["floating_scalar"].as<double>(), 10.0);
  ASSERT_EQ(config["root_key"]["option1"]["integral_scalar"].as<int>(), 1);

  // Feed in another configuration tree as an overlay
  YAML::Node overlay(YAML::LoadFile("trellis/core/test/test_overlay_config.yml"));
  config.Overlay(overlay);

  // Make sure the overlay sits on top of the base configuration properly
  ASSERT_EQ(config["root_key"]["option1"]["string_value"].as<std::string>(), "updated_foo_bar");

  const std::vector<bool> expected2{true, true, true};
  ASSERT_EQ(config["root_key"]["option1"]["boolean_vector"].as<std::vector<bool>>(), expected2);
  ASSERT_EQ(config["root_key"]["option1"]["floating_scalar"].as<double>(), -4.0);
  ASSERT_EQ(config["root_key"]["option1"]["integral_scalar"].as<int>(), -100);
  ASSERT_EQ(config["root_key"]["option1"]["new_value"].as<double>(), 23.0);
}
