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
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

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

// Stop() marks a loop stopped at once, while the handler that called it is still running. An object torn down off the
// loop's thread used to take Stopped() as leave to touch what that handler may be using.
TEST(TrellisEventLoop, IsBeingRunUntilTheRunCallReturnsEvenOnceStopped) {
  trellis::core::EventLoop loop;
  const trellis::core::EventLoop copy = loop;
  EXPECT_FALSE(copy.IsBeingRun());
  EXPECT_FALSE(copy.HasStoppedRunning());

  std::promise<void> inside;
  std::promise<void> release;
  auto released = release.get_future();
  asio::post(*loop, [&loop, &inside, &released]() {
    loop.Stop();
    inside.set_value();
    released.wait();
  });
  std::thread runner([&loop]() { loop.Run(); });
  inside.get_future().wait();
  EXPECT_TRUE(copy.Stopped());
  EXPECT_TRUE(copy.IsBeingRun()) << "the loop is stopped, but its handler is still running";
  EXPECT_FALSE(copy.HasStoppedRunning()) << "the loop is stopped, but its handler is still running";

  release.set_value();
  runner.join();
  EXPECT_FALSE(copy.IsBeingRun());
  EXPECT_TRUE(copy.HasStoppedRunning());
}

// A loop that has never run may start running at any moment, so what belongs to its thread can't be touched elsewhere
// until it has been stopped
TEST(TrellisEventLoop, HasStoppedRunningOnlyOnceStopped) {
  trellis::core::EventLoop loop;
  EXPECT_TRUE(loop.Stopped()) << "a loop that has never run reports itself stopped";
  EXPECT_FALSE(loop.HasStoppedRunning());

  loop.Stop();
  EXPECT_TRUE(loop.HasStoppedRunning());

  bool ran = false;
  asio::post(*loop, [&ran]() { ran = true; });
  loop.Run();
  EXPECT_FALSE(ran) << "a stopped loop runs nothing, which is what lets HasStoppedRunning() stay true";
  EXPECT_TRUE(loop.HasStoppedRunning());
}

TEST(TrellisEventLoop, IsBeingRunInsideEveryRunMethod) {
  trellis::core::EventLoop loop;
  std::vector<bool> seen;
  const auto post_check = [&loop, &seen]() {
    asio::post(*loop, [&loop, &seen]() { seen.push_back(loop.IsBeingRun()); });
  };
  post_check();
  loop.RunOne();
  post_check();
  loop.PollOne();
  post_check();
  loop.RunFor(std::chrono::milliseconds{10});
  post_check();
  asio::post(*loop, [&loop]() { loop.Stop(); });
  loop.Run();
  EXPECT_EQ(seen, (std::vector<bool>{true, true, true, true}));
  EXPECT_FALSE(loop.IsBeingRun());
}

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

TEST(TrellisRunOnEventLoop, ReturnsTheResultWhileTheStopConditionIsFalse) {
  const auto running = RunningLoop{};
  EXPECT_EQ(trellis::core::RunOnEventLoop(running.loop(), []() { return 7; }, []() { return false; }), 7);
}

TEST(TrellisRunOnEventLoop, GivesUpOnceTheStopConditionIsTrue) {
  // Never run, so the work stays queued and only the stop condition can end the wait.
  trellis::core::EventLoop loop;
  auto checks = 0;
  const auto result = trellis::core::RunOnEventLoop(
      loop, []() { return 7; }, [&checks]() { return ++checks == 3; }, std::chrono::milliseconds{1});
  EXPECT_EQ(result, std::nullopt);
  EXPECT_EQ(checks, 3);
}

// A stopped loop never runs queued work again, so waiting for it would never end.
TEST(TrellisRunOnEventLoop, ThrowsOnceTheLoopIsStoppedInsteadOfWaiting) {
  trellis::core::EventLoop loop;
  loop.Stop();
  auto ran = std::make_shared<std::atomic<bool>>(false);
  std::promise<bool> threw;
  auto waiter = std::thread{[&loop, ran, &threw]() {
    try {
      trellis::core::RunOnEventLoop(loop, [ran]() { *ran = true; });
      threw.set_value(false);
    } catch (const trellis::core::EventLoopStopped&) {
      threw.set_value(true);
    }
  }};
  auto result = threw.get_future();
  if (result.wait_for(std::chrono::seconds{2}) != std::future_status::ready) {
    waiter.detach();
    FAIL() << "RunOnEventLoop waited on a stopped loop";
  }
  waiter.join();
  EXPECT_TRUE(result.get());
  EXPECT_FALSE(*ran);
}
