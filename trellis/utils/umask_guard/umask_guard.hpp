/*
 * Copyright (C) 2025 Agtonomy
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

#ifndef TRELLIS_UTILS_UMASK_GUARD_UMASK_GUARD_HPP_
#define TRELLIS_UTILS_UMASK_GUARD_UMASK_GUARD_HPP_

#include <sys/stat.h>

#include <mutex>

namespace trellis::utils {

/**
 * @brief RAII guard for thread-safe umask manipulation.
 *
 * Since umask is a process-wide setting shared by all threads, concurrent
 * modifications can lead to race conditions where one thread restores the
 * wrong umask value. This guard uses a global mutex to ensure only one
 * thread can modify the umask at a time.
 *
 * Usage:
 *   {
 *     UmaskGuard guard(000);  // Sets umask to 000, acquires lock
 *     // ... create files/resources with desired permissions ...
 *   }  // Destructor restores previous umask and releases lock
 */
class UmaskGuard {
 public:
  /**
   * @brief Construct a guard that sets the process umask to the given value.
   * @param new_umask The umask value to set (default: 000 for full permissions)
   *
   * Acquires the global umask mutex and sets the umask to new_umask.
   */
  explicit UmaskGuard(mode_t new_umask = 000) : lock_(GetMutex()), previous_umask_(::umask(new_umask)) {}

  /**
   * @brief Destructor restores the previous umask and releases the mutex.
   */
  ~UmaskGuard() { ::umask(previous_umask_); }

  // Non-copyable
  UmaskGuard(const UmaskGuard&) = delete;
  UmaskGuard& operator=(const UmaskGuard&) = delete;

  // Non-movable (lock_guard is not movable)
  UmaskGuard(UmaskGuard&&) = delete;
  UmaskGuard& operator=(UmaskGuard&&) = delete;

 private:
  static std::mutex& GetMutex() {
    static std::mutex mutex;
    return mutex;
  }

  std::lock_guard<std::mutex> lock_;
  mode_t previous_umask_;
};

}  // namespace trellis::utils

#endif  // TRELLIS_UTILS_UMASK_GUARD_UMASK_GUARD_HPP_
