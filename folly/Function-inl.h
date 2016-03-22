/*
 * Copyright 2016 Facebook, Inc.
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

namespace folly {
namespace detail {
namespace function {

// ---------------------------------------------------------------------------
// HELPER TYPES

enum class AllocationStatus { EMPTY, EMBEDDED, ALLOCATED };

// ---------------------------------------------------------------------------
// EXECUTOR CLASSES

// function::ExecutorIf
template <typename FunctionType>
class Executors<FunctionType>::ExecutorIf
    : public Executors<FunctionType>::Traits::ExecutorMixin {
 protected:
  ExecutorIf(InvokeFunctionPtr invoke_ptr)
      : Traits::ExecutorMixin(invoke_ptr){};

 public:
  // executors are neither copyable nor movable
  ExecutorIf(ExecutorIf const&) = delete;
  ExecutorIf& operator=(ExecutorIf const&) = delete;
  ExecutorIf(ExecutorIf&&) = delete;
  ExecutorIf& operator=(ExecutorIf&&) = delete;

  virtual ~ExecutorIf() {}
  virtual detail::function::AllocationStatus getAllocationStatus() const
      noexcept = 0;
  virtual std::pair<std::type_info const&, void*> target() const noexcept = 0;

  // moveTo: move this executor to a different place
  // preconditions:
  // * *this is a valid executor object (derived from ExecutorIf)
  // * the memory at [dest; dest+size) may be overwritten
  // postconditions:
  // * *this is an EmptyExecutor
  // * *dest is a valid executor object (derived from ExecutorIf)
  // You can move this executor into one for a non-const or const
  // function.
  virtual void moveTo(
      typename NonConstFunctionExecutors::ExecutorIf* dest,
      size_t size,
      FunctionMoveCtor throws) = 0;
  virtual void moveTo(
      typename ConstFunctionExecutors::ExecutorIf* dest,
      size_t size,
      FunctionMoveCtor throws) = 0;
};

// function::EmptyExecutor
template <typename FunctionType>
class Executors<FunctionType>::EmptyExecutor final
    : public Executors<FunctionType>::ExecutorIf {
 public:
  EmptyExecutor() noexcept : ExecutorIf(&EmptyExecutor::invokeEmpty) {}
  ~EmptyExecutor() {}
  detail::function::AllocationStatus getAllocationStatus() const noexcept {
    return detail::function::AllocationStatus::EMPTY;
  }

  std::pair<std::type_info const&, void*> target() const noexcept {
    return {typeid(void), nullptr};
  }

  template <typename DestinationExecutors>
  void moveToImpl(typename DestinationExecutors::ExecutorIf* dest) noexcept {
    new (dest) typename DestinationExecutors::EmptyExecutor();
  }

  void moveTo(
      typename NonConstFunctionExecutors::ExecutorIf* dest,
      size_t /*size*/,
      FunctionMoveCtor /*throws*/) noexcept {
    moveToImpl<Executors<typename Traits::NonConstFunctionType>>(dest);
  }
  void moveTo(
      typename ConstFunctionExecutors::ExecutorIf* dest,
      size_t /*size*/,
      FunctionMoveCtor /*throws*/) noexcept {
    moveToImpl<Executors<typename Traits::ConstFunctionType>>(dest);
  }
};

