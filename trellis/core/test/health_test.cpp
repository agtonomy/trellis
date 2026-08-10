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

#include "trellis/core/health.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <utility>

namespace {

static const std::string kTestAppName = "health_test";
static const std::string kTestConfigString = R"foo(
trellis:
  health:
    reporting_topic: /test/health/topic
    interval_ms: 100
    history_size: 3
)foo";
static constexpr unsigned kTestHistorySize = 3U;  // matching config above

trellis::core::EventLoop GetEventLoop() {
  static trellis::core::EventLoop ev_loop_{};
  return ev_loop_;
}

std::shared_ptr<trellis::core::discovery::Discovery> GetDiscovery() {
  static std::shared_ptr<trellis::core::discovery::Discovery> discovery_{nullptr};
  if (discovery_ == nullptr) {
    discovery_ =
        std::make_shared<trellis::core::discovery::Discovery>("health_test", GetEventLoop(), trellis::core::Config{});
  }
  return discovery_;
}

trellis::core::Publisher<trellis::core::HealthHistory> test_publisher =
    std::make_shared<trellis::core::PublisherImpl<trellis::core::HealthHistory>>(
        GetEventLoop(), "/test/health/topic", GetDiscovery(), trellis::core::Config{});

}  // namespace

TEST(TrellisHealth, SingleUpdate) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  // First confirm it's empty
  ASSERT_TRUE(health.GetHealthHistory().empty());
  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");
  // Now we should have a single update
  ASSERT_EQ(health.GetHealthHistory().size(), 1);
  ASSERT_EQ(health.GetLastHealthStatus().health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), 0x01);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out");
}

// The status code is application-defined and opaque to trellis, so the whole width of Code has to survive the round
// trip. A narrower Code or status_code field would truncate silently rather than fail.
TEST(TrellisHealth, CodeWiderThanThirtyTwoBits) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  constexpr trellis::core::Health::Code kWideCode = 0x1234'5678'9ABCULL;
  static_assert(kWideCode > std::numeric_limits<uint32_t>::max());

  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, kWideCode, "Wide code");
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), kWideCode);
}

// Every HealthState enumerator must reach the wire as its proto counterpart. A reordered or mismapped entry would
// report the wrong severity to the rest of the system while still looking like a successful update.
TEST(TrellisHealth, HealthStatusOverloadMapsEveryState) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  const std::array<std::pair<trellis::core::health::HealthState, trellis::core::HealthState>, 6> expected_mappings = {{
      {trellis::core::health::HealthState::kUnspecified, trellis::core::HealthState::HEALTH_STATE_UNSPECIFIED},
      {trellis::core::health::HealthState::kNormal, trellis::core::HealthState::HEALTH_STATE_NORMAL},
      {trellis::core::health::HealthState::kDegraded, trellis::core::HealthState::HEALTH_STATE_DEGRADED},
      {trellis::core::health::HealthState::kRecoverable, trellis::core::HealthState::HEALTH_STATE_RECOVERABLE},
      {trellis::core::health::HealthState::kCritical, trellis::core::HealthState::HEALTH_STATE_CRITICAL},
      {trellis::core::health::HealthState::kLost, trellis::core::HealthState::HEALTH_STATE_LOST},
  }};

  trellis::core::Health::Code code = 0;
  for (const auto& [state, expected_proto_state] : expected_mappings) {
    // Vary the code so consecutive updates are never treated as duplicates.
    ++code;
    health.Update(trellis::core::health::HealthStatus{.state = state, .code = code, .description = "mapping"});
    ASSERT_EQ(health.GetLastHealthStatus().health_state(), expected_proto_state);
    ASSERT_EQ(health.GetLastHealthStatus().status_code(), code);
  }
}

