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

#include <chrono>
#include <future>
#include <thread>
#include <type_traits>

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
        ::testing::UnitTest::GetInstance()->current_test_info()->name(),
        trellis::core::Config(YAML::Load(CreateConfig(kNumPubBuffers, kTestDiscoveryInterval, kTestDiscoveryTimeout))));
  }

  void TearDown() override {
    // Join first: the loop reads the simulated clock flag.
    StopAndJoinRunnerThread();
    time::DisableSimulatedClock();
    // Destroy the node to clean up all discovery state
    node_.reset();
  }

  static void WaitForDiscovery() { std::this_thread::sleep_for(std::chrono::milliseconds(kTestDiscoveryTimeout)); }
  static void WaitForSendReceive() { std::this_thread::sleep_for(kSendReceiveTime); }
  void Stop() { node_->Stop(); }
  void StartRunnerThread() {
    runner_thread_ = std::thread([this]() { node_->Run(); });
  }

  /// @brief Stop the node and wait for the runner thread to finish the handler it is executing
  ///
  /// A timer's asio completion handler holds a raw pointer to the timer, and TimerImpl::Fire() reads members after the
  /// user callback returns. Any test that lets a timer go out of scope while the loop may still be inside that timer's
  /// handler must call this first, otherwise the handler resumes on a destroyed object.
  void StopAndJoinRunnerThread() {
    Stop();
    if (runner_thread_.joinable()) {
      runner_thread_.join();
    }
  }

  /**
   * @brief Run `fn` on the node's event loop and wait for it.
   *
   * Use it for whatever the loop owns: reading what a callback wrote, or calling something documented as loop-only,
   * such as Inbox::GetMessages(), Outbox::UpdateMsgs() or TimerImpl::Stop(). The runner thread must be running.
   *
   * @param fn the work to run
   * @return what `fn` returns
   */
  template <typename Fn>
  auto RunOnLoop(Fn&& fn) {
    std::packaged_task<std::invoke_result_t<Fn&>()> task{std::forward<Fn>(fn)};
    auto result = task.get_future();
    asio::post(*node_->GetEventLoop(), [&task]() { task(); });
    return result.get();
  }

  /**
   * @brief Poll `predicate` on this thread until it holds or `timeout` passes.
   *
   * @param predicate what to wait for; it runs on this thread, so it should read atomics
   * @param timeout how long to wait
   * @return whether `predicate` held
   */
  template <typename Predicate>
  static bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
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
