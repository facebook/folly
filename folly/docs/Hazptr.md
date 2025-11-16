RCU and Hazard Pointers
-----------------------

# Overview

Hazard pointers are a lifetime-protection mechanism for shared objects published
by producers (aka writers) and observed by consumers (aka readers). They are
asymmetric and biased towards consumers.

But that is a confusing paragraph! Let us start more simply.

Let us consider the motivating case of why we need some lifetime-protection
mechanism for shared objects in the first place. It looks like this:

``` cpp
template <typename object>
struct shared_object {
private:
  std::shared_mutex mut_;
  object* obj_{nullptr};

public:
  /// release operation
  void publish(std::unique_ptr<object> obj) noexcept {
    std::unique_lock lock{mut_};
    auto old = std::exchange(obj_, obj.release());
    lock.unlock();
    delete old;
  }

  /// acquire operation
  object* observe() const noexcept {
    std::shared_lock lock{mut_};
    return obj_;
  }
};
```

This example is utterly broken. Suppose we have these operations:
``` cpp
struct config;
void do_something_with(config&);

shared_object<config> cfg;

void producer() { // Thread A
  cfg.publish(std::make_unique<config>(/*...*/));
}
void consumer() { // Thread B
  if (config* ptr = cfg.observe()) {
    do_something_with(*ptr);
  }
}
```
Then Thread B might be using the object pointed to by `ptr` concurrently with
Thread A deleting that very object! Formally, that is a category of undefined
behavior called a data-race, but it means that the program will, *at best*,
crash immediately or, in increasing order of badness, silently corrupt data,
silently mix data belonging to different users, or silently perform destructive
actions like launching the missiles.

What Thread B needs is a way to continue using the object pointed to by `ptr`
by arranging that neither Thread B nor any other thread will delete that object
until Thread B is done using it.

The most common technique to mitigate this problem is to extend the shared-lock
lifetime to cover the entire critical section of observing the current object,
not just the critical section of copying the pointer to the current object. Here
is how that looks:

``` cpp
template <typename object>
struct shared_object {
private:
  mutable std::shared_mutex mut_;
  object* obj_;

public:
  struct protected_ptr {
  private:
    friend shared_object;

    std::shared_lock<std::shared_mutex> lock_;
    object* obj_;

    protected_ptr(std::shared_mutex& mut, object*& obj) noexcept
        : lock_{mut}, obj_{obj} {}

    protected_ptr(protected_ptr&&) = delete;
    protected_ptr(protected_ptr const&) = delete;
    protected_ptr& operator=(protected_ptr&&) = delete;
    protected_ptr& operator=(protected_ptr const&) = delete;

  public:
    explicit operator bool() const noexcept { return obj_; }
    object* operator->() const noexcept { return obj_; }
    object& operator*() const noexcept { return *obj_; }
  };

  ~shared_object() {
    delete obj_;
  }

  void publish(std::unique_ptr<object> obj) noexcept {
    std::unique_lock lock{mut_};
    auto old = std::exchange(obj_, obj.release());
    lock.unlock();
    delete old;
  }

  protected_ptr observe() const noexcept {
    return protected_ptr(mut_, obj_);
  }
};
```

But there is a problem. The critical section of observing the current object may
easily be very long. Indeed, there may be two or more threads all observing the
shared object, and there might never be a moment when the publisher can acquire
an exclusive lock on the shared mutex. If the shared mutex has reader-priority,
then the publisher may starve, waiting forever without making any progress.
Alternatively, if the shared mutex has writer-priority, then the publisher may
wait a very long time to acquire the exclusive mutex. As it waits, it blocks all
new observers that want to acquire a shared lock - new observers must wait for
all existing observers to complete their critical sections before beginning
their critical sections. Either way, that is a lot of waiting. But what we want
is a technique that does not involve this risk of waiting.

Now let us consider the typical publish/observe lifetime-protection mechanism
that we tend to use for shared objects which do not risk blocking publishers or
observers. It looks like this:

``` cpp
template <typename object>
struct shared_object {
private:
  std::shared_mutex mut_;
  std::shared_ptr<object> obj_;

public:
  /// release operation
  void publish(std::shared_ptr<object> obj) noexcept {
    std::unique_lock lock{mut_};
    std::swap(obj_, obj); // destroy only after lock expires
  }

  /// acquire operation
  std::shared_ptr<object> observe() const noexcept {
    std::shared_lock lock{mut_};
    return obj_;
  }
};
```

