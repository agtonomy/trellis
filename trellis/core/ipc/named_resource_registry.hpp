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

#ifndef TRELLIS_CORE_IPC_NAMED_RESOURCE_REGISTRY_HPP_
#define TRELLIS_CORE_IPC_NAMED_RESOURCE_REGISTRY_HPP_

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trellis::core::ipc {

/**
 * @brief Singleton class that tracks and manages named IPC resources for cleanup.
 *
 * This registry is used to track named resources such as POSIX IPC names (e.g., semaphores, shared memory)
 * that may need to be unlinked (cleaned up) during shutdown or error handling.
 *
 * This is useful in cases where cleanup is needed after uncaught exceptions or software traps (SIGINT, SIGTERM, etc)
 *
 * It ensures that resources like `shm_open()` or `open()` with named paths are cleaned up properly.
 *
 * @note Constructing the singleton installs a process-wide `std::terminate` handler that calls `UnlinkAll`, so an
 * abort cleans up too. Callers still have to invoke `UnlinkAll` on the paths they control; the handler is only a last
 * resort. It cannot help when no handler runs at all, such as SIGKILL or a segfault.
 *
 * Thread-safe. The singleton is never destroyed, so the handler stays valid through static destruction. The lock is
 * never held while blocking, and an exception never leaves a method with it held, so the handler can always take it.
 */
class NamedResourceRegistry {
 public:
  /**
   * @brief Get the singleton instance of the registry.
   *
   * @return Reference to the singleton instance of NamedResourceRegistry.
   */
  static NamedResourceRegistry& Get() {
    // Leaked on purpose: the terminate handler outlives every static, and may run while they are destroyed.
    static NamedResourceRegistry& registry_ = *new NamedResourceRegistry;
    return registry_;
  }

  /**
   * @brief Insert a named file or resource path to track for cleanup using `unlink()`.
   *
   * @param handle The path to the resource.
   */
  void Insert(std::string_view handle) { Track(named_resources_, handle); }

  /**
   * @brief Insert a named shared memory resource to track for cleanup using `shm_unlink()`.
   *
   * @param handle The shared memory name (e.g., passed to `shm_open`).
   */
  void InsertShm(std::string_view handle) { Track(named_shm_, handle); }

  /**
   * @brief Stop tracking a resource its owner has unlinked itself, so the name doesn't stay for the process's life.
   *
   * Removes one registration of `handle`, if any. It doesn't allocate, so an owner's destructor may call it.
   *
   * @param handle The path passed to Insert().
   */
  void Remove(std::string_view handle) { Untrack(named_resources_, handle); }

  /**
   * @brief Stop tracking a shared memory resource its owner has unlinked itself. See Remove().
   *
   * @param handle The shared memory name passed to InsertShm().
   */
  void RemoveShm(std::string_view handle) { Untrack(named_shm_, handle); }

  /**
   * @brief Unlink all tracked resources.
   *
   * Iterates over all tracked resource names and attempts to remove them using `unlink()` and `shm_unlink()`.
   * Errors are ignored (best-effort cleanup).
   */
  void UnlinkAll() {
    const std::lock_guard lock{mutex_};
    Drain(named_resources_, ::unlink);
    Drain(named_shm_, ::shm_unlink);
  }

 private:
  /**
   * @brief Private constructor for singleton pattern. Installs the terminate handler described above.
   */
  NamedResourceRegistry() {
    // An uncaught exception reaches std::terminate without unwinding (libstdc++), so destructors and catch blocks
    // don't run. This is insurance to prevent orphaning the pages behind the shm files.
    static std::terminate_handler previous_handler = std::set_terminate([]() {
      Get().UnlinkAll();
      if (previous_handler != nullptr) {
        previous_handler();
      }
      std::abort();
    });
  }

  /**
   * @brief Track `handle` in `names`.
   *
   * The name is built before the lock is taken, but growing `names` can still throw while it is held. The catch makes
   * the lock release before the exception goes on, even when nothing above catches it: without unwinding, the
   * terminate handler would otherwise wait on this thread's lock forever.
   */
  void Track(std::vector<std::string>& names, std::string_view handle) {
    std::string name{handle};
    try {
      const std::lock_guard lock{mutex_};
      names.push_back(std::move(name));
    } catch (...) {
      throw;
    }
  }

  /**
   * @brief Forget one registration of `handle` in `names`.
   */
  void Untrack(std::vector<std::string>& names, std::string_view handle) {
    const std::lock_guard lock{mutex_};
    if (const auto it = std::ranges::find(names, handle); it != names.end()) {
      names.erase(it);
    }
  }

  /**
   * @brief Remove every name in `names` with `remove`, ignoring failures (best-effort cleanup), and forget them.
   */
  static void Drain(std::vector<std::string>& names, int (*remove)(const char*)) {
    for (const auto& name : names) {
      (void)remove(name.c_str());
    }
    names.clear();
  }

  std::mutex mutex_;                          ///< Guards both lists: resources are inserted and removed on any thread.
  std::vector<std::string> named_resources_;  ///< Regular named resources, for `unlink()`.
  std::vector<std::string> named_shm_;        ///< Shared memory names, for `shm_unlink()`.
};

}  // namespace trellis::core::ipc

#endif  // TRELLIS_CORE_IPC_NAMED_RESOURCE_REGISTRY_HPP_
