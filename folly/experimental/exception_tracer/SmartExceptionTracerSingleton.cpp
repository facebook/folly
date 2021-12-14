#include <folly/experimental/exception_tracer/SmartExceptionTracerSingleton.h>

namespace folly::exception_tracer::detail {

Synchronized<F14FastMap<void*, std::unique_ptr<SynchronizedExceptionMeta>>>&
getMetaMap() {
  // Leaky Meyers Singleton
  static Indestructible<Synchronized<
      F14FastMap<void*, std::unique_ptr<SynchronizedExceptionMeta>>>>
      meta;
  return *meta;
}

static std::atomic_bool hookEnabled{false};

bool isSmartExceptionTracerHookEnabled() {
  return hookEnabled.load(std::memory_order_relaxed);
}
void setSmartExceptionTracerHookEnabled(bool enabled) {
  hookEnabled = enabled;
}

} // namespace folly::exception_tracer::detail
