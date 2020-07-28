/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/io/async/Request.h>
#include <folly/experimental/SingleWriterFixedHashMap.h>
#include <folly/synchronization/Hazptr.h>
#include <folly/tracing/StaticTracepoint.h>

#include <glog/logging.h>

#include <folly/MapUtil.h>
#include <folly/SingletonThreadLocal.h>

DEFINE_bool(
    folly_reqctx_use_hazptr,
    true,
    "RequestContext implementation using hazard pointers");

namespace folly {

namespace {
using SingletonT =
    SingletonThreadLocal<RequestContext::StaticContext, RequestContext>;
}

RequestToken::RequestToken(const std::string& str) {
  auto& cache = getCache();
  {
    auto c = cache.rlock();
    auto res = c->find(str);
    if (res != c->end()) {
      token_ = res->second;
      return;
    }
  }
  auto c = cache.wlock();
  auto res = c->find(str);
  if (res != c->end()) {
    token_ = res->second;
    return;
  }
  static uint32_t nextToken{1};

  token_ = nextToken++;
  (*c)[str] = token_;
}

std::string RequestToken::getDebugString() const {
  auto& cache = getCache();
  auto c = cache.rlock();
  for (auto& v : *c) {
    if (v.second == token_) {
      return v.first;
    }
  }
  throw std::logic_error("Could not find debug string in RequestToken");
}

Synchronized<F14FastMap<std::string, uint32_t>>& RequestToken::getCache() {
  static Indestructible<Synchronized<F14FastMap<std::string, uint32_t>>> cache;
  return *cache;
}

FOLLY_ALWAYS_INLINE
void RequestData::acquireRef() {
  auto rc = keepAliveCounter_.fetch_add(
      kClearCount + kDeleteCount, std::memory_order_relaxed);
  DCHECK_GE(rc, 0);
}

void RequestData::releaseRefClearOnly() {
  auto rc =
      keepAliveCounter_.fetch_sub(kClearCount, std::memory_order_acq_rel) -
      kClearCount;
  DCHECK_GT(rc, 0);
  if (rc < kClearCount) {
    this->onClear();
  }
}

void RequestData::releaseRefDeleteOnly() {
  auto rc =
      keepAliveCounter_.fetch_sub(kDeleteCount, std::memory_order_acq_rel) -
      kDeleteCount;
  DCHECK_GE(rc, 0);
  if (rc == 0) {
    delete this;
  }
}

FOLLY_ALWAYS_INLINE
void RequestData::releaseRefClearDelete() {
  auto rc = keepAliveCounter_.load(std::memory_order_acquire);
  if (FOLLY_LIKELY(rc == (kClearCount + kDeleteCount))) {
    this->onClear();
    delete this;
  } else {
    releaseRefClearDeleteSlow();
  }
}

FOLLY_NOINLINE
void RequestData::releaseRefClearDeleteSlow() {
  releaseRefClearOnly();
  releaseRefDeleteOnly();
}

void RequestData::DestructPtr::operator()(RequestData* ptr) {
  if (ptr) {
    auto keepAliveCounter =
        ptr->keepAliveCounter_.fetch_sub(1, std::memory_order_acq_rel);
    // Note: this is the value before decrement, hence == 1 check
    DCHECK(keepAliveCounter > 0);
    if (keepAliveCounter == 1) {
      ptr->onClear();
      delete ptr;
    }
  }
}

/* static */ RequestData::SharedPtr RequestData::constructPtr(
    RequestData* ptr) {
  if (ptr) {
    auto keepAliveCounter =
        ptr->keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
    DCHECK(keepAliveCounter >= 0);
  }
  return SharedPtr(ptr);
}

// The Combined struct keeps the two structures for context data
// and callbacks together, so that readers can protect consistent
// versions of the two structures together using hazard pointers.
struct RequestContext::StateHazptr::Combined : hazptr_obj_base<Combined> {
  static constexpr size_t kInitialCapacity = 4;
  static constexpr size_t kSlackReciprocal = 4; // unused >= 1/4 capacity

