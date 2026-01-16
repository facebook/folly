# What is the small-value optimization for `result`?

`result<T>` is effectively `Expected<storage_type, non_value_result>`, where the
error type wraps `rich_exception_ptr` (REP). REP is a bit-discriminated variant
that already reserves `SMALL_VALUE_eq` for future small-value storage.

On 64-bit platformse, at least 61 bits are available:

  - Trivially copyable values with `sizeof(T) <= 4 && alignof(T) <= 4` can be
    stored in the top 4 bytes and transparently exposed by-reference.

  - Non-trivial objects <= 4 bytes could also be stored transparently, but there
    are few use-cases for lifetime-managed objects smaller than a pointer.

  - Aligned pointers (e.g., `T*`, `std::unique_ptr<T>` with `alignof(T) >= 8`)
    fit in 61 bits, but cannot support standard by-reference accessors — this
    requires specialization-driven access APIs.

The optimization reduces `result<T>` from 16 to 8 bytes for eligible types. No
immediate implementation plans exist, but the design preserves feasibility.

## Notes on implementing small-value support

Later in this file, you will find a discarded implementation of small-value storage, with lifetime management done by `rich_exception_ptr<SmallValue>`.

I removed that approach since it conflicts with providing const reference
access to `non_value_result` from `result<SV>`.

```cpp
const non_value_result& non_value() const& {
```

The `result` could store some kind of `non_value_result_impl<SV>` that manages
lifetime, and play a type-punning game to expose `non_value_result` with the
same bit layout, but incapable of small-value access.  However, this would add
even more UB to the implementation, and doesn't seem like the right design.

So, the preferred design is instead to branch `result` on small-value vs not,
where it remains TBD whether `result<int>` etc should automatically opt into
the optimization, or we should use some kind of marker type.

When `sizeof(T) <= 4` and `alignof(T) <= 4`, like `uint32_t`, the small-value
optimization can be fully transparent -- the mangled small value and "packed"
union bits can coexist in the same 8 bytes. So,by-reference accessors like
this, or the equivalent `or_unwind`, will just work:

```cpp
const T& value_or_throw() const&;
```

Some 5-7 byte types also fit by shifting bits on LE systems (or no shift on BE):

```cpp
struct A { char c [5]; };
struct alignas(2) B { char c[5]; };
```

Thanks to the low alignment requirements, and sizeof 5 & 6 respectively, we can
simply shift them up by 1-2 bytes on LE systems, or leave them in place on BE
systems, and not conflict with the union bits.

However, not all structs that look like they fit in 7 bytes can actually
fit. One problem is that alignment will waste the first alignof(T) bytes on LE
systems. Another problem is that not all padding is reusable padding. See:
  https://github.com/llvm/llvm-project/issues/125863

The above small value types can be termed "transparently accessible". But we
can also consider "non-transparent" storage, which lets us fit anything that
needs less than 61 bits. This is most compelling for `unique_ptr<T>` and
friends.

To implement small-value support for `result<pointer-like>`, the user-facing
API has to change.  Specifically, to support thread-safe `const` value access,
when the low-bits of the pointer are occupied, we cannot use the 8 bytes owned
by the `result` to provide by-reference accessors.  This means we can only
provide:

(1) By-value access for safely copyable stored types like `T*` or
    `std::reference_wrapper<T>` (powering `result<T&>`), OR

(2) Use a specializable trait to define copyable "reference-like" types for
    these. For example, `result_small_value_ref_t<unique_ptr<T>>` might be
    prototyped as just `T*` (though a full version should reference the actual
    `result` and demangle it to implement `get`/`release`/`reset`/etc).
    This generalization of (1) is a bit laborious, but it's probably
    worth it since only a handful of pointer-like types are ever likely
    to opt into the small-value optimization.

(3) Only have "move-out access" for move-only types like `unique_ptr<T>`, OR

