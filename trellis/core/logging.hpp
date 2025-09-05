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

#include <fmt/core.h>
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

/**
 * @brief Internal logging function used by log level wrappers.
 *
 * @param msg The message to log.
 * @param prefix A string to prefix the log line (e.g., "[INFO]  ").
 * @param level The level of the log message.
 */
void DoLog(std::string_view msg, std::string_view prefix, LogLevel level);

/**
 * @brief Log an informational message.
 *
 * @tparam Args Format arguments.
 * @param fmt_msg A fmtlib-compatible format string.
 * @param args Arguments to format into the message.
 */
template <typename... Args>
inline void Info(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[INFO]  ", LogLevel::kInfo);
}
inline void Info(std::string_view msg) { DoLog(msg, "[INFO]  ", LogLevel::kInfo); }

/**
 * @brief Log a warning message.
 *
 * @tparam Args Format arguments.
 * @param fmt_msg A fmtlib-compatible format string.
 * @param args Arguments to format into the message.
 */
template <typename... Args>
inline void Warn(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[WARN]  ", LogLevel::kWarn);
}
inline void Warn(std::string_view msg) { DoLog(msg, "[WARN]  ", LogLevel::kWarn); }

/**
 * @brief Log an error message.
 *
 * @tparam Args Format arguments.
 * @param fmt_msg A fmtlib-compatible format string.
 * @param args Arguments to format into the message.
 */
template <typename... Args>
inline void Error(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[ERROR] ", LogLevel::kError);
}
inline void Error(std::string_view msg) { DoLog(msg, "[ERROR] ", LogLevel::kError); }

/**
 * @brief Log a fatal error message and terminate the program.
 *
 * This function logs the message, delays briefly to allow logs to flush, and then calls `abort()`.
 *
 * @tparam Args Format arguments.
 * @param fmt_msg A fmtlib-compatible format string.
 * @param args Arguments to format into the message.
 */
template <typename... Args>
inline void Fatal(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[FATAL] ", LogLevel::kFatal);
  sleep(1);
  abort();
}
inline void Fatal(std::string_view msg) {
  DoLog(msg, "[FATAL] ", LogLevel::kFatal);
  sleep(1);
  abort();
}

/**
 * @brief Log a debug-level message.
 *
 * @tparam Args Format arguments.
 * @param fmt_msg A fmtlib-compatible format string.
 * @param args Arguments to format into the message.
 */
template <typename... Args>
inline void Debug(fmt::format_string<Args...> fmt_msg, Args&&... args) {
  std::string msg = fmt::format(fmt_msg, std::forward<Args>(args)...);
  DoLog(msg, "[DEBUG] ", LogLevel::kDebug);
}
inline void Debug(std::string_view msg) { DoLog(msg, "[DEBUG] ", LogLevel::kDebug); }

}  // namespace Log
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_LOGGING_HPP
