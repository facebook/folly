# Coroutine-scoped metadata

`co_withMetadata()` lets application code attach an opaque `uintptr_t` value to
a `folly::coro::Task`. The value is intended to let an in-process stack walker
attribute executing frames to a request, job, tenant, or another
application-defined scope. Attaching is available, but no finalized public C++
reader currently exposes presence-preserving values.

The value follows the wrapped task and ordinary tasks it `co_await`s, including
across suspension and executor changes. Work started with detached ancestry,
such as an `AsyncScope::add()` child, does not inherit it unless the child is
wrapped explicitly. Heap and sampling profilers do not currently transport the
value.

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

The same rule applies to genuinely detached or escaping work. The metadata scope
owns a node in the wrapper coroutine frame; retaining its parent link after the
wrapper has finished would be unsafe. A new async stack must establish its own
metadata scope.

## Reading metadata

No finalized public C++ interface currently exposes the required
presence-preserving, per-frame result. This is a blocking gap for OSS code that
needs to read the value. The result must have the semantics below, but its type
name, include, and callable signature remain unresolved.

The implementation inserts a synthetic metadata node below the real async frame
that owns the annotation. A conforming metadata-aware reader returns one
metadata slot for each displayed stack address. It omits synthetic nodes from
the address list because they cannot be symbolized.

The address-only `folly::symbolizer::getAsyncStackTraceSafe()` path removes the
synthetic nodes. Do not assume every async-stack walker does:
`getAsyncStackTraceFromInitialFrame()` can currently return the marker cookie.
Callers of that API must filter metadata nodes before symbolizing the trace.

A bounded read can omit an outer annotation when its owning frame lies beyond
the returned portion of the trace. Absence in a truncated trace therefore does
not prove that no outer scope exists.

A reader of the underlying chain is supported only when it is either:

- running on the same thread, including an interruption of that thread; or
- inspecting a stopped target, as a debugger may do.

Concurrent traversal from another running thread is outside the publication
contract.

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

The value width follows `uintptr_t`; it is not a fixed 64-bit wire format. The
intended 64-bit metadata slot exposes a presence-aware accessor. On 32-bit
builds, that accessor is intentionally omitted. A 32-bit build is not globally
rejected, but consuming coroutine metadata there is unsupported and has no
public value-width contract.

Pointer widths other than 32 and 64 bits are rejected at compile time. The
coroutine API also requires `FOLLY_HAS_COROUTINES`.

This contract makes no availability guarantee for metadata readers on Windows or
macOS, or with libc++ or libstdc++. Debugger and profiler support is
backend-specific and currently incomplete.

## Cost model

Code that does not call `co_withMetadata()` does not gain metadata state in
every `Task`, `AsyncStackFrame`, `AsyncStackRoot`, or executor transition. An
active scope adds a wrapper coroutine and one metadata node to its logical async
stack. Stack readers perform an extra marker check while walking that chain.

Nested scopes add one wrapper and one metadata node each. They may therefore
bring a bounded walk's truncation point closer even though metadata nodes are
not displayed as stack frames.
