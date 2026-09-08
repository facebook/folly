# Review of the sparse metadata-frame stack

Status: reviewed 2026-08-06. The code was read at `ca2243505d97`, the top of the
seven-diff stack below. No conclusions here depend on diff descriptions alone.

| Revision       | Diff       | Role                                                       |
| -------------- | ---------- | ---------------------------------------------------------- |
| `0bd3a5fe8940` | D114608692 | Allow a null `AsyncStackFrame` parent                      |
| `d640cd7ac378` | D114608695 | Add the marker frame and `co_withMetadata()`               |
| `7fe24cbc9763` | D114735088 | Remove marker cookies from sampling-profiler output        |
| `8b5771a0237a` | D114866637 | Hide marker cookies in the LLDB walkers                    |
| `0b1c6ec2d3fb` | D114650341 | Hide marker cookies in the native mixed-stack walker       |
| `07bb6149a72c` | D115061390 | Add parallel address and metadata arrays to the C++ walker |
| `ca2243505d97` | D114608693 | Add `co_withMetadata()` tests                              |

D114608694 is abandoned. Its helper was folded into D114608695. The unnamed
walker diff is D115061390; its current API takes parallel arrays, not a lambda.

## Bottom line

The core propagation idea is credible. A metadata marker lives in the wrapper
coroutine frame and is spliced into the existing `AsyncStackFrame::parentFrame`
chain. That chain already follows ordinary awaited work across suspension,
executor changes, and structured fanout. The implementation does not add state
to every Task, every executor hop, or every `AsyncStackRoot`.

The current stack is not yet an implementation of the profiler-facing
requirement. The sampling profiler drops the marker without reading its value,
the heap profiler still calls the address-only walker, and no output or
aggregation key carries the value and its raw marker index. The BPF and LLDB
walkers also truncate a root transition when a marker is inserted below a bottom
frame. The missing profiler transport is a design-level gap; both root-stitching
defects have local repairs.

Separately, the C++ metadata-array API conflicts with the settled contract: it
uses zero as absence, removes the raw marker index, and exposes every nested
scope rather than one effective value. That API should not become the external
contract by accident.

## Contract assumed by this review

The review used these then-current requirements:

- The value is an opaque `uint64_t`; zero is valid.
- The wrapped Task and its awaited structured descendants inherit the value.
- Nested scopes expose the effective innermost value and restore the outer value
  on exit.
- Concurrent siblings are isolated.
- Suspension and executor changes preserve the value.
- External heap- and sampling-profiler capture is required. In-process lookup is
  only a testing and debugging convenience.
- A capture returns the effective value together with the exact raw marker
  index. It does not return the full metadata ancestry.
- Truly escaping descendants need not inherit. The exact boundary still needs to
  be named.

## AsyncStack context for this review

`AsyncStackFrame::parentFrame` is the persistent logical coroutine ancestry.
Task promises normally own these frames, so the chain survives suspension and
executor changes even though the native worker stack does not.

An `AsyncStackRoot` joins the active logical chain to one native execution turn.
When a walker reaches the end of the real-operation chain, the last real frame's
`stackRoot` tells it where to resume through the native stack or an enclosing
root. A metadata marker is only a passive parent-chain node; it has no root link
of its own. Walkers must therefore look through markers before deciding that the
logical chain has ended.

## Design feedback

These findings require a contract or architecture decision. They should not be
silently repaired as local cleanup.

### 1. Blocker: the sampling profiler does not capture metadata

D114735088 is a stack-cleanup change, not metadata integration. The BPF walker
writes only instruction addresses into its event. Userspace reconstructs that
address vector and then erases the marker cookie. At no point does this path
read `AsyncStackMetadataFrame::metadata_` or retain the raw marker index.

Userspace cannot recover the payload after this filtering: the BPF event
contains return addresses, not addresses of the `AsyncStackFrame` objects from
which the adjacent payload could be read. The same completeness gap exists in
the heap profiler. It still calls the two-argument address-only
`getAsyncStackTraceSafe()`, and its aggregate identity has no scoped value.

The next integration experiment should make the BPF walker recognize the first
marker, read one scalar payload, and retain its raw marker index while
continuing the ordinary stack walk. This fixed-size result is the smallest BPF
experiment that can satisfy the contract. Userspace may still remove marker
addresses from the presented stack, but it must preserve the value and raw
marker index first, then define how that index maps to the filtered stack.

