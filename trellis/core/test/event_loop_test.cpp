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

#include "trellis/core/event_loop.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>

TEST(TrellisEventLoop, DefaultConstruct) { ASSERT_NO_THROW(trellis::core::EventLoop{}); }

TEST(TrellisEventLoop, DefaultToStoppedState) {
  trellis::core::EventLoop loop;
  ASSERT_TRUE(loop.Stopped());
}

TEST(TrellisEventLoop, TransitionToRunStateAfterRunOneCall) {
  trellis::core::EventLoop loop;

  // Initially stopped
  ASSERT_TRUE(loop.Stopped());

  // Start running in a thread
  std::thread thread([&loop]() mutable { loop.RunOne(); });

  // Give thread time to run
  unsigned count = 0;
  while (loop.Stopped() && ++count < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // No longer stopped
  ASSERT_FALSE(loop.Stopped());
  loop.Stop();
  ASSERT_TRUE(loop.Stopped());
  if (thread.joinable()) {
    thread.join();
  }
}

TEST(TrellisEventLoop, TransitionToRunStateAfterRunCall) {
  trellis::core::EventLoop loop;

  // Initially stopped
  ASSERT_TRUE(loop.Stopped());

  // Start running in a thread
  std::thread thread([&loop]() mutable { loop.Run(); });

  // Give thread time to run
  unsigned count = 0;
  while (loop.Stopped() && ++count < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // No longer stopped
  ASSERT_FALSE(loop.Stopped());
  loop.Stop();
  ASSERT_TRUE(loop.Stopped());
  if (thread.joinable()) {
    thread.join();
  }
}

TEST(TrellisEventLoop, TransitionToRunStateAfterRunForCall) {
  trellis::core::EventLoop loop;

  // Initially stopped
  ASSERT_TRUE(loop.Stopped());

  // Start running in a thread
  std::thread thread([&loop]() mutable { loop.RunFor(std::chrono::milliseconds(5000)); });

  // Give thread time to run
  unsigned count = 0;
  while (loop.Stopped() && ++count < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // No longer stopped
  ASSERT_FALSE(loop.Stopped());
  loop.Stop();
  ASSERT_TRUE(loop.Stopped());
  if (thread.joinable()) {
    thread.join();
  }
}

TEST(TrellisEventLoop, CopiesOfEventLoopShareState) {
  trellis::core::EventLoop loop;
  trellis::core::EventLoop loop2 = loop;
  ASSERT_TRUE(loop.Stopped());
  ASSERT_TRUE(loop2.Stopped());

  // Start running in a thread
  std::thread thread([&loop]() mutable { loop.RunOne(); });

  // Give thread time to run
  unsigned count = 0;
  while (loop.Stopped() && ++count < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // No longer stopped
  ASSERT_FALSE(loop.Stopped());
  ASSERT_FALSE(loop2.Stopped());
  loop.Stop();
  ASSERT_TRUE(loop.Stopped());
  ASSERT_TRUE(loop2.Stopped());
  if (thread.joinable()) {
    thread.join();
  }
}

namespace {

/// Runs a loop on a thread of its own for the lifetime of the object.
class RunningLoop {
 public:
  RunningLoop() : thread_{[this]() { loop_.Run(); }} {}
  ~RunningLoop() {
    loop_.Stop();
    thread_.join();
  }
  const trellis::core::EventLoop& loop() const { return loop_; }
  std::thread::id thread_id() const { return thread_.get_id(); }

 private:
  trellis::core::EventLoop loop_;
  std::thread thread_;
};

}  // namespace

TEST(TrellisRunOnEventLoop, RunsTheWorkOnTheLoopThread) {
  const auto running = RunningLoop{};
  const auto ran_on = trellis::core::RunOnEventLoop(running.loop(), []() { return std::this_thread::get_id(); });
  EXPECT_EQ(ran_on, running.thread_id());
}

TEST(TrellisRunOnEventLoop, RunsVoidWork) {
  const auto running = RunningLoop{};
  auto ran = false;
  trellis::core::RunOnEventLoop(running.loop(), [&ran]() { ran = true; });
  EXPECT_TRUE(ran);
}

TEST(TrellisRunOnEventLoop, RethrowsWhatTheWorkThrows) {
  const auto running = RunningLoop{};
  EXPECT_THROW(trellis::core::RunOnEventLoop(running.loop(), []() -> int { throw std::runtime_error{"failed"}; }),
               std::runtime_error);
}

TEST(TrellisRunOnEventLoop, RunsInlineWhenCalledFromTheLoop) {
  trellis::core::EventLoop loop;
  auto result = std::optional<int>{};
  // Run on the loop, which is the only thread that would run the inner work: posting it would wait forever.
  asio::post(*loop, [&loop, &result]() { result = trellis::core::RunOnEventLoop(loop, []() { return 7; }); });
  loop.RunOne();
  EXPECT_EQ(result, 7);
}

TEST(TrellisRunOnEventLoop, ReturnsTheResultWhileNotStopping) {
  const auto running = RunningLoop{};
  const auto stopping = std::atomic<bool>{false};
  EXPECT_EQ(trellis::core::RunOnEventLoop(running.loop(), []() { return 7; }, stopping), 7);
}

TEST(TrellisRunOnEventLoop, GivesUpOnceStoppingIsSet) {
  // Never run, so the work stays queued and only the flag can end the wait.
  trellis::core::EventLoop loop;
  auto stopping = std::atomic<bool>{false};
  auto setter = std::thread{[&stopping]() {
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    stopping = true;
  }};
  const auto result = trellis::core::RunOnEventLoop(loop, []() { return 7; }, stopping, std::chrono::milliseconds{1});
  setter.join();
  EXPECT_EQ(result, std::nullopt);
}
