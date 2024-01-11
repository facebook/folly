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

#include <folly/lang/Exception.h>

#include <atomic>
#include <cassert>
#include <cstring>

#include <folly/lang/New.h>

//  Accesses std::type_info and std::exception_ptr internals. Since these vary
//  by platform and library, import or copy the structure and function
//  signatures from each platform and library.
//
//  Support:
//    libstdc++ via libgcc libsupc++
//    libc++ via llvm libcxxabi
//    libc++ on freebsd via libcxxrt
//    win32 via msvc crt
//
//  Both libstdc++ and libc++ are based on cxxabi but they are not identical.
//
//  Reference: https://github.com/RedBeard0531/better_exception_ptr.

//  imports ---

#if defined(__GLIBCXX__)

//  nada

#endif // defined(__GLIBCXX__)

#if defined(_LIBCPP_VERSION) && !defined(__FreeBSD__)

//  https://github.com/llvm/llvm-project/blob/llvmorg-11.0.1/libcxxabi/src/cxa_exception.h
//  https://github.com/llvm/llvm-project/blob/llvmorg-11.0.1/libcxxabi/src/private_typeinfo.h

#include <cxxabi.h>
#include <unwind.h>

namespace __cxxabiv1 {

//  the definition until llvm v10.0.0-rc2
struct __folly_cxa_exception_sans_reserve {
#if defined(__LP64__) || defined(_WIN64) || defined(_LIBCXXABI_ARM_EHABI)
  size_t referenceCount;
#endif
  std::type_info* exceptionType;
  void (*exceptionDestructor)(void*);
  void (*unexpectedHandler)();
  std::terminate_handler terminateHandler;
  __folly_cxa_exception_sans_reserve* nextException;
  int handlerCount;
#if defined(_LIBCXXABI_ARM_EHABI)
  __folly_cxa_exception_sans_reserve* nextPropagatingException;
  int propagationCount;
#else
  int handlerSwitchValue;
  const unsigned char* actionRecord;
  const unsigned char* languageSpecificData;
  void* catchTemp;
  void* adjustedPtr;
#endif
#if !defined(__LP64__) && !defined(_WIN64) && !defined(_LIBCXXABI_ARM_EHABI)
  size_t referenceCount;
#endif
  _Unwind_Exception unwindHeader;
};

//  the definition since llvm v10.0.0-rc2
struct __folly_cxa_exception_with_reserve {
#if defined(__LP64__) || defined(_WIN64) || defined(_LIBCXXABI_ARM_EHABI)
  void* reserve;
  size_t referenceCount;
#endif
  std::type_info* exceptionType;
  void (*exceptionDestructor)(void*);
  void (*unexpectedHandler)();
  std::terminate_handler terminateHandler;
  __folly_cxa_exception_with_reserve* nextException;
  int handlerCount;
#if defined(_LIBCXXABI_ARM_EHABI)
  __folly_cxa_exception_with_reserve* nextPropagatingException;
  int propagationCount;
#else
  int handlerSwitchValue;
  const unsigned char* actionRecord;
  const unsigned char* languageSpecificData;
  void* catchTemp;
  void* adjustedPtr;
#endif
#if !defined(__LP64__) && !defined(_WIN64) && !defined(_LIBCXXABI_ARM_EHABI)
  size_t referenceCount;
#endif
  _Unwind_Exception unwindHeader;
};

//  named differently from the real shim type __shim_type_info and all members
//  are pure virtual; as long as the vtable is the same, though, it should work
class __folly_shim_type_info : public std::type_info {
 public:
  virtual ~__folly_shim_type_info() = 0;

  virtual void noop1() const = 0;
  virtual void noop2() const = 0;
  virtual bool can_catch(
      const std::type_info* thrown_type, void*& adjustedPtr) const = 0;
};

} // namespace __cxxabiv1

namespace abi = __cxxabiv1;

#endif // defined(_LIBCPP_VERSION) && !defined(__FreeBSD__)

#if defined(__FreeBSD__)

//  https://github.com/freebsd/freebsd-src/blob/release/13.0.0/contrib/libcxxrt/cxxabi.h
//  https://github.com/freebsd/freebsd-src/blob/release/13.0.0/contrib/libcxxrt/typeinfo.h

#include <cxxabi.h>