The heap profiler needs a versioned storage representation for the same pair and
must make the value part of aggregate identity, not merely display it after
aggregation. Its host and remote reader share raw C++ layouts without a
compatibility shim, so a shared-layout change must bump the format version and
update the reader in lockstep. An end-to-end test should run overlapping scopes,
including value zero, and verify exclusive aggregate ownership plus the retained
raw marker index.

### 2. High: the new C++ output shape is not the settled output contract

D115061390 writes one `uint64_t` beside every returned stack address. An
ordinary frame gets zero, and callers are told to scan rootward for the first
nonzero entry (`folly/debugging/symbolizer/StackTrace.h:84-110`). Three
consequences matter:

1. A scope carrying zero is indistinguishable from no scope. Worse, an inner
   zero scope lets a scan find an outer nonzero value. `co_withMetadata()` does
   not reserve zero, and the existing behavior suite explicitly covers both zero
   and `UINT64_MAX`.
2. The walker removes marker frames from the address array and assigns each
   payload to the real frame above it. It therefore loses the required raw
   marker index.
3. It can expose every nested scope in one trace. The required result is only
   the effective innermost value and its raw marker index.

The tests reinforce the accidental API: `WithMetadataTest.cpp` filters out
zeros, and `StackTraceTest.cpp` expects a per-frame metadata array. Before this
API lands, encode the settled presence-preserving sample-level result, such as
an optional `{value, rawMarkerIndex}`. The remaining output decision is how the
raw marker index maps to a trace after marker filtering.

### 3. High: the marker is now part of a remote-reader ABI

Ordinary walkers treat each node in the parent chain as an async operation with
a presentable return address. The stack adds an inline non-operation node, so
the traversal becomes a tagged-union protocol. Every in-process, debugger, and
remote walker that presents the chain must recognize the tag, skip the synthetic
node for presentation, and preserve root-transition semantics.

The audit already missed `getAsyncStackTraceFromInitialFrame()`, and the BPF and
LLDB fixes got the bottom-frame case wrong. That is evidence that duplicated
cookie handling is an ongoing correctness cost. Folly should provide one
canonical in-process predicate or visitor, and each remote reader should have an
explicit compatibility policy.

The remote contract also needs to state:

- the supported architectures and pointer width;
- why the cookie cannot be a valid instruction address there;
- the enclosing-object layout and metadata offset;
- how a profiler discovers the cookie and layout across Folly versions; and
- what older readers do when they encounter a marker.

The helper explicitly limits its publication protocol to same-thread
interruption or an external observer that has stopped the target; it excludes a
reader concurrent with writes. Verify that every intended profiler path meets
that assumption. If any reader samples a running target from another thread, the
current signal-fence protocol is outside its own contract. This review did not
establish such a path or a use-after-free.

The current alignment assertion does not prove that an integer cookie is an
impossible instruction address. The 64-bit cookie is non-canonical on x86-64,
which is a useful platform-specific argument; the 32-bit `0xdeadbeef` branch and
the LLDB scripts' 64-bit-only comparison do not form a coherent cross-platform
contract. If this facility is intentionally 64-bit only, enforce and document
that instead of carrying an untested 32-bit shape.

### 4. Medium: define the structured/escaping boundary

Ordinary Task awaits and `collectAll` branches inherit naturally because their
frames retain the caller as an ancestor. `AsyncScope::add()` does not: it starts
a `DetachedBarrierTask` rooted at `getDetachedRootAsyncStackFrame()`. Thus a
locally joined `AsyncScope` child loses the marker even though many users may
regard it as structured work.

This is not automatically a bug. The settled minimum permits truly escaping work
to drop metadata, and a raw pointer into a completed wrapper coroutine cannot
safely support escape. The public contract still needs to name the boundary. In
particular, decide whether `AsyncScope::add()` follows the existing async-stack
detachment boundary or whether a stronger join/lifetime API should preserve
metadata. Test the chosen behavior explicitly.

### 5. Medium: bounded BPF walks can lose attribution

