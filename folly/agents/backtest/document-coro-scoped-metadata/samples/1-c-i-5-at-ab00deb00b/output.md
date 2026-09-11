# Coroutine-scoped metadata

`co_withMetadata()` lets application code attach an opaque `uintptr_t` value to
a `folly::coro::Task`. The value is intended to let an in-process stack walker
attribute executing frames to a request, job, tenant, or another
application-defined scope. On builds where Folly enables coroutines,
`co_withMetadata()` can attach the value today. OSS code cannot retrieve it
through a supported C++ API; a presence-aware in-process reader is still being
finalized.

The value follows the wrapped task through the chain of tasks it awaits,
including across suspension and executor changes. A child passed to
`AsyncScope::add()` does not inherit the caller's value unless that child is
wrapped explicitly. Heap and sampling profilers do not currently transport the
value.

This contract defines attachment and propagation, plus the semantics that a
future metadata reader must provide. It does not define that reader's C++
spelling.

## Apply metadata to a task

Include `<folly/coro/WithMetadata.h>` and wrap a `Task<T>` before awaiting or
binding it to an executor:

```cpp
folly::coro::Task<Response> handle(Request request) {
  co_return co_await folly::coro::co_withMetadata(
      static_cast<uintptr_t>(request.tracingId()),
      process(std::move(request)));
}
```

If the task must be bound to an executor, apply the metadata first:

```cpp
auto work = folly::coro::co_withExecutor(
    executor,
    folly::coro::co_withMetadata(value, operation()));
```

`co_withMetadata()` currently accepts `Task<T>`. It does not accept a task that
has already been bound to an executor, so reversing the nesting above does not
compile. Other task wrappers, including `now_task` and `safe_task`, are not yet
supported. Do not treat the function as a generic-awaitable adapter.

The wrapper preserves the task's result and failure behavior. Its metadata is
active while the wrapped task runs. It is removed when the task exits normally
or exceptionally, or when the wrapper is destroyed.

## Value semantics

The metadata value has type `uintptr_t`. Folly copies the bits but does not
interpret them. In particular:

- zero is a valid value;
- no value is reserved to mean "absent"; and
- Folly does not dereference the value or manage an object that it may encode.

Presence is represented separately from the value. A reader must therefore use
the presence-aware metadata result rather than compare the payload with zero. A
conforming in-process reader's metadata slot has optional semantics: an empty
result means that the frame is untagged, while a present result may contain any
`uintptr_t`, including zero.

Code must not depend on a parallel integer array that uses zero for untagged
frames.

## Nesting and isolation

Scopes nest according to the async call chain. If code with value `outer` awaits
a task wrapped with value `inner`, a metadata-aware stack reader exposes both
annotations. The inner value is effective while that task runs. When the inner
task exits, the outer value is restored.

The reader reports metadata sparsely. Each annotation belongs to the real async
frame immediately above its scope boundary. It is not copied onto every
descendant frame. To find the effective value for the current leaf, scan the
returned frames from leaf toward root and select the first present metadata
value.

For example, a trace through nested scopes may contain:

| Displayed frame | Metadata slot |
| --------------- | ------------- |
| leaf            | empty         |
| inner owner     | present: `0`  |
| outer owner     | present: `7`  |

The effective value is zero, not seven. Selecting the first nonzero payload
would violate the contract.

Sibling branches remain isolated. A metadata wrapper changes only its own
logical ancestry. Concurrent siblings may carry different values without
overwriting one another, and an unwrapped sibling does not acquire another
sibling's value.

## What inherits the scope

Ordinary `co_await` preserves the logical parent chain. The wrapped task and
tasks it awaits therefore inherit the scope across suspension, resumption, and
executor changes. Structured fan-out that keeps this parent chain, such as
awaited `collectAll` branches, inherits it as well.

Starting work on a new async stack is the boundary. Such work does not inherit
the caller's metadata merely because it is later joined. In current behavior,
`AsyncScope::add()` starts its child on a detached async stack, so that child
does not see metadata from the coroutine that called `add()`.

Pass the value explicitly and wrap the child if it needs an annotation:

```cpp
scope.add(folly::coro::co_withExecutor(
    executor,
    folly::coro::co_withMetadata(value, child())));
co_await scope.joinAsync();
```

A task that starts a new async-stack chain must establish its own metadata
scope.

## Reader contract

The pending public reader must return a presence-preserving, per-frame result
with the semantics below. Its type name, include, and callable signature remain
unresolved.

A conforming metadata-aware reader returns one metadata slot for each displayed
stack address. It does not return the synthetic nodes used internally to carry
metadata because they are not code frames and cannot be symbolized.

A bounded read can omit an outer annotation when its owning frame lies beyond
the returned portion of the trace. Absence in a truncated trace therefore does
not prove that no outer scope exists.

The underlying async-stack chain may be read by the thread that owns it,
including when a signal interrupts that thread. An external observer may read it
while the target thread is stopped. Concurrent inspection from another running
thread is unsupported.

## Profiler and debugger support

Attaching metadata does not currently make it available in heap-profiler or
sampling-profiler output. Those paths collect addresses but do not transport the
metadata payload. In particular, heap-profiler samples with the same stack but
different values may be combined before metadata could distinguish them.

Debugger and profiler walkers may recognize and hide the synthetic marker so
that it does not appear as a bogus code frame. Marker filtering alone is not
metadata capture. Do not rely on a tool exposing scoped metadata unless that
tool explicitly documents a payload-carrying path.

## Portability

The value passed to `co_withMetadata()` has type `uintptr_t`, including on
32-bit builds; it is not a fixed 64-bit wire value. The intended 64-bit reader
slot exposes a presence-aware accessor. That accessor is deliberately omitted on
32-bit builds, so consuming coroutine metadata there is unsupported. This does
not globally reject a 32-bit build.

Pointer widths other than 32 and 64 bits are rejected at compile time. The
coroutine API also requires `FOLLY_HAS_COROUTINES`.

This contract makes no availability guarantee for metadata readers on Windows or
macOS, or with libc++ or libstdc++. Debugger and profiler support is
backend-specific and currently incomplete.