(4) (maybe not) Can specialize the awaiter (`or_unwind`) to provide ephemeral
    storage for by-reference access. The reason this is a dubious idea is that
    these refs would only work in the same full-expression as the `co_await`,
    whereas the lifetime of normal refs is as long as the result being awaited.
    This by-ref usage CAN be useful for pointers anyway, **if** you deref them
    in that same full expression—after all, the contained pointer has sane
    lifetime—but it's tricky to define semantics that aren't excessively
    dangerous. It's worth investigating if any of the clang lifetime safety
    attributes could make this usage robust.

(5) (maybe) A `small_value_ref_guard`-like (bleh) or `with_value()`-like
    (safer) API that works uniformly even for `unique_ptr`.

My current preferred resolution for the above pointer troubles is to implement
(1), and (2) for `unique_ptr` and perhaps a few other highly useful types.

---

For whatever `result` instantiations are opted in, we need a separate
`result_crtp` that only stores `non_value_result` and manages small-value
lifetime on top, following a5f4e603c5151fae144437f99a9bea069745e8ea.

Careful: The above rev has some minor deviations from the desired end
state for when `result_crtp` / `result` are managing value lifetime.

Obviously, these need to be lifted -- and I started sketching this below:
  - dtor
  - copy/move ctor/assignment when SV copy/move ctor is nontrivial
    need to lift the ctor gating on MayCopySmallValue /
    MayMoveSmallValue
  - operator== should dfatal & return false on comparisons EITHER
    just SV <-> SV, or anything involving SV at all.

Furthermore, REP itself needs changes. Specifically:
  - It contains the mangled small-value as uintptr but trusts its owner
    to manage lifetime for the small-value. It just stores the bits.
  - To accommodate that, all its functions must assume that SMALL_VALUE_eq
    bits may occur, and handle them appropriately.
  - Specifically, throw_exception_impl and to_exception_ptr_impl should ALWAYS
    check for SV bits and act accordingly.
  - Besides that, it provides just a couple of small-value accessors
    private to the `result` impl:
      * get_small_value_uintptr() -- returns 0 if no small value stored.
        This is fine since SMALL_VALUE_eq bits cannot be zero (assert).
      * assign_small_value_uintptr() -- destroys any owned eptr, and overwrites
        the data + bits with the supplied small value.  TBD: If the REP
        previously stored a small-value, is it assumed that the owner already
        destructed it, OR do we return the previous value here as-if
        get_small_value_uintptr() was called?  Probably the former?

Other things to tidy:

  - We of course reuse `detail/rich_exception_ptr_small_value.h`, but rename
    everything to say "result" instead of "rich exception".

  - The logic there should be updated in view of the "what can we store
    with transparent access" discussion above. Having `result` support
    `result_small_value_ref_t` accessors for non-transparently accessible types
    is probably also worthwhile.

  - It is possible for a type not to be trivially copyable, but to have
    a trivial move ctor or copy ctor. Need to consider if these are
    worthwhile automatically opting into small-value storage.

  - The discussion of bitwise relocatability needs to mention:
      * P1149, and some of its conflict with P2786 which is in C++26.
      * `folly::IsRelocatable` / `FOLLY_ASSUME_RELOCATABLE`
      * Avoidance of polymorphic objects if using P2786
        https://stackoverflow.com/questions/79481690
      * TBH I don't understand P2786 `is_replaceable`, but it's worth
        double-checking if that's relevant to our needs here.

## Code snippets from a discarded small-value implementation

As noted above, this implementation was discarded because implementing
small-value lifetime management within REP directly conflicts with `result`
being able to expose its `non_value()` by reference, which is an important
use-case.

Nonetheless, these notes should be instructive in building similar lifetime
management into a specialization of `result`.

Below, `SmallValue` was a REP template param with the type being stored.

```cpp
static_assert(
    std::is_void_v<SmallValue> ||
    rich_exception_ptr_valid_small_value<SmallValue>);

static constexpr bool MayHaveSmallValue = !std::is_void_v<SmallValue>;
// Explained on the ctors and in `copy_from_impl` / `move_from_impl`
static constexpr bool MayCopySmallValue =
    !MayHaveSmallValue || std::is_nothrow_copy_constructible_v<SmallValue>;
static constexpr bool MayMoveSmallValue =
    !MayHaveSmallValue || std::is_nothrow_move_constructible_v<SmallValue>;
```

