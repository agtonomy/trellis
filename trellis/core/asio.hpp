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

#ifndef TRELLIS_CORE_ASIO_HPP_
#define TRELLIS_CORE_ASIO_HPP_

// The asio.cpp that trellis links, into libtrellis.so and into every binary that links trellis statically, is compiled
// with ASIO_SEPARATE_COMPILATION. Code that includes asio's headers without the define compiles asio's implementation
// inline as well, so the process holds two copies of asio's static state, such as the signal-service list and the
// error categories, and the code on either side disagrees about both. Bazel targets get the define from
// //trellis/core:core_asio. A build outside Bazel must define it itself.
#ifndef ASIO_SEPARATE_COMPILATION
#error "Define ASIO_SEPARATE_COMPILATION to compile against trellis, which links asio compiled with it"
#endif

#include <asio.hpp>

#endif  // TRELLIS_CORE_ASIO_HPP_
