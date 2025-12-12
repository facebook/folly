# Fast RTTI for rich errors

Querying a `rich_exception_ptr` for a specific exception type is usually very
fast -- especially for immortal instances. This is due to several *best-effort*
RTTI-avoidance tricks.

In some uses of rich errors, RTTI costs persist:

- Querying a non-immortal exception for a missing type still hits RTTI overhead.

- In multi-DSO programs, we may see fallback to RTTI for types that are "hits".
  Our "fast path" compares `type_info*`, but the pointers will differ across
  DSOs for the same type, forcing expensive `dynamic_cast` fallback.

The traditional C++ answer to "RTTI is too slow" is to implement a custom
type-registration system. The aims are to use short, cache-friendly type
identifiers & to synthesize efficient lookup code.

The existing `rich_error` design requires usage of `rich_error_hints`. We can
strengthen its contract to enumerate all base types, which is the one key
requirement for a fast-RTTI registration system.

## Proposed design

Using the base-registration input from the user (or, C++26 reflection) to assign
integer identifiers to each base of `UserBase`, all the way down to the
`rich_error_base` root. There are 2 approaches:

**Likely-unique** -- use a compile-time hash of the type name:
  - The hash needs to be ABI-stable, so the mangled type name would work.
  - But, prior to C++26 P2996, there's no great way to measure a type name at
    build time. With guards on "allowed compiler & version",
    `folly/lang/Pretty.h` might be safe enough.
  - TBD: 8 bits? 16? 32? 64? -- it's easy to handle collisions, so shorter is
    likely better. When hashes are equal, simply compare `type_info*`, falling
    back to `type_info::operator==`.
  - We're targeting the consteval VM, so pick a short-implementation hash with
    top scores on SMHasher, e.g. xxHash32

**Globally unique** -- require each rich error type to declare a UUID
  - Main risk: UUID collisions due to copy-paste, or other forms of
    carelessness. Even if unlikely, the consequences can be catastrophic, so
    this approach is only OK if used in a monorepo, with a tool that uses global
    code-grep to ensure uniqueness, and matching linters to make sure the grep
    has 100% complete coverage.
  - May be faster by avoiding fallback to `type_info::operator==` (should still
    do so in debug builds to detect UUID collisions).
  - Has user-facing boilerplate, but 1 line is acceptable.

## Side benefit

`rich_error<T>` is `final`, so querying it can skip base type checks. Currently
`rich_error.md` (correctly) recommends querying unwrapped `T` to match derived
types. This optimization would make exact `rich_error<T>` queries extra-fast,
which may be relevant to very hot error code.