  // This must be optimized for lookup, its hot path is getContextData
  // Efficiency of copying the container also matters in setShallowCopyContext
  SingleWriterFixedHashMap<RequestToken, RequestData*> requestData_;
  // This must be optimized for iteration, its hot path is setContext
  SingleWriterFixedHashMap<RequestData*, bool> callbackData_;
  // Vector of cleared data. Accessed only sequentially by writers.
  std::vector<std::pair<RequestToken, RequestData*>> cleared_;

  Combined()
      : requestData_(kInitialCapacity), callbackData_(kInitialCapacity) {}

  Combined(const Combined& o)
      : Combined(o.requestData_.capacity(), o.callbackData_.capacity(), o) {}

  Combined(size_t dataCapacity, size_t callbackCapacity, const Combined& o)
      : requestData_(dataCapacity, o.requestData_),
        callbackData_(callbackCapacity, o.callbackData_) {}

  Combined(Combined&&) = delete;
  Combined& operator=(const Combined&) = delete;
  Combined& operator=(Combined&&) = delete;

  ~Combined() {
    releaseDataRefs();
  }

  /* acquireDataRefs - Called at most once per Combined instance. */
  void acquireDataRefs() {
    for (auto it = requestData_.begin(); it != requestData_.end(); ++it) {
      auto p = it.value();
      if (p) {
        p->acquireRef();
      }
    }
  }

  /* releaseDataRefs - Called only once from ~Combined */
  void releaseDataRefs() {
    if (!cleared_.empty()) {
      for (auto& pair : cleared_) {
        pair.second->releaseRefDeleteOnly();
        requestData_.erase(pair.first);
      }
    }
    for (auto it = requestData_.begin(); it != requestData_.end(); ++it) {
      RequestData* data = it.value();
      if (data) {
        data->releaseRefClearDelete();
      }
    }
  }

  /* needExpand */
  bool needExpand() {
    return needExpandRequestData() || needExpandCallbackData();
  }

  /* needExpandRequestData */
  bool needExpandRequestData() {
    return kSlackReciprocal * (requestData_.available() - 1) <
        requestData_.capacity();
  }

