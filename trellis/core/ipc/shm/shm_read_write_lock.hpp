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

#ifndef TRELLIS_CORE_IPC_SHM_SHM_READ_WRITE_LOCK_HPP_
#define TRELLIS_CORE_IPC_SHM_SHM_READ_WRITE_LOCK_HPP_

#include <pthread.h>

#include <chrono>
#include <string>

#include "trellis/core/config.hpp"

namespace trellis::core::ipc::shm {

/**
 * @brief A reader-writer lock that operates across shared memory.
 *
 * This class wraps a `pthread_rwlock_t` and makes it accessible via shared memory,
 * allowing multiple processes to synchronize access using standard read/write locking semantics.
 */
class ShmReadWriteLock {
 public:
  /**
   * @brief A structure representing the raw shared memory layout containing a pthread read-write lock.
   */
  struct NamedRwLock {
    pthread_rwlock_t rwlock;  ///< POSIX read-write lock stored in shared memory.
  };

  /**
   * @brief Constructs a shared memory read-write lock.
   *
   * @param handle Name of the shared memory segment backing the lock.
   * @param owner If true, this instance is responsible for unlinking the named shm region.
   * @param config Configuration object for reading IPC settings (uid/gid).
   */
  ShmReadWriteLock(std::string handle, bool owner, const trellis::core::Config& config);

  /**
   * @brief Destructor that cleans up the shared memory and lock if owned.
   */
  ~ShmReadWriteLock();

  ShmReadWriteLock(const ShmReadWriteLock&) = delete;
  ShmReadWriteLock& operator=(const ShmReadWriteLock&) = delete;

  /**
   * @brief Move constructor.
   *
   * @param other Instance to move from.
   */
  ShmReadWriteLock(ShmReadWriteLock&&);

  ShmReadWriteLock& operator=(ShmReadWriteLock&&) = delete;

  // Lock functions

  /**
   * @brief Acquires the lock in read (shared) mode, blocking until successful.
   * @return true if the lock was successfully acquired.
   */
  bool LockRead();

  /**
   * @brief Acquires the lock in write (exclusive) mode, blocking until successful.
   * @return true if the lock was successfully acquired.
   */
  bool LockWrite();

  /**
   * @brief Attempts to acquire the lock in read (shared) mode without blocking.
   * @return true if the lock was successfully acquired.
   */
  bool TryLockRead();

  /**
   * @brief Attempts to acquire the lock in write (exclusive) mode without blocking.
   * @return true if the lock was successfully acquired.
   */
  bool TryLockWrite();

  /**
   * @brief Unlocks the lock regardless of mode (read or write).
   * @return true if the lock was successfully released.
   */
  bool Unlock();

 private:
  std::string handle_;   ///< Name of the shared memory object.
  bool owner_{false};    ///< True if this instance created the shared memory and initialized the lock.
  NamedRwLock* rwlock_;  ///< Pointer to the shared memory containing the POSIX read-write lock.
};

}  // namespace trellis::core::ipc::shm

#endif  // TRELLIS_CORE_IPC_SHM_SHM_READ_WRITE_LOCK_HPP_