Any implementationw would need a simple setter for the storage:

```cpp
template <rich_exception_ptr_valid_small_value SV = SmallValue>
constexpr void set_small_value_and_bits(SV& sv, B::bits_t bits) {
  B::apply_bits_after_setting_data_with(
      [&sv](auto& d) {
        d.uintptr_ =
            rich_exception_ptr_small_value_traits<SmallValue>::to_mangled(sv);
      },
      bits);
}
```

The destructor `destroy_owned_state_and_bits_caller_must_reset` grew:

```cpp
if constexpr (MayHaveSmallValue) {
  if (B::SMALL_VALUE_eq == B::get_bits()) {
    small_value_ref_guard<SmallValue> sv{B::get_uintptr()};
    sv.ref().~SmallValue();
    return;
  }
}
```

The `debug_assert` in `copy_unowned_pointer_sized_state` got another clause:

```cpp
// We use this code path to copy / move small values, if the corresponding ctor
// is trivial.
//
// Future: If benchmarks show that major compilers consistently optimizes away
// the combo of (unmangle, trivial ctor, mangle), then remove the optimizations
// and this `||` clause.
|| (B::SMALL_VALUE_eq == other.get_bits() &&
    (std::is_trivially_copy_constructible_v<SmallValue> ||
    std::is_trivially_move_constructible_v<SmallValue>))
```

In `copy_from_impl`, we had to run the copy ctor of the small value:

```cpp
if constexpr (
    MayHaveSmallValue &&
    !std::is_trivially_copy_constructible_v<SmallValue>) {
  // When a small value has a nontrivial copy ctor, we have to run it --
  // just copying the bits may violate the type's internal invariants, even
  // if it's bitwise-relocatable (silly example: it counts copy ops).
  //
  // Note that we implement both copy construction & assignment in terms of
  // the `SmallValue` copy ctor.  We cannot use `SmallValue::operator=`,
  // since the destination might not even be in a valid `SmallValue` state.
  if (B::SMALL_VALUE_eq == other.get_bits()) {
    small_value_ref_guard<SmallValue> other_sv{other.get_uintptr()};
    alignas(SmallValue) char buf[sizeof(SmallValue)];
    // Future: We might want to relax this for copies, but then there's
    // some work to do to ensure that throwing during copy-assignment
    // doesn't cause the assigned-to object to become invalid.
    static_assert(noexcept(SmallValue{std::as_const(other_sv.ref())}));
    auto& sv = new (&buf) SmallValue{std::as_const(other_sv.ref())};
    set_small_value_and_bits(sv, other.get_bits());
    return;
  }
  // else: Fall back to safe & fast `copy_unowned_pointer_sized_state`
}
```

Similarly, `move_from_impl` has to run the move ctor:

```cpp
if constexpr (
    MayHaveSmallValue &&
    !std::is_trivially_move_constructible_v<SmallValue>) {
  // Run nontrivial move ctors, for the same reason as in `copy_from_impl`.
  // The stronger reason is that we want to match the moved-from state
  // semantics of `std::expected` / `folly::{Try,Expected,result}`, which
  // preserves the discriminator & keeps the value.  Here, that requires
  // leaving `other` containing a moved-out `SmallValue`.
  if (B::SMALL_VALUE_eq == other_bits) {
    small_value_ref_guard<SmallValue> other_sv{other.get_uintptr()};
    alignas(SmallValue) char buf[sizeof(SmallValue)];
    // Why?  (1) Move should generally not throw.  (2) Throwing today
    // leaves an invalid assigned-to object, same as `copy_from_impl`.
    static_assert(noexcept(SmallValue{std::move(other_sv.ref())}));
    auto& sv = new (&buf) SmallValue{std::move(other_sv.ref())};
    set_small_value_and_bits(sv, other_bits);
    // Leave `other` with the moved-out small value.
    other.set_small_value_and_bits(other_sv, other_bits);
    return;
  }
  // else: Fall back to safe & fast `copy_unowned_pointer_sized_state`
}
```

