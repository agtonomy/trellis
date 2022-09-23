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

#include "trellis/core/health_monitor.hpp"

namespace {

void AddUpdate(trellis::core::HealthHistory& status, trellis::core::HealthState state, unsigned code,
               const std::string& description) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::HealthStatus update;
  *update.mutable_timestamp() = trellis::core::time::TimePointToTimestamp(trellis::core::time::Now());
  // increment our fake timestamp so that every update will have a new timestamp
  trellis::core::time::IncrementSimulatedTime(std::chrono::milliseconds(200));
  update.set_health_state(state);
  update.set_status_code(code);
  update.set_status_description(description);
  *status.add_history() = update;
}

}  // namespace

using namespace trellis::core;

TEST(TrellisHealthMonitor, UpdatesTriggerCallbacks) {
  static const std::vector<std::pair<std::string, trellis::core::HealthMonitor::Event>> expected_events{
      {"node_foo", trellis::core::HealthMonitor::Event::kNodeTransitionToHealthy},
      {"node_bar", trellis::core::HealthMonitor::Event::kNodeTransitionToUnhealthy},
      {"node_foo", trellis::core::HealthMonitor::Event::kNodeTransitionToHealthy},
      {"node_foo", trellis::core::HealthMonitor::Event::kNodeTransitionToUnhealthy},
      {"node_bar", trellis::core::HealthMonitor::Event::kNodeTransitionToUnhealthy},
      {"node_bar", trellis::core::HealthMonitor::Event::kNodeTransitionToUnhealthy},
      {"node_foo", trellis::core::HealthMonitor::Event::kNodeHealthUpdateLost},
  };
  static unsigned callback_count{0};

  trellis::core::Config config;
  trellis::core::EventLoop loop{nullptr};
  trellis::core::HealthMonitor monitor{
      loop, config, [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) { return nullptr; },
      [this](const std::string& topic, trellis::core::SubscriberImpl<trellis::core::HealthHistory>::Callback) {
        return nullptr;
      },
      [](const std::string& node_name, trellis::core::HealthMonitor::Event event) {
        ASSERT_TRUE(callback_count < expected_events.size());
        ASSERT_EQ(event, expected_events[callback_count].second);
        ASSERT_EQ(node_name, expected_events[callback_count].first);
        ++callback_count;
      }};

  trellis::core::HealthHistory status_node1;
  status_node1.set_name("node_foo");
  trellis::core::HealthHistory status_node2;
  status_node2.set_name("node_bar");

  ASSERT_FALSE(monitor.HasHealthInfo("node_foo"));
  ASSERT_FALSE(monitor.HasHealthInfo("node_bar"));

  // We should get a callback immediately since this is the first update from this node. Our health is also good.
  AddUpdate(status_node1, trellis::core::HealthState::HEALTH_STATE_NORMAL, 0, "");
  monitor.NewUpdate(status_node1);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 1);
  ASSERT_TRUE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_FALSE(monitor.HasHealthInfo("node_bar"));

  // We should get another callback immediately since this is the first update from this node. Our health is less than
  // good.
  AddUpdate(status_node2, trellis::core::HealthState::HEALTH_STATE_DEGRADED, 10, "Hey I'm a little sick.");
  monitor.NewUpdate(status_node2);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 1);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").size(), 1);
  ASSERT_FALSE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_TRUE(monitor.HasHealthInfo("node_bar"));

  // We get another update from node1 but nothing changed, so no callbacks
  monitor.NewUpdate(status_node1);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 1);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").size(), 1);
  ASSERT_FALSE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_TRUE(monitor.HasHealthInfo("node_bar"));

  // We get another update from node1 with another update. Even though we're still healthy it's a new update, so we get
  // a callback
  AddUpdate(status_node1, trellis::core::HealthState::HEALTH_STATE_NORMAL, 0, "");
  monitor.NewUpdate(status_node1);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 2);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").size(), 1);
  ASSERT_FALSE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_TRUE(monitor.HasHealthInfo("node_bar"));

  // We get yet another update from node1, and we're no longer healthy. We expect a callback.
  AddUpdate(status_node1, trellis::core::HealthState::HEALTH_STATE_CRITICAL, 20, "Stuff is real bad!");
  monitor.NewUpdate(status_node1);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 3);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").size(), 1);
  ASSERT_FALSE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_TRUE(monitor.HasHealthInfo("node_bar"));

  // Node2 is still not healthly, but has a new update, so we shall get a callback
  AddUpdate(status_node2, trellis::core::HealthState::HEALTH_STATE_DEGRADED, 30, "Still not feeling well over here");
  monitor.NewUpdate(status_node2);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 3);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").size(), 2);
  ASSERT_FALSE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_TRUE(monitor.HasHealthInfo("node_bar"));

  // Identical update, but it'll have a new timestamp so it will trigger a callback
  AddUpdate(status_node2, trellis::core::HealthState::HEALTH_STATE_DEGRADED, 30, "Still not feeling well over here");
  monitor.NewUpdate(status_node2);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").size(), 3);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").size(), 3);
  ASSERT_FALSE(monitor.IsAllHealthy());
  ASSERT_TRUE(monitor.HasHealthInfo("node_foo"));
  ASSERT_TRUE(monitor.HasHealthInfo("node_bar"));

  // Now let's make sure all the values are correct
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(0).health_state(), trellis::core::HealthState::HEALTH_STATE_NORMAL);
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(1).health_state(), trellis::core::HealthState::HEALTH_STATE_NORMAL);
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(2).health_state(),
            trellis::core::HealthState::HEALTH_STATE_CRITICAL);

  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(0).health_state(),
            trellis::core::HealthState::HEALTH_STATE_DEGRADED);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(1).health_state(),
            trellis::core::HealthState::HEALTH_STATE_DEGRADED);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(2).health_state(),
            trellis::core::HealthState::HEALTH_STATE_DEGRADED);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(0).status_code(), 0);
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(1).status_code(), 0);
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(2).status_code(), 20);

  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(0).status_code(), 10);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(1).status_code(), 30);
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(2).status_code(), 30);

  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(0).status_description(), "");
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(1).status_description(), "");
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(2).status_description(), "Stuff is real bad!");

  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(0).status_description(), "Hey I'm a little sick.");
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(1).status_description(), "Still not feeling well over here");
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(2).status_description(), "Still not feeling well over here");

  // Double check latest update yields the same thing as the last element in the historical list
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(2).health_state(),
            monitor.GetLastHealthUpdate("node_foo").health_state());
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(2).status_code(),
            monitor.GetLastHealthUpdate("node_foo").status_code());
  ASSERT_EQ(monitor.GetHealthHistory("node_foo").at(2).status_description(),
            monitor.GetLastHealthUpdate("node_foo").status_description());

  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(2).health_state(),
            monitor.GetLastHealthUpdate("node_bar").health_state());
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(2).status_code(),
            monitor.GetLastHealthUpdate("node_bar").status_code());
  ASSERT_EQ(monitor.GetHealthHistory("node_bar").at(2).status_description(),
            monitor.GetLastHealthUpdate("node_bar").status_description());

  // Now let's say the watchdog tripped for node_foo
  monitor.WatchdogExpired("node_foo");
  ASSERT_EQ(monitor.GetLastHealthUpdate("node_foo").health_state(), trellis::core::HealthState::HEALTH_STATE_LOST);

  ASSERT_EQ(callback_count, expected_events.size());
}
