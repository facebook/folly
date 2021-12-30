/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/GLog.h>
#include <folly/MapUtil.h>
#include <folly/experimental/SingleWriterFixedHashMap.h>
#include <folly/synchronization/Hazptr.h>
#include <folly/tracing/StaticTracepoint.h>

namespace folly {

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

// The Combined struct keeps the two structures for context data
// and callbacks together, so that readers can protect consistent
// versions of the two structures together using hazard pointers.
struct RequestContext::State::Combined : hazptr_obj_base<Combined> {
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

  ~Combined() { releaseDataRefs(); }

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

RequestContext::State::State() = default;

FOLLY_ALWAYS_INLINE
RequestContext::State::State(const State& o) {
  Combined* oc = o.combined();
  if (oc) {
    auto p = new Combined(*oc);
    p->acquireDataRefs();
    setCombined(p);
  }
}

RequestContext::State::~State() {
  cohort_.shutdown_and_reclaim();
  auto p = combined();
  if (p) {
    delete p;
  }
}

FOLLY_ALWAYS_INLINE
RequestContext::State::Combined* RequestContext::State::combined() const {
  return combined_.load(std::memory_order_acquire);
}

FOLLY_ALWAYS_INLINE
RequestContext::State::Combined* RequestContext::State::ensureCombined() {
  auto c = combined();
  if (!c) {
    c = new Combined;
    setCombined(c);
  }
  return c;
}

FOLLY_ALWAYS_INLINE
void RequestContext::State::setCombined(Combined* p) {
  p->set_cohort_tag(&cohort_);
  combined_.store(p, std::memory_order_release);
}

FOLLY_ALWAYS_INLINE
bool RequestContext::State::doSetContextData(
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
    FB_LOG_ONCE(WARNING) << "Calling RequestContext::setContextData for "
                         << token.getDebugString() << " but it is already set";
  }
  if (result.replaced) {
    result.replaced->retire(); // Retire to hazptr library
  }
  return result.changed;
}

FOLLY_ALWAYS_INLINE
RequestContext::State::SetContextDataResult
RequestContext::State::doSetContextDataHelper(
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
      return {
          false /* no changes made */,
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
  return {
      true, /* changes were made */
      unexpected,
      replaced};
}

FOLLY_ALWAYS_INLINE
RequestContext::State::Combined* FOLLY_NULLABLE
RequestContext::State::eraseOldData(
    RequestContext::State::Combined* cur,
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
RequestContext::State::Combined* FOLLY_NULLABLE
RequestContext::State::insertNewData(
    RequestContext::State::Combined* cur,
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
bool RequestContext::State::hasContextData(const RequestToken& token) const {
  hazptr_local<1> h;
  Combined* combined = h[0].protect(combined_);
  return combined ? combined->requestData_.contains(token) : false;
}

FOLLY_ALWAYS_INLINE
RequestData* FOLLY_NULLABLE
RequestContext::State::getContextData(const RequestToken& token) {
  hazptr_local<1> h;
  Combined* combined = h[0].protect(combined_);
  if (!combined) {
    return nullptr;
  }
  auto& reqData = combined->requestData_;
  auto it = reqData.find(token);
  return it == reqData.end() ? nullptr : it.value();
}

FOLLY_ALWAYS_INLINE
const RequestData* FOLLY_NULLABLE
RequestContext::State::getContextData(const RequestToken& token) const {
  hazptr_local<1> h;
  Combined* combined = h[0].protect(combined_);
  if (!combined) {
    return nullptr;
  }
  auto& reqData = combined->requestData_;
  auto it = reqData.find(token);
  return it == reqData.end() ? nullptr : it.value();
}

FOLLY_ALWAYS_INLINE
void RequestContext::State::onSet() {
  // Don't use hazptr_local because callback may use hazptr
  hazptr_holder<> h = make_hazard_pointer<>();
  Combined* combined = h.protect(combined_);
  if (!combined) {
    return;
  }
  auto& cb = combined->callbackData_;
  for (auto it = cb.begin(); it != cb.end(); ++it) {
    it.key()->onSet();
  }
}

FOLLY_ALWAYS_INLINE
void RequestContext::State::onUnset() {
  // Don't use hazptr_local because callback may use hazptr
  hazptr_holder<> h = make_hazard_pointer<>();
  Combined* combined = h.protect(combined_);
  if (!combined) {
    return;
  }
  auto& cb = combined->callbackData_;
  for (auto it = cb.begin(); it != cb.end(); ++it) {
    it.key()->onUnset();
  }
}

void RequestContext::State::clearContextData(const RequestToken& token) {
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

RequestContext::State::Combined* RequestContext::State::expand(
    RequestContext::State::Combined* c) {
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

RequestContext::RequestContext() : rootId_(reinterpret_cast<intptr_t>(this)) {}

RequestContext::RequestContext(intptr_t rootid) : rootId_(rootid) {}

RequestContext::RequestContext(const RequestContext& ctx, intptr_t rootid, Tag)
    : RequestContext(ctx) {
  rootId_ = rootid;
}

RequestContext::RequestContext(const RequestContext& ctx, Tag)
    : RequestContext(ctx) {}

/* static */ std::shared_ptr<RequestContext> RequestContext::copyAsRoot(
    const RequestContext& ctx, intptr_t rootid) {
  return std::make_shared<RequestContext>(ctx, rootid, Tag{});
}

/* static */ std::shared_ptr<RequestContext> RequestContext::copyAsChild(
    const RequestContext& ctx) {
  return std::make_shared<RequestContext>(ctx, Tag{});
}

void RequestContext::setContextData(
    const RequestToken& token, std::unique_ptr<RequestData> data) {
  state_.doSetContextData(token, data, DoSetBehaviour::SET, false);
}

bool RequestContext::setContextDataIfAbsent(
    const RequestToken& token, std::unique_ptr<RequestData> data) {
  return state_.doSetContextData(
      token, data, DoSetBehaviour::SET_IF_ABSENT, false);
}

void RequestContext::overwriteContextData(
    const RequestToken& token, std::unique_ptr<RequestData> data, bool safe) {
  state_.doSetContextData(token, data, DoSetBehaviour::OVERWRITE, safe);
}

bool RequestContext::hasContextData(const RequestToken& val) const {
  return state_.hasContextData(val);
}

RequestData* FOLLY_NULLABLE
RequestContext::getContextData(const RequestToken& val) {
  return state_.getContextData(val);
}

const RequestData* FOLLY_NULLABLE
RequestContext::getContextData(const RequestToken& val) const {
  return state_.getContextData(val);
}

void RequestContext::onSet() {
  state_.onSet();
}

void RequestContext::onUnset() {
  state_.onUnset();
}

void RequestContext::clearContextData(const RequestToken& val) {
  state_.clearContextData(val);
}

/* static */ std::shared_ptr<RequestContext> RequestContext::setContext(
    std::shared_ptr<RequestContext> const& newCtx) {
  return setContext(copy(newCtx));
}

/* static */ std::shared_ptr<RequestContext> RequestContext::setContext(
    std::shared_ptr<RequestContext>&& newCtx_) {
  auto newCtx = std::move(newCtx_); // enforce that it is really moved-from

  auto& staticCtx = getStaticContext();
  if (newCtx == staticCtx.requestContext) {
    return newCtx;
  }

  FOLLY_SDT(
      folly,
      request_context_switch_before,
      staticCtx.requestContext.get(),
      newCtx.get(),
      staticCtx.requestContext ? staticCtx.requestContext->getRootId() : 0,
      newCtx ? newCtx->getRootId() : 0);

  std::shared_ptr<RequestContext> prevCtx;
  RequestContext* curCtx = staticCtx.requestContext.get();
  bool checkCur = curCtx && curCtx->state_.combined();
  bool checkNew = newCtx && newCtx->state_.combined();
  if (checkCur && checkNew) {
    hazptr_array<2> h = make_hazard_pointer_array<2>();
    auto curc = h[0].protect(curCtx->state_.combined_);
    auto newc = h[1].protect(newCtx->state_.combined_);
    auto& curcb = curc->callbackData_;
    auto& newcb = newc->callbackData_;
    for (auto it = curcb.begin(); it != curcb.end(); ++it) {
      DCHECK(it.key());
      auto data = it.key();
      if (!newcb.contains(data)) {
        data->onUnset();
      }
    }
    prevCtx = std::move(staticCtx.requestContext);
    staticCtx.requestContext = std::move(newCtx);
    staticCtx.rootId.store(
        staticCtx.requestContext->getRootId(), std::memory_order_relaxed);
    for (auto it = newcb.begin(); it != newcb.end(); ++it) {
      DCHECK(it.key());
      auto data = it.key();
      if (!curcb.contains(data)) {
        data->onSet();
      }
    }
  } else {
    if (curCtx) {
      curCtx->state_.onUnset();
    }
    prevCtx = std::move(staticCtx.requestContext);
    staticCtx.requestContext = std::move(newCtx);
    if (staticCtx.requestContext) {
      staticCtx.rootId.store(
          staticCtx.requestContext->rootId_, std::memory_order_relaxed);
      staticCtx.requestContext->state_.onSet();
    } else {
      staticCtx.rootId.store(0, std::memory_order_relaxed);
    }
  }
  return prevCtx;
}

namespace {
thread_local bool getStaticContextCalled = false;
}

/* static */ RequestContext::StaticContext& RequestContext::getStaticContext() {
  getStaticContextCalled = true;
  return StaticContextThreadLocal::get();
}

/* static */ RequestContext::StaticContext*
RequestContext::tryGetStaticContext() {
  return getStaticContextCalled ? &StaticContextThreadLocal::get() : nullptr;
}

/* static */ RequestContext::StaticContextAccessor
RequestContext::accessAllThreads() {
  return StaticContextAccessor{StaticContextThreadLocal::accessAllThreads()};
}

/* static */ std::vector<RequestContext::RootIdInfo>
RequestContext::getRootIdsFromAllThreads() {
  std::vector<RootIdInfo> result;
  auto accessor = RequestContext::accessAllThreads();
  for (auto it = accessor.begin(); it != accessor.end(); ++it) {
    result.push_back(it.getRootIdInfo());
  }
  return result;
}

/* static */ std::shared_ptr<RequestContext>
RequestContext::setShallowCopyContext() {
  auto& parent = getStaticContext().requestContext;
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

/* static */ RequestContext* RequestContext::get() {
  auto& context = getStaticContext().requestContext;
  if (!context) {
    static RequestContext defaultContext(0);
    return std::addressof(defaultContext);
  }
  return context.get();
}

/* static */ RequestContext* RequestContext::try_get() {
  if (auto* staticContext = tryGetStaticContext()) {
    return staticContext->requestContext.get();
  }
  return nullptr;
}

} // namespace folly
