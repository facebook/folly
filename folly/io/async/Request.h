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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <folly/SharedMutex.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/Synchronized.h>
#include <folly/concurrency/ProcessLocalUniqueId.h>
#include <folly/container/F14Map.h>
#include <folly/detail/Iterators.h>
#include <folly/synchronization/Hazptr.h>

namespace folly {

/*
 * A token to be used to fetch data from RequestContext.
 * Generally you will want this to be a static, created only once using a
 * string, and then only copied. The string constructor is expensive.
 */
class RequestToken {
 public:
  RequestToken() = default;
  explicit RequestToken(const std::string& str);

  bool operator==(const RequestToken& other) const {
    return token_ == other.token_;
  }

  // Slow, use only for debug log messages.
  std::string getDebugString() const;

  friend struct std::hash<folly::RequestToken>;

 private:
  static Synchronized<F14FastMap<std::string, uint32_t>>& getCache();

  uint32_t token_;
};
static_assert(
    std::is_trivially_destructible<RequestToken>::value,
    "must be trivially destructible");

} // namespace folly

namespace std {
template <>
struct hash<folly::RequestToken> {
  size_t operator()(const folly::RequestToken& token) const {
    return hash<uint32_t>()(token.token_);
  }
};
} // namespace std

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

  // hasCallback() applies only to onSet() and onUnset().
  // onClear() is always executed exactly once.
  virtual bool hasCallback() = 0;
  // Callback executed when setting RequestContext. Make sure your RequestData
  // instance overrides the hasCallback method to return true otherwise
  // the callback will not be executed
  virtual void onSet() {}
  // Callback executed when unsetting RequestContext. Make sure your RequestData
  // instance overrides the hasCallback method to return true otherwise
  // the callback will not be executed
  virtual void onUnset() {}
  // Callback executed exactly once upon the release of the last
  // reference to the request data (as a result of either a call to
  // clearContextData or the destruction of a request context that
  // contains a reference to the data). It can be overridden in
  // derived classes. There may be concurrent executions of onSet()
  // and onUnset() with that of onClear().
  virtual void onClear() {}
  // For debugging
  int refCount() { return keepAliveCounter_.load(std::memory_order_acquire); }

 private:
  // For efficiency, RequestContext provides a raw ptr interface.
  // To support shallow copy, we need a shared ptr.
  // To keep it as safe as possible (even if a raw ptr is passed back),
  // the counter lives directly in RequestData.

  friend class RequestContext;

  static constexpr int kDeleteCount = 0x1;
  static constexpr int kClearCount = 0x1000;

  // Reference-counting functions.
  // Increment the reference count.
  void acquireRef();
  // Decrement the reference count. Clear only if last.
  void releaseRefClearOnly();
  // Decrement the reference count. Delete only if last.
  void releaseRefDeleteOnly();
  // Decrement the reference count. Clear and delete if last.
  void releaseRefClearDelete();
  void releaseRefClearDeleteSlow();

  std::atomic<int> keepAliveCounter_{0};
};

/**
 * ImmutableRequestData is a folly::RequestData that holds an immutable value.
 * It is thread-safe (a requirement of RequestData) because it is immutable.
 */
template <typename T>
class ImmutableRequestData : public folly::RequestData {
 public:
  template <
      typename... Args,
      typename = typename std::enable_if<
          std::is_constructible<T, Args...>::value>::type>
  explicit ImmutableRequestData(Args&&... args) noexcept(
      std::is_nothrow_constructible<T, Args...>::value)
      : val_(std::forward<Args>(args)...) {}

  const T& value() const { return val_; }

  bool hasCallback() override { return false; }

 private:
  const T val_;
};

using RequestDataItem = std::pair<RequestToken, std::unique_ptr<RequestData>>;

// If you do not call create() to create a unique request context,
// this default request context will always be returned, and is never
// copied between threads.
class RequestContext {
 public:
  RequestContext();
  RequestContext(RequestContext&& ctx) = delete;
  RequestContext& operator=(const RequestContext&) = delete;
  RequestContext& operator=(RequestContext&&) = delete;

  // copy ctor is disabled, use copyAsRoot/copyAsChild instead.
  static std::shared_ptr<RequestContext> copyAsRoot(
      const RequestContext& ctx, intptr_t rootid);
  static std::shared_ptr<RequestContext> copyAsChild(const RequestContext& ctx);

  // Create a unique request context for this request.
  // It will be passed between queues / threads (where implemented),
  // so it should be valid for the lifetime of the request.
  static void create() { setContext(std::make_shared<RequestContext>()); }

