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

#include <queue>
#include <string>

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
 */
class NamedResourceRegistry {
 public:
  /**
   * @brief Get the singleton instance of the registry.
   *
   * @return Reference to the singleton instance of NamedResourceRegistry.
   */
  static NamedResourceRegistry& Get() {
    static NamedResourceRegistry registry_;
    return registry_;
  }

  /**
   * @brief Insert a named file or resource path to track for cleanup using `unlink()`.
   *
   * @param handle The path to the resource.
   */
  void Insert(std::string_view handle) { named_resources_.emplace(std::string(handle)); }

  /**
   * @brief Insert a named shared memory resource to track for cleanup using `shm_unlink()`.
   *
   * @param handle The shared memory name (e.g., passed to `shm_open`).
   */
  void InsertShm(std::string_view handle) { named_shm_.emplace(std::string(handle)); }

  /**
   * @brief Unlink all tracked resources.
   *
   * Iterates over all tracked resource names and attempts to remove them using `unlink()` and `shm_unlink()`.
   * Errors are ignored (best-effort cleanup).
   */
  void UnlinkAll() {
    while (!named_resources_.empty()) {
      const auto& handle = named_resources_.front();
      (void)::unlink(handle.c_str());  ///< Best-effort removal; ignore return value.
      named_resources_.pop();
    }

    while (!named_shm_.empty()) {
      const auto& handle = named_shm_.front();
      (void)::shm_unlink(handle.c_str());  ///< Best-effort removal; ignore return value.
      named_shm_.pop();
    }
  }

 private:
  /**
   * @brief Private constructor for singleton pattern.
   */
  NamedResourceRegistry() = default;

  std::queue<std::string> named_resources_;  ///< Queue of regular named resources for `unlink()`.
  std::queue<std::string> named_shm_;        ///< Queue of shared memory names for `shm_unlink()`.
};

}  // namespace trellis::core::ipc

#endif  // TRELLIS_CORE_IPC_NAMED_RESOURCE_REGISTRY_HPP_
