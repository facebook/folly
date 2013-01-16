/*
 * Copyright 2013 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

namespace folly { namespace gen {

/**
 * IsCompatibleSignature - Trait type for testing whether a given Functor
 * matches an expected signature.
 *
 * Usage:
 *   IsCompatibleSignature<FunctorType, bool(int, float)>::value
 */
template<class Candidate, class Expected>
class IsCompatibleSignature {
  static constexpr bool value = false;
};

template<class Candidate,
         class ExpectedReturn,
         class... ArgTypes>
class IsCompatibleSignature<Candidate, ExpectedReturn(ArgTypes...)> {
  template<class F,
           class ActualReturn =
             decltype(std::declval<F>()(std::declval<ArgTypes>()...)),
           bool good = std::is_same<ExpectedReturn, ActualReturn>::value>
  static constexpr bool testArgs(int* p) {
    return good;
  }

  template<class F>
  static constexpr bool testArgs(...) {
    return false;
  }
public:
  static constexpr bool value = testArgs<Candidate>(nullptr);
};

/**
 * ArgumentReference - For determining ideal argument type to receive a value.
 */
template<class T>
struct ArgumentReference :
  public std::conditional<std::is_reference<T>::value,
                          T, // T& -> T&, T&& -> T&&, const T& -> const T&
                          typename std::conditional<
                            std::is_const<T>::value,
                            T&, // const int -> const int&
                            T&& // int -> int&&
                          >::type> {};

/**
 * FBounded - Helper type for the curiously recurring template pattern, used
 * heavily here to enable inlining and obviate virtual functions
 */
template<class Self>
struct FBounded {
  const Self& self() const {
    return *static_cast<const Self*>(this);
  }

  Self& self() {
    return *static_cast<Self*>(this);
  }
};

/**
 * Operator - Core abstraction of an operation which may be applied to a
 * generator. All operators implement a method compose(), which takes a
 * generator and produces an output generator.
 */
template<class Self>
class Operator : public FBounded<Self> {
 public:
  /**
   * compose() - Must be implemented by child class to compose a new Generator
   * out of a given generator. This function left intentionally unimplemented.
   */
  template<class Source,
           class Value,
           class ResultGen = void>
  ResultGen compose(const GenImpl<Value, Source>& source) const;

 protected:
  Operator() = default;
  Operator(const Operator&) = default;
  Operator(Operator&&) = default;
};

/**
 * operator|() - For composing two operators without binding it to a
 * particular generator.
 */
template<class Left,
         class Right,
         class Composed = detail::Composed<Left, Right>>
Composed operator|(const Operator<Left>& left,
                   const Operator<Right>& right) {
  return Composed(left.self(), right.self());
}

template<class Left,
         class Right,
         class Composed = detail::Composed<Left, Right>>
Composed operator|(const Operator<Left>& left,
                   Operator<Right>&& right) {
  return Composed(left.self(), std::move(right.self()));
}

template<class Left,
         class Right,
         class Composed = detail::Composed<Left, Right>>
Composed operator|(Operator<Left>&& left,
                   const Operator<Right>& right) {
  return Composed(std::move(left.self()), right.self());
}

template<class Left,
         class Right,
         class Composed = detail::Composed<Left, Right>>
Composed operator|(Operator<Left>&& left,
                   Operator<Right>&& right) {
  return Composed(std::move(left.self()), std::move(right.self()));
}

template<class Value,
         class Source,
         class Yield = detail::Yield<Value, Source>>
Yield operator+(const detail::GeneratorBuilder<Value>&,
                Source&& source) {
  return Yield(std::forward<Source>(source));
}

/**
 * GenImpl - Core abstraction of a generator, an object which produces values by
 * passing them to a given handler lambda. All generator implementations must
 * implement apply(). foreach() may also be implemented to special case the
 * condition where the entire sequence is consumed.
 */
template<class Value,
         class Self>
class GenImpl : public FBounded<Self> {
 protected:
  // To prevent slicing
  GenImpl() = default;
  GenImpl(const GenImpl&) = default;
  GenImpl(GenImpl&&) = default;

 public:
  typedef Value ValueType;
  typedef typename std::decay<Value>::type StorageType;

  /**
   * apply() - Send all values produced by this generator to given
   * handler until it returns false. Returns true if the false iff the handler
   * returned false.
   */
  template<class Handler>
  bool apply(Handler&& handler) const;

  /**
   * foreach() - Send all values produced by this generator to given lambda.
   */
  template<class Body>
  void foreach(Body&& body) const {
    this->self().apply([&](Value value) -> bool {
        body(std::forward<Value>(value));
        return true;
      });
  }
};

template<class LeftValue,
         class Left,
         class RightValue,
         class Right,
         class Chain = detail::Chain<LeftValue, Left, Right>>
