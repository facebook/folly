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
#include <folly/synchronization/Hazptr.h>

namespace folly {

struct RequestContext::State : hazptr_obj_base<State> {
  std::map<std::string, std::shared_ptr<RequestData>> requestData_;
  std::set<RequestData*> callbackData_;
};

RequestContext::RequestContext() : state_(new State) {}

RequestContext::~RequestContext() {
  auto p = state_.load(std::memory_order_relaxed);
  delete p;
}

bool RequestContext::doSetContextData(
    const std::string& val,
    std::unique_ptr<RequestData>& data,
    bool strict) {
  State* p{nullptr};

  {
    std::lock_guard<std::mutex> g(m_);
    p = state_.load(std::memory_order_acquire);

    bool conflict = false;
    auto it = p->requestData_.find(val);
    if (it != p->requestData_.end()) {
      if (strict) {
        return false;
      } else {
        LOG_FIRST_N(WARNING, 1) << "Calling RequestContext::setContextData for "
                                << val << " but it is already set";
        conflict = true;
      }
    }

    auto newstate = new State(*p);
    it = newstate->requestData_.find(val);

    if (conflict) {
      if (it->second) {
        if (it->second->hasCallback()) {
          it->second->onUnset();
          newstate->callbackData_.erase(it->second.get());
        }
        it->second.reset();
      }
    }

    if (data && data->hasCallback()) {
      newstate->callbackData_.insert(data.get());
      data->onSet();
    }
    newstate->requestData_[val] = std::move(data);
    state_.store(newstate, std::memory_order_release);
  }

  if (p) {
    p->retire();
  }

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
  hazptr_local<1> hptr;
  auto p = hptr[0].get_protected(state_);
  return p->requestData_.count(val) != 0;
}

RequestData* RequestContext::getContextData(const std::string& val) {
  const std::shared_ptr<RequestData> dflt{nullptr};
  hazptr_local<1> hptr;
  auto p = hptr[0].get_protected(state_);
  p->requestData_.count(val);
  return get_ref_default(p->requestData_, val, dflt).get();
}

const RequestData* RequestContext::getContextData(
    const std::string& val) const {
  const std::shared_ptr<RequestData> dflt{nullptr};
  hazptr_local<1> hptr;
  auto p = hptr[0].get_protected(state_);
  p->requestData_.count(val);
  return get_ref_default(p->requestData_, val, dflt).get();
}

void RequestContext::onSet() {
  hazptr_holder<> hptr;
  auto p = hptr.get_protected(state_);
  for (const auto& data : p->callbackData_) {
    data->onSet();
  }
}

void RequestContext::onUnset() {
  hazptr_holder<> hptr;
  auto p = hptr.get_protected(state_);
  for (const auto& data : p->callbackData_) {
    data->onUnset();
  }
}

std::shared_ptr<RequestContext> RequestContext::createChild() {
  hazptr_local<1> hptr;
  auto p = hptr[0].get_protected(state_);
  auto child = std::make_shared<RequestContext>();
  for (const auto& entry : p->requestData_) {
    auto& key = entry.first;
    auto childData = entry.second->createChild();
    if (childData) {
      child->setContextData(key, std::move(childData));
    }
  }
  return child;
}

void RequestContext::clearContextData(const std::string& val) {
  std::shared_ptr<RequestData> requestData;
  // requestData is deleted while lock is not held, in case
  // destructor contains calls to RequestContext
  State* p{nullptr};

  {
    std::lock_guard<std::mutex> g(m_);
    p = state_.load(std::memory_order_acquire);

    auto it = p->requestData_.find(val);
    if (it == p->requestData_.end()) {
      return;
    }

    auto newstate = new State(*p);

    it = newstate->requestData_.find(val);
    CHECK(it != newstate->requestData_.end());

    if (it->second && it->second->hasCallback()) {
      it->second->onUnset();
      newstate->callbackData_.erase(it->second.get());
    }

    requestData = it->second;
    newstate->requestData_.erase(it);
    state_.store(newstate, std::memory_order_release);
  }

  if (p) {
    p->retire();
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
