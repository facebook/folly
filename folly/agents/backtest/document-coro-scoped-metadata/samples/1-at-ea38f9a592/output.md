# Coroutine-scoped metadata

After a coroutine suspends, its native thread stack no longer shows the chain of
tasks that led to it. Folly preserves that ancestry as a logical async stack.
Coroutine-scoped metadata attaches an opaque value to that chain so an
in-process stack reader can attribute work to a request, job, tenant, or another
application-defined scope.

The value follows ordinary awaited work through suspension and executor changes.
Work launched on a detached chain, including through `AsyncScope::add()`, does
not inherit it. This is stack metadata, not thread-local storage or a general
coroutine-local variable.

## Attach metadata

Include `<folly/coro/WithMetadata.h>` and wrap the task whose execution should
carry the value:

```cpp
folly::coro::Task<void> handle(std::uintptr_t requestId) {
  co_await folly::coro::co_withMetadata(requestId, doWork());
}
```

The metadata value is an opaque `std::uintptr_t`. Folly reports it without
interpreting it or managing the lifetime of anything it might identify.

`co_withMetadata()` returns a `Task<T>` with the wrapped task's result. The
metadata scope remains active while that task and its connected, awaited
descendants run. It is removed when the wrapper completes, including when the
wrapped task completes with an exception.

The current overload accepts `Task<T>`. It does not accept a task that already
has an executor. Put the metadata wrapper inside `co_withExecutor()`:

```cpp
co_await folly::coro::co_withExecutor(
    executor,
    folly::coro::co_withMetadata(requestId, doWork()));
```

Other task wrappers, including `now_task`, `safe_task`, and an already
executor-bound task, are not part of the current API.

## Propagation boundary

Metadata propagation follows Folly's logical async-stack links, not C++ object
lifetime, thread identity, or whether a caller eventually waits for the work.

- **Connected awaited work inherits.** Direct `co_await` chains and structured
  fanout such as `collectAll` retain their caller as an async-stack ancestor.
  Suspension, resumption on another thread, and executor changes preserve the
  metadata.
- **Nested scopes follow async ancestry.** An inner `co_withMetadata()` supplies
  the effective value while it is active. When it finishes, the outer value is
  visible again. Concurrent siblings have separate frame chains, so one
  sibling's inner value does not replace another's.
- **A new async stack does not inherit.** `AsyncScope::add()` currently starts
  its child at Folly's detached async-stack root. The child cannot see metadata
  from the coroutine that called `add()`. Calling `joinAsync()` later does not
  reconnect the two stacks.

To tag work launched through `AsyncScope`, attach metadata inside the work being
started:

```cpp
scope.add(folly::coro::co_withExecutor(
    executor,
    folly::coro::co_withMetadata(requestId, doWork())));
```

`CancellableAsyncScope` uses the same underlying launch boundary. More
generally, any facility that starts work from Folly's detached async-stack root
must establish a new metadata scope if that work needs attribution.

## Read metadata from a stack

Use the metadata-aware overload of `folly::symbolizer::getAsyncStackTraceSafe`
for in-process capture. It presents the ordinary stack from leaf toward root,
with an optional metadata value associated with each displayed frame. Internal
marker frames do not appear as stack frames.

Metadata is sparse. A value is attached to the async frame that owns its scope;
it is not copied onto every descendant frame. To find the effective value for a
sample, scan from the leaf toward the root and select the first present value.
Continue scanning when the value is absent, but stop when a value is present,
including when that value is zero.

For example, nested scopes produce a stack like this:

```text
leaf                  no metadata
inner scope owner     0
outer scope owner     42
parent                no metadata
```

The effective value is `0`, not `42`. Presence is modeled separately from the
stored integer; every `std::uintptr_t` value, including zero, is valid.

The in-process result contains all nested metadata scopes, rather than only the
effective value. This lets a renderer show scope boundaries or lets a consumer
choose the first present value. Consumers must not attribute one sample to every
value in the ancestry unless that is their intended policy.

The planned 64-bit reader interface gives each per-frame metadata object
optional semantics: `get()` returns `std::optional<std::uintptr_t>`. The final
public result type and function signature are not yet specified. Code must
therefore use the concrete metadata-aware overload declared by the Folly version
it builds against.

## Portability

The facility is available only when Folly coroutine support is enabled.

- On 64-bit targets, the public value and in-process reader contract use
  `std::uintptr_t`, and the reader distinguishes absence from every possible
  value.
- On 32-bit targets, Folly remains buildable, but the metadata getter is
  omitted. No contract is made yet for retrieving a 32-bit or 64-bit payload on
  those targets.
- Targets whose pointer width is neither 32 nor 64 bits are unsupported.

No additional restriction is intended for Windows, macOS, libc++, or libstdc++.
Cross-platform build and test results are not available, so compatibility with
those combinations is not yet verified.

## Current limitations

- Metadata is currently observable through the in-process metadata-aware stack
  reader only. The sampling profiler does not transport the payload in its BPF
  event, and the heap profiler does not include it in allocation aggregation. Do
  not rely on either profiler to report or separate samples by metadata.
- Stack capture is bounded. If a trace is truncated before reaching a metadata
  scope, that sample has no attribution for the missing scope. Each active scope
  consumes a wrapper entry and a marker entry in the logical chain, although the
  marker is omitted from the rendered trace.
- Do not walk metadata from one thread while the owning thread continues to
  modify the frame chain. Supported observations are a walk on the owning
  thread, an interruption of that thread, or an external read while the target
  is stopped.