Chain operator+(const GenImpl<LeftValue, Left>& left,
                const GenImpl<RightValue, Right>& right) {
  static_assert(
    std::is_same<LeftValue, RightValue>::value,
    "Generators may ony be combined if Values are the exact same type.");
  return Chain(left.self(), right.self());
}

template<class LeftValue,
         class Left,
         class RightValue,
         class Right,
         class Chain = detail::Chain<LeftValue, Left, Right>>
Chain operator+(const GenImpl<LeftValue, Left>& left,
                GenImpl<RightValue, Right>&& right) {
  static_assert(
    std::is_same<LeftValue, RightValue>::value,
    "Generators may ony be combined if Values are the exact same type.");
  return Chain(left.self(), std::move(right.self()));
}

template<class LeftValue,
         class Left,
         class RightValue,
         class Right,
         class Chain = detail::Chain<LeftValue, Left, Right>>
Chain operator+(GenImpl<LeftValue, Left>&& left,
                const GenImpl<RightValue, Right>& right) {
  static_assert(
    std::is_same<LeftValue, RightValue>::value,
    "Generators may ony be combined if Values are the exact same type.");
  return Chain(std::move(left.self()), right.self());
}

template<class LeftValue,
         class Left,
         class RightValue,
         class Right,
         class Chain = detail::Chain<LeftValue, Left, Right>>
Chain operator+(GenImpl<LeftValue, Left>&& left,
                GenImpl<RightValue, Right>&& right) {
  static_assert(
    std::is_same<LeftValue, RightValue>::value,
    "Generators may ony be combined if Values are the exact same type.");
  return Chain(std::move(left.self()), std::move(right.self()));
}

/**
 * operator|() which enables foreach-like usage:
 *   gen | [](Value v) -> void {...};
 */
template<class Value,
         class Gen,
         class Handler>
typename std::enable_if<
  IsCompatibleSignature<Handler, void(Value)>::value>::type
operator|(const GenImpl<Value, Gen>& gen, Handler&& handler) {
  gen.self().foreach(std::forward<Handler>(handler));
}

/**
 * operator|() which enables foreach-like usage with 'break' support:
 *   gen | [](Value v) -> bool { return shouldContinue(); };
 */
template<class Value,
         class Gen,
         class Handler>
typename std::enable_if<
  IsCompatibleSignature<Handler, bool(Value)>::value, bool>::type
operator|(const GenImpl<Value, Gen>& gen, Handler&& handler) {
  return gen.self().apply(std::forward<Handler>(handler));
}

/**
 * operator|() for composing generators with operators, similar to boosts' range
 * adaptors:
 *   gen | map(square) | sum
 */
template<class Value,
         class Gen,
         class Op>
auto operator|(const GenImpl<Value, Gen>& gen, const Operator<Op>& op) ->
decltype(op.self().compose(gen.self())) {
  return op.self().compose(gen.self());
}

template<class Value,
         class Gen,
         class Op>
auto operator|(GenImpl<Value, Gen>&& gen, const Operator<Op>& op) ->
decltype(op.self().compose(std::move(gen.self()))) {
  return op.self().compose(std::move(gen.self()));
}

