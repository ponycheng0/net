// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_DATA_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_DATA_JOB_H_

#include <string>

#include "net/url_request/url_request.h"
#include "net/url_request/url_request_simple_job.h"

namespace net {

class URLRequest;

class URLRequestDataJob : public URLRequestSimpleJob {
 public:
  explicit URLRequestDataJob(URLRequest* request);

  static URLRequest::ProtocolFactory Factory;

  // URLRequestSimpleJob
  virtual bool GetData(std::string* mime_type,
                       std::string* charset,
                       std::string* data) const OVERRIDE;

 private:
  virtual ~URLRequestDataJob();

  DISALLOW_COPY_AND_ASSIGN(URLRequestDataJob);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_DATA_JOB_H_
