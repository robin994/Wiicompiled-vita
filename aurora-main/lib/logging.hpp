#pragma once

#include <aurora/aurora.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

#if !defined(MKW_TARGET_VITA)
#include <fmt/base.h>
#include <fmt/format.h>
#endif

namespace aurora {
void log_internal(AuroraLogLevel level, const char* module, const char* message, unsigned int len) noexcept;

extern AuroraConfig g_config;

struct Module {
  const char* name;
  explicit Module(const char* name) noexcept : name(name) {}

#if defined(MKW_TARGET_VITA)
  template <typename... T>
  void report(const AuroraLogLevel level, std::string_view format, T&&...) noexcept {
    if (g_config.logLevel > level) return;
    log_internal(level, name, format.data(), static_cast<unsigned int>(format.size()));
  }

  template <typename... T>
  void debug(std::string_view format, T&&... args) noexcept {
    report(LOG_DEBUG, format, std::forward<T>(args)...);
  }

  template <typename... T>
  void info(std::string_view format, T&&... args) noexcept {
    report(LOG_INFO, format, std::forward<T>(args)...);
  }

  template <typename... T>
  void warn(std::string_view format, T&&... args) noexcept {
    report(LOG_WARNING, format, std::forward<T>(args)...);
  }

  template <typename... T>
  void error(std::string_view format, T&&... args) noexcept {
    report(LOG_ERROR, format, std::forward<T>(args)...);
  }

  template <typename... T>
  [[noreturn]] void fatal(std::string_view format, T&&...) noexcept {
    log_internal(LOG_FATAL, name, format.data(), static_cast<unsigned int>(format.size()));
    if (g_config.logCallback == nullptr) {
      show_fatal_dialog(name, format);
    }
    std::abort();
  }
#else
  template <typename... T>
  void report(const AuroraLogLevel level, fmt::format_string<T...> fmt, T&&... args) noexcept {
    if (g_config.logLevel > level) return;

    auto message = fmt::format(fmt, std::forward<T>(args)...);
    log_internal(level, name, message.c_str(), static_cast<unsigned int>(message.size()));
  }

  template <typename... T>
  void debug(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_DEBUG, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  void info(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_INFO, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  void warn(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_WARNING, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  void error(fmt::format_string<T...> fmt, T&&... args) noexcept {
    report(LOG_ERROR, fmt, std::forward<T>(args)...);
  }

  template <typename... T>
  [[noreturn]] void fatal(fmt::format_string<T...> fmt, T&&... args) noexcept {
    auto message = fmt::format(fmt, std::forward<T>(args)...);
    log_internal(LOG_FATAL, name, message.c_str(), static_cast<unsigned int>(message.size()));
    // Embedders can provide a fatal log callback, which the runtime uses for its own crash dialog.
    // Standalone Aurora builds fall back to logging.cpp's native one.
    if (g_config.logCallback == nullptr) {
      show_fatal_dialog(name, message);
    }
    std::abort();
  }
#endif

private:
  static void show_fatal_dialog(const char* module, std::string_view message) noexcept;
};
} // namespace aurora

#if !defined(MKW_TARGET_VITA)
template <>
struct fmt::formatter<AuroraLogLevel> : formatter<std::string_view> {
  auto format(AuroraLogLevel level, format_context& ctx) const -> format_context::iterator;
};
#endif
