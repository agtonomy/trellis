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

#include "trellis/core/logging.hpp"

#include <gtest/gtest.h>

namespace {

struct CountsFormatting {
  static inline int format_calls{0};
};

}  // namespace

template <>
struct fmt::formatter<CountsFormatting> : fmt::formatter<std::string_view> {
  template <typename FormatContext>
  auto format(const CountsFormatting&, FormatContext& ctx) const {
    ++CountsFormatting::format_calls;
    return fmt::formatter<std::string_view>::format("formatted", ctx);
  }
};

namespace trellis::core::Log {

TEST(Logging, IsEnabledFollowsTheConfiguredLevel) {
  SetLogLevel(LogLevel::kFatal);
  EXPECT_TRUE(detail::IsEnabled(LogLevel::kInfo));
  EXPECT_TRUE(detail::IsEnabled(LogLevel::kError));
  EXPECT_TRUE(detail::IsEnabled(LogLevel::kFatal));
  EXPECT_FALSE(detail::IsEnabled(LogLevel::kDebug));

  SetLogLevel(LogLevel::kDebug);
  EXPECT_TRUE(detail::IsEnabled(LogLevel::kDebug));

  SetLogLevel("warn");
  EXPECT_TRUE(detail::IsEnabled(LogLevel::kInfo));
  EXPECT_TRUE(detail::IsEnabled(LogLevel::kWarn));
  EXPECT_FALSE(detail::IsEnabled(LogLevel::kError));
}

TEST(Logging, DisabledLevelDoesNotFormatArguments) {
  SetLogLevel(LogLevel::kFatal);
  CountsFormatting::format_calls = 0;

  Debug("debug {}", CountsFormatting{});
  EXPECT_EQ(CountsFormatting::format_calls, 0);

  Info("info {}", CountsFormatting{});
  EXPECT_EQ(CountsFormatting::format_calls, 1);

  SetLogLevel(LogLevel::kDebug);
  Debug("debug {}", CountsFormatting{});
  EXPECT_EQ(CountsFormatting::format_calls, 2);
}

}  // namespace trellis::core::Log
