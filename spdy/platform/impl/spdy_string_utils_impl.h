// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_PLATFORM_IMPL_SPDY_STRING_UTILS_IMPL_H_
#define NET_SPDY_PLATFORM_IMPL_SPDY_STRING_UTILS_IMPL_H_

#include <sstream>
#include <string>

#include "base/strings/stringprintf.h"

namespace net {

template <typename... Args>
inline std::string SpdyStrCatImpl(const Args&... args) {
  std::ostringstream oss;
  int dummy[] = {1, (oss << args, 0)...};
  static_cast<void>(dummy);
  return oss.str();
}

template <typename... Args>
inline void SpdyStrAppendImpl(std::string* output, Args... args) {
  output->append(SpdyStrCatImpl(args...));
}

template <typename... Args>
inline std::string SpdyStringPrintfImpl(const Args&... args) {
  return base::StringPrintf(std::forward<const Args&>(args)...);
}

}  // namespace net

#endif  // NET_SPDY_PLATFORM_IMPL_SPDY_STRING_UTILS_IMPL_H_
