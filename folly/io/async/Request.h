/*
 * Copyright 2014-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <map>
#include <memory>
#include <set>

#include <folly/Synchronized.h>

namespace folly {

// Some request context that follows an async request through a process
// Everything in the context must be thread safe

class RequestData {
 public:
  virtual ~RequestData() = default;

  // Avoid calling RequestContext::setContextData, setContextDataIfAbsent, or
  // clearContextData from these callbacks. Doing so will cause deadlock. We
  // could fix these deadlocks, but only at significant performance penalty, so
  // just don't do it!

  virtual bool hasCallback() = 0;
  // Callback executed when setting RequestContext. Make sure your RequestData
  // instance overrides the hasCallback method to return true otherwise
  // the callback will not be executed
  virtual void onSet() {}
  // Callback executed when unsetting RequestContext. Make sure your RequestData
  // instance overrides the hasCallback method to return true otherwise
  // the callback will not be executed
  virtual void onUnset() {}
};

// If you do not call create() to create a unique request context,
// this default request context will always be returned, and is never
// copied between threads.
class RequestContext {
 public:
  // Create a unique request context for this request.
  // It will be passed between queues / threads (where implemented),
  // so it should be valid for the lifetime of the request.
  static void create() {
    setContext(std::make_shared<RequestContext>());
  }

  // Get the current context.
  static RequestContext* get();

  // The following APIs are used to add, remove and access RequestData instance
  // in the RequestContext instance, normally used for per-RequestContext
  // tracking or callback on set and unset. These APIs are Thread-safe.
  // These APIs are performance sensitive, so please ask if you need help
  // profiling any use of these APIs.

  // Add RequestData instance "data" to this RequestContext instance, with
  // string identifier "val". If the same string identifier has already been
  // used, will print a warning message for the first time, clear the existing
  // RequestData instance for "val", and **not** add "data".
  void setContextData(
      const std::string& val,
      std::unique_ptr<RequestData> data);

  // Add RequestData instance "data" to this RequestContext instance, with
  // string identifier "val". If the same string identifier has already been
  // used, return false and do nothing. Otherwise add "data" and return true.
  bool setContextDataIfAbsent(
      const std::string& val,
      std::unique_ptr<RequestData> data);

  // Remove the RequestData instance with string identifier "val", if it exists.
  void clearContextData(const std::string& val);

  // Returns true if and only if the RequestData instance with string identifier
  // "val" exists in this RequestContext instnace.
  bool hasContextData(const std::string& val) const;

  // Get (constant) raw pointer of the RequestData instance with string
  // identifier "val" if it exists, otherwise returns null pointer.
  RequestData* getContextData(const std::string& val);
  const RequestData* getContextData(const std::string& val) const;

  void onSet();
  void onUnset();

  // The following API is used to pass the context through queues / threads.
  // saveContext is called to get a shared_ptr to the context, and
  // setContext is used to reset it on the other side of the queue.
  //
  // Whenever possible, use RequestContextScopeGuard instead of setContext
  // to make sure that RequestContext is reset to the original value when
  // we exit the scope.
  //
  // A shared_ptr is used, because many request may fan out across
  // multiple threads, or do post-send processing, etc.
  static std::shared_ptr<RequestContext> setContext(
      std::shared_ptr<RequestContext> ctx);

  static std::shared_ptr<RequestContext> saveContext() {
    return getStaticContext();
  }

 private:
  static std::shared_ptr<RequestContext>& getStaticContext();

  bool doSetContextData(
      const std::string& val,
      std::unique_ptr<RequestData>& data,
      bool strict);

  struct State {
    std::map<std::string, std::unique_ptr<RequestData>> requestData_;
    std::set<RequestData*> callbackData_;
  };
  folly::Synchronized<State> state_;
};

class RequestContextScopeGuard {
 private:
  std::shared_ptr<RequestContext> prev_;

 public:
  RequestContextScopeGuard(const RequestContextScopeGuard&) = delete;
  RequestContextScopeGuard& operator=(const RequestContextScopeGuard&) = delete;
  RequestContextScopeGuard(RequestContextScopeGuard&&) = delete;
  RequestContextScopeGuard& operator=(RequestContextScopeGuard&&) = delete;

  // Create a new RequestContext and reset to the original value when
  // this goes out of scope.
  RequestContextScopeGuard() : prev_(RequestContext::saveContext()) {
    RequestContext::create();
  }

  // Set a RequestContext that was previously captured by saveContext(). It will
  // be automatically reset to the original value when this goes out of scope.
  explicit RequestContextScopeGuard(std::shared_ptr<RequestContext> ctx)
      : prev_(RequestContext::setContext(std::move(ctx))) {}

  ~RequestContextScopeGuard() {
    RequestContext::setContext(std::move(prev_));
  }
};
} // namespace folly
