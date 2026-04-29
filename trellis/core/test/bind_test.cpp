/*
 * Copyright (C) 2026 Agtonomy
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

#include "trellis/core/bind.hpp"

#include <gmock/gmock.h>

#include <functional>
#include <string>

namespace trellis::core {
namespace {

class Counter {
 public:
  void Add(int x) { sum_ += x; }
  void AddTwo(int a, int b) { sum_ += a + b; }
  int GetSumPlus(int extra) const { return sum_ + extra; }
  void AppendName(const std::string& name) { name_ += name; }
  void Noop() { called_ = true; }

  int sum() const { return sum_; }
  const std::string& name() const { return name_; }
  bool called() const { return called_; }

  auto MakeAddCallback() { return BindThis(&Counter::Add, this); }
  auto MakeGetSumPlusCallback() const { return BindThis(&Counter::GetSumPlus, this); }

 private:
  int sum_ = 0;
  std::string name_;
  bool called_ = false;
};

}  // namespace

TEST(BindThis, CallsNonConstMethodWithArg) {
  Counter c;
  auto fn = BindThis(&Counter::Add, &c);
  fn(5);
  fn(7);
  EXPECT_EQ(c.sum(), 12);
}

TEST(BindThis, CallsConstMethod) {
  Counter c;
  BindThis(&Counter::Add, &c)(10);
  auto fn = BindThis(&Counter::GetSumPlus, &c);
  EXPECT_EQ(fn(3), 13);
}

TEST(BindThis, CallsConstMethodOnConstPointer) {
  Counter c;
  BindThis(&Counter::Add, &c)(4);
  const Counter* cptr = &c;
  auto fn = BindThis(&Counter::GetSumPlus, cptr);
  EXPECT_EQ(fn(1), 5);
}

TEST(BindThis, ForwardsMultipleArgs) {
  Counter c;
  auto fn = BindThis(&Counter::AddTwo, &c);
  fn(3, 4);
  EXPECT_EQ(c.sum(), 7);
}

TEST(BindThis, AcceptsConstReferenceArg) {
  Counter c;
  auto fn = BindThis(&Counter::AppendName, &c);
  const std::string s = "hello";
  fn(s);
  EXPECT_EQ(c.name(), "hello");
}

TEST(BindThis, ZeroArgMethod) {
  Counter c;
  auto fn = BindThis(&Counter::Noop, &c);
  fn();
  EXPECT_TRUE(c.called());
}

TEST(BindThis, BindsFromEnclosingMember) {
  Counter c;
  auto fn = c.MakeAddCallback();
  fn(42);
  EXPECT_EQ(c.sum(), 42);
}

TEST(BindThis, BindsFromConstEnclosingMember) {
  Counter c;
  BindThis(&Counter::Add, &c)(7);
  auto fn = c.MakeGetSumPlusCallback();
  EXPECT_EQ(fn(3), 10);
}

namespace {

struct NoexceptMethods {
  void ThrowingAdd(int) {}
  void NothrowAdd(int) noexcept {}
};

}  // namespace

TEST(BindThis, PropagatesNoexcept) {
  NoexceptMethods obj;
  auto nothrow_fn = BindThis(&NoexceptMethods::NothrowAdd, &obj);
  auto throwing_fn = BindThis(&NoexceptMethods::ThrowingAdd, &obj);
  static_assert(noexcept(nothrow_fn(0)), "Binding a noexcept method should produce a noexcept callable");
  static_assert(!noexcept(throwing_fn(0)), "Binding a non-noexcept method must not claim noexcept");
}

TEST(BindThis, BaseClassPointerBindsDerivedMethod) {
  struct Base {
    int value = 0;
  };
  struct Derived : Base {
    void Set(int v) { value = v; }
  };
  Derived d;
  auto fn = BindThis(&Derived::Set, &d);
  fn(5);
  EXPECT_EQ(d.value, 5);
}

}  // namespace trellis::core
