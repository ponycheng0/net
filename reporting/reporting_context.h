// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_CONTEXT_H_
#define NET_REPORTING_REPORTING_CONTEXT_H_

#include <memory>

#include "base/time/time.h"
#include "net/base/backoff_entry.h"
#include "net/base/net_export.h"
#include "net/reporting/reporting_policy.h"

namespace base {
class Clock;
class TickClock;
}  // namespace base

namespace net {

class ReportingCache;
class ReportingDelegate;
class ReportingDeliveryAgent;
class ReportingEndpointManager;
class ReportingUploader;
class URLRequestContext;

// Contains the various internal classes that make up the Reporting system.
// Wrapped by ReportingService, which provides the external interface.
class NET_EXPORT ReportingContext {
 public:
  static std::unique_ptr<ReportingContext> Create(
      const ReportingPolicy& policy,
      std::unique_ptr<ReportingDelegate> delegate,
      URLRequestContext* request_context);

  ~ReportingContext();

  const ReportingPolicy& policy() { return policy_; }
  ReportingDelegate* delegate() { return delegate_.get(); }

  base::Clock* clock() { return clock_.get(); }
  base::TickClock* tick_clock() { return tick_clock_.get(); }
  ReportingUploader* uploader() { return uploader_.get(); }

  ReportingCache* cache() { return cache_.get(); }
  ReportingEndpointManager* endpoint_manager() {
    return endpoint_manager_.get();
  }
  ReportingDeliveryAgent* delivery_agent() { return delivery_agent_.get(); }

 protected:
  ReportingContext(const ReportingPolicy& policy,
                   std::unique_ptr<ReportingDelegate> delegate,
                   std::unique_ptr<base::Clock> clock,
                   std::unique_ptr<base::TickClock> tick_clock,
                   std::unique_ptr<ReportingUploader> uploader);

 private:
  ReportingPolicy policy_;
  std::unique_ptr<ReportingDelegate> delegate_;

  std::unique_ptr<base::Clock> clock_;
  std::unique_ptr<base::TickClock> tick_clock_;
  std::unique_ptr<ReportingUploader> uploader_;

  std::unique_ptr<ReportingCache> cache_;

  // |endpoint_manager_| must come after |tick_clock_| and |cache_|.
  std::unique_ptr<ReportingEndpointManager> endpoint_manager_;

  // |delivery_agent_| must come after |tick_clock_|, |uploader_|, |cache_|,
  // and |endpoint_manager_|.
  std::unique_ptr<ReportingDeliveryAgent> delivery_agent_;

  DISALLOW_COPY_AND_ASSIGN(ReportingContext);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_CONTEXT_H_
