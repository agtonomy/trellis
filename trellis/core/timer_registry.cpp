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

#include <stdexcept>

namespace trellis::core {

TimerRegistry::RegistrationHandle TimerRegistry::Add(TimerImpl* timer, asio::io_context* loop, TimerKind kind) {
  std::lock_guard guard(mutex_);
  const auto handle = next_handle_++;
  const auto inserted =
      entries_.try_emplace(handle, Entry{.handle = handle, .timer = timer, .loop = loop, .kind = kind});
  if (!inserted.second) {
    // Two live timers sharing one entry: one would go untracked, and the other would leave the entry behind when it
    // died. Unreachable while handles are 64 bits wide, but too damaging to let pass quietly if it ever were.
    throw std::runtime_error("Timer registration handles exhausted, this should not happen");
  }
  return handle;
}

void TimerRegistry::Remove(RegistrationHandle handle) {
  if (handle == kInvalidRegistrationHandle) {
    return;
  }
  std::lock_guard guard(mutex_);
  entries_.erase(handle);
}

bool TimerRegistry::Contains(RegistrationHandle handle) const {
  if (handle == kInvalidRegistrationHandle) {
    return false;
  }
  std::lock_guard guard(mutex_);
  return entries_.contains(handle);
}

std::vector<TimerRegistry::Entry> TimerRegistry::GetEntries() const {
  std::lock_guard guard(mutex_);
  std::vector<Entry> entries;
  entries.reserve(entries_.size());
  for (const auto& entry : entries_ | std::views::values) {
    entries.push_back(entry);
  }
  return entries;
}

}  // namespace trellis::core
