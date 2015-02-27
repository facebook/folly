/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#pragma once

#include <map>
#include <memory>
#include <glog/logging.h>
#include <folly/ThreadLocal.h>
#include <folly/RWSpinLock.h>

namespace folly {

// Some request context that follows an async request through a process
// Everything in the context must be thread safe

class RequestData {
 public:
  virtual ~RequestData() {}
};

class RequestContext;

// If you do not call create() to create a unique request context,
// this default request context will always be returned, and is never
// copied between threads.
extern RequestContext* defaultContext;

class RequestContext {
 public:
  // Create a unique requext context for this request.
  // It will be passed between queues / threads (where implemented),
  // so it should be valid for the lifetime of the request.
  static void create() {
    getStaticContext() = std::make_shared<RequestContext>();
  }

  // Get the current context.
  static RequestContext* get() {
    if (getStaticContext() == nullptr) {
      if (defaultContext == nullptr) {
        defaultContext = new RequestContext;
      }
      return defaultContext;
    }
    return getStaticContext().get();
  }

  // The following API may be used to set per-request data in a thread-safe way.
  // This access is still performance sensitive, so please ask if you need help
  // profiling any use of these functions.
  void setContextData(
    const std::string& val, std::unique_ptr<RequestData> data) {
    folly::RWSpinLock::WriteHolder guard(lock);
    if (data_.find(val) != data_.end()) {
      LOG_FIRST_N(WARNING, 1) <<
        "Called RequestContext::setContextData with data already set";

      data_[val] = nullptr;
    } else {
      data_[val] = std::move(data);
    }
  }

  bool hasContextData(const std::string& val) {
    folly::RWSpinLock::ReadHolder guard(lock);
    return data_.find(val) != data_.end();
  }

  RequestData* getContextData(const std::string& val) {
    folly::RWSpinLock::ReadHolder guard(lock);
    auto r = data_.find(val);
    if (r == data_.end()) {
      return nullptr;
    } else {
      return r->second.get();
    }
  }

  void clearContextData(const std::string& val) {
    folly::RWSpinLock::WriteHolder guard(lock);
    data_.erase(val);
  }

  // The following API is used to pass the context through queues / threads.
  // saveContext is called to geta shared_ptr to the context, and
  // setContext is used to reset it on the other side of the queue.
  //
  // A shared_ptr is used, because many request may fan out across
  // multiple threads, or do post-send processing, etc.

  static std::shared_ptr<RequestContext>
  setContext(std::shared_ptr<RequestContext> ctx) {
    std::shared_ptr<RequestContext> old_ctx;
    if (getStaticContext()) {
      old_ctx = getStaticContext();
    }
    getStaticContext() = ctx;
    return old_ctx;
  }

  static std::shared_ptr<RequestContext> saveContext() {
    return getStaticContext();
  }

  // Used to solve static destruction ordering issue.  Any static object
  // that uses RequestContext must call this function in its constructor.
  //
  // See below link for more details.
  // http://stackoverflow.com/questions/335369/
  // finding-c-static-initialization-order-problems#335746
  static std::shared_ptr<RequestContext>&
  getStaticContext() {
    static folly::ThreadLocal<std::shared_ptr<RequestContext> > context;
    return *context;
  }

 private:
  folly::RWSpinLock lock;
  std::map<std::string, std::unique_ptr<RequestData>> data_;
};

}
