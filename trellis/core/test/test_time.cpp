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

#include <thread>

#include "trellis/core/time.hpp"

using namespace trellis::core;

TEST(TrellisTimeAPI, SecondsConversion) {
  time::TimePoint start(std::chrono::milliseconds(500));
  time::TimePoint end(std::chrono::milliseconds(1000));

  const double start_secs = time::TimePointToSeconds(start);
  const double end_secs = time::TimePointToSeconds(end);

  const double dt = end_secs - start_secs;

  ASSERT_TRUE(end_secs > start_secs);
  ASSERT_TRUE(dt == 0.5);
}