namespace detail {

/*
 * ReferencedSource - Generate values from an STL-like container using
 * iterators from .begin() until .end(). Value type defaults to the type of
 * *container->begin(). For std::vector<int>, this would be int&. Note that the
 * value here is a reference, so the values in the vector will be passed by
 * reference to downstream operators.
 *
 * This type is primarily used through the 'from' helper method, like:
 *
 *   string& longestName = from(names)
 *                       | maxBy([](string& s) { return s.size() });
 */
template<class Container,
         class Value>
class ReferencedSource :
    public GenImpl<Value, ReferencedSource<Container, Value>> {
  Container* container_;
public:
  explicit ReferencedSource(Container* container)
    : container_(container) {}

  template<class Body>
  void foreach(Body&& body) const {
    for (auto& value : *container_) {
      body(std::forward<Value>(value));
    }
  }

  template<class Handler>
  bool apply(Handler&& handler) const {
    for (auto& value : *container_) {
      if (!handler(std::forward<Value>(value))) {
        return false;
      }
    }
    return true;
  }
};

/**
 * CopiedSource - For producing values from eagerly from a sequence of values
 * whose storage is owned by this class. Useful for preparing a generator for
 * use after a source collection will no longer be available, or for when the
 * values are specified literally with an initializer list.
 *
 * This type is primarily used through the 'fromCopy' function, like:
 *
 *   auto sourceCopy = fromCopy(makeAVector());
 *   auto sum = sourceCopy | sum;
 *   auto max = sourceCopy | max;
 *
 * Though it is also used for the initializer_list specialization of from().
 */
template<class StorageType,
         class Container>
class CopiedSource :
  public GenImpl<const StorageType&,
                 CopiedSource<StorageType, Container>> {
  static_assert(
    !std::is_reference<StorageType>::value, "StorageType must be decayed");
 public:
  // Generator objects are often copied during normal construction as they are
  // encapsulated by downstream generators. It would be bad if this caused
  // a copy of the entire container each time, and since we're only exposing a
  // const reference to the value, it's safe to share it between multiple
  // generators.
  static_assert(
    !std::is_reference<Container>::value,
    "Can't copy into a reference");
  std::shared_ptr<const Container> copy_;
public:
  typedef Container ContainerType;

  template<class SourceContainer>
  explicit CopiedSource(const SourceContainer& container)
    : copy_(new Container(begin(container), end(container))) {}

  explicit CopiedSource(Container&& container) :
    copy_(new Container(std::move(container))) {}

  // To enable re-use of cached results.
  CopiedSource(const CopiedSource<StorageType, Container>& source)
    : copy_(source.copy_) {}

  template<class Body>
  void foreach(Body&& body) const {
    for (const auto& value : *copy_) {
      body(value);
    }
  }

  template<class Handler>
  bool apply(Handler&& handler) const {
    // The collection may be reused by others, we can't allow it to be changed.
    for (const auto& value : *copy_) {
      if (!handler(value)) {
        return false;
      }
    }
    return true;
  }
};

/**
 * Sequence - For generating values from beginning value, incremented along the
 * way with the ++ and += operators. Iteration may continue indefinitely by
 * setting the 'endless' template parameter to true. If set to false, iteration
 * will stop when value reaches 'end', either inclusively or exclusively,
 * depending on the template parameter 'endInclusive'. Value type specified
 * explicitly.
 *
 * This type is primarily used through the 'seq' and 'range' function, like:
 *
 *   int total = seq(1, 10) | sum;
 *   auto indexes = range(0, 10);
 */
template<class Value,
         bool endless,
         bool endInclusive>
class Sequence : public GenImpl<const Value&,
                                Sequence<Value, endless, endInclusive>> {
  static_assert(!std::is_reference<Value>::value &&
                !std::is_const<Value>::value, "Value mustn't be const or ref.");
  Value bounds_[endless ? 1 : 2];
public:
  explicit Sequence(const Value& begin)
      : bounds_{begin} {
    static_assert(endless, "Must supply 'end'");
  }

  explicit Sequence(const Value& begin, const Value& end)
    : bounds_{begin, end} {}

  template<class Handler>
  bool apply(Handler&& handler) const {
    Value value = bounds_[0];
    for (;endless || value < bounds_[1]; ++value) {
      const Value& arg = value;
      if (!handler(arg)) {
        return false;
      }
    }
    if (endInclusive && value == bounds_[1]) {
      const Value& arg = value;
      if (!handler(arg)) {
        return false;
      }
    }
    return true;
  }

  template<class Body>
  void foreach(Body&& body) const {
    Value value = bounds_[0];
    for (;endless || value < bounds_[1]; ++value) {
      const Value& arg = value;
      body(arg);
    }
    if (endInclusive && value == bounds_[1]) {
      const Value& arg = value;
      body(arg);
    }
  }
};

/**
 * Chain - For concatenating the values produced by two Generators.
 *
 * This type is primarily used through using '+' to combine generators, like:
 *
 *   auto nums = seq(1, 10) + seq(20, 30);
 *   int total = nums | sum;
 */
template<class Value, class First, class Second>
class Chain : public GenImpl<Value,
                             Chain<Value, First, Second>> {
  First first_;
  Second second_;
public:
  explicit Chain(First first, Second second)
      : first_(std::move(first))
      , second_(std::move(second)) {}

  template<class Handler>
  bool apply(Handler&& handler) const {
    return first_.apply(std::forward<Handler>(handler))
        && second_.apply(std::forward<Handler>(handler));
  }

  template<class Body>
  void foreach(Body&& body) const {
    first_.foreach(std::forward<Body>(body));
    second_.foreach(std::forward<Body>(body));
  }
};

/**
 * Yield - For producing values from a user-defined generator by way of a
 * 'yield' function.
 **/
template<class Value, class Source>
class Yield : public GenImpl<Value, Yield<Value, Source>> {
  Source source_;
 public:
  explicit Yield(Source source)
    : source_(std::move(source)) {
  }

  template<class Handler>
  bool apply(Handler&& handler) const {
    struct Break {};
    auto body = [&](Value value) {
      if (!handler(std::forward<Value>(value))) {
        throw Break();
      }
    };
    try {
      source_(body);
      return true;
    } catch (Break&) {
      return false;
    }
  }

