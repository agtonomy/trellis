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

#include "trellis/containers/memory_pool.hpp"

namespace {
struct TestMsg {
  TestMsg(int foo, float bar) : foo(foo), bar(bar) { ++constructor_count; };
  TestMsg() { ++constructor_count; }
  ~TestMsg() { ++destructor_count; }
  static void ClearCounters() {
    constructor_count = 0;
    destructor_count = 0;
  }
  int foo{1337};
  float bar{42.0};
  static unsigned constructor_count;
  static unsigned destructor_count;
};

unsigned TestMsg::constructor_count = 0;
unsigned TestMsg::destructor_count = 0;

constexpr size_t kTestSlotSize = 10U;
}  // namespace

TEST(MemoryPoolTests, DefaultConstruction) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
}

TEST(MemoryPoolTests, AllocateAndFree) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  void* ptr = pool.Allocate();
  ASSERT_NE(ptr, nullptr);
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize - 1);
  ASSERT_NO_THROW(pool.Free(ptr));
}

TEST(MemoryPoolTests, ExhaustPoolAllocations) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  std::set<void*> ptrs;

  for (size_t i = 0; i < kTestSlotSize; ++i) {
    auto ptr = pool.Allocate();
    ASSERT_NE(ptr, nullptr);
    ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize - i - 1);
    ptrs.insert(ptr);
  }

  // Pool is full
  EXPECT_THROW(pool.Allocate(), std::bad_alloc);

  // All ptrs were unique
  ASSERT_EQ(kTestSlotSize, ptrs.size());

  unsigned free_count{0};
  for (auto ptr : ptrs) {
    pool.Free(ptr);
    ASSERT_EQ(pool.FreeSlotsRemaining(), ++free_count);
  }
}

TEST(MemoryPoolTests, DestructNullPtrIsOkay) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  pool.Destruct(nullptr);
}

TEST(MemoryPoolTests, FreeInvalidPointerThrows) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  uint8_t* ptr = reinterpret_cast<uint8_t*>(pool.Allocate());
  EXPECT_THROW(pool.Free(ptr + 1), std::invalid_argument);
}

TEST(MemoryPoolTests, DoubleFreeThrows) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  void* ptr = pool.Allocate();
  ASSERT_NE(ptr, nullptr);
  pool.Free(ptr);
  EXPECT_THROW(pool.Free(ptr), std::runtime_error);
}

TEST(MemoryPoolTests, SharedPointerAllocatesAndFrees) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
  {
    // Construct with default constructor
    auto ptr = pool.ConstructSharedPointer();
    ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize - 1);
    ASSERT_EQ(ptr->foo, 1337);
    ASSERT_EQ(ptr->bar, 42.0);
  }
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
}

TEST(MemoryPoolTests, UniquePointerAllocatesAndFrees) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
  {
    // Construct with default constructor
    auto ptr = pool.ConstructUniquePointer();
    ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize - 1);
    ASSERT_EQ(ptr->foo, 1337);
    ASSERT_EQ(ptr->bar, 42.0);
  }
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
}

TEST(MemoryPoolTests, UniquePointerConstructorForwarding) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
  {
    // Construct with default constructor
    auto ptr = pool.ConstructUniquePointer(TestMsg{4321, 24.0});
    ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize - 1);
    ASSERT_EQ(ptr->foo, 4321);
    ASSERT_EQ(ptr->bar, 24.0);
  }
  ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize);
}

TEST(MemoryPoolTests, ExhaustPoolWithUniquePtr) {
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  std::vector<trellis::containers::MemoryPool<TestMsg>::UniquePtr> ptrs;
  for (size_t i = 0; i < kTestSlotSize; ++i) {
    auto ptr = pool.ConstructUniquePointer();
    ASSERT_NE(ptr, nullptr);
    ptrs.push_back(std::move(ptr));
  }

  // Now the pool is exhausted
  EXPECT_THROW(pool.ConstructUniquePointer(), std::bad_alloc);
}

TEST(MemoryPoolTests, ConstructorAndDestructorCalled) {
  TestMsg::ClearCounters();
  trellis::containers::MemoryPool<TestMsg> pool{kTestSlotSize};
  {
    auto ptr = pool.ConstructUniquePointer(13, 2.0);
    ASSERT_EQ(pool.FreeSlotsRemaining(), kTestSlotSize - 1);
    ASSERT_EQ(TestMsg::constructor_count, 1);
    ASSERT_EQ(TestMsg::destructor_count, 0);
  }
  ASSERT_EQ(TestMsg::destructor_count, 1);
}
