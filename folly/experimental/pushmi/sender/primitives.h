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

#pragma once

#include <exception>
#include <type_traits>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/experimental/pushmi/traits.h>
#include <folly/experimental/pushmi/awaitable/concepts.h>
#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/detail/if_constexpr.h>
#include <folly/experimental/pushmi/receiver/concepts.h>
#include <folly/experimental/pushmi/sender/detail/concepts.h>
#include <folly/lang/CustomizationPoint.h>

namespace folly {
namespace pushmi {

namespace _submit_adl {
  struct submit_adl_disabled;
  template <class S, class R>
  submit_adl_disabled submit(S&&, R&&);

  template <class T>
  constexpr typename T::value_type const _v = T::value;

  PUSHMI_CONCEPT_DEF(
    template(class S, class R)
    concept HasMemberSubmit_,
      requires (S&& from, R&& to) (
        ((S&&) from).submit((R&&) to),
        // requires_<noexcept(((S&&) from).submit((R&&) to))>,
        requires_<std::is_void<decltype(((S&&) from).submit((R&&) to))>::value>
      )
  );

  PUSHMI_CONCEPT_DEF(
    template(class S, class R)
    concept HasNonMemberSubmit_,
      requires (S&& from, R&& to) (
        submit((S&&) from, (R&&) to),
        // requires_<noexcept(submit((S&&) from, (R&&) to))>,
        requires_<std::is_void<decltype(submit((S&&) from, (R&&) to))>::value>
      )
  );

#if FOLLY_HAS_COROUTINES
  PUSHMI_CONCEPT_DEF(
    template (class A, class R)
    concept IsSubmittableAwaitable_,
      not detail::SenderLike_<A> && Awaitable<A> &&
      ReceiveError<R, std::exception_ptr> &&
      ReceiveValue<R, coro::await_result_t<A>>
  );
#endif

  struct _fn
  {
  private:
#if FOLLY_HAS_COROUTINES
    template<class A, class R>
    static void _submit_awaitable_(A awaitable, R to) noexcept;
#endif

    struct _impl_ {
      // these pull the line numbers for the different invocations
      // out of the IF_CONSTEXPR macros
      PUSHMI_TEMPLATE(class S, class R)
      (requires Sender<S> && Receiver<R>)
      auto adl_submit(S&& from, R&& to) const {
        return submit((S&&) from, (R&&) to);
      }
      PUSHMI_TEMPLATE(class S, class R)
      (requires Sender<S> && Receiver<R>)
      auto member_submit(S&& from, R&& to) const {
        return ((S&&) from).submit((R&&) to);
      }
      PUSHMI_TEMPLATE(class S, class R)
      (requires Sender<S> && Receiver<R>)
      void debug_submit(S&& from, R&& to) const {
        // try to get a useful compile error
        PUSHMI_IF_CONSTEXPR((
          Same<decltype(submit((S&&) from, (R&&) to)),
            submit_adl_disabled>) (
          static_assert(std::is_void<decltype(member_submit(id((S&&) from), (R&&) to))>::value, "s.submit(r) failed and adl was disabled");
        ) else (
          static_assert(std::is_void<decltype(adl_submit(id((S&&) from), (R&&) to))>::value, "submit(s, r) via ADL failed");
        ))
      }

      PUSHMI_TEMPLATE(class S, class R)
      (requires Sender<S> && Receiver<R>)
      auto operator()(S&& from, R&& to) const {
        // Prefer a .submit() member if it exists:
        PUSHMI_IF_CONSTEXPR_RETURN((HasMemberSubmit_<S, R>) (
          member_submit(id((S&&) from), (R&&) to);
          return std::true_type{};
        ) else (
          // Otherwise, dispatch to a submit() free function if it exists:
          PUSHMI_IF_CONSTEXPR_RETURN((HasNonMemberSubmit_<S, R>) (
            adl_submit(id((S&&) from), (R&&) to);
            return std::true_type{};
          ) else (
#if FOLLY_HAS_COROUTINES
            // Otherwise, if we support coroutines and S looks like an
            // awaitable, dispatch to the coroutine
            PUSHMI_IF_CONSTEXPR_RETURN((IsSubmittableAwaitable_<S, R>) (
              _submit_awaitable_(id((S&&) from), (R&&) to);
              return std::true_type{};
            ) else (
              debug_submit(id((S&&) from), (R&&) to);
              return std::false_type{};
            ))
#else
            debug_submit(id((S&&) from), (R&&) to);
            return std::false_type{};
#endif
          ))
        ))
      }
    };

  public:

    PUSHMI_TEMPLATE_DEBUG(class S, class R)
    (requires Invocable<_impl_, S, R> && invoke_result_t<_impl_, S, R>::value)
    constexpr void operator()(S&& from, R&& to) const {
      (void) _impl_{}((S&&) from, (R&&) to);
    }
  };

#if FOLLY_HAS_COROUTINES
  struct FOLLY_MAYBE_UNUSED oneway_task
  {
    struct promise_type
    {
      oneway_task get_return_object() noexcept { return {}; }
      std::experimental::suspend_never initial_suspend() noexcept { return {}; }
      std::experimental::suspend_never final_suspend() noexcept { return {}; }
      void return_void() noexcept {}
      [[noreturn]] void unhandled_exception() noexcept { std::terminate(); }
    };
  };
#endif

#if FOLLY_HAS_COROUTINES
  // Make all awaitables senders:
  template<class A, class R>
  void _fn::_submit_awaitable_(A awaitable, R to) noexcept
  {
    // TRICKY: We want to make sure that if copying/moving 'awaitable' throws
    // or if allocating the coroutine frame throws that we can still call
    // op::set_error() on the receiver. So we pass the receiver by reference
    // so that if the call to std::invoke() throws that we can catch the
    // exception and pass it into 'receiver'.
#if FOLLY_HAS_EXCEPTIONS
    try
    {
#endif
      // Create a lambda coroutine and immediately invoke it:
      [](A a, R&& r) -> oneway_task
      {
        // Receivers should be nothrow move-constructible so we should't need to
        // worry about this potentially throwing.
        R rCopy(static_cast<R&&>(r));
#if FOLLY_HAS_EXCEPTIONS
        try
        {
#endif
          PUSHMI_IF_CONSTEXPR ((std::is_void<coro::await_result_t<A>>::value) (
            co_await static_cast<A&&>(a);
            set_value(id(rCopy));
          ) else (
            set_value(id(rCopy), co_await static_cast<A&&>(a));
          ))
          set_done(rCopy);
#if FOLLY_HAS_EXCEPTIONS
        }
        catch (...)
        {
          set_error(rCopy, std::current_exception());
        }
#endif
      }(static_cast<A&&>(awaitable), static_cast<R&&>(to));
#if FOLLY_HAS_EXCEPTIONS
    }
    catch (...)
    {
      set_error(to, std::current_exception());
    }
#endif
  }
#endif
} // namespace _submit_adl

FOLLY_DEFINE_CPO(_submit_adl::_fn, submit)

} // namespace pushmi
} // namespace folly