namespace __cxxabiv1 {

class __folly_shim_type_info {
 public:
  virtual ~__folly_shim_type_info() = 0;
  virtual bool __is_pointer_p() const = 0;
  virtual bool __is_function_p() const = 0;
  virtual bool __do_catch(
      std::type_info const* thrown_type,
      void** thrown_object,
      unsigned outer) const = 0;
  virtual bool __do_upcast(
      std::type_info const* target, void** thrown_object) const = 0;
};

} // namespace __cxxabiv1

namespace abi = __cxxabiv1;

#endif // defined(__FreeBSD__)

#if defined(_WIN32)

#if defined(__clang__)
struct _s_ThrowInfo; // compiler intrinsic in msvc
typedef const struct _s_ThrowInfo _ThrowInfo;
#endif

#include <ehdata.h> // @manual

extern "C" _CRTIMP2 void* __cdecl __AdjustPointer(void*, PMD const&);

// clang for windows built-in is available under std::__GetExceptionInfo
#if !defined(__clang__)
template <class _E>
void* __GetExceptionInfo(_E); // builtin
#endif

#endif // defined(_WIN32)

//  implementations ---

namespace folly {

namespace detail {

std::atomic<int> exception_ptr_access_rt_cache_{0};

bool exception_ptr_access_rt_() noexcept {
  auto& cache = exception_ptr_access_rt_cache_;
  auto const result = exception_ptr_access_rt_v_();
  cache.store(result ? 1 : -1, std::memory_order_relaxed);
  return result;
}

std::type_info const* exception_ptr_exception_typeid(
    std::exception const& ex) noexcept {
  return type_info_of(ex);
}

#if defined(__GLIBCXX__)

bool exception_ptr_access_rt_v_() noexcept {
  static_assert(exception_ptr_access_ct, "mismatch");
  return true;
}

std::type_info const* exception_ptr_get_type_(
    std::exception_ptr const& ptr) noexcept {
  return !ptr ? nullptr : ptr.__cxa_exception_type();
}

void* exception_ptr_get_object_(
    std::exception_ptr const& ptr,
    std::type_info const* const target) noexcept {
  if (!ptr) {
    return nullptr;
  }
  auto object = reinterpret_cast<void* const&>(ptr);
  auto type = ptr.__cxa_exception_type();
  return !target || target->__do_catch(type, &object, 1) ? object : nullptr;
}

#endif // defined(__GLIBCXX__)

#if defined(_LIBCPP_VERSION) && !defined(__FreeBSD__)

bool exception_ptr_access_rt_v_() noexcept {
  static_assert(exception_ptr_access_ct || kIsAppleIOS, "mismatch");
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wunsupported-availability-guard")
  return exception_ptr_access_ct //
#if __clang__
      || __builtin_available(iOS 12, *)
#endif
      ;
  FOLLY_POP_WARNING
}

static void* cxxabi_get_object(std::exception_ptr const& ptr) noexcept {
  return reinterpret_cast<void* const&>(ptr);
}

static bool cxxabi_cxa_exception_sans_reserve() noexcept {
  // detect and cache the layout of __cxa_exception in the loaded libc++abi
  //
  // for 32-bit arm-ehabi llvm ...
  //
  // before v5.0.1, _Unwind_Exception is alignas(4)
  // as of v5.0.1, _Unwind_Exception is alignas(8)
  // _Unwind_Exception is the last field in __cxa_exception
  //
  // before v10.0.0-rc2, __cxa_exception has 4b padding before the unwind field
  // as of v10.0.0-rc2, __cxa_exception moves the 4b padding to the start in a
  // field called reserve
  //
  // before 32-bit arm-ehabi llvm v10.0.0-rc2, the reserve field does not exist
  // in the struct explicitly but the refcount field is there instead due to
  // implicit padding
  //
  // before 32-bit arm-ehabi llvm v5.0.1, and before 64-bit llvm v10.0.0-rc2,
  // the reserve field is before the struct start and so is presumably before
  // the struct allocation and so must not be accessed
  //
  // __cxa_allocate_exception zero-fills the __cxa_exception before __cxa_throw
  // assigns fields so if, after incref, the refcount field is still zero, then
  // the runtime llvm is at least v5.0.1 and before v10.00-rc2 and then all the
  // fields except for the unwind field are shifted up by 4b
  //
  // prefer optimistic concurrency over pessimistic concurrency
  static std::atomic<int> cache{};
  if (auto value = cache.load(std::memory_order_relaxed)) {
    return value > 0;
  }
  auto object = abi::__cxa_allocate_exception(0);
  abi::__cxa_increment_exception_refcount(object);
  auto exception =
      static_cast<abi::__folly_cxa_exception_sans_reserve*>(object) - 1;
  auto result = exception->referenceCount == 1;
  assert(
      result ||
      (static_cast<abi::__folly_cxa_exception_with_reserve*>(object) - 1)
              ->referenceCount == 1);
  abi::__cxa_free_exception(object); // no need for decref
  cache.store(result ? 1 : -1, std::memory_order_relaxed);
  return result;
}

template <typename F>
static decltype(auto) cxxabi_with_cxa_exception(void* object, F f) {
  if (cxxabi_cxa_exception_sans_reserve()) {
    using cxa_exception = abi::__folly_cxa_exception_sans_reserve;
    auto exception = object ? static_cast<cxa_exception*>(object) - 1 : nullptr;
    return f(exception);
  } else {
    using cxa_exception = abi::__folly_cxa_exception_with_reserve;
    auto exception = object ? static_cast<cxa_exception*>(object) - 1 : nullptr;
    return f(exception);
  }
}

std::type_info const* exception_ptr_get_type_(
    std::exception_ptr const& ptr) noexcept {
  if (!ptr) {
    return nullptr;
  }
  auto object = cxxabi_get_object(ptr);
  return cxxabi_with_cxa_exception(object, [](auto exception) { //
    return exception->exceptionType;
  });
}

#if defined(__clang__)
__attribute__((no_sanitize("undefined")))
#endif // defined(__clang__)
void* exception_ptr_get_object_(
    std::exception_ptr const& ptr,
    std::type_info const* const target) noexcept {
  if (!ptr) {
    return nullptr;
  }
  auto object = cxxabi_get_object(ptr);
  auto type = exception_ptr_get_type(ptr);
  auto starget = static_cast<abi::__folly_shim_type_info const*>(target);
  return !target || starget->can_catch(type, object) ? object : nullptr;
}

#endif // defined(_LIBCPP_VERSION) && !defined(__FreeBSD__)

#if defined(__FreeBSD__)

bool exception_ptr_access_rt_v_() noexcept {
  static_assert(exception_ptr_access_ct, "mismatch");
  return true;
}

std::type_info const* exception_ptr_get_type_(
    std::exception_ptr const& ptr) noexcept {
  if (!ptr) {
    return nullptr;
  }
  auto object = reinterpret_cast<void* const&>(ptr);
  auto exception = static_cast<abi::__cxa_exception*>(object) - 1;
  return exception->exceptionType;
}

void* exception_ptr_get_object_(
    std::exception_ptr const& ptr,
    std::type_info const* const target) noexcept {
  if (!ptr) {
    return nullptr;
  }
  auto object = reinterpret_cast<void* const&>(ptr);
  auto type = exception_ptr_get_type(ptr);
  auto starget = reinterpret_cast<abi::__folly_shim_type_info const*>(target);
  return !target || starget->__do_catch(type, &object, 1) ? object : nullptr;
}

#endif // defined(__FreeBSD__)

#if defined(_WIN32)

template <typename T>
static T* win32_decode_pointer(T* ptr) {
  return static_cast<T*>(
      DecodePointer(const_cast<void*>(static_cast<void const*>(ptr))));
}

static EHExceptionRecord* win32_get_record(
    std::exception_ptr const& ptr) noexcept {
  return reinterpret_cast<std::shared_ptr<EHExceptionRecord> const&>(ptr).get();
}

static bool win32_eptr_throw_info_ptr_is_encoded() {
  // detect and cache whether this version of the microsoft c++ standard library
  // encodes the throw-info pointer in the std::exception_ptr internals
  //
  // earlier versions of std::exception_ptr did encode the throw-info pointer
  // but the most recent versions do not, as visible on github at
  // https://github.com/microsoft/STL
  //
  // prefer optimistic concurrency over pessimistic concurrency
  static std::atomic<int> cache{0}; // 0 uninit, -1 false, 1 true
  if (auto value = cache.load(std::memory_order_relaxed)) {
    return value > 0;
  }
  // detection is done by observing actual runtime behavior, using int as the
  // exception object type to save cost
#if defined(__clang__)
  auto info = std::__GetExceptionInfo(0);
#else
  auto info = __GetExceptionInfo(0);
#endif
  auto ptr = std::make_exception_ptr(0);
  auto rec = win32_get_record(ptr);
  int value = 0;
  if (info == rec->params.pThrowInfo) {
    value = -1;
  }
  if (info == win32_decode_pointer(rec->params.pThrowInfo)) {
    value = +1;
  }
  assert(value);
  // last writer wins for simplicity, assuming it to be impossible for multiple
  // writers to write different values
  cache.store(value, std::memory_order_relaxed);
  return value > 0;
}

static ThrowInfo* win32_throw_info(EHExceptionRecord* rec) {
  auto encoded = win32_eptr_throw_info_ptr_is_encoded();
  auto info = rec->params.pThrowInfo;
  return encoded ? win32_decode_pointer(info) : info;
}

static std::uintptr_t win32_throw_image_base(EHExceptionRecord* rec) {
#if _EH_RELATIVE_TYPEINFO
  return reinterpret_cast<std::uintptr_t>(rec->params.pThrowImageBase);
#else
  (void)rec;
  return 0;
#endif
}

bool exception_ptr_access_rt_v_() noexcept {
  static_assert(exception_ptr_access_ct, "mismatch");
  return true;
}

std::type_info const* exception_ptr_get_type_(
    std::exception_ptr const& ptr) noexcept {
  auto rec = win32_get_record(ptr);
  if (!rec) {
    return nullptr;
  }
  auto base = win32_throw_image_base(rec);
  auto info = win32_throw_info(rec);
  auto cta_ = base + info->pCatchableTypeArray;
  auto cta = reinterpret_cast<CatchableTypeArray*>(cta_);
  // assumption: the compiler emits the most-derived type first
  auto ct_ = base + cta->arrayOfCatchableTypes[0];
  auto ct = reinterpret_cast<CatchableType*>(ct_);
  auto td_ = base + ct->pType;
  auto td = reinterpret_cast<TypeDescriptor*>(td_);
  return reinterpret_cast<std::type_info*>(td);
}

void* exception_ptr_get_object_(
    std::exception_ptr const& ptr,
    std::type_info const* const target) noexcept {
  auto rec = win32_get_record(ptr);
  if (!rec) {
    return nullptr;
  }
  auto object = rec->params.pExceptionObject;
  if (!target) {
    return object;
  }
  auto base = win32_throw_image_base(rec);
  auto info = win32_throw_info(rec);
  auto cta_ = base + info->pCatchableTypeArray;
  auto cta = reinterpret_cast<CatchableTypeArray*>(cta_);
  for (int i = 0; i < cta->nCatchableTypes; i++) {
    auto ct_ = base + cta->arrayOfCatchableTypes[i];
    auto ct = reinterpret_cast<CatchableType*>(ct_);
    auto td_ = base + ct->pType;
    auto td = reinterpret_cast<TypeDescriptor*>(td_);
    if (*target == *reinterpret_cast<std::type_info*>(td)) {
      return __AdjustPointer(object, ct->thisDisplacement);
    }
  }
  return nullptr;
}

#endif // defined(_WIN32)

} // namespace detail

struct exception_shared_string::state {
  // refcount ops use relaxed order since the string is immutable: side-effects
  // need not be made visible to the destructor since there are none
  static constexpr auto relaxed = std::memory_order_relaxed;
  std::atomic<std::size_t> refs{0u};
  std::size_t const size{0u};
  static constexpr std::size_t object_size(std::size_t const len) noexcept {
    return sizeof(state) + len + 1u;
  }
  static state* make(char const* const str, std::size_t const len) {
    assert(len == std::strlen(str));
    auto addr = operator_new(object_size(len), align_val_t{alignof(state)});
    return new (addr) state(str, len);
  }
  state(char const* const str, std::size_t const len) noexcept : size{len} {
    std::memcpy(static_cast<void*>(this + 1u), str, len + 1u);
  }
  char const* what() const noexcept {
    return static_cast<char const*>(static_cast<void const*>(this + 1u));
  }
  void copy() noexcept { refs.fetch_add(1u, relaxed); }
  void ruin() noexcept {
    if (!refs.load(relaxed) || !refs.fetch_sub(1u, relaxed)) {
      operator_delete(this, object_size(size), align_val_t{alignof(state)});
    }
  }
};

exception_shared_string::exception_shared_string(char const* const str)
    : exception_shared_string{str, std::strlen(str)} {}
exception_shared_string::exception_shared_string(
    char const* const str, std::size_t const len)
    : state_{state::make(str, len)} {}
exception_shared_string::exception_shared_string(
    exception_shared_string const& that) noexcept
    : state_{(that.state_->copy(), that.state_)} {}
exception_shared_string::~exception_shared_string() {
  state_->ruin();
}

char const* exception_shared_string::what() const noexcept {
  return state_->what();
}

} // namespace folly
