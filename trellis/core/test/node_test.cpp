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
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

constexpr auto kBaseFilename = "trellis/core/test/test_base_config.yml";

TEST_F(TrellisFixture, StartAndStopNode) {
  // Simply start the runner thread and then test that it will gracefully
  // stop without hanging
  StartRunnerThread();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  Stop();
}

TEST(TrellisNode, Name) {
  Config config(kBaseFilename);
  Node node("name", config);
  ASSERT_EQ("name", node.GetName());
}
