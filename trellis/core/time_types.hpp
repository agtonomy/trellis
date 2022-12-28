/*
 * Copyright (C) 2022 Agtonomy
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

#ifndef TRELLIS_CORE_TIME_TYPES_HPP_
#define TRELLIS_CORE_TIME_TYPES_HPP_

#include <chrono>

namespace trellis {
namespace core {
namespace time {

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using SystemTimePoint = std::chrono::time_point<std::chrono::system_clock>;

}  // namespace time
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIME_TYPES_HPP_
