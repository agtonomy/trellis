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

#ifndef TRELLIS_CORE_TIMER_HPP
#define TRELLIS_CORE_TIMER_HPP

#include <asio.hpp>

#include "trellis/core/error_code.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/time.hpp"
#include "trellis/core/timer_registry.hpp"

namespace trellis {
namespace core {

/**
 * TimerImpl is the base class for one-shot and periodic timers
 */
class TimerImpl {
 public:
  using Callback = std::function<void(const time::TimePoint&)>;
  enum Type { kOneShot = 0, kPeriodic };

  virtual ~TimerImpl();

  // Non-copyable, non-movable
  TimerImpl(const TimerImpl&) = delete;
  TimerImpl& operator=(const TimerImpl&) = delete;
  TimerImpl(TimerImpl&&) = delete;
  TimerImpl& operator=(TimerImpl&&) = delete;

  /**
   * Reset resets the timer, which extends the expiration
   */
  void Reset();

  /**
   * Stop stops the timer callback from firing
   */
  void Stop();

  /**
   * Expired returns true if the timer is expired
   *
   * Useful for one-shot timers
   *
   * @return a boolean representing expired state
   */
  bool Expired() const;

  /**
   * GetTimeInterval get the time interval for the timer (in milliseconds)
   */
  std::chrono::milliseconds GetTimeInterval() const;

  /**
   * Fire fires the timer
   *
   * Not needed to be called externally except if simulated time is active
   */
  void Fire();

  /**
   * GetExpiry get the expiry time
   */
  virtual time::TimePoint GetExpiry() const = 0;

  /**
   * GetType get the timer type
   */
  virtual Type GetType() const = 0;

  bool IsCancelled() { return cancelled_; }

  /**
   * IsSimulationDriven returns true if this timer only fires when the simulated clock is advanced
   *
   * A simulation-driven timer has no underlying asio timer, so its expiry is derived from time::Now() and is directly
   * comparable with simulated time. Every other timer is driven by asio and its expiry is a steady clock reading, which
   * is not comparable with a simulated clock even though both are a time::TimePoint. Callers stepping the simulated
   * clock must consult this before comparing against GetExpiry().
   *
   * This is fixed when the timer is constructed and never changes afterwards. It depends on whether the simulated clock
   * was already enabled at that moment, so two otherwise identical timers differ if they were built on opposite sides
   * of time::EnableSimulatedClock(). A management timer is never simulation-driven.
   *
   * @return true if the simulated clock drives this timer, false if asio does
   */
  bool IsSimulationDriven() const { return timer_ == nullptr; }

  /**
   * GetKind returns whether this is an application timer or one of trellis's own housekeeping timers
   */
  TimerKind GetKind() const { return kind_; }

  /**
   * GetOverrunCount returns the number of times the callback execution time exceeded the timer interval
   *
   * @return the number of overruns
   */
  virtual uint64_t GetOverrunCount() const = 0;

  struct SchedLatencyStats {
    int64_t max_us{0};
    int64_t total_us{0};
    double mean_us{0.0};
    uint64_t count{0};
  };

  /**
   * GetAndResetSchedLatencyStats returns scheduling latency stats (actual fire time minus expected expiry)
   * observed since the last call, then resets the internal accumulators.
   *
   * Must be called on the thread running the timer's event loop, or while that loop is not running at all; throws
   * otherwise. The timer accumulates into these figures as it fires, so a caller elsewhere would race those writes and
   * could come away with a total belonging to a different count than the one it read. Collecting from a timer callback
   * satisfies this, which is how it is normally reached.
   *
   * Note that whoever calls this consumes the samples: a second collector sees only what accumulated after the first.
   *
   * @throws std::runtime_error if called from a thread other than the one running the timer's loop while it is running
   */
  virtual SchedLatencyStats GetAndResetSchedLatencyStats() = 0;

 protected:
  /**
   * Construct a new timer and start it immediately
   *
   * @param loop the event loop used to process the timer
   * @param callback the function to call when the timer expires
   * @param interval_ms the timer interval in milliseconds
   * @param delay_ms an initial delay which can be separate from the timer interval (0 is immediate)
   * @param kind whether this is an application timer or one of trellis's own housekeeping timers
   */
  TimerImpl(EventLoop loop, Callback callback, unsigned interval_ms, unsigned delay_ms,
            TimerKind kind = TimerKind::kApplication);

  void KickOff();

