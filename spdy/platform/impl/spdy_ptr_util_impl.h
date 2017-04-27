// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_PLATFORM_IMPL_SPDY_PTR_UTIL_IMPL_H_
#define NET_SPDY_PLATFORM_IMPL_SPDY_PTR_UTIL_IMPL_H_

#include "base/memory/ptr_util.h"

namespace net {

template <typename T, typename... Args>
std::unique_ptr<T> SpdyMakeUniqueImpl(Args&&... args) {
  return base::MakeUnique<T>(std::forward<Args>(args)...);
}

template <typename T>
std::unique_ptr<T> SpdyWrapUniqueImpl(T* ptr) {
  return base::WrapUnique<T>(ptr);
}

}  // namespace net

#endif  // NET_SPDY_PLATFORM_IMPL_SPDY_PTR_UTIL_IMPL_H_