  // Get the current context.
  static RequestContext* get();

  // Get the current context, if it has already been created, or nullptr.
  static RequestContext* try_get();

  intptr_t getRootId() const { return rootId_; }

  struct RootIdInfo {
    intptr_t id;
    std::thread::id tid;
    uint64_t tidOS;
  };
  static std::vector<RootIdInfo> getRootIdsFromAllThreads();

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
      const RequestToken& token, std::unique_ptr<RequestData> data);
  void setContextData(
      const std::string& val, std::unique_ptr<RequestData> data) {
    setContextData(RequestToken(val), std::move(data));
  }

  // Add RequestData instance "data" to this RequestContext instance, with
  // string identifier "val". If the same string identifier has already been
  // used, return false and do nothing. Otherwise add "data" and return true.
  bool setContextDataIfAbsent(
      const RequestToken& token, std::unique_ptr<RequestData> data);
  bool setContextDataIfAbsent(
      const std::string& val, std::unique_ptr<RequestData> data) {
    return setContextDataIfAbsent(RequestToken(val), std::move(data));
  }

  // Remove the RequestData instance with string identifier "val", if it exists.
  void clearContextData(const RequestToken& val);
  void clearContextData(const std::string& val) {
    clearContextData(RequestToken(val));
  }

  // Returns true if and only if the RequestData instance with string identifier
  // "val" exists in this RequestContext instnace.
  bool hasContextData(const RequestToken& val) const;
  bool hasContextData(const std::string& val) const {
    return hasContextData(RequestToken(val));
  }

  // Get (constant) raw pointer of the RequestData instance with string
  // identifier "val" if it exists, otherwise returns null pointer.
  RequestData* getContextData(const RequestToken& val);
  const RequestData* getContextData(const RequestToken& val) const;
  RequestData* getContextData(const std::string& val) {
    return getContextData(RequestToken(val));
  }
  const RequestData* getContextData(const std::string& val) const {
    return getContextData(RequestToken(val));
  }

  // Same as getContextData(), but caching the RequestData pointer in
  // thread-local storage to avoid the lookup cost. The thread cache is
  // invalidated if the current request context changes or it gets modified.
  //
  // This can be used for RequestData that are queried very frequently. It
  // should almost always be faster than getContextData(), but it consumes
  // thread-local storage space, so it is worth doing only when a high hit rate
  // is expected.
  //
  // The storage for the caches is associated to a type tag passed as template
  // argument. This tag also contains the RequestToken key as a static member
  // kToken. This guarantees that a given tag cannot be accidentally used with
  // multiple tokens.
  //
  // For example:
  //
  // struct MyRequestDataTraits {
  //   static inline const RequestToken kToken{"my_request_data"};
  // };
  //
  // ...
  // auto* data = ctx->getThreadCachedContextData<MyRequestDataTraits>();
  //
  template <class Traits>
  RequestData* getThreadCachedContextData();

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
      std::shared_ptr<RequestContext> const& ctx);
  static std::shared_ptr<RequestContext> setContext(
      std::shared_ptr<RequestContext>&& newCtx_);

  static std::shared_ptr<RequestContext> saveContext() {
    return getStaticContext().requestContext;
  }

 private:
  struct Tag {};
  RequestContext(const RequestContext& ctx) = default;

 public:
  RequestContext(const RequestContext& ctx, intptr_t rootid, Tag tag);
  RequestContext(const RequestContext& ctx, Tag tag);
  explicit RequestContext(intptr_t rootId);

  struct StaticContext {
    std::shared_ptr<RequestContext> requestContext;
    std::atomic<intptr_t> rootId{0};
  };

 private:
  static StaticContext& getStaticContext();
  static StaticContext* tryGetStaticContext();
  static std::shared_ptr<RequestContext> setContextHelper(
      std::shared_ptr<RequestContext>& newCtx, StaticContext& staticCtx);

  using StaticContextThreadLocal = SingletonThreadLocal<
      RequestContext::StaticContext,
      RequestContext /* Tag */>;

 public:
  class StaticContextAccessor {
   private:
    using Inner = StaticContextThreadLocal::Accessor;
    using IteratorBase = Inner::Iterator;
    using IteratorTag = std::bidirectional_iterator_tag;

    Inner inner_;

    explicit StaticContextAccessor(Inner&& inner) noexcept
        : inner_(std::move(inner)) {}

   public:
    friend class RequestContext;

    class Iterator : public detail::IteratorAdaptor<
                         Iterator,
                         IteratorBase,
                         StaticContext,
                         IteratorTag> {
      using Super = detail::
          IteratorAdaptor<Iterator, IteratorBase, StaticContext, IteratorTag>;

     public:
      using Super::Super;

      StaticContext& dereference() const { return *base(); }

      RootIdInfo getRootIdInfo() const {
        return {
            base()->rootId.load(std::memory_order_relaxed),
            base().getThreadId(),
            base().getOSThreadId()};
      }
    };

    StaticContextAccessor(const StaticContextAccessor&) = delete;
    StaticContextAccessor& operator=(const StaticContextAccessor&) = delete;
    StaticContextAccessor(StaticContextAccessor&&) = default;
    StaticContextAccessor& operator=(StaticContextAccessor&&) = default;

    Iterator begin() const { return Iterator(inner_.begin()); }
    Iterator end() const { return Iterator(inner_.end()); }
  };
  // Returns an accessor object that blocks the construction and destruction of
  // StaticContext objects on all threads. This is useful to quickly introspect
  // the context from all threads while ensuring that their thread-local
  // StaticContext object is not destroyed.
  static StaticContextAccessor accessAllThreads();

  // Start shallow copy guard implementation details:
  // All methods are private to encourage proper use
  friend struct ShallowCopyRequestContextScopeGuard;

  // This sets a shallow copy of the current context as current,
  // then return the previous context (so it can be reset later).
  static std::shared_ptr<RequestContext> setShallowCopyContext();

  // For functions with a parameter safe, if safe is true then the
  // caller guarantees that there are no concurrent readers or writers
  // accessing the structure.
  void overwriteContextData(
      const RequestToken& token,
      std::unique_ptr<RequestData> data,
      bool safe = false);
  void overwriteContextData(
      const std::string& val,
      std::unique_ptr<RequestData> data,
      bool safe = false) {
    overwriteContextData(RequestToken(val), std::move(data), safe);
  }

  enum class DoSetBehaviour {
    SET,
    SET_IF_ABSENT,
    OVERWRITE,
  };

  bool doSetContextDataHelper(
      const RequestToken& token,
      std::unique_ptr<RequestData>& data,
      DoSetBehaviour behaviour,
      bool safe = false);
  bool doSetContextDataHelper(
      const std::string& val,
      std::unique_ptr<RequestData>& data,
      DoSetBehaviour behaviour,
      bool safe = false) {
    return doSetContextDataHelper(RequestToken(val), data, behaviour, safe);
  }

  // State implementation with single-writer multi-reader data
  // structures protected by hazard pointers for readers and a lock
  // for writers.
  struct State {
    // Hazard pointer-protected combined structure for request data
    // and callbacks.
    struct Combined;
    hazptr_obj_cohort<> cohort_; // For destruction order
    std::atomic<Combined*> combined_{nullptr};
    // Version used to invalidate getThreadCachedContextData() caches on
    // modifications. A process-wide unique id is used (instead of, for example,
    // a local counter) so that it is not necessary to compare the request
    // context pointer as well. This saves one word of TLS and one comparison.
    std::atomic<uint64_t> version_{processLocalUniqueId()};
    // This should never be used directly. Use LockGuard so that thread caches
    // are invalidated at the end of the critical section.
    folly::SharedMutex mutex_; // exclusive mutex, but smaller than std::mutex

    State();
    State(const State& o);
    State(State&&) = delete;
    State& operator=(const State&) = delete;
    State& operator=(State&&) = delete;
    ~State();

   private:
    friend class RequestContext;

    struct SetContextDataResult {
      bool changed; // Changes were made
      bool unexpected; // Update was unexpected
      Combined* replaced; // The combined structure was replaced
    };

    class LockGuard;

    Combined* combined() const;
    Combined* ensureCombined(); // Lazy allocation if needed
    void setCombined(Combined* p);
    Combined* expand(Combined* combined);
    bool doSetContextData(
        const RequestToken& token,
        std::unique_ptr<RequestData>& data,
        DoSetBehaviour behaviour,
        bool safe);
    bool hasContextData(const RequestToken& token) const;
    RequestData* getContextData(const RequestToken& token);
    const RequestData* getContextData(const RequestToken& token) const;
    void onSet();
    void onUnset();
    void clearContextData(const RequestToken& token);
    SetContextDataResult doSetContextDataHelper(
        const RequestToken& token,
        std::unique_ptr<RequestData>& data,
        DoSetBehaviour behaviour,
        bool safe);
    Combined* eraseOldData(
        Combined* cur,
        const RequestToken& token,
        RequestData* oldData,
        bool safe);
    Combined* insertNewData(
        Combined* cur,
        const RequestToken& token,
        std::unique_ptr<RequestData>& data,
        bool found);
  }; // State
  State state_;
  // Shallow copies keep a note of the root context
  intptr_t rootId_;
};
static_assert(sizeof(RequestContext) <= 64, "unexpected size");

