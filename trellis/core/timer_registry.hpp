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

#ifndef TRELLIS_CORE_TIMER_REGISTRY_HPP_
#define TRELLIS_CORE_TIMER_REGISTRY_HPP_

#include <asio.hpp>
#include <cstdint>
#include <mutex>
#include <ranges>
#include <unordered_map>
#include <vector>

namespace trellis::core {

class TimerImpl;

/**
 * TimerKind distinguishes timers belonging to the application's timeline from the framework's own housekeeping timers
 *
 * The dividing line is not who constructs the timer, but what its cadence is tied to.
 *
 * kApplication covers any timer whose schedule is meaningful relative to the data the application is processing. That
 * includes timers an application creates for itself, and also the ones the framework creates on its behalf, such as
 * health reporting, metrics publication and subscriber watchdogs. Those are paced by the application's own activity and
 * are expected to advance with it. A watchdog shows why most clearly: under a simulated clock it has to move with that
 * clock, or it would either never expire or expire at wall-clock moments bearing no relation to the data. These are the
 * timers a simulated clock drives, and the ones a node reports metrics for, since their scheduling latency is a real
 * signal about whether the application's event loop is keeping up.
 *
 * kManagement covers timers that service the process regardless of what it happens to be processing: discovery
 * housekeeping, per-subscriber statistics bookkeeping, request timeouts and file flushing. Their cadence has no
 * relationship to the data, so they keep running on wall time even when a simulated clock is active, and they stay out
 * of the application-facing metrics because their latency says nothing about application health. They are tracked all
 * the same.
 *
 * kApplication is the default, which makes management opt in: a timer that merely services the process must say so.
 */
enum class TimerKind { kApplication = 0, kManagement };

/**
 * TimerRegistry keeps track of the timers driven by one or more event loops
 *
 * A timer adds itself once fully constructed and removes itself as it starts being destroyed, so the registry never
 * holds an entry for a timer that is not safe to use, and never requires a pruning pass.
 *
 * Entries are keyed by an opaque handle rather than by the timer's address. A raw pointer is unavoidable -- a timer
 * must register itself, and timers may be stack allocated or held by value, so no owning or weak pointer to them exists
 * -- but an address is recycled by the allocator, so a caller holding a stale pointer could otherwise match an
 * unrelated timer that happens to have landed at the same address. A handle is never reused: the generator is 64 bits
 * wide, which cannot be exhausted by any real process, and that width is what the no-reuse guarantee rests on. Handles
 * are also monotonic, so comparing two of them orders the timers by creation.
 *
 * All methods are thread-safe. The lock is taken on timer construction and destruction and while reading entries, never
 * on the timer fire path.
 */
class TimerRegistry {
 public:
  using RegistrationHandle = uint64_t;
  static constexpr RegistrationHandle kInvalidRegistrationHandle = 0;

  /**
   * Entry a registered timer, the event loop that drives it, and how it should be treated
   *
   * The loop is recorded so that callers can attribute a timer to the loop it runs on, which a single registry shared
   * by several loops requires.
   */
  struct Entry {
    RegistrationHandle handle{kInvalidRegistrationHandle};
    TimerImpl* timer{nullptr};
    asio::io_context* loop{nullptr};
    TimerKind kind{TimerKind::kApplication};
  };

  /**
   * Add register a timer
   *
   * @param timer the timer to register
   * @param loop the io_context that drives the timer
   * @param kind how the timer should be treated by metrics collection and by a simulated clock
   *
   * @return the handle identifying this registration
   */
  RegistrationHandle Add(TimerImpl* timer, asio::io_context* loop, TimerKind kind);

  /**
   * Remove deregister a timer
   *
   * Does nothing for kInvalidRegistrationHandle or a handle that is no longer registered, so calling it more than once
   * is harmless.
   *
   * @param handle the handle returned by Add
   */
  void Remove(RegistrationHandle handle);

  /**
   * Contains check whether a registration is still live
   *
   * Callers holding entries read earlier use this to confirm a timer still exists before dereferencing it, since a user
   * callback may have destroyed it in the meantime.
   *
   * @param handle the handle to look up
   *
   * @return true if the handle is registered, false otherwise
   */
  bool Contains(RegistrationHandle handle) const;

  /**
   * ForEach invoke a function for every registered timer, with the lock held
   *
   * Holding the lock means an entry cannot be erased midway through the call, which is what makes dereferencing
   * Entry::timer safe. It says nothing about a timer's own internals, so anything the function reads through that
   * pointer must be safe to read while the timer's loop thread is running.
   *
   * In exchange the function must not construct or destroy a timer, and must not call back into the registry: the mutex
   * is not recursive, so either would deadlock. Note this cannot be enforced -- Entry::timer is deliberately non-const
   * so that callers can collect and reset counters, and the methods they reach are virtual, so an override could
   * violate the contract. Callers that need to run user callbacks must use GetEntries instead.
   *
   * Const because it does not modify the registry, even though callers use it to mutate the timers it refers to.
   *
   * @param fn invoked as fn(const Entry&) for each registered timer
   */
  template <typename Fn>
  void ForEach(Fn&& fn) const {
    std::lock_guard guard(mutex_);
    for (const auto& entry : entries_ | std::views::values) {
      fn(entry);
    }
  }

  /**
   * GetEntries retrieve a copy of the registered entries
   *
   * For callers that cannot hold the lock across their work because they run user callbacks, which may in turn create
   * or destroy timers and re-enter the registry. Such a caller must re-check each entry with Contains before
   * dereferencing it, since the copy goes stale as soon as a callback runs.
   *
   * @return the registered entries
   */
  std::vector<Entry> GetEntries() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<RegistrationHandle, Entry> entries_;
  RegistrationHandle next_handle_{1};  ///< Monotonically increasing handle generator; 0 is reserved as the sentinel
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_TIMER_REGISTRY_HPP_