Each active scope adds a wrapper Task frame and a marker node. BPF counts both
toward `MAX_ASYNC_STACK_DEPTH` before userspace removes markers. A high outer
scope can therefore disappear from a bounded trace; nested scopes consume two
chain entries each.

This may be acceptable if the effective marker is normally near the leaf, but
for attribution a missing marker means an unattributed sample, not merely a
shorter backtrace. Measure the observed marker depth and define the acceptable
miss rate. BPF recognition could stop metadata search after the first marker
while continuing its ordinary bounded address walk.

### 6. Medium: measure the actual common read paths

The execution-side cost profile is favorable: ordinary coroutine scheduling,
executor hops, `AsyncStackFrame`, and `AsyncStackRoot` are unchanged. Only an
active `co_withMetadata()` scope allocates its wrapper coroutine and marker.

The read side is broader. D114650341 adds a cookie check to every async node in
the native mixed-stack walker, and D114735088 scans every userspace async-stack
sample to erase cookies. Existing coroutine benchmarks do not measure either
path. Before rollout, benchmark metadata-free stack collection and
sampling-profiler handling as well as active metadata scopes. This is a
measurement gate, not evidence of a current regression.

### 7. Low: decide whether `Task`-only composition is intentional

The helper accepts `Task<T>`, not a general semi-awaitable or
`TaskWithExecutor<T>`. The supported composition is therefore
`co_withExecutor(exec, co_withMetadata(value, task))`; placing
`co_withMetadata()` outside `co_withExecutor()` does not compile. A narrow API
is reasonable, but it should be deliberate and documented.

## Easy fixes

These changes do not choose among the unresolved output or profiler designs.
They can be patched independently.

### 1. Blocker: repair BPF root stitching

Consider a one-frame logical chain whose real frame `F` carries root link `R`.
After `co_withMetadata()` inserts its passive marker, the chain is:

```text
F(stackRoot = R) -> marker(stackRoot = null) -> null
```

The BPF walker records `F`, advances to the marker, records it, then sees the
marker's null parent. It reads `stackRoot` from the marker and treats the stack
as detached (`folly_async_stacks.bpf.c:118-143`). D114735088 later deletes the
cookie, leaving a silently truncated trace. Userspace cannot restore the missing
native or enclosing-root frames because BPF never collected them.

D115061390 contains the repair pattern: while processing `F`, look through the
adjacent marker before testing for the end of the async segment, and obtain the
root from `F` (`folly/debugging/symbolizer/StackTrace.cpp:401-425`). Apply the
same bounded look-through in BPF. This is independent of payload encoding. If
the concrete change fails BPF verification, that result is evidence to revisit
the representation rather than to filter the marker only in userspace.

### 2. Fix every existing in-process walker

`getAsyncStackTraceFromInitialFrame()` still emits every parent node
(`folly/tracing/AsyncStack-inl.h:70-79`). A metadata marker therefore appears as
a bogus address and consumes the caller's depth limit. Existing callers include
`folly::coro::co_current_async_stack_trace` and suspended-stack symbolization.

Teach this walker to skip markers and add focused coverage for both direct
initial-frame walking and a suspended coroutine chain. Bound the number of
physical nodes examined, not only the number of output addresses, so a corrupt
marker cycle cannot hang crash handling. The same bound is needed in the native
mixed walker and the limited LLDB walker, whose marker-skipping loops currently
advance no output budget.

### 3. Preserve the reference setter overload

D114608692 replaces the public `setParentFrame(AsyncStackFrame&)` signature with
a pointer signature. Add the nullable pointer overload, but retain the reference
overload as a forwarding convenience. That gives the metadata guard the null
restoration it needs without unnecessarily breaking downstream source
compatibility.

### 4. Repair the LLDB root transition

Both LLDB mixed-stack walkers skip a marker before checking whether the real
frame above it ends the async segment. They therefore lose the same bottom-frame
root transition as BPF, but unlike BPF they can directly mirror D115061390's
look-through logic. Apply the change to both copies and cover it with a shared
mock chain if the scripts' test infrastructure permits.

### 5. Make a valid marker constructible in one step

`AsyncStackMetadataFrame` says its return address is always the cookie, but its
default constructor produces an empty `AsyncStackFrame`; public setters leave
every caller responsible for installing both the cookie and payload. Give it a
payload-taking constructor that establishes both fields before publication and
remove unnecessary post-construction mutation.

