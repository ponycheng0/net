// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_HOST_RESOLVER_H_
#define NET_BASE_HOST_RESOLVER_H_

#include <string>

#include "base/ref_counted.h"
#include "googleurl/src/gurl.h"
#include "net/base/completion_callback.h"

class MessageLoop;

namespace net {

class AddressList;
class LoadLog;

// This class represents the task of resolving hostnames (or IP address
// literal) to an AddressList object.
//
// HostResolver can handle multiple requests at a time, so when cancelling a
// request the RequestHandle that was returned by Resolve() needs to be
// given.  A simpler alternative for consumers that only have 1 outstanding
// request at a time is to create a SingleRequestHostResolver wrapper around
// HostResolver (which will automatically cancel the single request when it
// goes out of scope).
class HostResolver : public base::RefCounted<HostResolver> {
 public:
  // The parameters for doing a Resolve(). |hostname| and |port| are required,
  // the rest are optional (and have reasonable defaults).
  class RequestInfo {
   public:
    RequestInfo(const std::string& hostname, int port)
        : hostname_(hostname),
          port_(port),
          allow_cached_response_(true),
          is_speculative_(false) {}

    const int port() const { return port_; }
    const std::string& hostname() const { return hostname_; }

    bool allow_cached_response() const { return allow_cached_response_; }
    void set_allow_cached_response(bool b) { allow_cached_response_ = b; }

    bool is_speculative() const { return is_speculative_; }
    void set_is_speculative(bool b) { is_speculative_ = b; }

    const GURL& referrer() const { return referrer_; }
    void set_referrer(const GURL& referrer) { referrer_ = referrer; }

   private:
    // The hostname to resolve.
    std::string hostname_;

    // The port number to set in the result's sockaddrs.
    int port_;

    // Whether it is ok to return a result from the host cache.
    bool allow_cached_response_;

    // Whether this request was started by the DNS prefetcher.
    bool is_speculative_;

    // Optional data for consumption by observers. This is the URL of the
    // page that lead us to the navigation, for DNS prefetcher's benefit.
    GURL referrer_;
  };

  // Interface for observing the requests that flow through a HostResolver.
  class Observer {
   public:
    virtual ~Observer() {}

    // Called at the start of HostResolver::Resolve(). |id| is a unique number
    // given to the request, so it can be matched up with a corresponding call
    // to OnFinishResolutionWithStatus() or OnCancelResolution().
    virtual void OnStartResolution(int id, const RequestInfo& info) = 0;

    // Called on completion of request |id|. Note that if the request was
    // cancelled, OnCancelResolution() will be called instead.
    virtual void OnFinishResolutionWithStatus(int id, bool was_resolved,
                                              const RequestInfo& info) = 0;

    // Called when request |id| has been cancelled. A request is "cancelled"
    // if either the HostResolver is destroyed while a resolution is in
    // progress, or HostResolver::CancelRequest() is called.
    virtual void OnCancelResolution(int id, const RequestInfo& info) = 0;
  };

  // Opaque type used to cancel a request.
  typedef void* RequestHandle;

  // If any completion callbacks are pending when the resolver is destroyed,
  // the host resolutions are cancelled, and the completion callbacks will not
  // be called.
  virtual ~HostResolver() {}

  // Resolves the given hostname (or IP address literal), filling out the
  // |addresses| object upon success.  The |info.port| parameter will be set as
  // the sin(6)_port field of the sockaddr_in{6} struct.  Returns OK if
  // successful or an error code upon failure.
  //
  // When callback is null, the operation completes synchronously.
  //
  // When callback is non-null, the operation will be performed asynchronously.
  // ERR_IO_PENDING is returned if it has been scheduled successfully. Real
  // result code will be passed to the completion callback. If |req| is
  // non-NULL, then |*req| will be filled with a handle to the async request.
  // This handle is not valid after the request has completed.
  //
  // Profiling information for the request is saved to |load_log| if non-NULL.
  virtual int Resolve(const RequestInfo& info,
                      AddressList* addresses,
                      CompletionCallback* callback,
                      RequestHandle* out_req,
                      LoadLog* load_log) = 0;

  // Cancels the specified request. |req| is the handle returned by Resolve().
  // After a request is cancelled, its completion callback will not be called.
  virtual void CancelRequest(RequestHandle req) = 0;

  // Adds an observer to this resolver. The observer will be notified of the
  // start and completion of all requests (excluding cancellation). |observer|
  // must remain valid for the duration of this HostResolver's lifetime.
  virtual void AddObserver(Observer* observer) = 0;

  // Unregisters an observer previously added by AddObserver().
  virtual void RemoveObserver(Observer* observer) = 0;

  // TODO(eroman): temp hack for http://crbug.com/18373
  virtual void Shutdown() = 0;

 protected:
  HostResolver() { }

 private:
  DISALLOW_COPY_AND_ASSIGN(HostResolver);
};

// This class represents the task of resolving a hostname (or IP address
// literal) to an AddressList object.  It wraps HostResolver to resolve only a
// single hostname at a time and cancels this request when going out of scope.
class SingleRequestHostResolver {
 public:
  explicit SingleRequestHostResolver(HostResolver* resolver);

  // If a completion callback is pending when the resolver is destroyed, the
  // host resolution is cancelled, and the completion callback will not be
  // called.
  ~SingleRequestHostResolver();

  // Resolves the given hostname (or IP address literal), filling out the
  // |addresses| object upon success. See HostResolver::Resolve() for details.
  int Resolve(const HostResolver::RequestInfo& info,
              AddressList* addresses,
              CompletionCallback* callback,
              LoadLog* load_log);

 private:
  // Callback for when the request to |resolver_| completes, so we dispatch
  // to the user's callback.
  void OnResolveCompletion(int result);

  // The actual host resolver that will handle the request.
  scoped_refptr<HostResolver> resolver_;

  // The current request (if any).
  HostResolver::RequestHandle cur_request_;
  CompletionCallback* cur_request_callback_;

  // Completion callback for when request to |resolver_| completes.
  net::CompletionCallbackImpl<SingleRequestHostResolver> callback_;

  DISALLOW_COPY_AND_ASSIGN(SingleRequestHostResolver);
};

// Creates a HostResolver implementation that queries the underlying system.
// (Except if a unit-test has changed the global HostResolverProc using
// ScopedHostResolverProc to intercept requests to the system).
HostResolver* CreateSystemHostResolver();

}  // namespace net

#endif  // NET_BASE_HOST_RESOLVER_H_
