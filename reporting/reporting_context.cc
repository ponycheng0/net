// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_context.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/base/backoff_entry.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_delegate.h"
#include "net/reporting/reporting_delivery_agent.h"
#include "net/reporting/reporting_endpoint_manager.h"
#include "net/reporting/reporting_policy.h"

namespace net {

class URLRequestContext;

namespace {

class ReportingContextImpl : public ReportingContext {
 public:
  ReportingContextImpl(const ReportingPolicy& policy,
                       std::unique_ptr<ReportingDelegate> delegate,
                       URLRequestContext* request_context)
      : ReportingContext(policy,
                         std::move(delegate),
                         base::MakeUnique<base::DefaultClock>(),
                         base::MakeUnique<base::DefaultTickClock>(),
                         ReportingUploader::Create(request_context)) {}
};

}  // namespace

// static
std::unique_ptr<ReportingContext> ReportingContext::Create(
    const ReportingPolicy& policy,
    std::unique_ptr<ReportingDelegate> delegate,
    URLRequestContext* request_context) {
  return base::MakeUnique<ReportingContextImpl>(policy, std::move(delegate),
                                                request_context);
}

ReportingContext::~ReportingContext() {}

ReportingContext::ReportingContext(const ReportingPolicy& policy,
                                   std::unique_ptr<ReportingDelegate> delegate,
                                   std::unique_ptr<base::Clock> clock,
                                   std::unique_ptr<base::TickClock> tick_clock,
                                   std::unique_ptr<ReportingUploader> uploader)
    : policy_(policy),
      delegate_(std::move(delegate)),
      clock_(std::move(clock)),
      tick_clock_(std::move(tick_clock)),
      uploader_(std::move(uploader)),
      cache_(base::MakeUnique<ReportingCache>(this)),
      endpoint_manager_(base::MakeUnique<ReportingEndpointManager>(this)),
      delivery_agent_(base::MakeUnique<ReportingDeliveryAgent>(this)) {}

}  // namespace net
