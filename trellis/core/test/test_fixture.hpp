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

#ifndef TRELLIS_CORE_TEST_TEST_FIXTURE_HPP
#define TRELLIS_CORE_TEST_TEST_FIXTURE_HPP

#include <gtest/gtest.h>

#include <thread>

#include "trellis/core/node.hpp"

namespace trellis {
namespace core {
namespace test {

static constexpr unsigned kNumPubBuffers = 200;
static constexpr unsigned kTestDiscoveryInterval = 100;
static constexpr unsigned kTestDiscoveryTimeout = 200;

namespace {

std::string CreateConfig(unsigned num_pub_buffers, unsigned interval_value, unsigned timeout_value) {
  return fmt::format(R"(
    # Example configuration
    trellis:
      publisher:
        attributes:
          num_buffers: {}
      discovery:
        interval: {}
        sample_timeout: {}
        loopback_enabled: true
    )",
                     num_pub_buffers, interval_value, timeout_value);
}
}  // namespace

class TrellisFixture : public ::testing::Test {
 protected:
  // Arbitrary delay that seems to be sufficient.
  static constexpr auto kSendReceiveTime = std::chrono::milliseconds{100};

  TrellisFixture() = default;

  void SetUp() override {
    // Create a fresh node for each test to avoid discovery state pollution
    node_ = std::make_unique<trellis::core::Node>(
        "test_fixture",
        trellis::core::Config(YAML::Load(CreateConfig(kNumPubBuffers, kTestDiscoveryInterval, kTestDiscoveryTimeout))));
  }

  void TearDown() override {
    time::DisableSimulatedClock();
    Stop();
    if (runner_thread_.joinable()) {
      runner_thread_.join();
    }
    // Destroy the node to clean up all discovery state
    node_.reset();
  }

  static void WaitForDiscovery() { std::this_thread::sleep_for(std::chrono::milliseconds(kTestDiscoveryTimeout)); }
  static void WaitForSendReceive() { std::this_thread::sleep_for(kSendReceiveTime); }
  void Stop() { node_->Stop(); }
  void StartRunnerThread() {
    runner_thread_ = std::thread([this]() { node_->Run(); });
  }

  /// @brief Get a reference to the node for use in tests
  /// Note, should be called after SetUp() (in test body)
  trellis::core::Node& GetNode() { return *node_; }

 private:
  std::unique_ptr<trellis::core::Node> node_;
  std::thread runner_thread_;
};

}  // namespace test
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TEST_TEST_FIXTURE_HPP