TEST(TrellisHealth, HealthStatusOverloadCarriesCodeAndDescription) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  constexpr trellis::core::Health::Code kWideCode = 0x1234'5678'9ABCULL;
  static_assert(kWideCode > std::numeric_limits<uint32_t>::max());

  health.Update(trellis::core::health::HealthStatus{
      .state = trellis::core::health::HealthState::kCritical, .code = kWideCode, .description = "Inputs timed out"});

  ASSERT_EQ(health.GetHealthHistory().size(), 1);
  ASSERT_EQ(health.GetLastHealthStatus().health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), kWideCode);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out");
}

// The overload must share the deduplication path with the enum-based Update rather than bypassing it.
TEST(TrellisHealth, HealthStatusOverloadHonorsCompareDescription) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  const trellis::core::health::HealthStatus status{
      .state = trellis::core::health::HealthState::kCritical, .code = 0x01, .description = "Inputs timed out"};
  const trellis::core::health::HealthStatus restated{
      .state = trellis::core::health::HealthState::kCritical, .code = 0x01, .description = "Inputs timed out again"};

  health.Update(status);
  health.Update(restated);
  ASSERT_EQ(health.GetHealthHistory().size(), 1);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out");

  health.Update(restated, true);
  ASSERT_EQ(health.GetHealthHistory().size(), 2);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out again");
}

TEST(TrellisHealth, MultipleUpdates) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  health.Update(trellis::core::HealthState::HEALTH_STATE_NORMAL, 0x00, "");
  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");

  // We should have 2 updates
  ASSERT_EQ(health.GetHealthHistory().size(), 2);
  ASSERT_EQ(health.GetHealthHistory().front().health_state(), trellis::core::HealthState::HEALTH_STATE_NORMAL);
  ASSERT_EQ(health.GetHealthHistory().front().status_code(), 0x00);
  ASSERT_EQ(health.GetHealthHistory().front().status_description(), "");
  ASSERT_EQ(health.GetLastHealthStatus().health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), 0x01);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out");
}

TEST(TrellisHealth, DuplicateUpdateRejected) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");
  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");

  // We should have a single update since the two were duplicates
  ASSERT_EQ(health.GetHealthHistory().size(), 1);
  ASSERT_EQ(health.GetLastHealthStatus().health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), 0x01);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out");
}

TEST(TrellisHealth, FillHistory) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  // Update twice as many times as our history size
  for (size_t i = 0; i < kTestHistorySize * 2; ++i) {
    health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, i + 1, "foobar");
  }

  // We should have our history filled with the latest updates
  ASSERT_EQ(health.GetHealthHistory().size(), kTestHistorySize);

  unsigned i = 0;
  for (const auto& update : health.GetHealthHistory()) {
    ASSERT_EQ(update.health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
    ASSERT_EQ(update.status_code(), kTestHistorySize + i + 1);
    ASSERT_EQ(update.status_description(), "foobar");
    ++i;
  }
}

TEST(TrellisHealth, CompareDescription) {
  trellis::core::Health health{kTestAppName, trellis::core::Config(YAML::Load(kTestConfigString)),
                               [this](const std::string& topic) { return test_publisher; },
                               [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; }};

  // First confirm it's empty
  ASSERT_TRUE(health.GetHealthHistory().empty());
  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");
  // Now update again with out the flag.
  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out again");
  // We should have a single update
  ASSERT_EQ(health.GetHealthHistory().size(), 1);
  ASSERT_EQ(health.GetLastHealthStatus().health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), 0x01);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out");
  // Now update again with the flag.
  health.Update(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out again", true);
  // We should have a two updates
  ASSERT_EQ(health.GetHealthHistory().size(), 2);
  ASSERT_EQ(health.GetLastHealthStatus().health_state(), trellis::core::HealthState::HEALTH_STATE_CRITICAL);
  ASSERT_EQ(health.GetLastHealthStatus().status_code(), 0x01);
  ASSERT_EQ(health.GetLastHealthStatus().status_description(), "Inputs timed out again");
}