/**
 * Note: you probably want to use ShallowCopyRequestContextScopeGuard
 * This resets all other RequestData for the duration of the scope!
 */
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
  explicit RequestContextScopeGuard(std::shared_ptr<RequestContext> const& ctx)
      : prev_(RequestContext::setContext(ctx)) {}
  explicit RequestContextScopeGuard(std::shared_ptr<RequestContext>&& ctx)
      : prev_(RequestContext::setContext(std::move(ctx))) {}

  ~RequestContextScopeGuard() { RequestContext::setContext(std::move(prev_)); }
};

/**
 * This guard maintains all the RequestData pointers of the parent.
 * This allows to overwrite a specific RequestData pointer for the
 * scope's duration, without breaking others.
 *
 * Only modified pointers will have their set/onset methods called
 */
struct ShallowCopyRequestContextScopeGuard {
  ShallowCopyRequestContextScopeGuard()
      : prev_(RequestContext::setShallowCopyContext()) {}

  /**
   * Shallow copy then overwrite one specific RequestData
   *
   * Helper constructor which is a more efficient equivalent to
   * "clearRequestData" then "setRequestData" after the guard.
   */
  ShallowCopyRequestContextScopeGuard(
      const RequestToken& token, std::unique_ptr<RequestData> data)
      : ShallowCopyRequestContextScopeGuard() {
    RequestContext::get()->overwriteContextData(token, std::move(data), true);
  }
  ShallowCopyRequestContextScopeGuard(
      const std::string& val, std::unique_ptr<RequestData> data)
      : ShallowCopyRequestContextScopeGuard() {
    RequestContext::get()->overwriteContextData(val, std::move(data), true);
  }