REP today has a small-value debug-fatal stub in `compare_equal`, but a
`result`-based implementation would of course need to actually handle
comparisons correctly, along these lines:

```cpp
if (B::SMALL_VALUE_eq == lbits && B::SMALL_VALUE_eq == rbits) {
  small_value_ref_guard<SmallValue> l{lp->get_uintptr()};
  small_value_ref_guard<SmallValue> r{rp->get_uintptr()};
  return std::as_const(l.ref()) == std::as_const(r.ref());
}
```

Storing a small-value with nontrivial standard ctors would correspondingly
constrain move/copy/assignment of the REP that stored it.

```cpp
/// Copy constructor & assignment
///
/// `SmallValue` requires **nothrow** copyability, since the current
// `operator=` impls would leave the assigned-to object in an invalid state
// if something threw. This could be fixed.

rich_exception_ptr_impl(const rich_exception_ptr_impl&)
  requires(!MayCopySmallValue)
= delete;

constexpr rich_exception_ptr_impl(const rich_exception_ptr_impl& other)
  requires MayCopySmallValue
{ ... }

void operator=(const rich_exception_ptr_impl&)
  requires(!MayCopySmallValue)
= delete;

rich_exception_ptr_impl& operator=(const rich_exception_ptr_impl& other)
  requires MayCopySmallValue
{ ... }

/// Move constructor & assignment
///
/// Only movable if `SmallValue` is **nothrow**-movable, same reason as for
/// the copy ctor.  More generally, we're not keen on supporting throwing
/// move, since it's a footgun.

rich_exception_ptr_impl(rich_exception_ptr_impl&&)
  requires(!MayMoveSmallValue)
= delete;

constexpr rich_exception_ptr_impl(rich_exception_ptr_impl&& other) noexcept
  requires MayMoveSmallValue
{ ... }

void operator=(rich_exception_ptr_impl&&)
  requires(!MayMoveSmallValue)
= delete;

rich_exception_ptr_impl& operator=(rich_exception_ptr_impl&& other) noexcept
  requires MayMoveSmallValue
{ ... }
```

## Small value mangling: `detail/rich_exception_ptr_small_value.h`

The file below was geared towards bit-packing bigger values into the 61 bits we
have available.  You would need something like this to store `unique_ptr<T>`.

Transparent, aligned, by-reference access to the small value is much more
restrictive & simple.

