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

#include <gtest/gtest.h>

#include "trellis/containers/multi_fifo.hpp"

using namespace trellis;

struct TestMsg {
  uint32_t x;
  uint32_t y;
  uint32_t z;
};

TEST(MultiFifoTests, BasicChecks) {
  containers::MultiFifo<2, TestMsg, std::string> f;
  ASSERT_EQ(f.Size<TestMsg>(), 0);
  ASSERT_EQ(f.Size<std::string>(), 0);
  // auto x = f.Pop<TestMsg>();
  f.Push<std::string>("test msg");
  ASSERT_EQ(f.Size<TestMsg>(), 0);
  ASSERT_EQ(f.Size<std::string>(), 1);

  f.Push<TestMsg>({1, 2, 3});
  ASSERT_EQ(f.Size<TestMsg>(), 1);
  ASSERT_EQ(f.Size<std::string>(), 1);

  TestMsg m = f.Pop<TestMsg>();
  ASSERT_EQ(f.Size<TestMsg>(), 0);
  ASSERT_EQ(f.Size<std::string>(), 1);
  ASSERT_EQ(m.x, 1);
  ASSERT_EQ(m.y, 2);
  ASSERT_EQ(m.z, 3);

  f.Push<std::string>("brown cow");
  auto s = f.Pop<std::string>();
  ASSERT_EQ(s, "test msg");
  s = f.Pop<std::string>();
  ASSERT_EQ(s, "brown cow");
}
