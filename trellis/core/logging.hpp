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

#ifndef TRELLIS_CORE_LOGGING_HPP
#define TRELLIS_CORE_LOGGING_HPP

#include <fmt/format.h>
#include <unistd.h>

#include <string>
#include <string_view>

namespace trellis {
namespace core {
namespace Log {

/**
 * @brief Mark a format string as requiring runtime evaluation.
 *
 * This is used for format strings that are not `constexpr`, such as dynamically constructed strings.
 *
 * @param s The runtime format string.
 * @return A format string suitable for use with fmt::format.
 */
inline auto runtime(std::string_view s) { return fmt::runtime(s); }

/**
 * @brief Log levels used to categorize log messages.
 */
enum LogLevel {
  kInfo = 0,  ///< Informational messages.
  kWarn,      ///< Warnings that do not affect functionality.
  kError,     ///< Recoverable errors.
  kFatal,     ///< Non-recoverable errors; program will terminate.
  kDebug      ///< Debug-level messages for development.
};

/**
 * @brief Set the log level from a string name (e.g., "info", "error").
 *
 * Supported values (case-insensitive): `"info"`, `"warn"`, `"warning"`, `"error"`, `"fatal"`, `"debug"`.
 * Unknown strings will fall back to the default log level (all but debug).
 *
 * @param log_level_string The log level as a string.
 */
void SetLogLevel(const std::string& log_level_string);

/**
 * @brief Set the current log level using the LogLevel enum.
 *
 * @param log_level The desired logging level.
 */
void SetLogLevel(LogLevel log_level);

namespace detail {

/**
 * @brief Whether messages at the given level are currently emitted.
 */
bool IsEnabled(LogLevel level);

/**
 * @brief Format and print one log line. Callers check IsEnabled first.
 *
 * Takes type-erased arguments so the formatting code is compiled once here rather than in every call site.
 */
void Log(LogLevel level, fmt::string_view fmt_msg, fmt::format_args args);

}  // namespace detail

/**
 * @brief Log an informational message.
 *
 * @tparam Args Format arguments.
 * @param fmt_msg A fmtlib-compatible format string.
 * @param args Arguments to format into the message.
 */
template <typename... Args>
inline void Info(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  if (detail::IsEnabled(kInfo)) detail::Log(kInfo, fmt_msg.str, fmt::make_format_args(args...));
}
inline void Info(std::string_view msg) { Info("{}", msg); }

/**
 * @brief Log a warning message.
 */
template <typename... Args>
inline void Warn(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  if (detail::IsEnabled(kWarn)) detail::Log(kWarn, fmt_msg.str, fmt::make_format_args(args...));
}
inline void Warn(std::string_view msg) { Warn("{}", msg); }

/**
 * @brief Log an error message.
 */
template <typename... Args>
inline void Error(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  if (detail::IsEnabled(kError)) detail::Log(kError, fmt_msg.str, fmt::make_format_args(args...));
}
inline void Error(std::string_view msg) { Error("{}", msg); }

/**
 * @brief Log a fatal error message and terminate the program.
 *
 * This function logs the message, delays briefly to allow logs to flush, and then calls `abort()`.
 */
template <typename... Args>
inline void Fatal(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  if (detail::IsEnabled(kFatal)) detail::Log(kFatal, fmt_msg.str, fmt::make_format_args(args...));
  sleep(1);
  abort();
}
inline void Fatal(std::string_view msg) { Fatal("{}", msg); }

/**
 * @brief Log a debug-level message.
 */
template <typename... Args>
inline void Debug(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  if (detail::IsEnabled(kDebug)) detail::Log(kDebug, fmt_msg.str, fmt::make_format_args(args...));
}
inline void Debug(std::string_view msg) { Debug("{}", msg); }

}  // namespace Log
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_LOGGING_HPP
