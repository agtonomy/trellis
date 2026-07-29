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
#include <optional>

#include "trellis/core/error_code.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/time.hpp"
#include "trellis/core/timer_options.hpp"
#include "trellis/core/timer_registry.hpp"

namespace trellis {
namespace core {

/**
 * TimerImpl is the base class for one-shot and periodic timers
 *
 * A timer's expiry is held in whichever domain time::Now() reports, and which clock drives the timer is
 * decided once, when it is constructed. That makes the simulated clock's enabled state part of a timer's
 * construction contract:
 *
 * - Enabling or disabling the simulated clock while timers exist leaves those timers holding an expiry in
 *   the previous domain. Node::UpdateSimulatedClock re-anchors every simulation-driven timer on its first
 *   forward jump, which covers the usual case of enabling the clock during startup, but constructing
 *   timers on one side of the switch and relying on them from the other is not supported.
 * - IsSimulationDriven() is what keeps the two apart afterwards: a timer asio is driving is never stepped
 *   by the simulated clock, because comparing its expiry against simulated time would span two unrelated
 *   epochs.
 * - A management timer keeps a real asio timer even under a simulated clock, so a process running one
 *   clock still has timers on the other. The clock is chosen per timer, not per process.
 *
 * Reset(), Stop() and Fire() carry the same thread affinity as the asio timer underneath them: they belong on
 * the thread running this timer's event loop, or on any thread while that loop is not running. They rewrite the
 * expiry and the underlying deadline without synchronizing, so calling one from elsewhere races whatever the
 * loop is doing with the same timer. Re-arming from another thread means posting the call onto the loop.
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
   * Not needed to be called externally except if simulated time is active.
   *
   * @param horizon how far ahead the caller has advanced, used to decide how many interval slots this
   * timer has missed. Under a real clock that is simply now, which is the default. A caller stepping a
   * simulated clock passes the time it is stepping to, which is ahead of the slot being fired -- the
   * callback still receives its own slot time, but the rearm needs to know about the slots in between.
   */
  void Fire(const time::TimePoint& horizon = time::Now());

  /**
   * GetExpiry the time this timer is next due to fire
   *
   * Reported in whichever domain time::Now() is reporting, so it is comparable with time::Now() and with
   * a simulated clock's readings, but not across a change of clock. See IsSimulationDriven().
   */
  time::TimePoint GetExpiry() const { return next_expiry_; }

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
   * GetOverrunCount returns the number of interval slots this timer has missed
   *
   * A slot is missed when the timer is dispatched more than one interval after the expiry it was
   * scheduled for. This measures dispatch lateness, not how long the callback takes: the fire time it
   * is compared against is captured before the callback runs.
   *
   * @return the number of missed slots
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
   * ArmInitialExpiry set where a simulation-driven timer first comes due, and do nothing otherwise
   *
   * Called by the most-derived constructor for the same reason as RegisterWithLoop: how far out a timer
   * starts is RestartDelayMs(), which does not exist until the derived subobject does. A subclass that
   * forgets this call and is driven by the simulated clock starts due at the epoch, so it fires on the
   * first advance.
   */
  void ArmInitialExpiry();

  /**
   * DeregisterFromLoop remove this timer from its registry
   *
   * Called at the top of the most-derived destructor, before that subobject is gone, for the same reason: an entry that
   * outlives it is an entry whose virtual methods cannot be called. Idempotent.
   */
  void DeregisterFromLoop();

  static std::unique_ptr<asio::steady_timer> CreateSteadyTimer(EventLoop loop, unsigned delay_ms, TimerKind kind);

  /**
   * RestartDelayMs how long after now a restarted timer should next fire - implemented by child classes
   *
   * Reload() continues an existing schedule and Reset() begins a new one, which is a difference in what
   * the caller is asking for rather than in the timer's state -- a timer being reset may equally have
   * been stopped for a minute or be running normally. Reload() therefore anchors on the previous expiry
   * so a running timer keeps its phase, and Reset() anchors on now, which is what this supplies.
   *
   * Anchoring a restart on the previous expiry instead breaks in both directions: a timer stopped long
   * ago comes back with an expiry deep in the past and fires once per missed interval to catch up, and a
   * timer reset more often than its interval has its expiry pushed steadily further out until it stops
   * firing at all.
   */
  virtual unsigned RestartDelayMs() const = 0;

  /**
   * SetNextExpiry records when this timer is next due and mirrors that deadline onto the underlying asio
   * timer, if one is driving it
   *
   * The expiry must be a reading from this timer's own clock; see NowInOwnDomain().
   */
  void SetNextExpiry(const time::TimePoint& expiry);

  /**
   * ClockDomainsAgree returns true if this timer's expiry is comparable with time::Now()
   *
   * False only for a timer asio is driving while a simulated clock is enabled: its expiry is a steady clock
   * reading while time::Now() reports simulated time. Both are a time::TimePoint, so nothing catches the
   * mistake at compile time.
   */
  bool ClockDomainsAgree() const { return IsSimulationDriven() || !time::IsSimulatedClockEnabled(); }

  /**
   * NowInOwnDomain the current time on the same clock as this timer's expiry
   *
   * Anything offsetting from or measuring against this timer's expiry starts here rather than at time::Now(),
   * which is only the same reading while ClockDomainsAgree().
   */
  time::TimePoint NowInOwnDomain() const;

  /**
   * ClearFireState marks the timer as neither fired nor cancelled, ready to be armed again
   */
  void ClearFireState() {
    did_fire_ = false;
    cancelled_ = false;
  }