A consumer can ask the `shared_object` instance for lifetime-protected access to
whatever shared object was most recently published and that it currently holds.

There are two key aspects to which to pay attention:
* Lifetime protection, via the shared pointer.
* Synchronization, via the shared mutex:
  * Synchronization between two producers publishing.
  * Synchronization between two consumers observing.
  * synchronization between a producer publishing and a consumer observing.

As we look at more techniques, the exact mechanisms for lifetime protection and
synchronization will change, but these will still be the problems being solved.

With this technique, there is still the risk of waiting. However, the critical
sections in the observer with this technique are extremely short - a handful of
instructions - at least unless the operating system suspends the thread during
the critical section. As long as that does not happen, observers will not block
publishers from publishing.

A more advanced version looks like:

``` cpp
template <typename object>
struct shared_object {
private:
  std::atomic<std::shared_ptr<object>> atom_;

public:
  /// release operation
  void publish(std::shared_ptr<object> obj) noexcept {
    atom_.store(obj, std::memory_order_release);
  }

  /// acquire operation
  std::shared_ptr<object> observe() const noexcept {
    return atom_.load(std::memory_order_acquire);
  }
};
```

This version swaps out a mutex and in an atomic. With the caveat that the easy
implementation of the atomic-shared-ptr is in terms of a shared-mutex! So YMMV.

There are alternative implementations of atomic-shared-ptr. Folly has one, for
example:

``` cpp
template <typename object>
struct shared_object {
private:
  folly::atomic_shared_ptr<object> atom_; // v.s. atomic<shared_ptr<>>

public:
  /// release operation
  void publish(std::shared_ptr<object> obj) noexcept {
    atom_.store(obj, std::memory_order_release);
  }

  /// acquire operation
  std::shared_ptr<object> observe() const noexcept {
    return atom_.load(std::memory_order_acquire);
  }
};
```

These prototypical publish/observe lifetime-protection mechanisms are sufficient
for most use-cases. But not for all. The trouble with them in some cases is that
they are asymmetric: observation is extremely hot and dominates publication. But
the prototypical mechanisms have symmetric costs, with the cost of observation
being comparable with the cost of publication, both involving several atomic
read-modify-write operations. And the trouble is that atomic read-modify-write
operations can be too costly for the hottest paths. For example, shared-mutex
lock and unlock operations each requires at least one atomic compare-exchange.
And a shared-ptr copy requires at least two atomic fetch-add operations, one in
the constructor and one in the destructor. Especially when many consumer threads
are involved - atomic read-modify-write operations scale extremely poorly with
core count. So we need a technique which amortizes or minimizes use of atomic
read-modify-write operations in the observation path and which scales very well
with core count.

There are two publish/observe lifetime-protection mechanisms for shared objects
which are designed for these hot asymmetric cases: RCU and Hazard Pointers.

RCU is slightly faster but coarse-grained, while Hazard Pointers is fine-grained
but slightly slower. But both optimize for hot paths needing lifetime-protected
observation of shared objects.

RCU might also be thought of as a scalable alternative to reader-writer locking,
and Hazard Pointers as a scalable alternative to reference counting. There is
considerable overlap between the two, and considerable difference with locking
and reference counting, but it is sometimes a helpful introductory mental model.

Both mechanisms require uses not to delete shared objects naively, since readers
may concurrently be reading the shared objects. Rather, uses must *retire* the
shared objects, which is an operation that marks them for future deletion. This
scheme is called *deferred reclamation*. Uses *retire* shared objects when they
are no longer needed, and then a garbage collection pass at some later point
*reclaims* all retired objects for which there cannot possibly be any concurrent
readers still reading them.

What these mechanisms give up is deterministic deletion. With shared pointers,
whether protected by a mutex or by an atomic, the shared object will be deleted
the exact moment it is no longer needed. Either the publisher or the observer
will internally decrement the refcount and, when the refcount becomes zero, the
decrementing operation will internally delete the shared object. But with RCU
and Hazard Pointers, this is not possible. Instead, there is another system, the
*domain*, with which the publishers and the observers cooperate to track when
it is safe to destroy shared objects - very much like the garbage collector in
many programming languages, but explicit and opt-in.

