#pragma once

#include <sys/types.h>

#include <optional>
#include <utility>

#include "trellis/core/config.hpp"

namespace trellis::core::ipc::utils {

inline std::pair<std::optional<uid_t>, std::optional<gid_t>> GetUidGidFromConfig(const trellis::core::Config& config) {
  const auto uid = config.AsIfExists<uid_t>("trellis.ipc.uid", 0);
  const auto gid = config.AsIfExists<gid_t>("trellis.ipc.gid", 0);
  const auto uid_opt = (uid != 0) ? std::optional<uid_t>(uid) : std::nullopt;
  const auto gid_opt = (gid != 0) ? std::optional<gid_t>(gid) : std::nullopt;
  return {uid_opt, gid_opt};
}

}  // namespace trellis::core::ipc::utils