  /**
   * ShouldFire returns true if the timer should fire - can be overridden
   */
  virtual bool ShouldFire() const { return true; }

  /**
   * OnFired is called after the timer fires - can be overridden
   * @param fire_time the current time captured immediately after firing and before executing the callback
   * @param horizon how far ahead the caller has advanced; see Fire()
   */
  virtual void OnFired(const time::TimePoint& fire_time, const time::TimePoint& horizon) {
    (void)fire_time;
    (void)horizon;
  }

  EventLoop loop_;
  const Callback callback_;
  const unsigned interval_ms_;
  const unsigned delay_ms_;
  const TimerKind kind_;
  std::unique_ptr<asio::steady_timer> timer_;
  // When this timer is next due, on this timer's own clock. The single source of truth: timer_, when present,
  // is the mechanism that wakes us at this deadline rather than where it is stored. Deliberately not atomic
  // like the flags below -- it is only ever written from the loop thread, which is where the methods that
  // touch it belong.
  time::TimePoint next_expiry_{};
  std::atomic<bool> did_fire_{false};
  std::atomic<bool> cancelled_{false};
  TimerRegistry::RegistrationHandle registration_{TimerRegistry::kInvalidRegistrationHandle};
};

/**
 * OneShotTimerImpl is a timer that fires only once
 */
class OneShotTimerImpl : public TimerImpl {
 public:
  /**
   * Construct a one-shot timer and start it immediately
   *
   * @param loop the event loop used to process the timer
   * @param callback the function to call when the timer expires
   * @param delay_ms how long from now the timer should fire
   * @param kind whether this is an application timer or one of trellis's own housekeeping timers
   */
  OneShotTimerImpl(EventLoop loop, Callback callback, unsigned delay_ms, TimerKind kind = TimerKind::kApplication);

  ~OneShotTimerImpl() override;

  /**
   * GetOverrunCount always zero: an overrun is a missed interval slot, and a one-shot has no interval
   */
  uint64_t GetOverrunCount() const override { return 0; }

  /**
   * GetAndResetSchedLatencyStats always empty, for the same reason as GetOverrunCount
   */
  SchedLatencyStats GetAndResetSchedLatencyStats() override { return {}; }

  /// GetType always kOneShot
  Type GetType() const override { return kOneShot; }

 protected:
  // A one shot timer's whole schedule is its delay, so restarting means waiting that delay again
  unsigned RestartDelayMs() const override { return delay_ms_; }
  bool ShouldFire() const override;
};

/**
 * PeriodicTimerImpl is a timer that fires repeatedly at a fixed interval
 */
class PeriodicTimerImpl : public TimerImpl {
 public:
  /**
   * Construct a periodic timer and start it immediately
   *
   * @param loop the event loop used to process the timer
   * @param callback the function to call each time the timer expires
   * @param interval_ms the interval between callbacks
   * @param delay_ms an initial delay before the first callback, separate from the interval (0 is immediate)
   * @param kind whether this is an application timer or one of trellis's own housekeeping timers
   * @param rearm_policy overrides the loop's policy for this timer alone; unset means use the loop's
   */
  PeriodicTimerImpl(EventLoop loop, Callback callback, unsigned interval_ms, unsigned delay_ms = 0,
                    TimerKind kind = TimerKind::kApplication, std::optional<RearmPolicy> rearm_policy = std::nullopt);

  /// As above, with the interval and callback swapped
  PeriodicTimerImpl(EventLoop loop, unsigned interval_ms, Callback callback, unsigned delay_ms = 0,
                    TimerKind kind = TimerKind::kApplication, std::optional<RearmPolicy> rearm_policy = std::nullopt);

  ~PeriodicTimerImpl() override;

  /// GetType always kPeriodic
  Type GetType() const override { return kPeriodic; }

  /**
   * GetOverrunCount the number of interval slots missed since construction
   *
   * @see TimerImpl::GetOverrunCount, and RearmPolicy for how missed slots are counted under each policy
   */
  uint64_t GetOverrunCount() const override { return overrun_count_.load(); }

  SchedLatencyStats GetAndResetSchedLatencyStats() override;

 protected:
  // Not delay_ms_: that is the one-off offset before the first tick, and most periodic timers leave it at
  // zero, so restarting on it would fire a spurious callback immediately every time
  unsigned RestartDelayMs() const override { return interval_ms_; }
  void OnFired(const time::TimePoint& fire_time, const time::TimePoint& horizon) override;

 private:
  /**
   * Reload places the next expiry one interval on from the last, or further under RearmPolicy::kSkipAligned
   *
   * Only a periodic timer has a schedule to continue, which is why this is not on TimerImpl. Contrast
   * RestartDelayMs(), which serves Reset() and begins a new schedule rather than continuing this one.
   *
   * @param horizon how far ahead the caller has advanced; see Fire()
   */
  void Reload(const time::TimePoint& horizon);

  // Resolved once at construction: the timer's own override if it has one, otherwise its loop's policy
  const RearmPolicy rearm_policy_;
  // How many missed slots the most recent Reload() decided this dispatch answers for, which depends on the
  // policy: every slot dropped under kSkipAligned, or just this one under kCatchUp, whose remaining slots
  // arrive as their own dispatches. A member rather than a return value so OnFired can read it without
  // Reload having to report through a signature shared with nothing else.
  uint64_t slots_accounted_{0};
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