  template<class Body>
  void foreach(Body&& body) const {
    source_(std::forward<Body>(body));
  }
};


/*
 * Operators
 */

/**
 * Map - For producing a sequence of values by passing each value from a source
 * collection through a predicate.
 *
 * This type is usually used through the 'map' or 'mapped' helper function:
 *
 *   auto squares = seq(1, 10) | map(square) | asVector;
 */
template<class Predicate>
class Map : public Operator<Map<Predicate>> {
  Predicate predicate_;
 public:
  explicit Map(const Predicate& predicate = Predicate())
    : predicate_(predicate)
  { }

  template<class Value,
           class Source,
           class Result = typename ArgumentReference<
                            typename std::result_of<Predicate(Value)>::type
                          >::type>
  class Generator :
      public GenImpl<Result, Generator<Value, Source, Result>> {
    Source source_;
    Predicate pred_;
  public:
    explicit Generator(Source source, const Predicate& pred)
      : source_(std::move(source)), pred_(pred) {}

    template<class Body>
    void foreach(Body&& body) const {
      source_.foreach([&](Value value) {
        body(pred_(std::forward<Value>(value)));
      });
    }

    template<class Handler>
    bool apply(Handler&& handler) const {
      return source_.apply([&](Value value) {
        return handler(pred_(std::forward<Value>(value)));
      });
    }
  };

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), predicate_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), predicate_);
  }
};


/**
 * Filter - For filtering values from a source sequence by a predicate.
 *
 * This type is usually used through the 'filter' helper function, like:
 *
 *   auto nonEmpty = from(strings)
 *                 | filter([](const string& str) -> bool {
 *                     return !str.empty();
 *                   });
 */
template<class Predicate>
class Filter : public Operator<Filter<Predicate>> {
  Predicate predicate_;
 public:
  explicit Filter(const Predicate& predicate)
    : predicate_(predicate)
  { }

  template<class Value,
           class Source>
  class Generator : public GenImpl<Value, Generator<Value, Source>> {
    Source source_;
    Predicate pred_;
  public:
    explicit Generator(Source source, const Predicate& pred)
      : source_(std::move(source)), pred_(pred) {}

    template<class Body>
    void foreach(Body&& body) const {
      source_.foreach([&](Value value) {
        if (pred_(std::forward<Value>(value))) {
          body(std::forward<Value>(value));
        }
      });
    }

    template<class Handler>
    bool apply(Handler&& handler) const {
      return source_.apply([&](Value value) -> bool {
        if (pred_(std::forward<Value>(value))) {
          return handler(std::forward<Value>(value));
        }
        return true;
      });
    }
  };

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), predicate_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), predicate_);
  }
};

/**
 * Until - For producing values from a source until a predicate is satisfied.
 *
 * This type is usually used through the 'until' helper function, like:
 *
 *   auto best = from(sortedItems)
 *             | until([](Item& item) { return item.score > 100; })
 *             | asVector;
 */
template<class Predicate>
class Until : public Operator<Until<Predicate>> {
  Predicate predicate_;
 public:
  explicit Until(const Predicate& predicate)
    : predicate_(predicate)
  { }

  template<class Value,
           class Source,
           class Result = typename std::result_of<Predicate(Value)>::type>
  class Generator :
      public GenImpl<Result, Generator<Value, Source, Result>> {
    Source source_;
    Predicate pred_;
   public:
    explicit Generator(Source source, const Predicate& pred)
      : source_(std::move(source)), pred_(pred) {}

    template<class Handler>
    bool apply(Handler&& handler) const {
      return source_.apply([&](Value value) -> bool {
        return !pred_(std::forward<Value>(value))
            && handler(std::forward<Value>(value));
      });
    }
  };

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), predicate_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), predicate_);
  }
};

/**
 * Take - For producing up to N values from a source.
 *
 * This type is usually used through the 'take' helper function, like:
 *
 *   auto best = from(docs)
 *             | orderByDescending(scoreDoc)
 *             | take(10);
 */
class Take : public Operator<Take> {
  size_t count_;
 public:
  explicit Take(size_t count)
    : count_(count) {}

  template<class Value,
           class Source>
  class Generator :
      public GenImpl<Value, Generator<Value, Source>> {
    Source source_;
    size_t count_;
  public:
    explicit Generator(Source source, size_t count)
      : source_(std::move(source)) , count_(count) {}

    template<class Handler>
    bool apply(Handler&& handler) const {
      if (count_ == 0) { return false; }
      size_t n = count_;
      return source_.apply([&](Value value) -> bool {
          if (!handler(std::forward<Value>(value))) {
            return false;
          }
          return --n;
        });
    }
  };

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), count_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), count_);
  }
};

/**
 * Skip - For skipping N items from the beginning of a source generator.
 *
 * This type is usually used through the 'skip' helper function, like:
 *
 *   auto page = from(results)
 *             | skip(pageSize * startPage)
 *             | take(10);
 */
