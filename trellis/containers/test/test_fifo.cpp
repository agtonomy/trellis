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

#include <algorithm>

#include "trellis/containers/fifo.hpp"

using namespace trellis;

namespace {
struct TestMsg {
  size_t x{0};
  size_t y{0};
  size_t z{0};
  bool operator==(const TestMsg& rhs) const { return (x == rhs.x) && (y == rhs.y) && (z == rhs.z); }
};

struct MoveCopyTest {
  MoveCopyTest() = default;
  MoveCopyTest(const MoveCopyTest& other) { ++copy_count; }
  MoveCopyTest(MoveCopyTest&& other) { ++move_count; }

  void operator=(const MoveCopyTest& other) { ++copy_count; }
  void operator=(MoveCopyTest&& other) { ++move_count; }

  static void ResetCounters() {
    copy_count = 0;
    move_count = 0;
  }

  static unsigned copy_count;
  static unsigned move_count;
};

unsigned MoveCopyTest::copy_count = 0;
unsigned MoveCopyTest::move_count = 0;

}  // namespace

TEST(FifoTests, FifoDoesntGrowPastLimit) {
  constexpr size_t kFifoSize = 10U;
  trellis::containers::Fifo<TestMsg, kFifoSize> fifo;

  // Push more than the FIFO holds and confirm size doesn't grow beyond kFifoSize
  for (size_t i = 0; i < kFifoSize * 2; ++i) {
    fifo.Push(TestMsg{i, i * 2, i * 3});
    ASSERT_EQ(fifo.Size(), std::min((1U + i), kFifoSize));
  }
}

TEST(FifoTests, NextReturnsTopOfQueue) {
  constexpr size_t kFifoSize = 10U;
  trellis::containers::Fifo<TestMsg, kFifoSize> fifo;
  for (size_t i = 0; i < kFifoSize; ++i) {
    fifo.Push({i, i * 2, i * 3});
  }
  ASSERT_EQ(fifo.Size(), kFifoSize);
  for (size_t i = 0; i < kFifoSize; ++i) {
    const auto& next = fifo.Next();
    ASSERT_EQ(next, TestMsg(i, i * 2, i * 3));
  }
}

TEST(FifoTests, NewestReturnsMostRecent) {
  constexpr size_t kFifoSize = 10U;
  trellis::containers::Fifo<TestMsg, kFifoSize> fifo;
  for (size_t i = 0; i < kFifoSize; ++i) {
    fifo.Push({i, i * 2, i * 3});
    const auto& newest = fifo.Newest();
    ASSERT_EQ(newest, TestMsg(i, i * 2, i * 3));
  }

  // Let's access the newest one again for good measure
  const auto& newest = fifo.Newest();
  ASSERT_EQ(newest, TestMsg(kFifoSize - 1, (kFifoSize - 1) * 2, (kFifoSize - 1) * 3));
}

TEST(FifoTests, MoveObjectsInAndOut) {
  MoveCopyTest::ResetCounters();
  constexpr size_t kFifoSize = 10U;

  trellis::containers::Fifo<MoveCopyTest, kFifoSize> fifo;

  MoveCopyTest dummy{};
  fifo.Push(std::move(dummy));

  MoveCopyTest next = fifo.Next();
  (void)next;

  ASSERT_EQ(MoveCopyTest::copy_count, 0);
  ASSERT_EQ(MoveCopyTest::move_count, 3);
}

TEST(FifoTests, CopyObjectsInAndMoveOut) {
  MoveCopyTest::ResetCounters();
  constexpr size_t kFifoSize = 10U;

  trellis::containers::Fifo<MoveCopyTest, kFifoSize> fifo;

  MoveCopyTest dummy{};
  fifo.Push(dummy);

  MoveCopyTest next = fifo.Next();
  (void)next;

  ASSERT_EQ(MoveCopyTest::copy_count, 1);
  ASSERT_EQ(MoveCopyTest::move_count, 2);
}

TEST(FifoTests, TemporaryObjectIn) {
  MoveCopyTest::ResetCounters();
  constexpr size_t kFifoSize = 10U;

  trellis::containers::Fifo<MoveCopyTest, kFifoSize> fifo;

  fifo.Push(MoveCopyTest{});

  MoveCopyTest next = fifo.Next();
  (void)next;

  ASSERT_EQ(MoveCopyTest::copy_count, 0);
  ASSERT_EQ(MoveCopyTest::move_count, 2);
}

TEST(FifoTests, NextOnEmptyFifoThrows) {
  constexpr size_t kFifoSize = 10U;
  trellis::containers::Fifo<TestMsg, kFifoSize> fifo;

  fifo.Push(TestMsg{1, 2, 3});

  (void)fifo.Next();
  EXPECT_THROW(fifo.Next(), std::runtime_error);
}

TEST(FifoTests, NewestBeforePushThrows) {
  constexpr size_t kFifoSize = 10U;
  trellis::containers::Fifo<TestMsg, kFifoSize> fifo;

  EXPECT_THROW(fifo.Newest(), std::runtime_error);
}

TEST(FifoTests, NewestAfterEmptyThrows) {
  constexpr size_t kFifoSize = 10U;
  trellis::containers::Fifo<TestMsg, kFifoSize> fifo;

  fifo.Push(TestMsg{1, 2, 3});
  ASSERT_EQ(fifo.Newest(), TestMsg(1, 2, 3));
  ASSERT_EQ(fifo.Next(), TestMsg(1, 2, 3));
  EXPECT_THROW(fifo.Newest(), std::runtime_error);
}