# RCU Shared Object

A typical implementation of a `shared_object` type that is similar to the prior
symmetric examples might look like this:

``` cpp
template <typename object>
struct shared_object {
private:
  rcu_domain& domain_;
  std::atomic<object*> atom_;

  void retire(object* ptr) {
    if (ptr) {
      rcu_retire(ptr, {}, domain);
    }
  }

public:
  struct protected_ptr {
  private:
    friend shared_object;

    std::scoped_lock<rcu_domain> prot_; // protects obj_ during prot_'s lifetime
    object* obj_; // access is valid while prot_ remains alive

    protected_ptr(
        rcu_domain& domain,
        std::atomic<object*>& atom) noexcept
        : prot_{domain}, obj_{atom.load(std::memory_order_acquire)} {}

    protected_ptr(protected_ptr&&) = delete;
    protected_ptr(protected_ptr const&) = delete;
    protected_ptr& operator=(protected_ptr&&) = delete;
    protected_ptr& operator=(protected_ptr const&) = delete;

  public:
    explicit operator bool() const noexcept { return obj_; }
    object* operator->() const noexcept { return obj_; }
    object& operator*() const noexcept { return *obj_; }
  };

  ~shared_object() {
    retire(atom_.load(std::memory_order_relaxed));
  }

  /// release operation
  void publish(std::unique_ptr<object> obj) {
    retire(atom_.exchange(obj.release(), std::memory_order_acq_rel));
  }

  /// acquire operation
  protected_ptr observe() const {
    return protected_ptr(domain_, atom_);
  }
};
```

# Hazard Pointers Shared Object

A typical implementation of a `shared_object` type that is similar to the prior
symmetric examples might look like this:

``` cpp
template <std::derived_from<hazptr_obj> object>
struct shared_object {
private:
  hazptr_domain& domain_;
  std::atomic<object*> atom_;

  void retire(object* ptr) {
    if (ptr) {
      domain_.retire(ptr);
    }
  }

public:
  struct protected_ptr {
  private:
    friend shared_object;

    hazptr_holder prot_; // protects obj_ during prot_'s lifetime
    object* obj_; // access is valid while prot_ remains alive

    protected_ptr(
        hazptr_domain& domain,
        std::atomic<object*>& atom) noexcept
        : prot_{make_hazard_pointer(domain)},
          obj_{prot_.protect(atom)} {}

    protected_ptr(protected_ptr&&) = delete;
    protected_ptr(protected_ptr const&) = delete;
    protected_ptr& operator=(protected_ptr&&) = delete;
    protected_ptr& operator=(protected_ptr const&) = delete;

  public:
    explicit operator bool() const noexcept { return obj_; }
    object* operator->() const noexcept { return obj_; }
    object& operator*() const noexcept { return *obj_; }
  };

  ~shared_object() {
    retire(atom_.load(std::memory_order_relaxed));
  }

  /// release operation
  void publish(std::unique_ptr<object> obj) {
    retire(atom_.exchange(obj.release(), std::memory_order_acq_rel));
  }

  /// acquire operation
  protected_ptr observe() const {
    return protected_ptr(domain_, atom_);
  }
};
```

# Explanation

A call to `observe()` returns a `protected_ptr`, which may be dereferenced to
give access to the protected object - but only while the `protected_ptr` stays
alive. This is similar to how `shared_object::observe()` with `std::shared_ptr`
returned a `std::shared_ptr`, which may also be dereferenced to give access to
the protected object - but only while the `std::shared_ptr` stays alive.

The call to `observe()` here is faster than using a `std::shared_mutex` and a
`std::shared_ptr` together, and is also faster than using even a well-optimized
`std::atomic<std::shared_ptr>`. But the price is that the call to `publish()` is
much more expensive.

So it makes sense to use the RCU or the Hazard Pointers version when observation
dominates publication by at least an order of magnitude.

