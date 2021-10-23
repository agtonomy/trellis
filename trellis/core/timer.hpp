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

#include "error_code.hpp"
#include "event_loop.hpp"

namespace trellis {
namespace core {

class TimerImpl {
 public:
  using Callback = std::function<void(void)>;
  enum Type { kOneShot = 0, kPeriodic };
  TimerImpl(EventLoop loop, Type type, Callback callback, unsigned interval_ms, unsigned delay_ms)
      : loop_{loop},
        type_{type},
        callback_{callback},
        interval_ms_{interval_ms},
        delay_ms_(delay_ms),
        timer_{*loop, asio::chrono::milliseconds(delay_ms)} {
    KickOff();
  }

  void Reset() {
    Stop();
    Reload();
    KickOff();
  }

  void Stop() { timer_.cancel(); }

 private:
  void KickOff() {
    timer_.async_wait([this](const trellis::core::error_code& e) {
      if (e) {
        return;
      }
      Fire();
    });
  }

  void Reload() {
    if (type_ == kPeriodic) {
      // We calculate the new expiration time based on the last expiration
      // rather than "now" in order to avoid drift due to jitter error
      timer_.expires_at(timer_.expiry() + asio::chrono::milliseconds(interval_ms_));
    } else if (type_ == kOneShot) {
      // If we're reloading a one shot timer we simply reload to now + our delay time
      timer_.expires_after(asio::chrono::milliseconds(delay_ms_));
    }
  }

  void Fire() {
    callback_();
    if (type_ != kOneShot) {
      Reload();
      KickOff();
    }
  }
  EventLoop loop_;
  const Type type_;
  const Callback callback_;
  const unsigned interval_ms_;
  const unsigned delay_ms_;
  asio::steady_timer timer_;
};

using Timer = std::shared_ptr<TimerImpl>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIMER_HPP
