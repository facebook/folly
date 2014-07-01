# Wangle
Wangle is a framework for expressing asynchronous code in C++ using the Future pattern.

**wan•gle** |ˈwaNGgəl| informal  
*verb*  
Obtain (something that is desired) by persuading others to comply or by manipulating events.

*noun*  
A framework for expressing asynchronous control flow in C++, that is composable and easily translated to/from synchronous code.

*synonyms*  
[Finagle](http://twitter.github.io/finagle/)

Wangle is a futures-based async framework inspired by [Twitter's Finagle](http://twitter.github.io/finagle/) (which is in scala), and (loosely) building upon the existing (but anemic) Futures code found in the C++11 standard ([`std::future`](http://en.cppreference.com/w/cpp/thread/future)) and [`boost::future`](http://www.boost.org/doc/libs/1_53_0/boost/thread/future.hpp) (especially >= 1.53.0). Although inspired by the std::future interface, it is not syntactically drop-in compatible because some ideas didn't translate well enough and we decided to break from the API. But semantically, it should be straightforward to translate from existing std::future code to Wangle.

The primary semantic differences are that Wangle Futures and Promises are not threadsafe; and as does `boost::future`, Wangle supports continuing callbacks (`then()`) and there are helper methods `whenAll()` and `whenAny()` which are important compositional building blocks.

## Brief Synopsis

```C++
#include <folly/wangle/Future.h>
using namespace folly::wangle;
using namespace std;

void foo(int x) {
  // do something with x
  cout << "foo(" << x << ")" << endl;
}

// ...

  cout << "making Promise" << endl;
  Promise<int> p;
  Future<int> f = p.getFuture();
  f.then(
    [](Try<int>&& t) {
      foo(t.value());
    });
  cout << "Future chain made" << endl;

// ... now perhaps in another event callback

  cout << "fulfilling Promise" << endl;
  p.setValue(42);
  cout << "Promise fulfilled" << endl;
```

This would print:
  
```
making Promise
Future chain made
fulfilling Promise
foo(42)
Promise fulfilled
```

## User Guide

Let's begin with an example. Consider a simplified Memcache client class with this interface:

```C++
class MemcacheClient {
 public:
  struct GetReply {
    enum class Result {
      FOUND,
      NOT_FOUND,
      SERVER_ERROR,
    };

    Result result;
    // The value when result is FOUND,
    // The error message when result is SERVER_ERROR or CLIENT_ERROR
    // undefined otherwise
    std::string value;
  };

  GetReply get(std::string key);
};
```

This API is synchronous, i.e. when you call `get()` you have to wait for the result. This is very simple, but unfortunately it is also very easy to write very slow code using synchronous APIs.

Now, consider this traditional asynchronous signature for `get()`:

```C++
int get(std::string key, std::function<void(GetReply)> callback);
```

When you call `get()`, your asynchronous operation begins and when it finishes your callback will be called with the result. (Unless something goes drastically wrong and you get an error code from `get()`.) Very performant code can be written with an API like this, but for nontrivial applications the code descends into a special kind of spaghetti code affectionately referred to as "callback hell".

The Future-based API looks like this:

```C++
Future<GetReply> get(std::string key);
```

A `Future<GetReply>` is a placeholder for the `GetReply` that we will eventually get. A Future usually starts life out "unfulfilled", or incomplete, i.e.:

```C++
fut.isReady() == false
fut.value()  // will throw an exception because the Future is not ready
```

At some point in the future, the Future will have been fulfilled, and we can access its value.

```C++
fut.isReady() == true
GetReply& reply = fut.value();
```

Futures support exceptions. If something exceptional happened, your Future may represent an exception instead of a value. In that case:

```C++
fut.isReady() == true
fut.value() // will rethrow the exception
```

Just what is exceptional depends on the API. In our example we have chosen not to raise exceptions for `SERVER_ERROR`, but represent this explicitly in the `GetReply` object. On the other hand, an astute Memcache veteran would notice that we left `CLIENT_ERROR` out of `GetReply::Result`, and perhaps a `CLIENT_ERROR` would have been raised as an exception, because `CLIENT_ERROR` means there's a bug in the library and this would be truly exceptional. These decisions are judgement calls by the API designer. The important thing is that exceptional conditions (including and especially spurious exceptions that nobody expects) get captured and can be handled higher up the "stack".

So far we have described a way to initiate an asynchronous operation via an API that returns a Future, and then sometime later after it is fulfilled, we get its value. This is slightly more useful than a synchronous API, but it's not yet ideal. There are two more very important pieces to the puzzle.

First, we can aggregate Futures, to define a new Future that completes after some or all of the aggregated Futures complete.  Consider two examples: fetching a batch of requests and waiting for all of them, and fetching a group of requests and waiting for only one of them.

```C++
vector<Future<GetReply>> futs;
for (auto& key : keys) {
  futs.push_back(mc.get(key));
}
auto all = whenAll(futs.begin(), futs.end());

vector<Future<GetReply>> futs;
for (auto& key : keys) {
  futs.push_back(mc.get(key));
}
auto any = whenAny(futs.begin(), futs.end());
```

`all` and `any` are Futures (for the exact type and usage see the header files).  They will be complete when all/one of `futs` are complete, respectively. (There is also `whenN()` for when you need *some*.)

Second, we can attach callbacks to a Future, and chain them together monadically. An example will clarify:

```C++
Future<GetReply> fut1 = mc.get("foo");

Future<string> fut2 = fut1.then(
  [](Try<GetReply>&& t) {
    if (t.value().result == MemcacheClient::GetReply::Result::FOUND)
      return t.value().value;
    throw SomeException("No value");
  });

Future<void> fut3 = fut2.then(
  [](Try<string>&& t) {
    try {
      cout << t.value() << endl;
    } catch (std::exception const& e) {
      cerr << e.what() << endl;
    }
  });
```

That example is a little contrived but the idea is that you can transform a result from one type to another, potentially in a chain, and unhandled errors propagate. Of course, the intermediate variables are optional. `Try<T>` is the object wrapper that supports both value and exception.

Using `then` to add callbacks is idiomatic. It brings all the code into one place, which avoids callback hell.

Up to this point we have skirted around the matter of waiting for Futures. You may never need to wait for a Future, because your code is event-driven and all follow-up action happens in a then-block. But if want to have a batch workflow, where you initiate a batch of asynchronous operations and then wait for them all to finish at a synchronization point, then you will want to wait for a Future.

Other future frameworks like Finagle and std::future/boost::future, give you the ability to wait directly on a Future, by calling `fut.wait()` (naturally enough). Wangle has diverged from this pattern because we don't want to be in the business of dictating how your thread waits. We may work out something that we feel is sufficiently general, in the meantime adapt this spin loop to however your thread should wait:

  while (!f.isReady()) {}

(Hint: you might want to use an event loop or a semaphore or something. You probably don't want to just spin like this.)

Wangle is partially threadsafe. A Promise or Future can migrate between threads as long as there's a full memory barrier of some sort. `Future::then` and `Promise::setValue` (and all variants that boil down to those two calls) can be called from different threads. BUT, be warned that you might be surprised about which thread your callback executes on. Let's consider an example.

```C++
// Thread A
Promise<void> p;
auto f = p.getFuture();

// Thread B
f.then(x).then(y).then(z);

// Thread A
p.setValue();
```

This is legal and technically threadsafe. However, it is important to realize that you do not know in which thread `x`, `y`, and/or `z` will execute. Maybe they will execute in Thread A when `p.setValue()` is called. Or, maybe they will execute in Thread B when `f.then` is called. Or, maybe `x` will execute in Thread B, but `y` and/or `z` will execute in Thread A. There's a race between `setValue` and `then`—whichever runs last will execute the callback. The only guarantee is that one of them will run the callback.

Naturally, you will want some control over which thread executes callbacks. We have a few mechanisms to help.

The first and most useful is `via`, which passes execution through an `Executor`, which usually has the effect of running the callback in a new thread.
```C++
aFuture
  .then(x)
  .via(e1).then(y1).then(y2)
  .via(e2).then(z);
```
`x` will execute in the current thread. `y1` and `y2` will execute in the thread on the other side of `e1`, and `z` will execute in the thread on the other side of `e2`. `y1` and `y2` will execute on the same thread, whichever thread that is. If `e1` and `e2` execute in different threads than the current thread, then the final callback does not happen in the current thread. If you want to get back to the current thread, you need to get there via an executor.

This works because `via` returns a deactivated ("cold") Future, which blocks the propagation of callbacks until it is activated. Activation happens either explicitly (`activate`) or implicitly when the Future returned by `via` is destructed. In this example, there is no ambiguity about in which context any of the callbacks happen (including `y2`), because propagation is blocked at the `via` callsites until after everything is wired up (temporaries are destructed after the calls to `then` have completed).

You can still have a race after `via` if you break it into multiple statements, e.g. in this counterexample:
```C++
f = f.via(e1).then(y1).then(y2); // nothing racy here
f2.then(y3); // racy
```
If you want more control over the delayed execution, check out `Later`.
```C++
Later<void> later;
later = later.via(e1).then(y1).then(y2); // nothing racy here
later = later.then(y3); // nor here
later.launch(); // explicit launch
```

The third and least flexible (but sometimes very useful) method assumes only two threads and that you want to do something in the far thread, then come back to the current thread. `ThreadGate` is an interface for a bidirectional gateway between two threads. It's usually easier to use a Later, but ThreadGate can be more efficient, and if the pattern is used often in your code it can be more convenient.
```C++
// Using a ThreadGate (which has two executors xe and xw)
tg.gate(a).then(b);

// Using via
makeFuture()
  .via(xe).then(a)
  .via(xw).then(b);
```

## You make me Promises, Promises

If you are wrapping an asynchronous operation, or providing an asynchronous API to users, then you will want to make Promises. Every Future has a corresponding Promise (except Futures that spring into existence already completed, with `makeFuture()`). Promises are simple, you make one, you extract the Future, and you fulfil it with a value or an exception. Example:

```C++
Promise<int> p;
Future<int> f = p.getFuture();

f.isReady() == false

p.setValue(42);

f.isReady() == true
f.value() == 42
```

and an exception example:

```C++
Promise<int> p;
Future<int> f = p.getFuture();

f.isReady() == false

p.setException(std::runtime_error("Fail"));

f.isReady() == true
f.value() // throws the exception
```

It's good practice to use fulfil which takes a function and automatically captures exceptions, e.g.

```C++
Promise<int> p;
p.fulfil([]{
  try {
    // do stuff that may throw
    return 42;
  } catch (MySpecialException const& e) {
    // handle it
    return 7;
  }
  // Any exceptions that we didn't catch, will be caught for us
});
```

## FAQ

### Why not use std::future?
No callback support.
See also http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3428.pdf

### Why not use boost::future?
- 1.53 is brand new, and not in fbcode
- It's still a bit buggy/bleeding-edge
- They haven't fleshed out the threading model very well yet, e.g. every single `then` currently spawns a new thread unless you explicitly ask it to work on this thread only, and there is no support for executors yet.

### Why use heap-allocated shared state? Why is Promise not a subclass of Future?
C++. It boils down to wanting to return a Future by value for performance (move semantics and compiler optimizations), and programmer sanity, and needing a reference to the shared state by both the user (which holds the Future) and the asynchronous operation (which holds the Promise), and allowing either to go out of scope.

### What about proper continuations? Futures suck.
People mean two things here, they either mean using continuations (as in CSP) or they mean using generators which require continuations. It's important to know those are two distinct questions, but in our context the answer is the same because continuations are a prerequisite for generators.

C++ doesn't directly support continuations very well. But there are some ways to do them in C/C++ that rely on some rather low-level facilities like `setjmp` and `longjmp` (among others). So yes, they are possible (cf. [Mordor](https://github.com/ccutrer/mordor)).

The tradeoff is memory. Each continuation has a stack, and that stack is usually fixed-size and has to be big enough to support whatever ordinary computation you might want to do on it. So each living continuation requires a relatively large amount of memory. If you know the number of continuations will be small, this might be a good fit. In particular, it might be faster and the code might read cleaner.

Wangle takes the middle road between callback hell and continuations, one which has been trodden and proved useful in other languages. It doesn't claim to be the best model for all situations. Use your tools wisely.