A `scoped_lock<rcu_domain>` can lock and unlock a domain extremely quickly,
using only load-acquire and store-release instructions for synchronization.
Protection of the pointee's lifetime is reset when the lock is released or
when its lifetime ends. This is faster than the symmetric versions. It is also
typically faster than hazard pointers in practical usage, but not always. It is
coarse-grained and does not specifically protect any individual shared objects,
so a single domain-lock critical section can protect many shared objects all at
once, with the `lock` and `unlock` operations amortized across them. But for
cases using the default domain, where only one object needs to be protected at
a time and with certain other constraints, hazard pointers can be faster than
RCU.

A `hazptr_holder` can load a pointer from an atomic-pointer and protect that
pointee's lifetime in a single transaction. This transaction is extremely fast,
using only load-acquire and store-release instructions for synchronization.
Protection of that pointee's lifetime is reset when the `hazptr_holder` loads
and protects another pointer, when its lifetime ends, or when protection is
explicitly reset.

Creation and destruction of a `hazptr_holder` is fairly fast. The ideal case is,
however, to use a single `hazptr_holder` repeatedly to protect and unprotect
multiple pointers, amortizing the cost of `make_hazard_pointer` across multiple
protections. For example, this makes sense when walking a linked data structure
like a linked list or linked tree, where each node during traversal requires its
own separate lifetime-protection.

However, even using a `hazptr_holder` just once for a single protection is still
faster than the prototypical mechanisms like shared-lock with shared-ptr or
atomic-shared-ptr.

## Hazard Pointer Patterns

With Hazard Pointers, we conceptually have readers and writers. Writers store
the address of a shared object to an atomic-pointer, while readers load the
address of the shared object from the atomic-pointer.

The typical writer pattern is:
* create a new object,
* swap its address into the atomic-pointer using atomic-exchange, and
* retire the old object.

In some cases, the new object is wholly new; in other cases, the new object is a
copy of the old object with point modifications.

Storing the address of the new object into the atomic-pointer must be a release
operation, while loading the address of the old object from the atomic-pointer
must be an acquire operation. Retirement of the old object must only occur after
the address of the old object has been removed from the atomic-pointer so that
no new readers can begin protection on the old object concurrently with its
retirement. Retirement of the old object may occur concurrently with existing
readers that have already protected the old object, but occurring concurrently
with new readers would be forbidden.

Retirement of the old object schedules the old object for deferred reclamation.
That means the old object will be reclaimed - typically deleted - at some later
point, once all of the hazard pointers that protect its lifetime have expired.

``` cpp
class object : public hazptr_obj_base<object> {
  // ...
};

hazptr_domain& domain = default_hazptr_domain(); // or a non-default domain
std::atomic<object*> atom;

object make_from_scratch();
object make_from_update(object const&);

object make_object(object const* curr) {
  return curr ? make_from_update(*curr) : make_from_scratch();
}

/// single writer, or multiple writers serialized by a mutex so they do not race
void single_writer() {
  object* prev = atom.load(std::memory_order_relaxed);
  object* next = new object(make_from_scratch());
  atom.store(next, std::memory_order_release);
  if (prev) {
    prev->retire(domain);
  }
}

/// multiple writers that can race with each other
void multi_writer() {
  object* next = new object(make_from_scratch());
  if (object* prev = atom.exchange(next, std::memory_order_acq_rel)) {
    prev->retire(domain);
  }
}

/// single writer, or multiple writers serialized by a mutex so they do not race
///
/// monotonic means that the writer must observe the current object, make a new
/// object from it, and install publish the new object in a single transaction
void single_writer_monotonic() {
  object* prev = atom.load(std::memory_order_relaxed);
  object* next = new object(make_object(prev));
  atom.store(next, std::memory_order_release);
  if (prev) {
    prev->retire(domain);
  }
}

/// multiple writers that can race with each other
///
/// monotonic means that the writer must observe the current object, make a new
/// object from it, and install publish the new object in a single transaction
void multi_writer_monotonic() {
  auto hazptr = make_hazard_pointer(domain);
  object* curr = hazptr.protect(atom); // load-acquire on atom
  while (true) {
    object* next = new object(make_object(curr));
    if (atom.compare_exchange_strong(
          curr, next, std::memory_order_acq_rel)) {
      hazptr.reset_protection();
      curr->retire(domain);
      return;
    }
    delete next;
    while (!hazptr.try_protect(curr, atom)) {} // load-acquire on atom
  }
}
```