class Skip : public Operator<Skip> {
  size_t count_;
 public:
  explicit Skip(size_t count)
    : count_(count) {}

  template<class Value,
           class Source>
  class Generator :
      public GenImpl<Value, Generator<Value, Source>> {
    Source source_;
    size_t count_;
   public:
    explicit Generator(Source source, size_t count)
      : source_(std::move(source)) , count_(count) {}

    template<class Body>
    void foreach(Body&& body) const {
      if (count_ == 0) {
        source_.foreach(body);
        return;
      }
      size_t n = 0;
      source_.foreach([&](Value value) {
          if (n < count_) {
            ++n;
          } else {
            body(std::forward<Value>(value));
          }
        });
    }

    template<class Handler>
    bool apply(Handler&& handler) const {
      if (count_ == 0) {
        return source_.apply(handler);
      }
      size_t n = 0;
      return source_.apply([&](Value value) -> bool {
          if (n < count_) {
            ++n;
            return true;
          }
          return handler(std::forward<Value>(value));
        });
    }
  };

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), count_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), count_);
  }
};

/**
 * Order - For ordering a sequence of values from a source by key.
 * The key is extracted by the given selector functor, and this key is then
 * compared using the specified comparator.
 *
 * This type is usually used through the 'order' helper function, like:
 *
 *   auto closest = from(places)
 *                | orderBy([](Place& p) {
 *                    return -distance(p.location, here);
 *                  })
 *                | take(10);
 */
template<class Selector, class Comparer>
class Order : public Operator<Order<Selector, Comparer>> {
  Selector selector_;
  Comparer comparer_;
 public:
  explicit Order(const Selector& selector = Selector(),
                 const Comparer& comparer = Comparer())
    : selector_(selector) , comparer_(comparer) {}

  template<class Value,
           class Source,
           class StorageType = typename std::decay<Value>::type,
           class Result = typename std::result_of<Selector(Value)>::type>
  class Generator :
    public GenImpl<StorageType&&,
                   Generator<Value, Source, StorageType, Result>> {
    Source source_;
    Selector selector_;
    Comparer comparer_;

    typedef std::vector<StorageType> VectorType;

    VectorType asVector() const {
      auto comparer = [&](const StorageType& a, const StorageType& b) {
        return comparer_(selector_(a), selector_(b));
      };
      auto vals = source_ | as<VectorType>();
      std::sort(vals.begin(), vals.end(), comparer);
      return std::move(vals);
    }
   public:
    Generator(Source source,
              const Selector& selector,
              const Comparer& comparer)
      : source_(std::move(source)),
        selector_(selector),
        comparer_(comparer) {}

    VectorType operator|(const Collect<VectorType>&) const {
      return asVector();
    }

    VectorType operator|(const CollectTemplate<std::vector>&) const {
      return asVector();
    }

    template<class Body>
    void foreach(Body&& body) const {
      for (auto& value : asVector()) {
        body(std::move(value));
      }
    }

    template<class Handler>
    bool apply(Handler&& handler) const {
      auto comparer = [&](const StorageType& a, const StorageType& b) {
        // swapped for minHeap
        return comparer_(selector_(b), selector_(a));
      };
      auto heap = source_ | as<VectorType>();
      std::make_heap(heap.begin(), heap.end(), comparer);
      while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), comparer);
        if (!handler(std::move(heap.back()))) {
          return false;
        }
        heap.pop_back();
      }
      return true;
    }
  };

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), selector_, comparer_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), selector_, comparer_);
  }
};

/**
 * Composed - For building up a pipeline of operations to perform, absent any
 * particular source generator. Useful for building up custom pipelines.
 *
 * This type is usually used by just piping two operators together:
 *
 * auto valuesOf = filter([](Optional<int>& o) { return o.hasValue(); })
 *               | map([](Optional<int>& o) -> int& { return o.value(); });
 *
 *  auto valuesIncluded = from(optionals) | valuesOf | as<vector>();
 */
template<class First,
         class Second>
class Composed : public Operator<Composed<First, Second>> {
  First first_;
  Second second_;
 public:
  Composed() {}
  Composed(First first, Second second)
    : first_(std::move(first))
    , second_(std::move(second)) {}

  template<class Source,
           class Value,
           class FirstRet = decltype(std::declval<First>()
                                     .compose(std::declval<Source>())),
           class SecondRet = decltype(std::declval<Second>()
                                      .compose(std::declval<FirstRet>()))>
  SecondRet compose(const GenImpl<Value, Source>& source) const {
    return second_.compose(first_.compose(source.self()));
  }

  template<class Source,
           class Value,
           class FirstRet = decltype(std::declval<First>()
                                     .compose(std::declval<Source>())),
           class SecondRet = decltype(std::declval<Second>()
                                      .compose(std::declval<FirstRet>()))>
  SecondRet compose(GenImpl<Value, Source>&& source) const {
    return second_.compose(first_.compose(std::move(source.self())));
  }
};

