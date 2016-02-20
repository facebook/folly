/*
 * Copyright 2016 Facebook, Inc.
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
#include <folly/RWSpinLock.h>
#include <folly/SingletonThreadLocal.h>

namespace folly {

// Some request context that follows an async request through a process
// Everything in the context must be thread safe

class RequestData {
 public:
  virtual ~RequestData() = default;
};

class RequestContext;

// If you do not call create() to create a unique request context,
// this default request context will always be returned, and is never
// copied between threads.
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
    auto context = getStaticContext();
    if (!context) {
      static RequestContext defaultContext;
      return std::addressof(defaultContext);
    }
    return context.get();
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

  // Unlike setContextData, this method does not panic if the key is already
  // present. Returns true iff the new value has been inserted.
  bool setContextDataIfAbsent(const std::string& val,
                              std::unique_ptr<RequestData> data) {
    folly::RWSpinLock::UpgradedHolder guard(lock);
    if (data_.find(val) != data_.end()) {
      return false;
    }

    folly::RWSpinLock::WriteHolder writeGuard(std::move(guard));
    data_[val] = std::move(data);
    return true;
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
    using std::swap;
    swap(ctx, getStaticContext());
    return ctx;
  }

  static std::shared_ptr<RequestContext> saveContext() {
    return getStaticContext();
  }

 private:
  static std::shared_ptr<RequestContext>& getStaticContext();

  folly::RWSpinLock lock;
  std::map<std::string, std::unique_ptr<RequestData>> data_;
};

}
