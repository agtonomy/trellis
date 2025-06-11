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

#include "trellis/core/config.hpp"

#include <gmock/gmock.h>

#include <fstream>

namespace trellis::core {

namespace {

using testing::DoubleEq;
using testing::Eq;
using testing::StrEq;

constexpr auto kBaseFilename = "trellis/core/test/test_base_config.yml";
constexpr auto kOverlayFilename = "trellis/core/test/test_overlay_config.yml";

}  // namespace

TEST(TrellisConfigAPI, OverlaySmokeTest) {
  Config config(kBaseFilename);

  // First make sure we loaded the base configuration correctly
  ASSERT_EQ(config["root_key"]["option1"]["string_value"].as<std::string>(), "foo_bar");

  const std::vector<bool> expected{true,  true,  false, false, false, true,  true, true,
                                   false, false, false, false, false, false, false};
  ASSERT_EQ(config["root_key"]["option1"]["boolean_vector"].as<std::vector<bool>>(), expected);
  ASSERT_EQ(config["root_key"]["option1"]["floating_scalar"].as<double>(), 10.0);
  ASSERT_EQ(config["root_key"]["option1"]["integral_scalar"].as<int>(), 1);

  // Feed in another configuration tree as an overlay
  config.OverlayFromFile(kOverlayFilename);

  // Make sure the overlay sits on top of the base configuration properly
  ASSERT_EQ(config["root_key"]["option1"]["string_value"].as<std::string>(), "updated_foo_bar");

  const std::vector<bool> expected2{true, true, true};
  ASSERT_EQ(config["root_key"]["option1"]["boolean_vector"].as<std::vector<bool>>(), expected2);
  ASSERT_EQ(config["root_key"]["option1"]["floating_scalar"].as<double>(), -4.0);
  ASSERT_EQ(config["root_key"]["option1"]["integral_scalar"].as<int>(), -100);
  ASSERT_EQ(config["root_key"]["option1"]["new_value"].as<double>(), 23.0);
}

TEST(WriteToFile, SimpleConfig) {
  constexpr auto kWriteFilename = "trellis/core/test/test_base_config_written.yml";

  const auto config = Config{kBaseFilename};
  config.WriteToFile(kWriteFilename);

  const auto read_config = Config{kWriteFilename};

  const auto config_option = config["root_key"]["option1"];
  const auto read_config_option = read_config["root_key"]["option1"];

  // NOTE: Generalized equality comparison of yamls is difficult, as they have multiple valid string formats and parsed
  // formats.
  ASSERT_THAT(read_config_option["string_value"].as<std::string>(),
              StrEq(config_option["string_value"].as<std::string>()));
  ASSERT_THAT(read_config_option["boolean_vector"].as<std::vector<bool>>(),
              Eq(config_option["boolean_vector"].as<std::vector<bool>>()));
  ASSERT_THAT(read_config_option["floating_scalar"].as<double>(),
              DoubleEq(config_option["floating_scalar"].as<double>()));
  ASSERT_THAT(read_config_option["integral_scalar"].as<int>(), Eq(config_option["integral_scalar"].as<int>()));
}

TEST(WriteToFile, Overlaid) {
  constexpr auto kWriteFilename = "trellis/core/test/test_base_config_written.yml";

  auto config = Config{kBaseFilename};
  config.OverlayFromFile(kOverlayFilename);
  config.WriteToFile(kWriteFilename);

  const auto read_config = Config{kWriteFilename};

  const auto config_option = config["root_key"]["option1"];
  const auto read_config_option = read_config["root_key"]["option1"];

  // NOTE: Generalized equality comparison of yamls is difficult, as they have multiple valid string formats and parsed
  // formats.
  ASSERT_THAT(read_config_option["string_value"].as<std::string>(),
              StrEq(config_option["string_value"].as<std::string>()));
  ASSERT_THAT(read_config_option["boolean_vector"].as<std::vector<bool>>(),
              Eq(config_option["boolean_vector"].as<std::vector<bool>>()));
  ASSERT_THAT(read_config_option["floating_scalar"].as<double>(),
              DoubleEq(config_option["floating_scalar"].as<double>()));
  ASSERT_THAT(read_config_option["integral_scalar"].as<int>(), Eq(config_option["integral_scalar"].as<int>()));
}

TEST(TrellisConfigAPI, AsIfExistsTests) {
  Config config(kBaseFilename);

  {
    // Key exists float
    const auto floating_scalar = config.AsIfExists<double>("root_key.option1.floating_scalar", 1.0);
    ASSERT_EQ(floating_scalar, 10.0);  // value from config
  }
  {
    // Key exists int
    const auto integral_scalar = config.AsIfExists<double>("root_key.option1.integral_scalar", 111);
    ASSERT_EQ(integral_scalar, 1);  // value from config
  }
  {
    // Key exists, bad type
    EXPECT_THROW(config.AsIfExists<std::vector<int>>("root_key.option1.floating_scalar", {}), YAML::BadConversion);
  }
  {
    // Leaf doesn't exist
    const auto missing_floating_scalar = config.AsIfExists<double>("root_key.option1.floating_scalar2", 1.0);
    ASSERT_EQ(missing_floating_scalar, 1.0);
  }

  {
    // Root doesn't exist
    const auto missing_floating_scalar = config.AsIfExists<double>("bad_key.option1.floating_scalar", 1.0);
    ASSERT_EQ(missing_floating_scalar, 1.0);
  }
}

}  // namespace trellis::core
