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

namespace trellis {
namespace core {

class TimerImpl {
 public:
  using Callback = std::function<void(const time::TimePoint&)>;
  enum Type { kOneShot = 0, kPeriodic };

  /**
   * Construct a new timer and start it immediately
   *
   * @param loop the event loop used to process the timer
   * @param type the timer type (one shot or periodic)
   * @param callback the function to callwhen the timer expires
   * @param interval_ms the timer internval in milliseconds
   * @param delay_ms an initial delay which can be separate from the timer interval (0 is immediate)
   */
  TimerImpl(EventLoop loop, Type type, Callback callback, unsigned interval_ms, unsigned delay_ms);

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
   * Fire fire the timer
   *
   * Not needed to be called externally except if simulated time is active
   */
  void Fire();

  /**
   * GetExpiry get the expiry time
   */
  time::TimePoint GetExpiry() const;

  Type GetType() const { return type_; }

  bool IsCancelled() { return cancelled_; }

 private:
  void KickOff();
  void Reload();
  bool SimulationActive() const;

  static std::unique_ptr<asio::steady_timer> CreateSteadyTimer(EventLoop loop, unsigned delay_ms);

  EventLoop loop_;
  const Type type_;
  const Callback callback_;
  const unsigned interval_ms_;
  const unsigned delay_ms_;
  std::unique_ptr<asio::steady_timer> timer_;
  time::TimePoint last_fire_time_{time::Now()};
  std::atomic<bool> did_fire_{false};
  std::atomic<bool> cancelled_{false};
};

using Timer = std::shared_ptr<TimerImpl>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIMER_HPP
