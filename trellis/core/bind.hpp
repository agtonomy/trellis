/*
 * Copyright (C) 2021 Agtonomy
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

#ifndef TRELLIS_CORE_BIND_HPP
#define TRELLIS_CORE_BIND_HPP

#include <functional>

#define bind0(fn, ...) std::bind(fn, this, ##__VA_ARGS__)
#define bind1(fn, ...) std::bind(fn, this, std::placeholders::_1, ##__VA_ARGS__)
#define bind2(fn, ...) std::bind(fn, this, std::placeholders::_1, std::placeholders::_2, ##__VA_ARGS__)
#define bind3(fn, ...) \
  std::bind(fn, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, ##__VA_ARGS__)

#endif  // TRELLIS_CORE_BIND_HPP
