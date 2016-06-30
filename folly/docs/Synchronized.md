`folly/Synchronized.h`
----------------------

|:exclamation: `Synchronized` is deprecated            |
|------------------------------------------------------|
|Use `Locked` in new code instead of `Synchronized<T>` |


### `Locked`

`Locked` is a newer replacement for `Synchronized`.  Like `Synchronized`, it
provides a way for defining a variable and associated lock, and ensuring that
the data can only be accessed with the lock held.

`Locked` has several advantages over `Synchronized`, but the primary ones are:

* It eliminates the `SYNCHRONIZED` macro.  Besides the fact that preprocessor
  macros are generally to be avoided, `SYNCHRONIZED` also had a few subtle
  issues that turned out to be unfixable.
* It uses exclusive locks by default, and when using shared locks it makes the
  decision to obtain an exclusive lock versus a shared lock much more explicit.
  With `Synchronized` this decision was usually implicit based on the
  const-ness of the object, making it easy for code to unintentionally acquire
  an exclusive lock when only a shared one was needed.

### Differences between `Locked` and `Synchronized`

##### `SYNCHRONIZED`

The main difference between `Synchronized` and `Locked` is that the
`SYNCHRONIZED` macro is now gone.

While this macro had some advantages (forcing a new scope when you acquired the
lock), it had a number of disadvantages as well.  Besides being a preprocessor
macro, and having all the disadvantages associated with that, there were
several minor issues with it that could not be fixed.

* It was implemented using `for` loops internally.  This resulted in confusing
  behavior for `break` and `continue` statements inside `SYNCHRONIZED` blocks.
  For instance

  ```cpp
  for (const auto* elem : lruQueue) {
    SYNCHRONIZED(lockedElem, *elem) {
      if (lockedElem.lastAccessedTime() > expirationTime) {
        break;
      }
    }
  }
  ```

  This break statement would apply to the internal `for` loops that
  `SYNCHRONIZED` expanded to, and would not break out of the loop that was
  actually visible to anyone reading this code.

* Due to the way for loops were used internally, this often confused compilers
  about possible function return paths, resulting in false positive warnings
  about unreachable code.  (Particularly warnings about unreachable code paths
  not returning a value from functions that return non-void.)

##### `operator->()`

`Synchronized<T>` provided an `->` operator that returned a `LockedPtr` or
`ConstLockedPtr` as appropriate.

While this provided a convenient API for acquiring the lock without having to
declare a `SYNCHRONIZED` block, it also had several pitfalls:

* It was easy for callers to unintentionally write code that acquired and
  released the lock multiple times, when it really should have been held for
  the entire duration of the operation.

  For example, the following code acquires and releases the lock separately for
  the `begin()` and `end()` calls, and is not holding the lock at all for the
  actual loop code.

  ```cpp
  Synchronized<vector<int>> vec;
  FOR_EACH_RANGE (i, vec->begin(), vec->end()) {
    ...
  }
  ```

* It led to callers acquiring a write lock when they really only needed a
  read-lock.  As long as you had a non-const pointer or reference to the
  object, `operator->` would acquire a write lock.

  You could use the `asConst()` method to cast the object to const if you knew
  you wanted a read-only lock, but it was very easy for callers to forget to do
  this, and not realize they would be acquiring an exclusive lock.

  With `Locked`, callers have to explicitly use `wlock()` or `rlock()` to hold
  the lock, making this choice explicit.

##### Move and swap operators

`Synchronized<T>` provided a move constructor and move assignment operator that
moved the underlying data, but not the lock.

TODO: Should we retain this for `Locked`?  The fact that it does not move the
lock seems potentially confusing, but I am not sure in what scenarios this will
matter.

With `Locked` you can construct a `Locked<T>` from an rvalue reference to `T`,
but not an rvalue-reference to `Locked<T>`.

##### std::condition_variable support

`Locked<T, std::mutex>` supports using a `std::condition_variable` with the
underlying mutex, while `Synchronized<T, std::mutex>` does not.

##### std::chrono durations

For acquiring the lock with a timeout, the `Locked<T>` APIs use `std::chrono`
duration arguments, while `Synchronized<T>` uses plain integers, and treats
them as milliseconds.


### Converting code from `Locked` to `Synchronized`

##### `SYNCHRONIZED`

`SYNCHRONIZED` blocks can be converted from this:

```cpp
  Synchronized<MyObject> obj;
  SYNCHRONIZED (obj) {
    obj.doStuff();
    obj.otherStuff();
  }
```

to this:

```cpp
  Locked<MyObject> obj;
  obj.withLock([](auto& locked) {
    locked.doStuff();
    locked.otherStuff();
  });
```

or this:

```cpp
  Locked<MyObject> obj;
  {
    auto locked = obj.lock();
    locked->doStuff();
    locked->otherStuff();
  }
```

##### Single statements

Statements that used `operator->` to acquire the lock implicitly can be
converted from this:

```cpp
  Synchronized<MyObject> obj;
  obj->doStuff();
```

to this:

```cpp
  Locked<MyObject> obj;
  obj.lock()->doStuff();
```