  /* needExpandCallbackData */
  bool needExpandCallbackData() {
    return kSlackReciprocal * (callbackData_.available() - 1) <
        callbackData_.capacity();
  }
}; // Combined

RequestContext::StateHazptr::StateHazptr() = default;

FOLLY_ALWAYS_INLINE
RequestContext::StateHazptr::StateHazptr(const StateHazptr& o) {
  Combined* oc = o.combined();
  if (oc) {
    auto p = new Combined(*oc);
    p->acquireDataRefs();
    setCombined(p);
  }
}

RequestContext::StateHazptr::~StateHazptr() {
  cohort_.shutdown_and_reclaim();
  auto p = combined();
  if (p) {
    delete p;
  }
}

FOLLY_ALWAYS_INLINE
RequestContext::StateHazptr::Combined* RequestContext::StateHazptr::combined()
    const {
  return combined_.load(std::memory_order_acquire);
}

FOLLY_ALWAYS_INLINE
RequestContext::StateHazptr::Combined*
RequestContext::StateHazptr::ensureCombined() {
  auto c = combined();
  if (!c) {
    c = new Combined;
    setCombined(c);
  }
  return c;
}

FOLLY_ALWAYS_INLINE
void RequestContext::StateHazptr::setCombined(Combined* p) {
  p->set_cohort_tag(&cohort_);
  combined_.store(p, std::memory_order_release);
}

FOLLY_ALWAYS_INLINE
bool RequestContext::StateHazptr::doSetContextData(
    const RequestToken& token,
    std::unique_ptr<RequestData>& data,
    DoSetBehaviour behaviour,
    bool safe) {
  SetContextDataResult result;
  if (safe) {
    result = doSetContextDataHelper(token, data, behaviour, safe);
  } else {
    std::lock_guard<std::mutex> g(mutex_);
    result = doSetContextDataHelper(token, data, behaviour, safe);
  }
  if (result.unexpected) {
    LOG_FIRST_N(WARNING, 1)
        << "Calling RequestContext::setContextData for "
        << token.getDebugString() << " but it is already set";
  }
  if (result.replaced) {
    result.replaced->retire(); // Retire to hazptr library
  }
  return result.changed;
}

FOLLY_ALWAYS_INLINE
RequestContext::StateHazptr::SetContextDataResult
RequestContext::StateHazptr::doSetContextDataHelper(
    const RequestToken& token,
    std::unique_ptr<RequestData>& data,
    DoSetBehaviour behaviour,
    bool safe) {
  bool unexpected = false;
  Combined* cur = ensureCombined();
  Combined* replaced = nullptr;
  auto it = cur->requestData_.find(token);
  bool found = it != cur->requestData_.end();
  if (found) {
    if (behaviour == DoSetBehaviour::SET_IF_ABSENT) {
      return {false /* no changes made */,
              false /* nothing unexpected */,
              nullptr /* combined not replaced */};
    }
    RequestData* oldData = it.value();
    // Always erase old data (and run onUnset callback, if any).
    // Old data will always be overwritten either by the new data
    // (if behavior is OVERWRITE) or by nullptr (if behavior is SET).
    Combined* newCombined = eraseOldData(cur, token, oldData, safe);
    DCHECK(oldData != nullptr || newCombined == nullptr);
    if (newCombined) {
      replaced = cur;
      cur = newCombined;
    }
    if (behaviour == DoSetBehaviour::SET) {
      // The expected behavior for SET when found is to reset the
      // pointer and warn, without updating to the new data.
      bool inserted = cur->requestData_.insert(token, nullptr);
      DCHECK(inserted);
      unexpected = true;
    } else {
      DCHECK(behaviour == DoSetBehaviour::OVERWRITE);
    }
  }
  if (!unexpected) {
    // Replace combined if needed, call onSet if any, insert new data.
    Combined* newCombined = insertNewData(cur, token, data, found);
    if (newCombined) {
      replaced = cur;
      cur = newCombined;
    }
  }
  if (replaced) {
    // Now the new Combined is consistent. Safe to publish.
    setCombined(cur);
  }
  return {true, /* changes were made */
          unexpected,
          replaced};
}

FOLLY_ALWAYS_INLINE
RequestContext::StateHazptr::Combined* FOLLY_NULLABLE
RequestContext::StateHazptr::eraseOldData(
    RequestContext::StateHazptr::Combined* cur,
    const RequestToken& token,
    RequestData* olddata,
    bool safe) {
  Combined* newCombined = nullptr;
  // Call onUnset, if any.
  if (olddata && olddata->hasCallback()) {
    olddata->onUnset();
    bool erased = cur->callbackData_.erase(olddata);
    DCHECK(erased);
  }
  if (safe || olddata == nullptr) {
    // If the caller guarantees thread-safety or the old data is null,
    // then erase the entry in the current version.
    bool erased = cur->requestData_.erase(token);
    DCHECK(erased);
    if (olddata) {
      olddata->releaseRefClearDelete();
    }
  } else {
    // If there may be concurrent readers, then copy-on-erase.
    // Update the data reference counts to account for the
    // existence of the new copy.
    newCombined = new Combined(*cur);
    bool erased = newCombined->requestData_.erase(token);
    DCHECK(erased);
    newCombined->acquireDataRefs();
  }
  return newCombined;
}

FOLLY_ALWAYS_INLINE
RequestContext::StateHazptr::Combined* FOLLY_NULLABLE
RequestContext::StateHazptr::insertNewData(
    RequestContext::StateHazptr::Combined* cur,
    const RequestToken& token,
    std::unique_ptr<RequestData>& data,
    bool found) {
  Combined* newCombined = nullptr;
  // Update value to point to the new data.
  if (!found && cur->needExpand()) {
    // Replace the current Combined with an expanded one
    newCombined = expand(cur);
    cur = newCombined;
    cur->acquireDataRefs();
  }
  if (data && data->hasCallback()) {
    // If data has callback, insert in callback structure, call onSet
    bool inserted = cur->callbackData_.insert(data.get(), true);
    DCHECK(inserted);
    data->onSet();
  }
  if (data) {
    data->acquireRef();
  }
  bool inserted = cur->requestData_.insert(token, data.release());
  DCHECK(inserted);
  return newCombined;
}

FOLLY_ALWAYS_INLINE
bool RequestContext::StateHazptr::hasContextData(
    const RequestToken& token) const {
  hazptr_local<1> h;
  Combined* combined = h[0].get_protected(combined_);
  return combined ? combined->requestData_.contains(token) : false;
}

FOLLY_ALWAYS_INLINE
RequestData* FOLLY_NULLABLE
RequestContext::StateHazptr::getContextData(const RequestToken& token) {
  hazptr_local<1> h;
  Combined* combined = h[0].get_protected(combined_);
  if (!combined) {
    return nullptr;
  }
  auto& reqData = combined->requestData_;
  auto it = reqData.find(token);
  return it == reqData.end() ? nullptr : it.value();
}

FOLLY_ALWAYS_INLINE
const RequestData* FOLLY_NULLABLE
RequestContext::StateHazptr::getContextData(const RequestToken& token) const {
  hazptr_local<1> h;
  Combined* combined = h[0].get_protected(combined_);
  if (!combined) {
    return nullptr;
  }
  auto& reqData = combined->requestData_;
  auto it = reqData.find(token);
  return it == reqData.end() ? nullptr : it.value();
}

FOLLY_ALWAYS_INLINE
void RequestContext::StateHazptr::onSet() {
  // Don't use hazptr_local because callback may use hazptr
  hazptr_holder<> h;
  Combined* combined = h.get_protected(combined_);
  if (!combined) {
    return;
  }
  auto& cb = combined->callbackData_;
  for (auto it = cb.begin(); it != cb.end(); ++it) {
    it.key()->onSet();
  }
}

FOLLY_ALWAYS_INLINE
void RequestContext::StateHazptr::onUnset() {
  // Don't use hazptr_local because callback may use hazptr
  hazptr_holder<> h;
  Combined* combined = h.get_protected(combined_);
  if (!combined) {
    return;
  }
  auto& cb = combined->callbackData_;
  for (auto it = cb.begin(); it != cb.end(); ++it) {
    it.key()->onUnset();
  }
}

void RequestContext::StateHazptr::clearContextData(const RequestToken& token) {
  RequestData* data;
  Combined* replaced = nullptr;
  { // Lock mutex_
    std::lock_guard<std::mutex> g(mutex_);
    Combined* cur = combined();
    if (!cur) {
      return;
    }
    auto it = cur->requestData_.find(token);
    if (it == cur->requestData_.end()) {
      return;
    }
    data = it.value();
    if (!data) {
      bool erased = cur->requestData_.erase(token);
      DCHECK(erased);
      return;
    }
    if (data->hasCallback()) {
      data->onUnset();
      bool erased = cur->callbackData_.erase(data);
      DCHECK(erased);
    }
    replaced = cur;
    cur = new Combined(*replaced);
    bool erased = cur->requestData_.erase(token);
    DCHECK(erased);
    cur->acquireDataRefs();
    setCombined(cur);
  } // Unlock mutex_
  DCHECK(data);
  data->releaseRefClearOnly();
  DCHECK(replaced);
  replaced->cleared_.emplace_back(std::make_pair(token, data));
  replaced->retire();
}

RequestContext::StateHazptr::Combined* RequestContext::StateHazptr::expand(
    RequestContext::StateHazptr::Combined* c) {
  size_t dataCapacity = c->requestData_.capacity();
  if (c->needExpandRequestData()) {
    dataCapacity *= 2;
  }
  size_t callbackCapacity = c->callbackData_.capacity();
  if (c->needExpandCallbackData()) {
    callbackCapacity *= 2;
  }
  return new Combined(dataCapacity, callbackCapacity, *c);
}

RequestContext::RequestContext()
    : useHazptr_(FLAGS_folly_reqctx_use_hazptr),
      rootId_(reinterpret_cast<intptr_t>(this)) {}

RequestContext::RequestContext(intptr_t rootid)
    : useHazptr_(FLAGS_folly_reqctx_use_hazptr), rootId_(rootid) {}

RequestContext::RequestContext(const RequestContext& ctx, intptr_t rootid, Tag)
    : RequestContext(ctx) {
  rootId_ = rootid;
}

RequestContext::RequestContext(const RequestContext& ctx, Tag)
    : RequestContext(ctx) {}

/* static */ std::shared_ptr<RequestContext> RequestContext::copyAsRoot(
    const RequestContext& ctx,
    intptr_t rootid) {
  return std::make_shared<RequestContext>(ctx, rootid, Tag{});
}

/* static */ std::shared_ptr<RequestContext> RequestContext::copyAsChild(
    const RequestContext& ctx) {
  return std::make_shared<RequestContext>(ctx, Tag{});
}

bool RequestContext::doSetContextDataLock(
    const RequestToken& token,
    std::unique_ptr<RequestData>& data,
    DoSetBehaviour behaviour) {
  auto wlock = state_.wlock();
  auto& state = *wlock;

  auto it = state.requestData_.find(token);
  if (it != state.requestData_.end()) {
    if (behaviour == DoSetBehaviour::SET_IF_ABSENT) {
      return false;
    }
    if (it->second) {
      if (it->second->hasCallback()) {
        it->second->onUnset();
        state.callbackData_.erase(it->second.get());
      }
      it->second.reset(nullptr);
    }
    if (behaviour == DoSetBehaviour::SET) {
      LOG_FIRST_N(WARNING, 1)
          << "Calling RequestContext::setContextData for "
          << token.getDebugString() << " but it is already set";
      return true;
    }
    DCHECK(behaviour == DoSetBehaviour::OVERWRITE);
  }

  if (data && data->hasCallback()) {
    state.callbackData_.insert(data.get());
    data->onSet();
  }
  auto ptr = RequestData::constructPtr(data.release());
  if (it != state.requestData_.end()) {
    it->second = std::move(ptr);
  } else {
    state.requestData_.insert(std::make_pair(token, std::move(ptr)));
  }
  return true;
}

void RequestContext::setContextData(
    const RequestToken& token,
    std::unique_ptr<RequestData> data) {
  if (useHazptr()) {
    stateHazptr_.doSetContextData(token, data, DoSetBehaviour::SET, false);
    return;
  }
  doSetContextDataLock(token, data, DoSetBehaviour::SET);
}

bool RequestContext::setContextDataIfAbsent(
    const RequestToken& token,
    std::unique_ptr<RequestData> data) {
  if (useHazptr()) {
    return stateHazptr_.doSetContextData(
        token, data, DoSetBehaviour::SET_IF_ABSENT, false);
  }
  return doSetContextDataLock(token, data, DoSetBehaviour::SET_IF_ABSENT);
}

void RequestContext::overwriteContextDataLock(
    const RequestToken& token,
    std::unique_ptr<RequestData> data) {
  doSetContextDataLock(token, data, DoSetBehaviour::OVERWRITE);
}

void RequestContext::overwriteContextDataHazptr(
    const RequestToken& token,
    std::unique_ptr<RequestData> data,
    bool safe) {
  stateHazptr_.doSetContextData(token, data, DoSetBehaviour::OVERWRITE, safe);
}

bool RequestContext::hasContextData(const RequestToken& val) const {
  if (useHazptr()) {
    return stateHazptr_.hasContextData(val);
  }
  return state_.rlock()->requestData_.count(val);
}

RequestData* FOLLY_NULLABLE
RequestContext::getContextData(const RequestToken& val) {
  if (useHazptr()) {
    return stateHazptr_.getContextData(val);
  }
  const RequestData::SharedPtr dflt{nullptr};
  return get_ref_default(state_.rlock()->requestData_, val, dflt).get();
}

const RequestData* FOLLY_NULLABLE
RequestContext::getContextData(const RequestToken& val) const {
  if (useHazptr()) {
    return stateHazptr_.getContextData(val);
  }
  const RequestData::SharedPtr dflt{nullptr};
  return get_ref_default(state_.rlock()->requestData_, val, dflt).get();
}

void RequestContext::onSet() {
  if (useHazptr()) {
    stateHazptr_.onSet();
    return;
  }
  auto rlock = state_.rlock();
  for (const auto& data : rlock->callbackData_) {
    data->onSet();
  }
}

void RequestContext::onUnset() {
  if (useHazptr()) {
    stateHazptr_.onUnset();
    return;
  }
  auto rlock = state_.rlock();
  for (const auto& data : rlock->callbackData_) {
    data->onUnset();
  }
}

void RequestContext::clearContextData(const RequestToken& val) {
  if (useHazptr()) {
    stateHazptr_.clearContextData(val);
    return;
  }
  RequestData::SharedPtr requestData;
  // Delete the RequestData after giving up the wlock just in case one of the
  // RequestData destructors will try to grab the lock again.
  {
    auto ulock = state_.ulock();
    // Need non-const iterators to use under write lock.
    auto& state = ulock.asNonConstUnsafe();
    auto it = state.requestData_.find(val);
    if (it == state.requestData_.end()) {
      return;
    }

    auto wlock = ulock.moveFromUpgradeToWrite();
    if (it->second && it->second->hasCallback()) {
      it->second->onUnset();
      wlock->callbackData_.erase(it->second.get());
    }

    requestData = std::move(it->second);
    wlock->requestData_.erase(it);
  }
}

namespace {
// Execute functor exec for all RequestData in data, which are not in other
// Similar to std::set_difference but avoid intermediate data structure
template <typename TData, typename TExec>
void exec_set_difference(const TData& data, const TData& other, TExec&& exec) {
  auto diter = data.begin();
  auto dend = data.end();
  auto oiter = other.begin();
  auto oend = other.end();
  while (diter != dend) {
    // Order of "if" optimizes for the 2 common cases:
    // 1) empty other, switching to default context
    // 2) identical other, switching to similar context with same callbacks
    if (oiter == oend) {
      exec(*diter);
      ++diter;
    } else if (*diter == *oiter) {
      ++diter;
      ++oiter;
    } else if (*diter < *oiter) {
      exec(*diter);
      ++diter;
    } else {
      ++oiter;
    }
  }
}
} // namespace

/* static */ std::shared_ptr<RequestContext> RequestContext::setContext(
    std::shared_ptr<RequestContext> const& newCtx) {
  return setContext(copy(newCtx));
}

/* static */ std::shared_ptr<RequestContext> RequestContext::setContext(
    std::shared_ptr<RequestContext>&& newCtx_) {
  auto newCtx = std::move(newCtx_); // enforce that it is really moved-from

  auto& staticCtx = getStaticContext();
  if (newCtx == staticCtx.first) {
    return newCtx;
  }

  FOLLY_SDT(
      folly,
      request_context_switch_before,
      staticCtx.first.get(),
      newCtx.get(),
      staticCtx.first ? staticCtx.first->getRootId() : 0,
      newCtx ? newCtx->getRootId() : 0);

  if ((newCtx.get() && newCtx->useHazptr()) ||
      (staticCtx.first.get() && staticCtx.first->useHazptr())) {
    DCHECK(!newCtx.get() || newCtx->useHazptr());
    DCHECK(!staticCtx.first.get() || staticCtx.first->useHazptr());
    return RequestContext::setContextHazptr(newCtx, staticCtx);
  } else {
    return RequestContext::setContextLock(newCtx, staticCtx);
  }
}

FOLLY_ALWAYS_INLINE
/* static */ std::shared_ptr<RequestContext> RequestContext::setContextLock(
    std::shared_ptr<RequestContext>& newCtx,
    StaticContext& staticCtx) {
  auto curCtx = staticCtx.first;
  if (newCtx && curCtx) {
    // Only call set/unset for all request data that differs
    auto ret = folly::acquireLocked(
        as_const(newCtx->state_), as_const(curCtx->state_));
    auto& newLock = std::get<0>(ret);
    auto& curLock = std::get<1>(ret);
    auto& newData = newLock->callbackData_;
    auto& curData = curLock->callbackData_;
    exec_set_difference(
        curData, newData, [](RequestData* data) { data->onUnset(); });
    staticCtx.first = newCtx;
    staticCtx.second.store(newCtx->rootId_, std::memory_order_relaxed);
    exec_set_difference(
        newData, curData, [](RequestData* data) { data->onSet(); });
  } else {
    if (curCtx) {
      curCtx->onUnset();
    }
    staticCtx.first = newCtx;
    if (newCtx) {
      staticCtx.second.store(newCtx->rootId_, std::memory_order_relaxed);
      newCtx->onSet();
    } else {
      staticCtx.second.store(0, std::memory_order_relaxed);
    }
  }
  return curCtx;
}

FOLLY_ALWAYS_INLINE
/* static */ std::shared_ptr<RequestContext> RequestContext::setContextHazptr(
    std::shared_ptr<RequestContext>& newCtx,
    StaticContext& staticCtx) {
  std::shared_ptr<RequestContext> prevCtx;
  auto curCtx = staticCtx.first.get();
  bool checkCur = curCtx && curCtx->stateHazptr_.combined();
  bool checkNew = newCtx && newCtx->stateHazptr_.combined();
  if (checkCur && checkNew) {
    hazptr_array<2> h;
    auto curc = h[0].get_protected(curCtx->stateHazptr_.combined_);
    auto newc = h[1].get_protected(newCtx->stateHazptr_.combined_);
    auto& curcb = curc->callbackData_;
    auto& newcb = newc->callbackData_;
    for (auto it = curcb.begin(); it != curcb.end(); ++it) {
      DCHECK(it.key());
      auto data = it.key();
      if (!newcb.contains(data)) {
        data->onUnset();
      }
    }
    prevCtx = std::move(staticCtx.first);
    staticCtx.first = std::move(newCtx);
    staticCtx.second.store(staticCtx.first->rootId_, std::memory_order_relaxed);
    for (auto it = newcb.begin(); it != newcb.end(); ++it) {
      DCHECK(it.key());
      auto data = it.key();
      if (!curcb.contains(data)) {
        data->onSet();
      }
    }
  } else {
    if (curCtx) {
      curCtx->stateHazptr_.onUnset();
    }
    prevCtx = std::move(staticCtx.first);
    staticCtx.first = std::move(newCtx);
    if (staticCtx.first) {
      staticCtx.second.store(
          staticCtx.first->rootId_, std::memory_order_relaxed);
      staticCtx.first->stateHazptr_.onSet();
    } else {
      staticCtx.second.store(0, std::memory_order_relaxed);
    }
  }
  return prevCtx;
}

RequestContext::StaticContext& RequestContext::getStaticContext() {
  return SingletonT::get();
}

/* static */ std::vector<RequestContext::RootIdInfo>
RequestContext::getRootIdsFromAllThreads() {
  std::vector<RootIdInfo> result;
  auto accessor = SingletonT::accessAllThreads();
  for (auto it = accessor.begin(); it != accessor.end(); ++it) {
    result.push_back({it->second.load(std::memory_order_relaxed),
                      it.getThreadId(),
                      it.getOSThreadId()});
  }
  return result;
}

/* static */ std::shared_ptr<RequestContext>
RequestContext::setShallowCopyContext() {
  auto& parent = getStaticContext().first;
  auto child = parent ? RequestContext::copyAsChild(*parent)
                      : std::make_shared<RequestContext>();
  if (!parent) {
    child->rootId_ = 0;
  }
  // Do not use setContext to avoid global set/unset
  // Also rootId does not change so do not bother setting it.
  std::swap(child, parent);
  return child;
}

RequestContext* RequestContext::get() {
  auto& context = getStaticContext().first;
  if (!context) {
    static RequestContext defaultContext(0);
    return std::addressof(defaultContext);
  }
  return context.get();
}
} // namespace folly
