/*
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
#include "folly/ThreadLocal.h"
#include "folly/RWSpinLock.h"

/**
 * In many cases this header is included as a
 * dependency to libraries which do not need
 * command line flags. GFLAGS is a large binary
 * and thus we do this so that a library which
 * is size sensitive doesn't have to pull in
 * GFLAGS if it doesn't want to.
 */
#ifndef NO_LIB_GFLAGS
  #include <gflags/gflags.h>
  DECLARE_bool(enable_request_context);
#endif

namespace folly {

#ifdef NO_LIB_GFLAGS
  extern bool FLAGS_enable_request_context;
#endif

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
  static bool create() {
    if(!FLAGS_enable_request_context) {
      return false;
    }
    bool prev = getStaticContext().get() != nullptr;
    getStaticContext().reset(new std::shared_ptr<RequestContext>(
                     std::make_shared<RequestContext>()));
    return prev;
  }

  // Get the current context.
  static RequestContext* get() {
    if (!FLAGS_enable_request_context ||
        getStaticContext().get() == nullptr) {
      if (defaultContext == nullptr) {
        defaultContext = new RequestContext;
      }
      return defaultContext;
    }
    return getStaticContext().get()->get();
  }

  // The following API may be used to set per-request data in a thread-safe way.
  // This access is still performance sensitive, so please ask if you need help
  // profiling any use of these functions.
  void setContextData(
    const std::string& val, std::unique_ptr<RequestData> data) {
    if (!FLAGS_enable_request_context) {
      return;
    }

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
    if (FLAGS_enable_request_context) {
      std::shared_ptr<RequestContext> old_ctx;
      if (getStaticContext().get()) {
        old_ctx = *getStaticContext().get();
      }
      if (ctx == nullptr) {
        getStaticContext().reset(nullptr);
      } else {
        getStaticContext().reset(new std::shared_ptr<RequestContext>(ctx));
      }
      return old_ctx;
    }
    return std::shared_ptr<RequestContext>();
  }

  static std::shared_ptr<RequestContext> saveContext() {
    if (!FLAGS_enable_request_context) {
      return std::shared_ptr<RequestContext>();
    }
    if (getStaticContext().get() == nullptr) {
      return std::shared_ptr<RequestContext>();
    } else {
      return *getStaticContext().get();
    }
  }

  // Used to solve static destruction ordering issue.  Any static object
  // that uses RequestContext must call this function in its constructor.
  //
  // See below link for more details.
  // http://stackoverflow.com/questions/335369/
  // finding-c-static-initialization-order-problems#335746
  static folly::ThreadLocalPtr<std::shared_ptr<RequestContext>>&
  getStaticContext() {
    static folly::ThreadLocalPtr<std::shared_ptr<RequestContext> > context;
    return context;
  }

 private:
  folly::RWSpinLock lock;
  std::map<std::string, std::unique_ptr<RequestData>> data_;
};

/**
 * Set the request context for a specific scope. For example,
 * if you ran a part of a request in another thread you could
 * use RequestContextGuard to copy apply the request context
 * inside the other therad.
 */
class RequestContextGuard {
 public:
  explicit RequestContextGuard(std::shared_ptr<RequestContext> ctx) {
    oldctx_ = RequestContext::setContext(std::move(ctx));
  }

  ~RequestContextGuard() {
    RequestContext::setContext(std::move(oldctx_));
  }

 private:
  std::shared_ptr<RequestContext> oldctx_;
};

}
