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
