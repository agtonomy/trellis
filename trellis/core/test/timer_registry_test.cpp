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

#include "trellis/core/timer_registry.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "trellis/core/test/test_fixture.hpp"
#include "trellis/core/timer.hpp"

using trellis::core::EventLoop;
using trellis::core::OneShotTimerImpl;
using trellis::core::PeriodicTimerImpl;
using trellis::core::TimerKind;
using trellis::core::TimerRegistry;
using trellis::core::test::TrellisFixture;
using trellis::core::time::TimePoint;

namespace {

constexpr unsigned kIntervalMs{5};

// Long enough that the timer never fires during a test, for tests that only care about registration
constexpr unsigned kNeverFiresIntervalMs{100000};

// A callback that does nothing, for tests that only care about registration
const auto kNoopCallback = [](const TimePoint&) {};

// A callback that takes longer than the intervals used here, to force overruns
const auto kSlowCallback = [](const TimePoint&) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); };

/// Look up the handle a timer registered under, or kInvalidRegistrationHandle if it is not registered
TimerRegistry::RegistrationHandle HandleFor(const TimerRegistry& registry, const trellis::core::TimerImpl* timer) {
  for (const auto& entry : registry.GetEntries()) {
    if (entry.timer == timer) {
      return entry.handle;
    }
  }
  return TimerRegistry::kInvalidRegistrationHandle;
}

}  // namespace

TEST(TrellisTimerRegistry, DefaultEventLoopCarriesNoRegistry) {
  EventLoop loop;
  ASSERT_EQ(loop.GetTimerRegistry(), nullptr);
}

TEST(TrellisTimerRegistry, DefaultEventLoopStillDrivesTimers) {
  EventLoop loop;
  unsigned fire_count{0};
  auto timer =
      std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, [&fire_count](const TimePoint&) { ++fire_count; });

  loop.RunFor(std::chrono::milliseconds(50));

  // An unregistered timer is unmeasured, not inert
  ASSERT_GT(fire_count, 0U);
}

TEST(TrellisTimerRegistry, TimerRegistersOnConstructionAndDeregistersOnDestruction) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};
  ASSERT_TRUE(registry->GetEntries().empty());

  TimerRegistry::RegistrationHandle handle{TimerRegistry::kInvalidRegistrationHandle};
  {
    auto timer = std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, kNoopCallback);

    const auto entries = registry->GetEntries();
    ASSERT_EQ(entries.size(), 1U);
    ASSERT_EQ(entries.front().timer, timer.get());
    ASSERT_EQ(entries.front().loop, &(*loop));
    ASSERT_EQ(entries.front().kind, TimerKind::kApplication);
    handle = entries.front().handle;
    ASSERT_TRUE(registry->Contains(handle));
  }

  ASSERT_FALSE(registry->Contains(handle));
  ASSERT_TRUE(registry->GetEntries().empty());
}

TEST(TrellisTimerRegistry, StackAllocatedTimerRegistersAndDeregisters) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};

  {
    PeriodicTimerImpl timer{loop, kIntervalMs, kNoopCallback};

    const auto entries = registry->GetEntries();
    ASSERT_EQ(entries.size(), 1U);
    ASSERT_EQ(entries.front().timer, &timer);
  }

  ASSERT_TRUE(registry->GetEntries().empty());
}

TEST(TrellisTimerRegistry, ManagementTimersRegisterWithTheirKind) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};

  auto timer =
      std::make_shared<PeriodicTimerImpl>(loop, kNeverFiresIntervalMs, kNoopCallback, 0u, TimerKind::kManagement);

  const auto entries = registry->GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  ASSERT_EQ(entries.front().kind, TimerKind::kManagement);
  ASSERT_EQ(timer->GetKind(), TimerKind::kManagement);
}

TEST(TrellisTimerRegistry, TimerDestroyedFromWithinAnotherTimersCallbackDeregisters) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};

  auto victim = std::make_shared<PeriodicTimerImpl>(loop, kNeverFiresIntervalMs, kNoopCallback);
  const auto victim_handle = HandleFor(*registry, victim.get());
  ASSERT_NE(victim_handle, TimerRegistry::kInvalidRegistrationHandle);

  auto killer = std::make_shared<OneShotTimerImpl>(loop, [&victim](const TimePoint&) { victim.reset(); }, kIntervalMs);
  loop.RunFor(std::chrono::milliseconds(100));

  ASSERT_EQ(victim, nullptr);
  ASSERT_FALSE(registry->Contains(victim_handle));
}