  /**
   * Shallow copy then overwrite multiple RequestData instances
   *
   * Helper constructor which is more efficient than using multiple scope guards
   * Accepts iterators to a container of <string/RequestToken, RequestData
   * pointer> pairs
   */
  template <typename... Item>
  explicit ShallowCopyRequestContextScopeGuard(
      RequestDataItem&& first, Item&&... rest)
      : ShallowCopyRequestContextScopeGuard(MultiTag{}, first, rest...) {}

  ~ShallowCopyRequestContextScopeGuard() {
    RequestContext::setContext(std::move(prev_));
  }

  ShallowCopyRequestContextScopeGuard(
      const ShallowCopyRequestContextScopeGuard&) = delete;
  ShallowCopyRequestContextScopeGuard& operator=(
      const ShallowCopyRequestContextScopeGuard&) = delete;
  ShallowCopyRequestContextScopeGuard(ShallowCopyRequestContextScopeGuard&&) =
      delete;
  ShallowCopyRequestContextScopeGuard& operator=(
      ShallowCopyRequestContextScopeGuard&&) = delete;

 private:
  struct MultiTag {};
  template <typename... Item>
  explicit ShallowCopyRequestContextScopeGuard(MultiTag, Item&... item)
      : ShallowCopyRequestContextScopeGuard() {
    auto rc = RequestContext::get();
    auto go = [&](RequestDataItem& i) {
      rc->overwriteContextData(i.first, std::move(i.second), true);
    };

    using _ = int[];
    void(_{0, (go(item), 0)...});
  }

  std::shared_ptr<RequestContext> prev_;
};

template <class Traits>
/* static */ FOLLY_EXPORT RequestData*
RequestContext::getThreadCachedContextData() {
  thread_local RequestData* cachedData = nullptr;
  thread_local uint64_t cachedVersion = 0; // Allowed sentinel value.

  // In case cache is invalid, version snapshot must be taken before performing
  // the lookup.
  uint64_t curVersion = state_.version_.load(std::memory_order_acquire);

  if (curVersion == cachedVersion) {
    return cachedData;
  }

  cachedData = getContextData(Traits::kToken);
  cachedVersion = curVersion;
  return cachedData;
}

} // namespace folly
