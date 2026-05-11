/*
 * Copyright (C) 2026 Agtonomy
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

#include "trellis/core/crash_counter.hpp"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kMarkerDir = "/tmp/trellis";
constexpr std::string_view kNodeName = "trellis_crash_counter_unit_test";

fs::path MarkerPath() { return fs::path(kMarkerDir) / (fmt::format("{}_crash_counter", kNodeName)); }

void CleanMarker() {
  std::error_code ec;
  fs::remove(MarkerPath(), ec);
  fs::remove(MarkerPath().string() + ".tmp", ec);
}

class CrashCounterTest : public ::testing::Test {
 protected:
  void SetUp() override { CleanMarker(); }
  void TearDown() override { CleanMarker(); }
};

}  // namespace

TEST_F(CrashCounterTest, CleanStartReportsZero) {
  trellis::core::CrashCounter c{kMarkerDir, kNodeName};
  EXPECT_EQ(c.UncleanExitCount(), 0);
  EXPECT_TRUE(fs::exists(MarkerPath()));
}

TEST_F(CrashCounterTest, CleanShutdownRemovesMarker) {
  { trellis::core::CrashCounter c{kMarkerDir, kNodeName}; }
  EXPECT_FALSE(fs::exists(MarkerPath()));
}

TEST_F(CrashCounterTest, UncleanExitPreservesMarker) {
  {
    trellis::core::CrashCounter c{kMarkerDir, kNodeName};
    c.MarkUncleanExit();
  }
  EXPECT_TRUE(fs::exists(MarkerPath()));
}

TEST_F(CrashCounterTest, NextStartAfterCrashIncrements) {
  {
    trellis::core::CrashCounter c{kMarkerDir, kNodeName};
    c.MarkUncleanExit();
  }
  trellis::core::CrashCounter c2{kMarkerDir, kNodeName};
  EXPECT_EQ(c2.UncleanExitCount(), 1);
}

TEST_F(CrashCounterTest, ConsecutiveCrashesAccumulate) {
  for (int expected = 0; expected < 4; ++expected) {
    trellis::core::CrashCounter c{kMarkerDir, kNodeName};
    EXPECT_EQ(c.UncleanExitCount(), expected);
    c.MarkUncleanExit();
  }
}

TEST_F(CrashCounterTest, CleanRestartResetsToZero) {
  {
    trellis::core::CrashCounter c{kMarkerDir, kNodeName};
    c.MarkUncleanExit();
  }
  { trellis::core::CrashCounter c{kMarkerDir, kNodeName}; }
  trellis::core::CrashCounter c3{kMarkerDir, kNodeName};
  EXPECT_EQ(c3.UncleanExitCount(), 0);
}

TEST_F(CrashCounterTest, CorruptMarkerResetsToOne) {
  fs::create_directories("/tmp/trellis");
  std::ofstream(MarkerPath()) << "not a number";
  trellis::core::CrashCounter c{kMarkerDir, kNodeName};
  EXPECT_EQ(c.UncleanExitCount(), 1);
}

TEST_F(CrashCounterTest, MarkerHoldsCurrentCountWhileRunning) {
  trellis::core::CrashCounter c{kMarkerDir, kNodeName};
  std::ifstream f(MarkerPath());
  int v = -1;
  f >> v;
  EXPECT_EQ(v, 0);
}

TEST_F(CrashCounterTest, DestructionDuringUnwindingPreservesMarker) {
  try {
    trellis::core::CrashCounter c{kMarkerDir, kNodeName};
    throw std::runtime_error("boom");
  } catch (const std::exception&) {
  }
  EXPECT_TRUE(fs::exists(MarkerPath()));
  trellis::core::CrashCounter c2{kMarkerDir, kNodeName};
  EXPECT_EQ(c2.UncleanExitCount(), 1);
}

TEST_F(CrashCounterTest, MarkUncleanExitIsIdempotent) {
  {
    trellis::core::CrashCounter c{kMarkerDir, kNodeName};
    c.MarkUncleanExit();
    c.MarkUncleanExit();
  }
  trellis::core::CrashCounter c2{kMarkerDir, kNodeName};
  EXPECT_EQ(c2.UncleanExitCount(), 1);
}