TEST(TrellisTimerRegistry, RegistryIsSharedAcrossCopiesOfTheLoop) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};
  EventLoop copy{loop};

  auto timer = std::make_shared<PeriodicTimerImpl>(copy, kIntervalMs, kNoopCallback);

  const auto entries = loop.GetTimerRegistry()->GetEntries();
  ASSERT_EQ(entries.size(), 1U);
  ASSERT_EQ(entries.front().timer, timer.get());
  ASSERT_EQ(entries.front().loop, &(*loop));
}

TEST(TrellisTimerRegistry, EntriesAreTaggedWithTheirOwningLoop) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop first{registry};
  EventLoop second{registry};

  auto first_timer = std::make_shared<PeriodicTimerImpl>(first, kIntervalMs, kNoopCallback);
  auto second_timer = std::make_shared<PeriodicTimerImpl>(second, kIntervalMs, kNoopCallback);

  const auto entries = registry->GetEntries();
  ASSERT_EQ(entries.size(), 2U);
  for (const auto& entry : entries) {
    if (entry.timer == first_timer.get()) {
      ASSERT_EQ(entry.loop, &(*first));
    } else if (entry.timer == second_timer.get()) {
      ASSERT_EQ(entry.loop, &(*second));
    } else {
      ADD_FAILURE() << "registry holds an entry for an unknown timer";
    }
  }
}

TEST(TrellisTimerRegistry, HandlesAreNotReusedAfterDeregistration) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};

  TimerRegistry::RegistrationHandle first_handle{TimerRegistry::kInvalidRegistrationHandle};
  {
    auto timer = std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, kNoopCallback);
    first_handle = HandleFor(*registry, timer.get());
  }
  auto replacement = std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, kNoopCallback);

  // A replacement may land on the freed timer's address, so a stale handle must never match it
  ASSERT_NE(HandleFor(*registry, replacement.get()), first_handle);
  ASSERT_FALSE(registry->Contains(first_handle));
}

TEST(TrellisTimerRegistry, RegistrationNeverYieldsTheInvalidSentinel) {
  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};

  // An entry registered under the sentinel could never be removed again, since Remove and Contains both reject it, so a
  // timer that died would leave a dangling pointer behind
  ASSERT_FALSE(registry->Contains(TimerRegistry::kInvalidRegistrationHandle));
  for (unsigned i = 0; i < 32; ++i) {
    auto timer = std::make_shared<PeriodicTimerImpl>(loop, kNeverFiresIntervalMs, kNoopCallback);
    ASSERT_NE(HandleFor(*registry, timer.get()), TimerRegistry::kInvalidRegistrationHandle);
  }
  ASSERT_TRUE(registry->GetEntries().empty());
  ASSERT_FALSE(registry->Contains(TimerRegistry::kInvalidRegistrationHandle));
}

TEST(TrellisTimerRegistry, ConcurrentRegistrationFromMultipleThreads) {
  static constexpr unsigned kThreadCount{4};
  static constexpr unsigned kTimersPerThread{50};

  auto registry = std::make_shared<TimerRegistry>();
  EventLoop loop{registry};

  // Assertions cannot be made from these threads: a fatal one only returns from the lambda, silently truncating that
  // thread's loop, and the terminal check below is satisfied by fewer timers too
  std::atomic<unsigned> unregistered_count{0};
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&loop, registry, &unregistered_count]() {
      for (unsigned j = 0; j < kTimersPerThread; ++j) {
        auto timer = std::make_shared<PeriodicTimerImpl>(loop, kIntervalMs, kNoopCallback);
        if (HandleFor(*registry, timer.get()) == TimerRegistry::kInvalidRegistrationHandle) {
          ++unregistered_count;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  ASSERT_EQ(unregistered_count.load(), 0U);
  // Every timer was destroyed at the end of its iteration, so RAII deregistration must have emptied the registry
  ASSERT_TRUE(registry->GetEntries().empty());
}

TEST_F(TrellisFixture, NodeTimersAreRegisteredAndDeregistered) {
  const auto registry = GetNode().GetEventLoop().GetTimerRegistry();
  ASSERT_NE(registry, nullptr);
  const auto baseline = registry->GetEntries().size();

  TimerRegistry::RegistrationHandle handle{TimerRegistry::kInvalidRegistrationHandle};
  {
    auto timer = GetNode().CreateTimer(kIntervalMs, kNoopCallback);
    handle = HandleFor(*registry, timer.get());
    ASSERT_NE(handle, TimerRegistry::kInvalidRegistrationHandle);
    ASSERT_EQ(registry->GetEntries().size(), baseline + 1);
  }

  // Releasing the timer removes the entry, so the registry cannot grow without bound
  ASSERT_FALSE(registry->Contains(handle));
  ASSERT_EQ(registry->GetEntries().size(), baseline);
}

TEST_F(TrellisFixture, TimerCreatedDirectlyOnTheNodeLoopIsRegistered) {
  const auto registry = GetNode().GetEventLoop().GetTimerRegistry();
  auto timer = std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), kIntervalMs, kNoopCallback);

  // Registration follows the loop, so a timer built directly against it is tracked too
  ASSERT_NE(HandleFor(*registry, timer.get()), TimerRegistry::kInvalidRegistrationHandle);
}