/*
 * Sinks
 */

/**
 * FoldLeft - Left-associative functional fold. For producing an aggregate value
 * from a seed and a folder function. Useful for custom aggregators on a
 * sequence.
 *
 * This type is primarily used through the 'foldl' helper method, like:
 *
 *   double movingAverage = from(values)
 *                        | foldl(0.0, [](double avg, double sample) {
 *                            return sample * 0.1 + avg * 0.9;
 *                          });
 */
template<class Seed,
         class Fold>
class FoldLeft : public Operator<FoldLeft<Seed, Fold>> {
  Seed seed_;
  Fold fold_;
 public:
  FoldLeft(const Seed& seed, const Fold& fold)
    : seed_(seed)
    , fold_(fold)
  {}

  template<class Source,
           class Value>
  Seed compose(const GenImpl<Value, Source>& source) const {
    Seed accum = seed_;
    source | [&](Value v) {
      accum = fold_(std::move(accum), std::forward<Value>(v));
    };
    return accum;
  }
};

/**
 * First - For finding the first value in a sequence.
 *
 * This type is primarily used through the 'first' static value, like:
 *
 *   int firstThreeDigitPrime = seq(100) | filter(isPrime) | first;
 */
class First : public Operator<First> {
 public:
  First() { }

  template<class Source,
           class Value,
           class StorageType = typename std::decay<Value>::type>
  StorageType compose(const GenImpl<Value, Source>& source) const {
    Optional<StorageType> accum;
    source | [&](Value v) -> bool {
      accum = std::forward<Value>(v);
      return false;
    };
    if (!accum.hasValue()) {
      throw EmptySequence();
    }
    return std::move(accum.value());
  }
};


/**
 * Any - For determining whether any values are contained in a sequence.
 *
 * This type is primarily used through the 'any' static value, like:
 *
 *   bool any20xPrimes = seq(200, 210) | filter(isPrime) | any;
 */
class Any : public Operator<Any> {
 public:
  Any() { }

  template<class Source,
           class Value>
  bool compose(const GenImpl<Value, Source>& source) const {
    bool any = false;
    source | [&](Value v) -> bool {
      any = true;
      return false;
    };
    return any;
  }
};

/**
 * Reduce - Functional reduce, for recursively combining values from a source
 * using a reducer function until there is only one item left. Useful for
 * combining values when an empty sequence doesn't make sense.
 *
 * This type is primarily used through the 'reduce' helper method, like:
 *
 *   sring longest = from(names)
 *                 | reduce([](string&& best, string& current) {
 *                     return best.size() >= current.size() ? best : current;
 *                   });
 */
template<class Reducer>
class Reduce : public Operator<Reduce<Reducer>> {
  Reducer reducer_;
 public:
  explicit Reduce(const Reducer& reducer)
    : reducer_(reducer)
  {}

  template<class Source,
           class Value,
           class StorageType = typename std::decay<Value>::type>
  StorageType compose(const GenImpl<Value, Source>& source) const {
    Optional<StorageType> accum;
    source | [&](Value v) {
      if (accum.hasValue()) {
        accum = reducer_(std::move(accum.value()), std::forward<Value>(v));
      } else {
        accum = std::forward<Value>(v);
      }
    };
    if (!accum.hasValue()) {
      throw EmptySequence();
    }
    return accum.value();
  }
};

/**
 * Count - for simply counting the items in a collection.
 *
 * This type is usually used through its singleton, 'count':
 *
 *   auto shortPrimes = seq(1, 100) | filter(isPrime) | count;
 */
class Count : public Operator<Count> {
 public:
  Count() { }

  template<class Source,
           class Value>
  size_t compose(const GenImpl<Value, Source>& source) const {
    return foldl(size_t(0),
                 [](size_t accum, Value v) {
                   return accum + 1;
                 }).compose(source);
  }
};

/**
 * Sum - For simply summing up all the values from a source.
 *
 * This type is usually used through its singleton, 'sum':
 *
 *   auto gaussSum = seq(1, 100) | sum;
 */
class Sum : public Operator<Sum> {
 public:
  Sum() { }

  template<class Source,
           class Value,
           class StorageType = typename std::decay<Value>::type>
  StorageType compose(const GenImpl<Value, Source>& source) const {
    return foldl(StorageType(0),
                 [](StorageType&& accum, Value v) {
                   return std::move(accum) + std::forward<Value>(v);
                 }).compose(source);
  }
};

/**
 * Contains - For testing whether a value matching the given value is contained
 * in a sequence.
 *
 * This type should be used through the 'contains' helper method, like:
 *
 *   bool contained = seq(1, 10) | map(square) | contains(49);
 */
