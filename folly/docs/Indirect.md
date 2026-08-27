`folly/Indirect.h`
-----------------

`folly::indirect<T, Alloc = std::allocator<T>>` is an allocator-aware
heap-allocated value wrapper that mirrors `std::indirect` (P1950R2 /
P3019R4, C++26) in the `folly` namespace so it can be used without a
C++26 toolchain. Its member set matches `[indirect.syn]` exactly, so
code written against `folly::indirect` ports to `std::indirect` by
changing the namespace.

It always owns a value (never empty) except for a valueless state
produced by moving from an `indirect` or by constructing / assigning
from a valueless source. `valueless_after_move()` queries that state.
`operator*` / `operator->` have "not valueless" as a precondition: they
assert in debug and are UB in release.

Copy is deep (allocates and copy-constructs `T` via
`allocator_traits<Alloc>::select_on_container_copy_construction`);
move steals the pointer and moves the allocator. Assignment from a `U`
is available when `T` is both constructible and assignable from `U`; it
assigns through the owned object, so pointer stability is preserved,
and constructs a new owned object only when the target is valueless.
`swap`, `==`/`<=>` (including mixed `indirect` vs `T`) and `std::hash`
are provided; valueless compares less than valued, two valueless
compare equal, and hash is `0` for valueless.

There is no `emplace` and no `value()`; neither is part of
`std::indirect`. Replace a value by assigning to it (`i = T(args...)`,
or `i = indirect<T>(std::in_place, args...)` when `T` is not assignable
from the arguments), and guard observation with
`valueless_after_move()`.

`Alloc::value_type` must be `T` (used directly without rebinding) and
`propagate_on_container_*` / `is_always_equal` are respected for
copy / move assignment and `swap`.

Simple usage:

```cpp
#include <folly/Indirect.h>

folly::indirect<std::string> s(std::in_place, "hello");
folly::indirect<std::string> t = s; // deep copy
t->append(" world");
assert(*s == "hello");
assert(*t == "hello world");

folly::indirect<int> v(std::in_place, 1);
folly::indirect<int> w = std::move(v);
assert(!w.valueless_after_move() && v.valueless_after_move());
assert(*w == 1);
// dereferencing v here would be undefined; assign to make it valued again
v = 2;
assert(*v == 2);
```

Allocator-aware usage:

```cpp
MyAlloc<std::string> alloc(my_pool);
folly::indirect<std::string, MyAlloc<std::string>> a(
    std::allocator_arg, alloc, std::in_place, "hi");
auto b = a; // uses select_on_container_copy_construction
a = std::pmr::string("new");
```

### Deviations from `std::indirect`

The public member set, the constraints and the effects all follow
`[indirect]`. Requirements the standard states as *Mandates* are
`static_assert`s rather than constraints, matching the standard: the
operation stays declared for every `T`, so for example
`std::is_copy_constructible_v<indirect<std::unique_ptr<int>>>` is
`true` and actually copying one is a static assertion failure. This
does not affect containers, whose reallocation picks the move path
because `indirect`'s move constructor is unconditionally `noexcept`.

Preconditions — `operator*` / `operator->` on a valueless `indirect`,
and `swap` with unequal allocators when `propagate_on_container_swap`
is `false` — assert in debug and are undefined in release.

One gap remains: there is no `folly::pmr::indirect` alias for
`std::pmr::indirect`. Spell the allocator out instead:
`folly::indirect<T, std::pmr::polymorphic_allocator<T>>`.
