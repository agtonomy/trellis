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

#include "trellis/core/node.hpp"

namespace trellis {
namespace core {
namespace test {

class TrellisFixture : public ::testing::Test {
 protected:
  static constexpr unsigned discovery_settling_time_ms{1100};
  TrellisFixture() : node_{"test_fixture"} {
    // allow pub/sub from same process, etc
    eCAL::Util::EnableLoopback(true);
  }

  ~TrellisFixture() {
    Stop();
    if (runner_thread_.joinable()) {
      runner_thread_.join();
    }
  }
  static void WaitForDiscovery() { std::this_thread::sleep_for(std::chrono::milliseconds(discovery_settling_time_ms)); }
  void Stop() { node_.Stop(); }
  void StartRunnerThread() {
    runner_thread_ = std::thread([this]() { node_.Run(); });
  }
  trellis::core::Node node_;
  std::thread runner_thread_;
};

}  // namespace test
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TEST_TEST_FIXTURE_HPP