```cpp
#pragma once

#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/Utility.h> // FOLLY_DECLVAL

#include <cstring>

#if FOLLY_HAS_RESULT

namespace folly {
namespace detail {

// IMPORTANT: the overall rule is that all the small-value conversions must
// live in this file, and must produce these bottom alignment bits as 0, even
// though the input representation might not have that property.
inline constexpr int min_alignment_for_packed_rich_exception_ptr = 8; // 3 bits

// DANGER: It is UB to use this, unless the memory layout of `T` is guaranteed
// to consist of a single pointer to an object of alignment of at least
// `min_alignment_for_packed_rich_exception_ptr` bytes.
template <typename T>
struct rich_exception_ptr_small_value_aligned_wrapped_pointer {
  static constexpr bool value = true;
  // Packed storage requires 0s in the bottom bits.
  static_assert(
      alignof(decltype(*FOLLY_DECLVAL(T))) >=
      min_alignment_for_packed_rich_exception_ptr);
  // These asserts aim to detect if `T` isn't just a pointer, or similar issues
  // that would make the `reinterpret_cast` into risky UB.
  static_assert(!std::is_polymorphic_v<T>);
  static_assert(sizeof(T) == sizeof(void*));
  static_assert(sizeof(T) == sizeof(uintptr_t));
  static_assert(alignof(T) >= alignof(uintptr_t));
  static uintptr_t to_mangled_uintptr(T& t) {
    return *std::launder(reinterpret_cast<uintptr_t*>(&t));
  }
  static uintptr_t unmangle_uintptr(uintptr_t n) { return n; }
  static T& aligned_unmangled_as_ref(uintptr_t* n) {
    return *std::launder(reinterpret_cast<T*>(n));
  }
};

// Collects the type invariants required across all the supported storage
// classes for `rich_exception_ptr`.  In practice, these are the requirements
// of `rich_exception_ptr_packed_storage`.  For now, we don't want to encourage
// usage of `rich_exception_ptr` with a fixed `Storage`, so there's no
// mechanism for relaxing the requirements for "separate bits" storage.
//
// In detail, since it's not reasonable to expect end users to know enough
// about the implementation of `rich_exception_ptr` to specialize this safely.
// Instead, see the note about P2786 / IsRelocatable.
template <typename>
// `false` means: types matching the primary template aren't valid small values
struct rich_exception_ptr_small_value_traits : std::false_type {};

template <typename T>
// Packed storage requires 0s in the bottom bits.
  requires(alignof(T) >= min_alignment_for_packed_rich_exception_ptr)
struct rich_exception_ptr_small_value_traits<T*>
    : rich_exception_ptr_small_value_aligned_wrapped_pointer<T*> {};

// Note: The C++ standard doesn't _require_ for `unique_ptr<T>` to have the
// memory layout of "just a pointer", but all our implementations of interest
// (clang, GCC, MSVC) implement it as-expected for the default deleter.  This
// specialization could be further extended to allow empty deleters.
// Conversely, if we have to support lower-quality implementations, conditional
// compilation can be added later.
template <typename T>
// Packed storage requires 0s in the bottom bits.
  requires(alignof(T) >= min_alignment_for_packed_rich_exception_ptr)
struct rich_exception_ptr_small_value_traits<std::unique_ptr<T>>
    : rich_exception_ptr_small_value_aligned_wrapped_pointer<
          std::unique_ptr<T>> {};

template <typename T>
// Future: We would be fine with any "bitwise-relocatable" type.  That is, if
// we `memcpy` the `sizeof(T)` bytes from its current location to another
// `alignas(T)` location, then we can safely treat the destination bytes as the
// new location of the object, as-if it was move-constructed in the new
// location, and the old moved-out object was destructed.  Compare:
//   - `std::unique_ptr<T>` is bitwise-relocatable since the type's internal
//     validity in no way depends on its address.
//   - `std::list<T>` may NOT be bitwise-relocatable, since it is often
//     implemented as a circular linked list.
//
// Reference: folly::IsRelocatable, P2786
  requires(sizeof(T) <= 7 && std::is_trivially_copyable_v<T>)
struct rich_exception_ptr_small_value_traits<T> {
  static constexpr bool value = true;
  static uintptr_t to_mangled(T& t) {
    uintptr_t n = 0;
    std::memcpy(&n, &t, sizeof(T)); // widen
    return n;
  }
  static void unmangle_to_uninitialized(uintptr_t mangled, T& t) {
    uintptr_t n = mangled >> 8;
    std::memcpy(&t, &n, sizeof(T)); // narrow
  }
};

// Non-lifetime-owning view of the small `T` stored in `data_t::uintptr_` of
// `rich_exception_ptr`. Assumes `rich_exception_ptr_valid_small_value<T>`.
template <typename T>
class small_value_ref_guard {
 private:
  alignas(T) union {
    uintptr_t unused_;
    T t_;
  };

 public:
  explicit small_value_ref_guard(uintptr_t mangled) {
    rich_exception_ptr_small_value_traits<T>::unmangle_to_uninitialized(
        mangled, t_);
  }
  T& ref() { return t_; }
};

} // namespace detail

// Check if `T` may be stored as a small value in `rich_exception_ptr`.
template <typename T>
concept rich_exception_ptr_valid_small_value =
    detail::rich_exception_ptr_small_value_traits<T>::value;

} // namespace folly

#endif // FOLLY_HAS_RESULT
```
