/*
 * Copyright 2016-present Facebook, Inc.
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

#include <folly/io/async/Request.h>
#include <folly/tracing/StaticTracepoint.h>

#include <glog/logging.h>

#include <folly/MapUtil.h>
#include <folly/SingletonThreadLocal.h>

namespace folly {

bool RequestContext::doSetContextData(
    const std::string& val,
    std::unique_ptr<RequestData>& data,
    bool strict) {
  auto ulock = state_.ulock();

  bool conflict = false;
  auto it = ulock->requestData_.find(val);
  if (it != ulock->requestData_.end()) {
    if (strict) {
      return false;
    } else {
      LOG_FIRST_N(WARNING, 1) << "Calling RequestContext::setContextData for "
                              << val << " but it is already set";
      conflict = true;
    }
  }

  auto wlock = ulock.moveFromUpgradeToWrite();
  if (conflict) {
    if (it->second) {
      if (it->second->hasCallback()) {
        wlock->callbackData_.erase(it->second.get());
      }
      it->second.reset(nullptr);
    }
    return true;
  }

  if (data && data->hasCallback()) {
    wlock->callbackData_.insert(data.get());
  }
  wlock->requestData_[val] = std::move(data);

  return true;
}

void RequestContext::setContextData(
    const std::string& val,
    std::unique_ptr<RequestData> data) {
  doSetContextData(val, data, false /* strict */);
}

bool RequestContext::setContextDataIfAbsent(
    const std::string& val,
    std::unique_ptr<RequestData> data) {
  return doSetContextData(val, data, true /* strict */);
}

bool RequestContext::hasContextData(const std::string& val) const {
  return state_.rlock()->requestData_.count(val);
}

RequestData* RequestContext::getContextData(const std::string& val) {
  const std::unique_ptr<RequestData> dflt{nullptr};
  return get_ref_default(state_.rlock()->requestData_, val, dflt).get();
}

const RequestData* RequestContext::getContextData(
    const std::string& val) const {
  const std::unique_ptr<RequestData> dflt{nullptr};
  return get_ref_default(state_.rlock()->requestData_, val, dflt).get();
}

void RequestContext::onSet() {
  auto rlock = state_.rlock();
  for (const auto& data : rlock->callbackData_) {
    data->onSet();
  }
}

void RequestContext::onUnset() {
  auto rlock = state_.rlock();
  for (const auto& data : rlock->callbackData_) {
    data->onUnset();
  }
}

void RequestContext::clearContextData(const std::string& val) {
  std::unique_ptr<RequestData> requestData;
  // Delete the RequestData after giving up the wlock just in case one of the
  // RequestData destructors will try to grab the lock again.
  {
    auto ulock = state_.ulock();
    auto it = ulock->requestData_.find(val);
    if (it == ulock->requestData_.end()) {
      return;
    }

    auto wlock = ulock.moveFromUpgradeToWrite();
    if (it->second && it->second->hasCallback()) {
      wlock->callbackData_.erase(it->second.get());
    }

    requestData = std::move(it->second);
    wlock->requestData_.erase(it);
  }
}

std::shared_ptr<RequestContext> RequestContext::setContext(
    std::shared_ptr<RequestContext> ctx) {
  auto& curCtx = getStaticContext();
  if (ctx != curCtx) {
    FOLLY_SDT(folly, request_context_switch_before, curCtx.get(), ctx.get());
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
  return SingletonT::get();
}

RequestContext* RequestContext::get() {
  auto& context = getStaticContext();
  if (!context) {
    static RequestContext defaultContext;
    return std::addressof(defaultContext);
  }
  return context.get();
}
} // namespace folly
