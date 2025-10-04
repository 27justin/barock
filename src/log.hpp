#pragma once

#include <string_view>
#include <iostream>
#include <cassert>
#include <cstring>
#include <sstream>

// public enum & simple API declarations
enum class log_level { trace = 1, info, warn, error, critical };

inline log_level filter = static_cast<log_level>(0);

void set_log_filter(log_level level);
log_level get_log_filter();

constexpr const char ANSI_RESET[] = "\x1b[0m";
constexpr const char ANSI_BOLD[] = "\x1b[1m";
constexpr const char ANSI_DIM[] = "\x1b[2m";
constexpr const char ANSI_ITALIC[] = "\x1b[3m";
constexpr const char ANSI_UNDERLINE[] = "\x1b[4m";

constexpr const char *
get_level_ansi_code(log_level level) {
  switch (level) {
    case log_level::trace:
      return "\x1b[0;36m";
    case log_level::info:
      return "\x1b[0;32m";
    case log_level::warn:
      return "\x1b[0;33m";
    case log_level::error:
      return "\x1b[0;31m";
    case log_level::critical:
      return "\x1b[1;31m";
    default:
      assert(false && "Unknown log level in `get_level_ansi_code`");
  }
}

constexpr const char *
get_level_text(log_level level) {
  switch (level) {
    case log_level::trace:
      return "TRACE";
    case log_level::info:
      return "INFO";
    case log_level::warn:
      return "WARN";
    case log_level::error:
      return "ERROR";
    case log_level::critical:
      return "CRITICAL";
    default:
      assert(false && "Unknown log level in `get_level_text`");
  }
}

inline int
compute_string_length(std::string_view str) {
  int chars = 0;

  bool escape_code = false;
  for (auto it = str.begin(); it != str.end(); ++it) {
    if (*it == '\x1b') {
      escape_code = true;
      continue;
    }

    if (escape_code && *it == 'm') {
      escape_code = false;
      continue;
    }

    if (!escape_code && std::isprint(*it)) {
      chars++;
    }
  }

  return chars;
}

// Helper function to replace markers with ANSI codes
inline std::string
embed_ansi_codes(std::string str) {
    auto replace_marker = [&](const std::string& marker, const char* ansi_code) {
        size_t pos = 0;
        bool open = true;
        while ((pos = str.find(marker, pos)) != std::string::npos) {
            str.replace(pos, marker.size(), open ? ansi_code : ANSI_RESET);
            pos += std::strlen(open ? ansi_code : ANSI_RESET);
            open = !open;
        }
    };

    replace_marker("**", ANSI_BOLD);
    replace_marker("__", ANSI_UNDERLINE);
    replace_marker("//", ANSI_ITALIC);

    return str;
}

template<log_level Level, typename... Args>
void
print_log(const char *format_view, Args &&...args) {
  if (static_cast<int>(filter) > static_cast<int>(Level))
    return;

  std::stringstream ss;
  ss << get_level_ansi_code(Level) << get_level_text(Level) << ": " << ANSI_RESET;

  // Embed escape codes for character based modifications
  std::string format_string(format_view);
  format_string = embed_ansi_codes(format_string);

  std::string log_message = std::vformat(format_string, std::make_format_args(args...));
  std::cout << ss.str();

  int left_fringe = compute_string_length(ss.str());

  auto pos = log_message.find("\n");
  if (pos == std::string::npos)
    std::cout << log_message << "\n";

  int line = 0;
  do {
    pos = log_message.find("\n");
    if (line == 0) {
      std::cout << log_message.substr(0, pos + 1);
    } else {
      bool is_last_line = log_message.substr(0, pos + 1).find("\n") == std::string::npos;
      std::cout << std::string(left_fringe - 2, ' ');
      if (is_last_line)
        std::cout << "╰ ";
      else
        std::cout << "├ ";
      std::cout << log_message.substr(0, pos) << "\n";
    }
    log_message.erase(0, pos + 1);
    line++;
  } while (pos != std::string::npos);
}


template<typename FormatString, typename... Args>
void
TRACE(FormatString message, Args... args) {
  print_log<log_level::trace, Args...>(message, std::forward<Args>(args)...);
}

template<typename FormatString, typename... Args>
void
INFO(FormatString message, Args... args) {
  print_log<log_level::info, Args...>(message, std::forward<Args>(args)...);
}

template<typename FormatString, typename... Args>
void
WARN(FormatString message, Args... args) {
  print_log<log_level::warn, Args...>(message, std::forward<Args>(args)...);
}

template<typename FormatString, typename... Args>
void
ERROR(FormatString message, Args... args) {
  print_log<log_level::error, Args...>(message, std::forward<Args>(args)...);
}

template<typename FormatString, typename... Args>
void
CRITICAL(FormatString message, Args... args) {
  print_log<log_level::critical, Args...>(message, std::forward<Args>(args)...);
}