template<class Needle>
class Contains : public Operator<Contains<Needle>> {
  Needle needle_;
 public:
  explicit Contains(Needle needle)
    : needle_(std::move(needle))
  {}

  template<class Source,
           class Value,
           class StorageType = typename std::decay<Value>::type>
  bool compose(const GenImpl<Value, Source>& source) const {
    return !(source | [this](Value value) {
        return !(needle_ == std::forward<Value>(value));
      });
  }
};

/**
 * Min - For a value which minimizes a key, where the key is determined by a
 * given selector, and compared by given comparer.
 *
 * This type is usually used through the singletone 'min' or through the helper
 * functions 'minBy' and 'maxBy'.
 *
 *   auto oldest = from(people)
 *               | minBy([](Person& p) {
 *                   return p.dateOfBirth;
 *                 });
 */
template<class Selector,
         class Comparer>
class Min : public Operator<Min<Selector, Comparer>> {
  Selector selector_;
  Comparer comparer_;
 public:
  explicit Min(const Selector& selector = Selector(),
               const Comparer& comparer = Comparer())
    : selector_(selector)
    , comparer_(comparer)
  {}

  template<class Value,
           class Source,
           class StorageType = typename std::decay<Value>::type,
           class Key = typename std::decay<
               typename std::result_of<Selector(Value)>::type
             >::type>
  StorageType compose(const GenImpl<Value, Source>& source) const {
    Optional<StorageType> min;
    Optional<Key> minKey;
    source | [&](Value v) {
      Key key = selector_(std::forward<Value>(v));
      if (!minKey.hasValue() || comparer_(key, minKey.value())) {
        minKey = key;
        min = std::forward<Value>(v);
      }
    };
    if (!min.hasValue()) {
      throw EmptySequence();
    }
    return min.value();
  }
};

/**
 * Append - For collecting values from a source into a given output container
 * by appending.
 *
 * This type is usually used through the helper function 'appendTo', like:
 *
 *   vector<int64_t> ids;
 *   from(results) | map([](Person& p) { return p.id })
 *                 | appendTo(ids);
 */
template<class Collection>
class Append : public Operator<Append<Collection>> {
  Collection* collection_;
 public:
  explicit Append(Collection* collection)
    : collection_(collection)
  {}

  template<class Value,
           class Source>
  Collection& compose(const GenImpl<Value, Source>& source) const {
    source | [&](Value v) {
      collection_->insert(collection_->end(), std::forward<Value>(v));
    };
    return *collection_;
  }
};

/**
 * Collect - For collecting values from a source in a collection of the desired
 * type.
 *
 * This type is usually used through the helper function 'as', like:
 *
 *   std::string upper = from(stringPiece)
 *                     | map(&toupper)
 *                     | as<std::string>();
 */
template<class Collection>
class Collect : public Operator<Collect<Collection>> {
 public:
  Collect() { }

  template<class Value,
           class Source,
           class StorageType = typename std::decay<Value>::type>
  Collection compose(const GenImpl<Value, Source>& source) const {
    Collection collection;
    source | [&](Value v) {
      collection.insert(collection.end(), std::forward<Value>(v));
    };
    return collection;
  }
};


/**
 * CollectTemplate - For collecting values from a source in a collection
 * constructed using the specified template type. Given the type of values
 * produced by the given generator, the collection type will be:
 *   Container<Value, Allocator<Value>>
 *
 * The allocator defaults to std::allocator, so this may be used for the STL
 * containers by simply using operators like 'as<set>', 'as<deque>',
 * 'as<vector>'. 'as', here is the helper method which is the usual means of
 * consturcting this operator.
 *
 * Example:
 *
 *   set<string> uniqueNames = from(names) | as<set>();
 */
template<template<class, class> class Container,
         template<class> class Allocator>
class CollectTemplate : public Operator<CollectTemplate<Container, Allocator>> {
 public:
  CollectTemplate() { }

  template<class Value,
           class Source,
           class StorageType = typename std::decay<Value>::type,
           class Collection = Container<StorageType, Allocator<StorageType>>>
  Collection compose(const GenImpl<Value, Source>& source) const {
    Collection collection;
    source | [&](Value v) {
      collection.insert(collection.end(), std::forward<Value>(v));
    };
    return collection;
  }
};

/**
 * Concat - For flattening generators of generators.
 *
 * This type is usually used through the 'concat' static value, like:
 *
 *   auto edges =
 *       from(nodes)
 *     | map([](Node& x) {
 *           return from(x.neighbors)
 *                | map([&](Node& y) {
 *                    return Edge(x, y);
 *                  });
 *         })
 *     | concat
 *     | as<std::set>();
 */
class Concat : public Operator<Concat> {
 public:
  Concat() { }

