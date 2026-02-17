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
#include <unistd.h>

#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <system_error>

#include "trellis/core/logging.hpp"

namespace trellis::utils {

namespace {

template <typename IdType>
void TrySetId(bool is_root, std::optional<IdType> requested_id, IdType (*getter)(), int (*setter)(IdType),
              const char* id_name, std::optional<IdType>& previous_storage, bool& changed_flag,
              std::function<void()> rollback_fn) {
  if (!requested_id.has_value()) {
    return;
  }

  const IdType current_id = getter();
  if (is_root) {
    previous_storage = current_id;
    if (setter(requested_id.value()) == -1) {
      const int err = errno;
      rollback_fn();
      throw std::system_error(err, std::generic_category(), std::string("UmaskGuard::set") + id_name + " failed");
    }
    changed_flag = true;
  } else if (requested_id.value() != current_id) {
    static std::once_flag warn_flag;
    std::call_once(warn_flag, [id_name, current_id, requested_id] {
      trellis::core::Log::Warn("Cannot change {} from {} to {} without root privileges", id_name, current_id,
                               requested_id.value());
    });
  }
}

}  // namespace

/**
 * @brief RAII guard for thread-safe umask and privilege manipulation.
 *
 * Since umask, euid, and egid are process-wide settings shared by all threads,
 * concurrent modifications can lead to race conditions. This guard uses a global
 * mutex to ensure only one thread can modify these settings at a time.
 *
 * Basic usage (umask only):
 *   {
 *     UmaskGuard guard(000);
 *     // Create files/resources with full permissions
 *   }  // Destructor restores previous umask
 *
 * Advanced usage (with privilege dropping):
 *   {
 *     UmaskGuard guard(000, 1000, 1000);  // umask=000, euid=1000, egid=1000
 *     // Create IPC resources owned by user 1000:1000
 *   }  // Destructor restores previous umask, euid, and egid
 *
 * Security notes:
 * - egid is set before euid (standard privilege dropping order)
 * - euid is restored before egid (reverse order)
 * - seteuid/setegid will throw std::system_error if they fail when running as root
 * - If not root, requested ID changes that differ from current IDs log a warning
 * - On failure, any successful changes are rolled back before throwing
 *
 * @throws std::system_error if seteuid or setegid fails when running as root
 */
class UmaskGuard {
 public:
  /**
   * @brief Construct a guard that sets the process umask and optionally euid/egid.
   * @param new_umask The umask value to set (default: 000 for full permissions)
   * @param euid Optional effective user ID to set (default: nullopt, no change)
   * @param egid Optional effective group ID to set (default: nullopt, no change)
   *
   * Acquires the global mutex and sets umask, and optionally euid/egid.
   * If not running as root (geteuid() != 0), euid/egid changes are skipped with a warning
   * if they differ from current values.
   */
  explicit UmaskGuard(mode_t new_umask = 000, std::optional<uid_t> euid = std::nullopt,
                      std::optional<gid_t> egid = std::nullopt)
      : lock_(GetMutex()), previous_umask_(::umask(new_umask)) {
    const bool is_root = (::geteuid() == 0);

    TrySetId(is_root, egid, ::getegid, ::setegid, "egid", previous_egid_, egid_changed_,
             [this]() { ::umask(previous_umask_); });

    TrySetId(is_root, euid, ::geteuid, ::seteuid, "euid", previous_euid_, euid_changed_, [this]() {
      if (egid_changed_) {
        if (::setegid(previous_egid_.value()) == -1) {
          trellis::core::Log::Error("Failed to restore egid during rollback to {}: {}", previous_egid_.value(),
                                    std::strerror(errno));
        }
      }
      ::umask(previous_umask_);
    });
  }

  /**
   * @brief Destructor restores the previous umask, euid, and egid, then releases the mutex.
   */
  ~UmaskGuard() {
    if (euid_changed_) {
      if (::seteuid(previous_euid_.value()) == -1) {
        trellis::core::Log::Error("Failed to restore euid to {}: {}", previous_euid_.value(), std::strerror(errno));
      }
    }
    if (egid_changed_) {
      if (::setegid(previous_egid_.value()) == -1) {
        trellis::core::Log::Error("Failed to restore egid to {}: {}", previous_egid_.value(), std::strerror(errno));
      }
    }
    ::umask(previous_umask_);
  }

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
  std::optional<uid_t> previous_euid_;
  std::optional<gid_t> previous_egid_;
  bool euid_changed_{false};
  bool egid_changed_{false};
};

}  // namespace trellis::utils

#endif  // TRELLIS_UTILS_UMASK_GUARD_UMASK_GUARD_HPP_
