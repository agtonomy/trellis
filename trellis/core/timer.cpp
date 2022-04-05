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

#include "timer.hpp"

namespace trellis {
namespace core {

TimerImpl::TimerImpl(EventLoop loop, Type type, Callback callback, unsigned interval_ms, unsigned delay_ms)
    : loop_{loop},
      type_{type},
      callback_{callback},
      interval_ms_{interval_ms},
      delay_ms_(delay_ms),
      timer_{*loop, asio::chrono::milliseconds(delay_ms)} {
  KickOff();
}

void TimerImpl::Reset() {
  Stop();
  Reload();
  KickOff();
}

void TimerImpl::Stop() { timer_.cancel(); }

bool TimerImpl::Expired() const { return expired_; }

void TimerImpl::KickOff() {
  expired_ = false;
  timer_.async_wait([this](const trellis::core::error_code& e) {
    expired_ = true;
    if (e) {
      return;
    }
    Fire();
  });
}

void TimerImpl::Reload() {
  if (type_ == kPeriodic) {
    // We calculate the new expiration time based on the last expiration
    // rather than "now" in order to avoid drift due to jitter error
    timer_.expires_at(timer_.expiry() + asio::chrono::milliseconds(interval_ms_));
  } else if (type_ == kOneShot) {
    // If we're reloading a one shot timer we simply reload to now + our delay time
    timer_.expires_after(asio::chrono::milliseconds(delay_ms_));
  }
}

void TimerImpl::Fire() {
  callback_();
  if (type_ != kOneShot) {
    Reload();
    KickOff();
  }
}

}  // namespace core
}  // namespace trellis
