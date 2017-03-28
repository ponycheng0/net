// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_
#define NET_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_

#include <string>
#include <utility>

#include "net/spdy/platform/impl/spdy_string_utils_impl.h"

namespace net {

template <typename... Args>
inline std::string SpdyStrCat(const Args&... args) {
  return SpdyStrCatImpl(std::forward<const Args&>(args)...);
}

template <typename... Args>
inline void SpdyStrAppend(std::string* output, const Args&... args) {
  SpdyStrAppendImpl(output, std::forward<const Args&>(args)...);
}

template <typename... Args>
inline std::string SpdyStringPrintf(const Args&... args) {
  return SpdyStringPrintfImpl(std::forward<const Args&>(args)...);
}

}  // namespace net

#endif  // NET_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_