  /**
   * RegisterWithLoop add this timer to the registry carried by its event loop, if it carries one
   *
   * Called by the most-derived constructor rather than by this one, because a registered timer is immediately reachable
   * by anything walking the registry, and the virtual methods such a walker calls do not exist until the derived
   * subobject has been constructed. A subclass that forgets this call is simply untracked; one that forgets
   * DeregisterFromLoop would leave the registry holding freed memory, so the base destructor repeats that call as a
   * backstop.
   *
   * This makes OneShotTimerImpl and PeriodicTimerImpl safe because they are the most-derived types. Subclassing either
   * of them reopens the same window one level down -- the base's constructor would register before the new subobject
   * exists, and its destructor would deregister after that subobject is gone. A further subclass must therefore take
   * over both calls itself.
   */
  void RegisterWithLoop();

  /**
   * DeregisterFromLoop remove this timer from its registry
   *
   * Called at the top of the most-derived destructor, before that subobject is gone, for the same reason: an entry that
   * outlives it is an entry whose virtual methods cannot be called. Idempotent.
   */
  void DeregisterFromLoop();

  static std::unique_ptr<asio::steady_timer> CreateSteadyTimer(EventLoop loop, unsigned delay_ms, TimerKind kind);

  /**
   * Reload resets the timer expiry - implemented by child classes
   */
  virtual void Reload() = 0;

  /**
   * ShouldFire returns true if the timer should fire - can be overridden
   */
  virtual bool ShouldFire() const { return true; }

  /**
   * OnFired is called after the timer fires - can be overridden
   * @param fire_time the current time captured immediately after firing and before executing the callback
   */
  virtual void OnFired(const time::TimePoint& fire_time) { (void)fire_time; }

  EventLoop loop_;
  const Callback callback_;
  const unsigned interval_ms_;
  const unsigned delay_ms_;
  const TimerKind kind_;
  std::unique_ptr<asio::steady_timer> timer_;
  time::TimePoint last_fire_time_{time::Now()};
  std::atomic<bool> did_fire_{false};
  std::atomic<bool> cancelled_{false};
  TimerRegistry::RegistrationHandle registration_{TimerRegistry::kInvalidRegistrationHandle};
};

/**
 * OneShotTimerImpl is a timer that fires only once
 */
class OneShotTimerImpl : public TimerImpl {
 public:
  OneShotTimerImpl(EventLoop loop, Callback callback, unsigned delay_ms, TimerKind kind = TimerKind::kApplication);
  ~OneShotTimerImpl() override;

  time::TimePoint GetExpiry() const override;
  uint64_t GetOverrunCount() const override { return 0; }  // no overruns for one-shot timers
  SchedLatencyStats GetAndResetSchedLatencyStats() override { return {}; }
  Type GetType() const override { return kOneShot; }

 protected:
  void Reload() override;
  bool ShouldFire() const override;
};

/**
 * PeriodicTimerImpl is a timer that fires repeatedly at a fixed interval
 */
class PeriodicTimerImpl : public TimerImpl {
 public:
  PeriodicTimerImpl(EventLoop loop, Callback callback, unsigned interval_ms, unsigned delay_ms = 0,
                    TimerKind kind = TimerKind::kApplication);
  // alternative argument list
  PeriodicTimerImpl(EventLoop loop, unsigned interval_ms, Callback callback, unsigned delay_ms = 0,
                    TimerKind kind = TimerKind::kApplication);
  ~PeriodicTimerImpl() override;

  time::TimePoint GetExpiry() const override;
  Type GetType() const override { return kPeriodic; }
  uint64_t GetOverrunCount() const override { return overrun_count_.load(); }
  SchedLatencyStats GetAndResetSchedLatencyStats() override;

 protected:
  void Reload() override;
  void OnFired(const time::TimePoint& now) override;

 private:
  std::atomic<uint64_t> overrun_count_{0};

  // Written as the timer fires and read and reset by the collector. Deliberately unsynchronized: both run on the loop
  // thread, which GetAndResetSchedLatencyStats requires and enforces. Synchronizing them instead would put a lock on
  // the fire path to serialize a thread against itself.
  int64_t max_sched_latency_us_{0};
  int64_t total_sched_latency_us_{0};
  uint64_t sched_latency_count_{0};
};

using Timer = std::shared_ptr<TimerImpl>;
using OneShotTimer = std::shared_ptr<OneShotTimerImpl>;
using PeriodicTimer = std::shared_ptr<PeriodicTimerImpl>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIMER_HPP
