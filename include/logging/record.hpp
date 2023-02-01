/*
 * Copyright (c) 2023, Alibaba Group Holding Limited;
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <util/meta_string.hpp>
#include <utility>

#if defined(_WIN32)
#ifndef _WINDOWS_
#ifndef WIN32_LEAN_AND_MEAN  // Sorry for the inconvenience. Please include any
                             // related headers you need manually.
                             // (https://stackoverflow.com/a/8294669)
#define WIN32_LEAN_AND_MEAN  // Prevent inclusion of WinSock2.h
#endif
#include <Windows.h>  // Force inclusion of WinGDI here to resolve name conflict
#endif
#ifdef ERROR  // Should be true unless someone else undef'd it already
#undef ERROR  // Windows GDI defines this macro; make it a global enum so it
              // doesn't conflict with our code
enum { ERROR = 0 };
#endif
#endif

enum class Severity {
  NONE = 0,
  TRACE = 1,
  DEBUG = 2,
  INFO = 3,
  WARN = 4,
  ERROR = 5,
  CRITICAL = 6,
};

namespace easylog_ns {

inline std::string_view severity_str(Severity severity) {
  switch (severity) {
    case Severity::TRACE:
      return "TRACE   ";
    case Severity::DEBUG:
      return "DEBUG   ";
    case Severity::INFO:
      return "INFO    ";
    case Severity::WARN:
      return "WARNING ";
    case Severity::ERROR:
      return "ERROR   ";
    case Severity::CRITICAL:
      return "CRITICAL";
    default:
      return "NONE";
  }
}

class record_t {
 public:
  record_t(auto tm_point, Severity severity, auto str)
      : tm_point_(tm_point), severity_(severity), tid_(get_tid()) {
    std::memcpy(buf_, str.data(), str.size());
    buf_len_ = str.size();
  }

  Severity get_severity() const { return severity_; }

  const char *get_message() const {
    msg_ = ss_.str();
    return msg_.data();
  }

  std::string_view get_file_str() const { return {buf_, buf_len_}; }

  unsigned int get_tid() const { return tid_; }

  auto get_time_point() const { return tm_point_; }

  record_t &ref() { return *this; }

  template <typename T>
  record_t &operator<<(const T &data) {
    ss_ << data;
    return *this;
  }

  template <typename... Args>
  record_t &sprintf(const char *fmt, Args... args) {
    printf_string_format(fmt, args...);

    return *this;
  }

 private:
  template <typename... Args>
  void printf_string_format(const char *fmt, Args... args) {
    size_t size = snprintf(nullptr, 0, fmt, args...);

    std::string buf;
    buf.reserve(size + 1);
    buf.resize(size);

    snprintf(&buf[0], size + 1, fmt, args...);

    ss_ << buf;
  }

  std::chrono::system_clock::time_point tm_point_;
  Severity severity_;
  unsigned int tid_;
  char buf_[64] = {};
  size_t buf_len_ = 0;
  std::ostringstream ss_;  // TODO: will replace it with std::string to improve
                           // the performance.
  mutable std::string msg_;
};

#define TO_STR(s) #s

#define GET_STRING(filename, line)                              \
  [] {                                                          \
    constexpr auto path = refvalue::meta_string{filename};      \
    constexpr size_t pos =                                      \
        path.rfind(std::filesystem::path::preferred_separator); \
    constexpr auto name = path.substr<pos + 1>();               \
    constexpr auto prefix = name + ":" + TO_STR(line);          \
    return "[" + prefix + "] ";                                 \
  }()

}  // namespace easylog_ns