// function::FunctorPtrExecutor
template <typename FunctionType>
template <typename F, typename SelectFunctionTag>
class Executors<FunctionType>::FunctorPtrExecutor final
    : public Executors<FunctionType>::ExecutorIf {
 public:
  FunctorPtrExecutor(F&& f)
      : ExecutorIf(
            &FunctorPtrExecutor::template invokeFunctor<FunctorPtrExecutor>),
        functorPtr_(new F(std::move(f))) {}
  FunctorPtrExecutor(F const& f)
      : ExecutorIf(
            &FunctorPtrExecutor::template invokeFunctor<FunctorPtrExecutor>),
        functorPtr_(new F(f)) {}
  FunctorPtrExecutor(std::unique_ptr<F> f)
      : ExecutorIf(
            &FunctorPtrExecutor::template invokeFunctor<FunctorPtrExecutor>),
        functorPtr_(std::move(f)) {}
  ~FunctorPtrExecutor() {}
  detail::function::AllocationStatus getAllocationStatus() const noexcept {
    return detail::function::AllocationStatus::ALLOCATED;
  }

  static auto getFunctor(
      typename Traits::template QualifiedPointer<ExecutorIf> self) ->
      typename SelectFunctionTag::template QualifiedPointer<F> {
    return FunctorPtrExecutor::selectFunctionHelper(
        static_cast<
            typename Traits::template QualifiedPointer<FunctorPtrExecutor>>(
            self)
            ->functorPtr_.get(),
        SelectFunctionTag());
  }

  std::pair<std::type_info const&, void*> target() const noexcept {
    return {typeid(F), const_cast<F*>(functorPtr_.get())};
  }

  template <typename DestinationExecutors>
  void moveToImpl(typename DestinationExecutors::ExecutorIf* dest) noexcept {
    new (dest) typename DestinationExecutors::
        template FunctorPtrExecutor<F, SelectFunctionTag>(
            std::move(functorPtr_));
    this->~FunctorPtrExecutor();
    new (this) EmptyExecutor();
  }

  void moveTo(
      typename NonConstFunctionExecutors::ExecutorIf* dest,
      size_t /*size*/,
      FunctionMoveCtor /*throws*/) noexcept {
    moveToImpl<Executors<typename Traits::NonConstFunctionType>>(dest);
  }
  void moveTo(
      typename ConstFunctionExecutors::ExecutorIf* dest,
      size_t /*size*/,
      FunctionMoveCtor /*throws*/) noexcept {
    moveToImpl<Executors<typename Traits::ConstFunctionType>>(dest);
  }

 private:
  std::unique_ptr<F> functorPtr_;
};

// function::FunctorExecutor
template <typename FunctionType>
template <typename F, typename SelectFunctionTag>
class Executors<FunctionType>::FunctorExecutor final
    : public Executors<FunctionType>::ExecutorIf {
 public:
  static constexpr bool kFunctorIsNTM =
      std::is_nothrow_move_constructible<F>::value;

  FunctorExecutor(F&& f)
      : ExecutorIf(&FunctorExecutor::template invokeFunctor<FunctorExecutor>),
        functor_(std::move(f)) {}
  FunctorExecutor(F const& f)
      : ExecutorIf(&FunctorExecutor::template invokeFunctor<FunctorExecutor>),
        functor_(f) {}
  ~FunctorExecutor() {}
  detail::function::AllocationStatus getAllocationStatus() const noexcept {
    return detail::function::AllocationStatus::EMBEDDED;
  }

  static auto getFunctor(
      typename Traits::template QualifiedPointer<ExecutorIf> self) ->
      typename SelectFunctionTag::template QualifiedPointer<F> {
    return FunctorExecutor::selectFunctionHelper(
        &static_cast<
             typename Traits::template QualifiedPointer<FunctorExecutor>>(self)
             ->functor_,
        SelectFunctionTag());
  }

  std::pair<std::type_info const&, void*> target() const noexcept {
    return {typeid(F), const_cast<F*>(&functor_)};
  }

  template <typename DestinationExecutors>
  void moveToImpl(
      typename DestinationExecutors::ExecutorIf* dest,
      size_t size,
      FunctionMoveCtor throws) noexcept(kFunctorIsNTM) {
    if ((kFunctorIsNTM || throws == FunctionMoveCtor::MAY_THROW) &&
        size >= sizeof(*this)) {
      // Either functor_ is no-except-movable or no-except-movability is
      // not requested *and* functor_ fits into destination
      // => functor_ will be moved into a FunctorExecutor at dest
      new (dest) typename DestinationExecutors::
          template FunctorExecutor<F, SelectFunctionTag>(std::move(functor_));
    } else {
      // Either functor_ may throw when moved and no-except-movabilty is
      // requested *or* the functor is too big to fit into destination
      // => functor_ will be moved into a FunctorPtrExecutor. This will
      // move functor_ onto the heap. The FunctorPtrExecutor object
      // contains a unique_ptr.
      new (dest) typename DestinationExecutors::
          template FunctorPtrExecutor<F, SelectFunctionTag>(
              std::move(functor_));
    }
    this->~FunctorExecutor();
    new (this) EmptyExecutor();
  }
  void moveTo(
      typename NonConstFunctionExecutors::ExecutorIf* dest,
      size_t size,
      FunctionMoveCtor throws) noexcept(kFunctorIsNTM) {
    moveToImpl<Executors<typename Traits::NonConstFunctionType>>(
        dest, size, throws);
  }
  void moveTo(
      typename ConstFunctionExecutors::ExecutorIf* dest,
      size_t size,
      FunctionMoveCtor throws) noexcept(kFunctorIsNTM) {
    moveToImpl<Executors<typename Traits::ConstFunctionType>>(
        dest, size, throws);
  }

 private:
  F functor_;
};
} // namespace function
} // namespace detail

