# Pushmi
## from None to Many

This library is counterpart to [P1055 - *A Modest Executor Proposal*](http://wg21.link/p1055r0).

*pushmi* is a header-only library that uses git submodules for dependencies (`git clone --recursive`), uses CMake to build, requires compliant C++14 compiler to build and has dependencies on meta and catch2 and some other libraries for testing and examples.

*pushmi* is an implementation for prototyping how Futures, Executors can be defined with shared Concepts. These Concepts can be implemented over and over again to solve different problems and make different tradeoffs. User implementations of the Concepts are first-class citizens due to the attention to composition. Composition also enables each implementation of the Concepts to focus on one concern and then be composed to build more complex solutions.

## Callbacks

*Callbacks* are very familiar though they take many forms. It is precisely the multiplicity of forms that make Callbacks difficult to compose.

A minimal callback might be passed some state. The state might include an error or might only be a result code. Maybe this is delivered as one parameter, or as many parameters. Maybe the Callback is called once, or many or even zero times.

*Promises* provide a stable contract for Callbacks of a single result or error.

## `std::promise<>`

The interface for `std::promise<void>` is fairly straightforward.

```cpp
struct promise<void> {
  void set_value();
  void set_exception(std::exception_ptr);

  future<void> get_future();
};
```

usage is also simple, but a bit convoluted (the promise produces the future, has the result set_ function called, and only then is future::get called to get the result).

```cpp
std::promise<void> p;
auto f = p.get_future();
p.set_value();
// or
// p.set_exception(std::exception_ptr{});
f.get();
```

it is this convolution that creates the race between the producer and consumer that requires expensive internal state to resolve.

## `none`

The `none` type in the library provides simple ways to construct new implementations of the None concept.

construct a sink type that accepts any error type (and aborts on error)

```cpp
none<> n;
```

construct new type using one or more lambdas, or with designated initializers, use multiple lambdas to build overload sets

```cpp
// provide done
auto n0 = none{on_done{[](){}}};

// these are quite dangerous as they suppress errors

// provide error
auto n1 = none{[](std::exception_ptr){}, [](){}};
auto n2 = none{on_error{[](std::exception_ptr){}, [](auto){}}};
auto n3 = none{[](std::exception_ptr){}};

```

construct a new type with shared state across the lambdas. very useful for building a filter on top of an existing none. The state must be a None, but can be a super-set with additional state for this filter.

```cpp
auto n0 = none{none{}};

auto n1 = none{none{}, on_done{
    [](none& out, std::exception_ptr ep){out | set_done();}}};

// these are quite dangerous as they suppress errors
auto n2 = none{none{},
  [](none<>& out, std::exception_ptr ep){out | set_done();},
  [](none<>&){out | set_done();}};
auto n3 = none{none{}, on_error{
  [](none<>& out, std::exception_ptr ep){out | set_done();},
  [](none<>& out, auto e){out | set_done();}}};
```

construct a type-erased type for a particular E (which could be a std::variant of supported types). I have a plan to provide operators to collapse values and errors to variant or tuple and then expand from variant or tuple back to their constituent values/errors.

```cpp
auto n0 = any_none{none{}};
auto n1 = any_none<std::exception_ptr>{none{}};
```

## `single`

The `single` type in the library provides simple ways to construct new implementations of the Single concept.

construct a sink type that accepts any value or error type (and aborts on error)

```cpp
single<> s;
```

construct new type using one or more lambdas, or with designated initializers, use multiple lambdas to build overload sets

```cpp
// provide done
auto s0 = single{on_done{[](){}}};

// provide value
auto s1 = single{[](auto v){}};
auto s2 = single{on_value{[](int){}, [](auto v){}}};

// these are quite dangerous as they suppress errors

// provide error
auto s3 = single{[](auto v){}, [](std::exception_ptr){}, [](){}};
auto s4 = single{on_error{[](std::exception_ptr){}}, on_done{[](){}}};
auto s5 = single{on_error{[](std::exception_ptr){}, [](auto){}}};
auto s6 = single{on_error{[](std::exception_ptr){}}};

```

construct a new type with shared state across the lambdas. very useful for building a filter on top of an existing single. The state must be a Single, but can be a super-set with additional state for this filter.

```cpp
auto s0 = single{single{}};

auto s1 = single{single{}, on_done{
    [](single<>& out, std::exception_ptr ep){out | set_done();}}};

auto s2 = single{single{},
  [](single<>& out, auto v){out | set_value(v);};
auto s3 = single{single{}, on_value{
  [](single<>& out, int v){out | set_value(v);},
  [](single<>& out, auto v){out | set_value(v);}}};

// these are quite dangerous as they suppress errors
auto s4 = single{single{},
  [](){}
  [](single<>& out, std::exception_ptr ep){out | set_done();},
  [](single<>&){out | set_done();}};
auto s5 = single{single{}, on_error{
  [](single<>& out, std::exception_ptr ep){out | set_done();},
  [](single<>& out, auto e){out | set_done();}}};

```

construct a type-erased type for a particular T & E (each of which could be a std::variant of supported types). I have a plan to provide operators to collapse values and errors to variant or tuple and then expand from variant or tuple back to their constituent values/errors.

```cpp
auto s0 = single<int>{single{}};
auto s1 = single<int, std::exception_ptr>{single{}};
```

## `deferred`

The `deferred` type in the library provides simple ways to construct new implementations of the NoneSender concept.

construct a producer of nothing, aka `never()`

```cpp
deferred<> d;
```

construct new type using one or more lambdas, or with designated initializers, use multiple lambdas to build overload sets

```cpp
auto d0 = deferred{on_submit{[](auto out){}}};
auto d1 = deferred{[](auto out){}};
auto d2 = deferred{on_submit{[](none<> out){}, [](auto out){}}};

```

construct a new type with shared state across the lambdas. very useful for building a filter on top of an existing deferred. The state must be a NoneSender, but can be a super-set with additional state for this filter.

```cpp
auto d0 = deferred{deferred{}};

auto d1 = deferred{deferred{}, on_submit{
    [](deferred<>& in, auto out){in | submit(out);}}};

auto d2 = deferred{deferred{},
    [](deferred<>& in, auto out){in | submit(out);}};

```

construct a type-erased type for a particular E (which could be a std::variant of supported types). I have a plan to provide operators to collapse values and errors to variant or tuple and then expand from variant or tuple back to their constituent values/errors.

```cpp
auto d0 = deferred<>{deferred{}};
auto d1 = deferred<std::exception_ptr>{deferred{}};
```

## `single_deferred`

The `single_deferred` type in the library provides simple ways to construct new implementations of the SingleSender concept.

construct a producer of nothing, aka `never()`

```cpp
single_deferred<> sd;
```

construct new type using one or more lambdas, or with designated initializers, use multiple lambdas to build overload sets

```cpp
auto sd0 = single_deferred{on_submit{[](auto out){}}};
auto sd1 = single_deferred{[](auto out){}};
auto sd2 = single_deferred{on_submit{[](single<> out){}, [](auto out){}}};

```

construct a new type with shared state across the lambdas. very useful for building a filter on top of an existing single_deferred. The state must be a SingleSender, but can be a super-set with additional state for this filter.

```cpp
auto sd0 = single_deferred{single_deferred{}};

auto sd1 = single_deferred{single_deferred{}, on_submit{
    [](single_deferred<>& in, auto out){in | submit(out);}}};

auto sd2 = single_deferred{single_deferred{},
    [](single_deferred<>& in, auto out){in | submit(out);}};

```

construct a type-erased type for a particular T & E (which could be a std::variant of supported types). I have a plan to provide operators to collapse values and errors to variant or tuple and then expand from variant or tuple back to their constituent values/errors.

```cpp
auto sd0 = single_deferred<int>{single_deferred{}};
auto sd1 = single_deferred<int, std::exception_ptr>{single_deferred{}};
```

## `time_single_deferred`

The `time_single_deferred` type in the library provides simple ways to construct new implementations of the TimeSingleSender concept.

construct a producer of nothing, aka `never()`

```cpp
time_single_deferred<> tsd;
```

construct new type using one or more lambdas, or with designated initializers, use multiple lambdas to build overload sets

```cpp
auto tsd0 = time_single_deferred{on_submit{[](auto at, auto out){}}};
auto tsd1 = time_single_deferred{[](auto at, auto out){}};
auto tsd2 = time_single_deferred{on_submit{[](auto at, single<> out){}, [](auto at, auto out){}}};

```

construct a new type with shared state across the lambdas. very useful for building a filter on top of an existing time_single_deferred. The state must be a SingleSender, but can be a super-set with additional state for this filter.

```cpp
auto tsd0 = time_single_deferred{single_deferred{}};

auto tsd1 = time_single_deferred{single_deferred{}, on_submit{
    [](time_single_deferred<>& in, auto at, auto out){in | submit(at, out);}}};

auto tsd2 = time_single_deferred{single_deferred{},
    [](time_single_deferred<>& in, auto at, auto out){in | submit(at, out);}};

```

construct a type-erased type for a particular T & E (which could be a std::variant of supported types). I have a plan to provide operators to collapse values and errors to variant or tuple and then expand from variant or tuple back to their constituent values/errors.

```cpp
auto tsd0 = time_single_deferred<int>{time_single_deferred{}};
auto tsd1 = time_single_deferred<int, std::exception_ptr>{time_single_deferred{}};
```

## put it all together with some algorithms

### Executor

```cpp
auto nt = new_thread();
nt | blocking_submit([](auto nt){
  nt |
    transform([](auto nt){ return 42; }) | submit_after(20ms, [](int){}) |
    transform([](auto nt){ return "42"s; }) | submit_after(40ms, [](std::string){});
});
```

### Single

```cpp
auto fortyTwo = just(42) |
  transform([](auto v){ return std::to_string(v); }) |
  on(new_thread) |
  via(new_thread) |
  get<std::string>();

just(42) |
    transform([](auto v){ return std::to_string(v); }) |
    on(new_thread) |
    via(new_thread) |
    blocking_submit([](std::string>){});
```
