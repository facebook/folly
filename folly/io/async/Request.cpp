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

#include <folly/io/async/Request.h>

#include <glog/logging.h>

#include <folly/SingletonThreadLocal.h>

namespace folly {

void RequestContext::setContextData(
    const std::string& val,
    std::unique_ptr<RequestData> data) {
  folly::RWSpinLock::WriteHolder guard(lock);
  if (data_.find(val) != data_.end()) {
    LOG_FIRST_N(WARNING, 1)
        << "Called RequestContext::setContextData with data already set";

    data_[val] = nullptr;
  } else {
    data_[val] = std::move(data);
  }
}

bool RequestContext::setContextDataIfAbsent(
    const std::string& val,
    std::unique_ptr<RequestData> data) {
  folly::RWSpinLock::UpgradedHolder guard(lock);
  if (data_.find(val) != data_.end()) {
    return false;
  }

  folly::RWSpinLock::WriteHolder writeGuard(std::move(guard));
  data_[val] = std::move(data);
  return true;
}

bool RequestContext::hasContextData(const std::string& val) const {
  folly::RWSpinLock::ReadHolder guard(lock);
  return data_.find(val) != data_.end();
}

RequestData* RequestContext::getContextData(const std::string& val) {
  folly::RWSpinLock::ReadHolder guard(lock);
  auto r = data_.find(val);
  if (r == data_.end()) {
    return nullptr;
  } else {
    return r->second.get();
  }
}

const RequestData* RequestContext::getContextData(
    const std::string& val) const {
  folly::RWSpinLock::ReadHolder guard(lock);
  auto r = data_.find(val);
  if (r == data_.end()) {
    return nullptr;
  } else {
    return r->second.get();
  }
}

void RequestContext::onSet() {
  folly::RWSpinLock::ReadHolder guard(lock);
  for (auto const& ent : data_) {
    if (RequestData* data = ent.second.get()) {
      data->onSet();
    }
  }
}

void RequestContext::onUnset() {
  folly::RWSpinLock::ReadHolder guard(lock);
  for (auto const& ent : data_) {
    if (RequestData* data = ent.second.get()) {
      data->onUnset();
    }
  }
}

void RequestContext::clearContextData(const std::string& val) {
  folly::RWSpinLock::WriteHolder guard(lock);
  data_.erase(val);
}

std::shared_ptr<RequestContext> RequestContext::setContext(
    std::shared_ptr<RequestContext> ctx) {
  auto& curCtx = getStaticContext();
  if (ctx != curCtx) {
    using std::swap;
    if (curCtx) {
      curCtx->onUnset();
    }
    swap(ctx, curCtx);
    if (curCtx) {
      curCtx->onSet();
    }
  }
  return ctx;
}

std::shared_ptr<RequestContext>& RequestContext::getStaticContext() {
  using SingletonT = SingletonThreadLocal<std::shared_ptr<RequestContext>>;
  static SingletonT singleton;

  return singleton.get();
}
}
