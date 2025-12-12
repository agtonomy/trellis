/*
 * Copyright (C) 2025 Agtonomy
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

#include "trellis/core/test/test_fixture.hpp"
#include "trellis/utils/metrics_pub/metrics_publisher.hpp"

using TrellisFixture = trellis::core::test::TrellisFixture;

TEST_F(TrellisFixture, simple_test) {
  using namespace std::literals;  // enables literal suffixes, e.g. 24h, 1ms, 1s.
  trellis::core::time::EnableSimulatedClock();
  const auto start = trellis::core::time::Now();
  trellis::core::time::SetSimulatedTime(start);

  static unsigned receive_count{0};
  trellis::utils::metrics::MetricsPublisher::Config config{.metrics_topic = "/metrics"};
  trellis::utils::metrics::MetricsPublisher pub(GetNode(), config);
  const auto sub = GetNode().CreateSubscriber<trellis::utils::metrics::MetricsGroup>(
      "/metrics", [&](const trellis::core::time::TimePoint&, const trellis::core::time::TimePoint&,
                      trellis::core::SubscriberImpl<trellis::utils::metrics::MetricsGroup>::MsgTypePtr msg) {
        EXPECT_STREQ(msg->source().c_str(), "test_fixture");
        ASSERT_TRUE(msg->measurements().size() == 1);
        EXPECT_STREQ(msg->measurements()[0].name().c_str(), "gauge");
        EXPECT_FLOAT_EQ(msg->measurements()[0].value(), -1.1 * receive_count);

        // Counters only start publishing after the first call establishes a baseline
        if (receive_count > 0) {
          ASSERT_TRUE(msg->counters().size() == 1);
          EXPECT_STREQ(msg->counters()[0].name().c_str(), "ctr");
          EXPECT_FLOAT_EQ(msg->counters()[0].delta(), 1000);
          EXPECT_EQ(msg->counters()[0].elapsed_ms(), 1000);
        }

        ++receive_count;
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  EXPECT_EQ(receive_count, 0U);

  for (int i = 0; i < 10; ++i) {
    trellis::core::time::SetSimulatedTime(start + std::chrono::seconds{i});
    auto now = trellis::core::time::Now();
    pub.AddMeasurement(now, "gauge", -1.1 * i);
    pub.AddCounter(now, "ctr", 1000 * i);
    pub.Publish(now);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(receive_count, 10);
}
