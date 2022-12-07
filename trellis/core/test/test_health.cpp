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

#include "trellis/core/health.hpp"

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

trellis::core::Publisher<trellis::core::HealthHistory> test_publisher =
    std::make_shared<trellis::core::PublisherImpl<trellis::core::HealthHistory>>("/test/health/topic");

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