Centralize conversion from marker `AsyncStackFrame*` to its enclosing object.
Add standard-layout and member-offset assertions for the cast used by
D115061390. The current alignment assertion does not establish that enclosing
object contract.

### 6. Repair and consolidate behavior coverage

The current ten helper tests instantiate only `Task<void>`. Their "sibling"
cases await branches sequentially, so they do not exercise overlapping sibling
state. Their local `getMetadata()` helper also fails to assert that the stack
walk succeeded; `-1` becomes an empty range and can make no-metadata tests pass.

Use the existing backend-neutral behavior suite as the basis for focused
coverage of:

- zero and `UINT64_MAX` with explicit presence;
- restoration after normal return and exception;
- a move-only value and `Task<T&>`;
- cancellation while the inner Task is suspended;
- a real same-executor suspension and three executor hops;
- overlapping sibling fanout whose scopes are live simultaneously;
- awaited structured descendants; and
- genuinely detached work that resumes and walks its stack after the wrapper has
  completed, asserting only that it does not crash or expose the expired scope.

The tests should sample synchronously while each scope is live by calling the
production walker. That is robust and deterministic; timing-based profiler
sampling belongs in each profiler integration test. Use `CO_TEST` for the new
coroutine-native cases. Keep `blockingWait()` only where creating a nested
`AsyncStackRoot` is itself the behavior under test.

### 7. Test the old and new walker surfaces independently

D115061390 rewrites the existing native stack test to call only the new
three-argument overload. Retain direct regression coverage for the legacy
two-argument overload, since the heap profiler and other callers still use it.
Add separate tests for:

- marker filtering without metadata capture;
- presence-preserving capture, once its representation is encoded;
- a bottom marker followed by an `AsyncStackRoot` transition;
- truncation immediately before a marker; and
- malformed/cyclic marker input terminating within a physical-node bound.

The LLDB copies need mock or live coverage of the bottom-marker root transition
and limited-walk behavior. The sampling-profiler side needs a BPF/event test
that proves the payload and raw marker index survive collection, rather than
only testing that a cookie is erased.

### 8. Clean local correctness and build issues

- Fix the new integer-to-pointer lint failures by comparing pointer values as
  integers through one helper, rather than materializing the cookie as an
  arbitrary pointer.
- Fix "If no async operation is progress" in the new API documentation.
- Avoid substring-based symbol matching in `StackTraceTest.cpp`, where one
  function name can match another.

## What appears sound

- The marker and guard live in the wrapper's coroutine frame, so they survive
  ordinary suspension and executor changes.
- The marker is part of persistent logical frame ancestry, not a per-turn
  `AsyncStackRoot`. No scheduler-specific propagation is required for ordinary
  awaited Tasks.
- Concurrent wrappers mutate different owner frames. Structured siblings do not
  share one mutable metadata slot.
- `co_awaitTry()` followed by an explicit guard reset unlinks the marker before
  the wrapper's final suspend. Guard destruction covers exception,
  cancellation/destruction, and other early exits.
- The stack does not grow every Task promise, `AsyncStackFrame`, or
  `AsyncStackRoot`. Untagged execution does not run new scheduler plumbing.

One suspected failure is not real under the helper's invariant: a marker cannot
normally appear as a standalone tail after its owner has disappeared. It is
always directly below the owner and is unlinked before the owner performs its
normal final pop. The defensive "current frame is a marker" branches still need
bounded behavior for corrupted or asynchronously observed data, but this does
not invalidate the core lifetime design.

## Recommended order

1. Encode the settled sample-level output contract: explicit presence, one
   effective value, and its exact raw marker index.
2. Patch and verify bottom-frame root stitching in BPF and LLDB.
3. Prototype BPF capture of the fixed scalar. Do not treat userspace cookie
   deletion as profiler integration.
4. Wire versioned heap-profiler aggregation and the sampling-profiler
   event/output path end to end, including overlapping-value aggregate-ownership
   tests.
5. Apply the remaining independent easy fixes and run the reusable behavior
   suite plus focused walker tests.
6. Measure active-scope cost, metadata-free native walking, and
   sampling-profiler sample processing before rollout.