TEST_F(TrellisFixture, OverrunsFromATimerCreatedDirectlyOnTheNodeLoopReachNodeMetrics) {
  StartRunnerThread();
  const auto baseline = GetNode().GetTimerOverrunCount();

  // 10ms interval with a 25ms callback guarantees overruns
  auto timer = std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), 10u, kSlowCallback);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // The timer's asio handler holds a raw pointer to it, so the loop must finish before it goes out of scope
  StopAndJoinRunnerThread();

  const auto own_overruns = timer->GetOverrunCount();
  ASSERT_GT(own_overruns, 0U);
  // Attributable: the node's total accounts for exactly this timer's contribution on top of the baseline
  ASSERT_EQ(GetNode().GetTimerOverrunCount(), baseline + own_overruns);
}

TEST_F(TrellisFixture, SchedLatencyStatsFromATimerCreatedDirectlyOnTheNodeLoopAreCollectedAndReset) {
  StartRunnerThread();

  auto timer = std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), kIntervalMs, kNoopCallback);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  StopAndJoinRunnerThread();

  const auto stats = GetNode().GetAndResetTimerSchedLatencyStats();
  ASSERT_GT(stats.count, 0U);

  // Attributable: the collection above could only have reset this timer's accumulators by walking this timer
  ASSERT_EQ(timer->GetAndResetSchedLatencyStats().count, 0U);
}

TEST_F(TrellisFixture, ManagementTimerOverrunsAreExcludedFromNodeMetrics) {
  StartRunnerThread();
  const auto baseline = GetNode().GetTimerOverrunCount();

  auto timer =
      std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), 10u, kSlowCallback, 0u, TimerKind::kManagement);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  StopAndJoinRunnerThread();

  ASSERT_GT(timer->GetOverrunCount(), 0U);
  ASSERT_EQ(GetNode().GetTimerOverrunCount(), baseline);
}

TEST_F(TrellisFixture, ManagementTimerSchedLatencySamplesAreNotResetByNodeCollection) {
  StartRunnerThread();

  auto timer = std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), kIntervalMs, kNoopCallback, 0u,
                                                   TimerKind::kManagement);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  StopAndJoinRunnerThread();

  GetNode().GetAndResetTimerSchedLatencyStats();

  // The node resets what it collects, so samples surviving that call prove it left this timer alone
  ASSERT_GT(timer->GetAndResetSchedLatencyStats().count, 0U);
}

TEST_F(TrellisFixture, MetricCollectionFromWithinATimerCallbackIsConsistent) {
  StartRunnerThread();

  // Collecting from a timer callback puts the collector on the loop thread, which is the affinity these getters require
  // and how a node reaches them in practice. Also exercises collecting while other timers on the same loop are firing.
  auto worker = std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), 1u, kNoopCallback);

  std::atomic<uint64_t> collected{0};
  std::atomic<unsigned> inconsistent{0};
  auto collector = GetNode().CreateTimer(10u, [this, &collected, &inconsistent](const TimePoint&) {
    const auto stats = GetNode().GetAndResetTimerSchedLatencyStats();
    collected += stats.count;
    // A count and a total that belong to one another
    const bool consistent =
        stats.count == 0 ? stats.total_us == 0
                         : stats.mean_us == static_cast<double>(stats.total_us) / static_cast<double>(stats.count);
    if (!consistent) {
      ++inconsistent;
    }
    GetNode().GetTimerOverrunCount();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  StopAndJoinRunnerThread();

  ASSERT_EQ(inconsistent.load(), 0U);
  ASSERT_GT(collected.load(), 0U);
}

TEST_F(TrellisFixture, CollectingMetricsFromAnotherThreadWhileTheLoopRunsThrows) {
  StartRunnerThread();
  const auto registry = GetNode().GetEventLoop().GetTimerRegistry();
  auto timer = std::make_shared<PeriodicTimerImpl>(GetNode().GetEventLoop(), 1u, kNoopCallback);
  const auto handle = HandleFor(*registry, timer.get());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // This thread is not the one firing the timer, so collecting here would race its accumulators
  ASSERT_THROW(GetNode().GetAndResetTimerSchedLatencyStats(), std::runtime_error);

  // Releasing the timer takes the registry lock, so this also confirms that unwinding out of the collection above did
  // not leave that lock held
  StopAndJoinRunnerThread();
  timer.reset();
  ASSERT_FALSE(registry->Contains(handle));
}