  template<class Inner,
           class Source,
           class InnerValue = typename std::decay<Inner>::type::ValueType>
  class Generator :
      public GenImpl<InnerValue, Generator<Inner, Source, InnerValue>> {
    Source source_;
   public:
    explicit Generator(Source source)
      : source_(std::move(source)) {}

    template<class Handler>
    bool apply(Handler&& handler) const {
      return source_.apply([&](Inner inner) -> bool {
          return inner.apply(std::forward<Handler>(handler));
        });
    }

    template<class Body>
    void foreach(Body&& body) const {
      source_.foreach([&](Inner inner) {
          inner.foreach(std::forward<Body>(body));
        });
    }
  };

  template<class Value,
           class Source,
           class Gen = Generator<Value, Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()));
  }

  template<class Value,
           class Source,
           class Gen = Generator<Value, Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self());
  }
};

/**
 * RangeConcat - For flattening generators of iterables.
 *
 * This type is usually used through the 'rconcat' static value, like:
 *
 *   map<int, vector<int>> adjacency;
 *   auto sinks =
 *       from(adjacency)
 *     | get<1>()
 *     | rconcat()
 *     | as<std::set>();
 */
class RangeConcat : public Operator<RangeConcat> {
 public:
  RangeConcat() { }

  template<class Source,
           class Range,
           class InnerValue = typename ValueTypeOfRange<Range>::RefType>
  class Generator
    : public GenImpl<InnerValue, Generator<Source, Range, InnerValue>> {
    Source source_;
   public:
    explicit Generator(Source source)
      : source_(std::move(source)) {}

    template<class Body>
    void foreach(Body&& body) const {
      source_.foreach([&](Range range) {
          for (auto& value : range) {
            body(value);
          }
        });
    }

    template<class Handler>
    bool apply(Handler&& handler) const {
      return source_.apply([&](Range range) -> bool {
          for (auto& value : range) {
            if (!handler(value)) {
              return false;
            }
          }
          return true;
        });
    }
  };

  template<class Value,
           class Source,
           class Gen = Generator<Source, Value>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()));
  }

  template<class Value,
           class Source,
           class Gen = Generator<Source, Value>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self());
  }
};

} //::detail

/**
 * VirtualGen<T> - For wrapping template types in simple polymorphic wrapper.
 **/
template<class Value>
class VirtualGen : public GenImpl<Value, VirtualGen<Value>> {
  class WrapperBase {
   public:
    virtual ~WrapperBase() {}
    virtual bool apply(const std::function<bool(Value)>& handler) const = 0;
    virtual void foreach(const std::function<void(Value)>& body) const = 0;
    virtual std::unique_ptr<const WrapperBase> clone() const = 0;
  };

  template<class Wrapped>
  class WrapperImpl : public WrapperBase {
    Wrapped wrapped_;
   public:
    explicit WrapperImpl(Wrapped wrapped)
     : wrapped_(std::move(wrapped)) {
    }

    virtual bool apply(const std::function<bool(Value)>& handler) const {
      return wrapped_.apply(handler);
    }

    virtual void foreach(const std::function<void(Value)>& body) const {
      wrapped_.foreach(body);
    }

    virtual std::unique_ptr<const WrapperBase> clone() const {
      return std::unique_ptr<const WrapperBase>(new WrapperImpl(wrapped_));
    }
  };

  std::unique_ptr<const WrapperBase> wrapper_;

 public:
  template<class Self>
  /* implicit */ VirtualGen(Self source)
   : wrapper_(new WrapperImpl<Self>(std::move(source)))
  { }

  VirtualGen(VirtualGen&& source)
   : wrapper_(std::move(source.wrapper_))
  { }

  VirtualGen(const VirtualGen& source)
   : wrapper_(source.wrapper_->clone())
  { }

  VirtualGen& operator=(const VirtualGen& source) {
    wrapper_.reset(source.wrapper_->clone());
    return *this;
  }

  VirtualGen& operator=(VirtualGen&& source) {
    wrapper_= std::move(source.wrapper_);
    return *this;
  }

  bool apply(const std::function<bool(Value)>& handler) const {
    return wrapper_->apply(handler);
  }

  void foreach(const std::function<void(Value)>& body) const {
    wrapper_->foreach(body);
  }
};

/**
 * non-template operators, statically defined to avoid the need for anything but
 * the header.
 */
static const detail::Sum sum;

static const detail::Count count;

static const detail::First first;

static const detail::Any any;

static const detail::Min<Identity, Less> min;

static const detail::Min<Identity, Greater> max;

static const detail::Order<Identity> order;

static const detail::Map<Move> move;

static const detail::Concat concat;

static const detail::RangeConcat rconcat;

inline detail::Take take(size_t count) {
  return detail::Take(count);
}

inline detail::Skip skip(size_t count) {
  return detail::Skip(count);
}

}} //folly::gen::detail