// ---------------------------------------------------------------------------
// MOVE CONSTRUCTORS & MOVE ASSIGNMENT OPERATORS

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
Function<FunctionType, NTM, EmbedFunctorSize>::Function(
    Function&& other) noexcept(hasNoExceptMoveCtor()) {
  other.access<ExecutorIf>()->moveTo(access<ExecutorIf>(), kStorageSize, NTM);
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
Function<FunctionType, NTM, EmbedFunctorSize>&
Function<FunctionType, NTM, EmbedFunctorSize>::operator=(
    Function&& rhs) noexcept(hasNoExceptMoveCtor()) {
  destroyExecutor();
  SCOPE_FAIL {
    initializeEmptyExecutor();
  };
  rhs.access<ExecutorIf>()->moveTo(access<ExecutorIf>(), kStorageSize, NTM);
  return *this;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <
    typename OtherFunctionType,
    FunctionMoveCtor OtherNTM,
    size_t OtherEmbedFunctorSize>
Function<FunctionType, NTM, EmbedFunctorSize>::
    Function(
        Function<OtherFunctionType,
        OtherNTM,
        OtherEmbedFunctorSize>&& other) noexcept(
        OtherNTM == FunctionMoveCtor::NO_THROW &&
        EmbedFunctorSize >= OtherEmbedFunctorSize) {
  using OtherFunction =
      Function<OtherFunctionType, OtherNTM, OtherEmbedFunctorSize>;

  static_assert(
      std::is_same<
          typename Traits::NonConstFunctionType,
          typename OtherFunction::Traits::NonConstFunctionType>::value,
      "Function: cannot move into a Function with different "
      "parameter signature");
  static_assert(
      !Traits::IsConst::value || OtherFunction::Traits::IsConst::value,
      "Function: cannot move Function<R(Args...)> into "
      "Function<R(Args...) const>; "
      "use folly::constCastFunction!");

  other.template access<typename OtherFunction::ExecutorIf>()->moveTo(
      access<ExecutorIf>(), kStorageSize, NTM);
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <
    typename OtherFunctionType,
    FunctionMoveCtor OtherNTM,
    size_t OtherEmbedFunctorSize>
Function<FunctionType, NTM, EmbedFunctorSize>&
Function<FunctionType, NTM, EmbedFunctorSize>::operator=(
    Function<OtherFunctionType, OtherNTM, OtherEmbedFunctorSize>&&
        rhs) noexcept(OtherNTM == FunctionMoveCtor::NO_THROW) {
  using OtherFunction =
      Function<OtherFunctionType, OtherNTM, OtherEmbedFunctorSize>;

  static_assert(
      std::is_same<
          typename Traits::NonConstFunctionType,
          typename OtherFunction::Traits::NonConstFunctionType>::value,
      "Function: cannot move into a Function with different "
      "parameter signature");
  static_assert(
      !Traits::IsConst::value || OtherFunction::Traits::IsConst::value,
      "Function: cannot move Function<R(Args...)> into "
      "Function<R(Args...) const>; "
      "use folly::constCastFunction!");

  destroyExecutor();
  SCOPE_FAIL {
    initializeEmptyExecutor();
  };
  rhs.template access<typename OtherFunction::ExecutorIf>()->moveTo(
      access<ExecutorIf>(), kStorageSize, NTM);
  return *this;
}

// ---------------------------------------------------------------------------
// PUBLIC METHODS

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <FunctionMoveCtor OtherNTM, size_t OtherEmbedFunctorSize>
inline void Function<FunctionType, NTM, EmbedFunctorSize>::
    swap(Function<FunctionType, OtherNTM, OtherEmbedFunctorSize>& o) noexcept(
        hasNoExceptMoveCtor() && OtherNTM == FunctionMoveCtor::NO_THROW) {
  Function<FunctionType, NTM, EmbedFunctorSize> tmp(std::move(*this));
  *this = std::move(o);
  o = std::move(tmp);
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
Function<FunctionType, NTM, EmbedFunctorSize>::operator bool() const noexcept {
  return access<ExecutorIf>()->getAllocationStatus() !=
      detail::function::AllocationStatus::EMPTY;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
inline bool Function<FunctionType, NTM, EmbedFunctorSize>::hasAllocatedMemory()
    const noexcept {
  return access<ExecutorIf>()->getAllocationStatus() ==
      detail::function::AllocationStatus::ALLOCATED;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
inline std::type_info const&
Function<FunctionType, NTM, EmbedFunctorSize>::target_type() const noexcept {
  return access<ExecutorIf>()->target().first;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <typename T>
T* Function<FunctionType, NTM, EmbedFunctorSize>::target() noexcept {
  auto type_target_pair = access<ExecutorIf>()->target();
  if (type_target_pair.first == typeid(T)) {
    return static_cast<T*>(type_target_pair.second);
  }
  return nullptr;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <typename T>
T const* Function<FunctionType, NTM, EmbedFunctorSize>::target() const
    noexcept {
  auto type_target_pair = access<ExecutorIf>()->target();
  if (type_target_pair.first == typeid(T)) {
    return static_cast<T const*>(type_target_pair.second);
  }
  return nullptr;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
    Function<
        typename detail::function::FunctionTypeTraits<
            FunctionType>::ConstFunctionType,
        NTM,
        EmbedFunctorSize>
    Function<FunctionType, NTM, EmbedFunctorSize>::castToConstFunction() &&
    noexcept(hasNoExceptMoveCtor()) {
  using ReturnType =
      Function<typename Traits::ConstFunctionType, NTM, EmbedFunctorSize>;

  ReturnType result;
  result.destroyExecutor();
  SCOPE_FAIL {
    result.initializeEmptyExecutor();
  };
  access<ExecutorIf>()->moveTo(
      result.template access<typename ReturnType::ExecutorIf>(),
      kStorageSize,
      NTM);
  return result;
}

// ---------------------------------------------------------------------------
// PRIVATE METHODS

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <typename T>
T* Function<FunctionType, NTM, EmbedFunctorSize>::access() {
  static_assert(
      std::is_base_of<ExecutorIf, T>::value,
      "Function::access<T>: ExecutorIf must be base class of T "
      "(this is a bug in the Function implementation)");
  static_assert(
      sizeof(T) <= kStorageSize,
      "Requested access to object not fitting into ExecutorStore "
      "(this is a bug in the Function implementation)");

  return reinterpret_cast<T*>(&data_);
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <typename T>
T const* Function<FunctionType, NTM, EmbedFunctorSize>::access() const {
  static_assert(
      std::is_base_of<ExecutorIf, T>::value,
      "Function::access<T>: ExecutorIf must be base class of T "
      "(this is a bug in the Function implementation)");
  static_assert(
      sizeof(T) <= kStorageSize,
      "Requested access to object not fitting into ExecutorStore "
      "(this is a bug in the Function implementation)");

  return reinterpret_cast<T const*>(&data_);
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
void Function<FunctionType, NTM, EmbedFunctorSize>::
    initializeEmptyExecutor() noexcept {
  new (access<EmptyExecutor>()) EmptyExecutor;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
template <typename F>
void Function<FunctionType, NTM, EmbedFunctorSize>::
    createExecutor(F&& f) noexcept(
        noexcept(typename std::decay<F>::type(std::forward<F>(f)))) {
  using ValueType = typename std::decay<F>::type;
  static constexpr bool kFunctorIsNTM =
      std::is_nothrow_move_constructible<ValueType>::value;
  using ExecutorType = typename std::conditional<
      (sizeof(FunctorExecutor<
              ValueType,
              typename Traits::DefaultSelectFunctionTag>) > kStorageSize ||
       (hasNoExceptMoveCtor() && !kFunctorIsNTM)),
      FunctorPtrExecutor<ValueType, typename Traits::DefaultSelectFunctionTag>,
      FunctorExecutor<ValueType, typename Traits::DefaultSelectFunctionTag>>::
      type;
  new (access<ExecutorType>()) ExecutorType(std::forward<F>(f));
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
void Function<FunctionType, NTM, EmbedFunctorSize>::destroyExecutor() noexcept {
  access<ExecutorIf>()->~ExecutorIf();
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
struct Function<FunctionType, NTM, EmbedFunctorSize>::MinStorageSize {
  using NotEmbeddedFunctor =
      FunctorPtrExecutor<void(void), detail::function::SelectConstFunctionTag>;

  using EmbeddedFunctor = FunctorExecutor<
      typename std::aligned_storage<
          constexpr_max(EmbedFunctorSize, sizeof(void (*)(void)))>::type,
      detail::function::SelectConstFunctionTag>;

  static constexpr size_t value =
      constexpr_max(sizeof(NotEmbeddedFunctor), sizeof(EmbeddedFunctor));

  static_assert(
      sizeof(EmptyExecutor) <= value,
      "Internal error in Function: EmptyExecutor does not fit "
      "in storage");
};

} // namespace folly
