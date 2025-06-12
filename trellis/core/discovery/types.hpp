#ifndef TRELLIS_CORE_DISCOVERY_TYPES_HPP_
#define TRELLIS_CORE_DISCOVERY_TYPES_HPP_

#include "trellis/core/discovery/pb/sample.pb.h"

namespace trellis::core::discovery {

// Pull this into the discovery namespace to add a level of indirection and prevent the type from leaking
using namespace trellis::core::discovery::pb;

}  // namespace trellis::core::discovery

#endif  // TRELLIS_CORE_DISCOVERY_TYPES_HPP_
