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

#include "trellis/core/ipc/shm/shm_read_write_lock.hpp"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <condition_variable>
#include <mutex>

#include "trellis/core/ipc/named_resource_registry.hpp"
#include "trellis/core/ipc/utils.hpp"
#include "trellis/core/time.hpp"
#include "trellis/utils/umask_guard/umask_guard.hpp"

namespace trellis::core::ipc::shm {

namespace {
namespace {

ShmReadWriteLock::NamedRwLock* CreateRwLock(std::string handle, const trellis::core::Config& config) {
  int fd;
  int err;
  {
    const auto [uid_opt, gid_opt] = trellis::core::ipc::utils::GetUidGidFromConfig(config);
    trellis::utils::UmaskGuard guard(000, uid_opt, gid_opt);
    fd = ::shm_open(handle.c_str(), O_RDWR | O_CREAT | O_EXCL,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    err = errno;
  }

  if (fd < 0) {
    if (err == EEXIST) return nullptr;
    throw std::system_error(err, std::generic_category(), "CreateRwLock shm_open failed on " + handle);
  }

  if (::ftruncate(fd, sizeof(ShmReadWriteLock::NamedRwLock)) == -1) {
    ::close(fd);
    throw std::system_error(errno, std::generic_category(), "CreateRwLock ftruncate failed");
  }

  NamedResourceRegistry::Get().InsertShm(handle);

  pthread_rwlockattr_t attr;
  pthread_rwlockattr_init(&attr);
  pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

  ShmReadWriteLock::NamedRwLock* rw = static_cast<ShmReadWriteLock::NamedRwLock*>(
      mmap(nullptr, sizeof(ShmReadWriteLock::NamedRwLock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  ::close(fd);

  pthread_rwlock_init(&rw->rwlock, &attr);
  return rw;
}

ShmReadWriteLock::NamedRwLock* OpenRwLock(std::string handle) {
  int fd = ::shm_open(handle.c_str(), O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (fd < 0) {
    return nullptr;
  }

  ShmReadWriteLock::NamedRwLock* rw = static_cast<ShmReadWriteLock::NamedRwLock*>(
      mmap(nullptr, sizeof(ShmReadWriteLock::NamedRwLock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  ::close(fd);
  return rw;
}

ShmReadWriteLock::NamedRwLock* CreateOrOpenRwLock(std::string handle, bool owner, const trellis::core::Config& config) {
  auto rw = CreateRwLock(handle, config);
  if (rw == nullptr) {
    rw = OpenRwLock(handle);
  }
  return rw;
}

}  // namespace

}  // namespace

ShmReadWriteLock::ShmReadWriteLock(std::string handle, bool owner, const trellis::core::Config& config)
    : handle_{handle}, owner_{owner}, rwlock_{CreateOrOpenRwLock(handle, owner, config)} {}

ShmReadWriteLock::~ShmReadWriteLock() {
  if (rwlock_ != nullptr) {
    munmap(static_cast<void*>(rwlock_), sizeof(ShmReadWriteLock::NamedRwLock));
  }
  if (owner_) {
    ::shm_unlink(handle_.c_str());
  }
}

bool ShmReadWriteLock::LockRead() { return pthread_rwlock_rdlock(&rwlock_->rwlock) == 0; }

bool ShmReadWriteLock::LockWrite() { return pthread_rwlock_wrlock(&rwlock_->rwlock) == 0; }

bool ShmReadWriteLock::TryLockRead() { return pthread_rwlock_tryrdlock(&rwlock_->rwlock) == 0; }

bool ShmReadWriteLock::TryLockWrite() { return pthread_rwlock_trywrlock(&rwlock_->rwlock) == 0; }

bool ShmReadWriteLock::Unlock() { return pthread_rwlock_unlock(&rwlock_->rwlock) == 0; }

ShmReadWriteLock::ShmReadWriteLock(ShmReadWriteLock&& other)
    : handle_{other.handle_}, owner_{other.owner_}, rwlock_{other.rwlock_} {
  other.handle_ = std::string{};
  other.owner_ = false;
  other.rwlock_ = nullptr;
}

}  // namespace trellis::core::ipc::shm
