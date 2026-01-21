## Guide to `as_unsafe()` and future->coro Migration

The `as_unsafe()` method on `now_task`, `now_task_with_executor`, and `safe_task`
is an escape hatch for interoperating with older futures-based code or other
places not yet compatible with true structured concurrency patterns. `as_unsafe()`
should only be used temporarily during migrations, particularly when you need to
integrate futures-based automated testing with in-development coroutine implementations,
but should not be used in production code.

**WARNING**: The `folly::coro::Task` is not safe to pass references to.
`now_task.as_unsafe()` returns a `folly::coro::Task`. You must be careful with
lifetime management when passing references to `as_unsafe()`.

### Why `now_task` is safe by default

When you use `now_task` normally (without `as_unsafe()`), the compiler forces
you to await it in the same full-expression that created it. This means C++
reference lifetime extension protects you from bugs:

```cpp
// Safe: awaited immediately in the same expression
co_await someNowTask(localRef);
```

Once you call `as_unsafe()`, you exit this protected zone and must manually
reason through reference lifetimes.

### The stack-use-after-return or use-after-free: A Common and Pernicious Bug Pattern

This bug pattern often manifests as a heisenbug that only shows up in edge
cases in production. It is **very hard to track down** if you don't run your
service under ASAN.

**Bug recipe:**

1. Create a task containing references with lifetime equal to scope X.
2. Call `.semi()` or `.start()` (with `now_task`, these are gated behind
   `as_unsafe()`). This often starts the task "in the background", on another
   thread.
3. Forget or fail to await the completion of the background task -- this often
   happens due to an uncaught exception.
4. Execution exits scope X, but your task is still running and is now touching
   invalid memory.

**Example of the bug:**

```cpp
now_task<int> processData(int& x);

folly::SemiFuture<int> BAD_processDataParent() {
  // BUG: `x` is a reference that will go out of scope!
  int x = 123;
  return processData(x).as_unsafe().semi();
  // The scope exits before the future completes,
  // `processData` will be reading invalid memory.
}
```

### Recommendation

**Do not use `as_unsafe()` in production code** unless you are in a very narrow
scenario where it is feasible to manually prove the resulting movable task is
used in a lifetime-safe way.

### Safe Migration Patterns

In order to safely pass a reference to an unsafe `Task`, this condition must hold:

**The underlying memory is guaranteed to outlive the coroutine execution.**

This condition can be achieved using `co_invoke` or `deferValue`. `co_invoke`
is the preferred pattern, however there is a small performance tax to allocate the coroutine frame.
`deferValue` is harder to read, and harder to get right, however pays less of a performance tax.
In the rare case where you must make a `now_task` take reference arguments self-contained,
use one of these patterns.

#### Pattern 1: `co_invoke` (Preferred)

Use `co_invoke` to make decay-copies of arguments into an additional coro frame (often on the heap),
guaranteeing their lifetime for the duration of the coroutine:

```cpp
now_task<int> processData(int& x);

folly::SemiFuture<int> processDataSafe(int& x) {
  // co_invoke makes decay-copies of arguments into a new coroutine frame
  return folly::coro::co_invoke(
             [](auto&&... args) {
               return processData(std::forward<decltype(args)>(args)...)
                   .as_unsafe();
             },
             x)
      .semi();
}
```

#### Pattern 2: `deferValue` with Explicit Heap Allocation

If `co_invoke` is not suitable, you can use `deferValue` to extend the
lifetime of heap-allocated data:

```cpp
now_task<int> processData(int& x);

folly::SemiFuture<int> processDataSafe(int& x) {
  // Allocate on heap
  auto dataPtr = std::make_unique<int>(x);

  // Use deferValue to ensure dataPtr lives until the future completes
  return processData(*dataPtr).as_unsafe().semi().deferValue(
      [dataPtr = std::move(dataPtr)](int result) { return result; });
}
```

**Note**: The `deferValue` callback captures `dataPtr`, ensuring it stays alive
until the future is resolved.