The typical reader pattern is:
* create a `hazptr_holder`,
* atomically load from the atomic-pointer and protect the resulting pointer, and
* hold the `hazptr_holder` alive for the entire duration of accessing the shared
  object.

``` cpp
class object : public hazptr_obj_base<object> {
  // ...
};

hazptr_domain<>& domain = default_hazptr_domain(); // or a non-default domain
std::atomic<object*> atom;

void reader() {
  auto hazptr = make_hazard_pointer(domain);
  if (object const* curr = hazptr.protect(atom)) {
    // the lifetime of the object pointed-to by `curr` will not end until hazard
    // pointer `hazptr` leaves scope below
    for (auto const& item : curr->data()) {
      // use `item`, etc ...
    }
  }
}
```

## Details

A `hazptr_holder` does two things:
* Extend the lifetime of a shared object.
* Synchronize between readers and the deferred deletion arising from retirement.

A `hazptr_holder` extends the lifetime of a shared object. But this does not
include synchronizing between concurrent readers. So a shared object either must
be immutable or, if concurrent accesses would be data-races, must have its own
synchronization.
* If immutable, there would structurally be no races between readers.
* If mutable, concurrent accesses could be data-races. Readers must synchronize
  their accesses to the shared object, such as with a shared mutex owned by the
  object and protecting its internal state.

A `hazptr_holder` extends the lifetime of a shared object from the moment the
shared object pointer is returned by a call to member `protect` or gotten from a
successful call to member `try_protect`, until the moment protection is reset
with a call to member `reset_protection` or destruction of the `hazptr_holder`.

The `hazptr_holder` is not thread-safe. Its implementation does synchronize
internally with other `hazptr_holder`s and with the `hazptr_domain`, but that
does not allow concurrent use of a single `hazptr_holder` instance. In a similar
way, a single instance of `std::shared_ptr` is also not thread-safe, even though
multiple instances may internally synchronize between each other.

If constructed with the default domain, it is thread-sensitive: it must only be
used during its lifetime in the specific thread in which it was constructed. The
default domain is a global singleton which lives for the entire process and, as
optimization, hazard pointers constructed with the default domain rely on state
which is thread-local. Other domains can provide some measure of isolation, but
do not have this guaranteed lifetime or optimization. The benefits of using the
default domain are: (1) global lifetime and (2) faster `make_hazptr_holder`. The
benefits of using the non-default domain are: (1) thread-insensitivity and lower
cost of reclamation.

A `hazptr_holder` synchronizes readers with the deferred deletion arising from
retirement.
* Explicit deletion is typically discouraged. Retirement is typically
  recommended.
* Explicit deletion concurrent with readers is undefined behavior and forbidden.
  Hazard Pointers cannot protect readers from concurrent deletion. This is why
  Hazard Pointers offers retirement as the way to mark shared objects for future
  deletion.
* Each object must be retired or deleted exactly once, and the caller must
  arrange for this exactly-once retirement or deletion.
* Multiple retirements or deletions of the same object, whether serialized or
  concurrent, is undefined behavior and forbidden.

Technically, a `hazptr_holder` synchronizes readers with however the protected
object might implement its own reclamation. Reclamation as deletion is the most
common form of reclamation and is the default, but, for example, an object can
reclaim itself by reinsertion into an object-pool. Regardless, reclamation of an
object is scheduled by retirement of the object, and is deferred until all live
`hazptr_holder`s which protect the lifetime of the object reset that protection.

Writers must typically mutually-exclude themselves somehow.
* Structurally, by having only a single writer thread.
* Pessimistically, with an exclusive mutex.
* Optimistically, with an atomic and the use of atomic-exchange.

The writer must only retire an object after having removed all pointers to it
from all atomic variables which might be the object of hazptr-protection, such
as with the `store`, `exchange`, or `compare_exchange` patterns noted above.

# For More Info

* [Linux Kernel Review Checklist for RCU Patches](https://www.kernel.org/doc/Documentation/RCU/checklist.rst)
  has a ton of useful information on RCU. The concrete details are specific to
  usage of RCU within the Linux Kernel, but much of the document generalizes.
* [Linux Kernel RCU Handbook](https://docs.kernel.org/RCU/index.html).
