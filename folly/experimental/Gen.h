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

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <random>
#include <vector>
#include <unordered_set>

#include "folly/Range.h"
#include "folly/Optional.h"
#include "folly/Conv.h"

/**
 * Generator-based Sequence Comprehensions in C++, akin to C#'s LINQ
 * @author Tom Jackson <tjackson@fb.com>
 *
 * This library makes it possible to write declarative comprehensions for
 * processing sequences of values efficiently in C++. The operators should be
 * familiar to those with experience in functional programming, and the
 * performance will be virtually identical to the equivalent, boilerplate C++
 * implementations.
 *
 * Generator objects may be created from either an stl-like container (anything
 * supporting begin() and end()), from sequences of values, or from another
 * generator (see below). To create a generator that pulls values from a vector,
 * for example, one could write:
 *
 *   vector<string> names { "Jack", "Jill", "Sara", "Tom" };
 *   auto gen = from(names);
 *
 * Generators are composed by building new generators out of old ones through
 * the use of operators. These are reminicent of shell pipelines, and afford
 * similar composition. Lambda functions are used liberally to describe how to
 * handle individual values:
 *
 *   auto lengths = gen
 *                | mapped([](const fbstring& name) { return name.size(); });
 *
 * Generators are lazy; they don't actually perform any work until they need to.
 * As an example, the 'lengths' generator (above) won't actually invoke the
 * provided lambda until values are needed:
 *
 *   auto lengthVector = lengths | as<std::vector>();
 *   auto totalLength = lengths | sum;
 *
 * 'auto' is useful in here because the actual types of the generators objects
 * are usually complicated and implementation-sensitive.
 *
 * If a simpler type is desired (for returning, as an example), VirtualGen<T>
 * may be used to wrap the generator in a polymorphic wrapper:
 *
 *  VirtualGen<float> powersOfE() {
 *    return seq(1) | mapped(&expf);
 *  }
 *
 * To learn more about this library, including the use of infinite generators,
 * see the examples in the comments, or the docs (coming soon).
*/

namespace folly { namespace gen {

template<class Value, class Self>
class GenImpl;

template<class Self>
class Operator;

class EmptySequence : public std::exception {
public:
  virtual const char* what() const noexcept {
    return "This operation cannot be called on an empty sequence";
  }
};

class Less {
public:
  template<class First,
           class Second>
  auto operator()(const First& first, const Second& second) const ->
  decltype(first < second) {
    return first < second;
  }
};

class Greater {
public:
  template<class First,
           class Second>
  auto operator()(const First& first, const Second& second) const ->
  decltype(first > second) {
    return first > second;
  }
};

template<int n>
class Get {
public:
  template<class Value>
  auto operator()(Value&& value) const ->
  decltype(std::get<n>(std::forward<Value>(value))) {
    return std::get<n>(std::forward<Value>(value));
  }
};

template<class Class,
         class Result>
class MemberFunction {
 public:
  typedef Result (Class::*MemberPtr)();
 private:
  MemberPtr member_;
 public:
  explicit MemberFunction(MemberPtr member)
    : member_(member)
  {}

  Result operator()(Class&& x) const {
    return (x.*member_)();
  }

  Result operator()(Class& x) const {
    return (x.*member_)();
  }
};

template<class Class,
         class Result>
class ConstMemberFunction{
 public:
  typedef Result (Class::*MemberPtr)() const;
 private:
  MemberPtr member_;
 public:
  explicit ConstMemberFunction(MemberPtr member)
    : member_(member)
  {}

  Result operator()(const Class& x) const {
    return (x.*member_)();
  }
};

template<class Class,
         class FieldType>
class Field {
 public:
  typedef FieldType (Class::*FieldPtr);
 private:
  FieldPtr field_;
 public:
  explicit Field(FieldPtr field)
    : field_(field)
  {}

  const FieldType& operator()(const Class& x) const {
    return x.*field_;
  }

  FieldType& operator()(Class& x) const {
    return x.*field_;
  }

  FieldType&& operator()(Class&& x) const {
    return std::move(x.*field_);
  }
};

class Move {
public:
  template<class Value>
  auto operator()(Value&& value) const ->
  decltype(std::move(std::forward<Value>(value))) {
    return std::move(std::forward<Value>(value));
  }
};

class Identity {
public:
  template<class Value>
  auto operator()(Value&& value) const ->
  decltype(std::forward<Value>(value)) {
    return std::forward<Value>(value);
  }
};

template <class Dest>
class Cast {
 public:
  template <class Value>
  Dest operator()(Value&& value) const {
    return Dest(std::forward<Value>(value));
  }
};

template <class Dest>
class To {
 public:
  template <class Value>
  Dest operator()(Value&& value) const {
    return ::folly::to<Dest>(std::forward<Value>(value));
  }
};

// Specialization to allow String->StringPiece conversion
template <>
class To<StringPiece> {
 public:
  StringPiece operator()(StringPiece src) const {
    return src;
  }
};

namespace detail {

template<class Self>
struct FBounded;

/*
 * Type Traits
 */
template<class Container>
struct ValueTypeOfRange {
 private:
  static Container container_;
 public:
  typedef decltype(*std::begin(container_))
    RefType;
  typedef typename std::decay<decltype(*std::begin(container_))>::type
    StorageType;
};


/*
 * Sources
 */
template<class Container,
         class Value = typename ValueTypeOfRange<Container>::RefType>
class ReferencedSource;

template<class Value,
         class Container = std::vector<typename std::decay<Value>::type>>
class CopiedSource;

template<class Value, bool endless = false, bool endInclusive = false>
class Sequence;

template<class Value, class First, class Second>
class Chain;

template<class Value, class Source>
class Yield;

template<class Value>
class Empty;


/*
 * Operators
 */
template<class Predicate>
class Map;

template<class Predicate>
class Filter;

template<class Predicate>
class Until;

class Take;

template<class Rand>
class Sample;

class Skip;

template<class Selector, class Comparer = Less>
class Order;

template<class Selector>
class Distinct;

template<class First, class Second>
class Composed;

template<class Expected>
class TypeAssertion;

/*
 * Sinks
 */
template<class Seed,
         class Fold>
class FoldLeft;

class First;

class Any;

template<class Predicate>
class All;

template<class Reducer>
class Reduce;

class Sum;

template<class Selector,
         class Comparer>
class Min;

template<class Container>
class Collect;

template<template<class, class> class Collection = std::vector,
         template<class> class Allocator = std::allocator>
class CollectTemplate;

template<class Collection>
class Append;

template<class Value>
struct GeneratorBuilder;

template<class Needle>
class Contains;

}

/**
 * Polymorphic wrapper
 **/
template<class Value>
class VirtualGen;

/*
 * Source Factories
 */
template<class Container,
         class From = detail::ReferencedSource<const Container>>
From fromConst(const Container& source) {
  return From(&source);
}

template<class Container,
         class From = detail::ReferencedSource<Container>>
From from(Container& source) {
  return From(&source);
}

template<class Container,
         class Value =
           typename detail::ValueTypeOfRange<Container>::StorageType,
         class CopyOf = detail::CopiedSource<Value>>
CopyOf fromCopy(Container&& source) {
  return CopyOf(std::forward<Container>(source));
}

template<class Value,
         class From = detail::CopiedSource<Value>>
From from(std::initializer_list<Value> source) {
  return From(source);
}

template<class Container,
         class From = detail::CopiedSource<typename Container::value_type,
                                           Container>>
From from(Container&& source) {
  return From(std::move(source));
}

template<class Value, class Gen = detail::Sequence<Value, false, false>>
Gen range(Value begin, Value end) {
  return Gen(begin, end);
}

template<class Value,
         class Gen = detail::Sequence<Value, false, true>>
Gen seq(Value first, Value last) {
  return Gen(first, last);
}

template<class Value,
         class Gen = detail::Sequence<Value, true>>
Gen seq(Value begin) {
  return Gen(begin);
}

template<class Value,
         class Source,
         class Yield = detail::Yield<Value, Source>>
Yield generator(Source&& source) {
  return Yield(std::forward<Source>(source));
}

/*
 * Create inline generator, used like:
 *
 *  auto gen = GENERATOR(int) { yield(1); yield(2); };
 */
#define GENERATOR(TYPE)                            \
  ::folly::gen::detail::GeneratorBuilder<TYPE>() + \
   [=](const std::function<void(TYPE)>& yield)

/*
 * empty() - for producing empty sequences.
 */
template<class Value>
detail::Empty<Value> empty() {
  return {};
}

/*
 * Operator Factories
 */
template<class Predicate,
         class Map = detail::Map<Predicate>>
Map mapped(Predicate pred = Predicate()) {
  return Map(std::move(pred));
}

template<class Predicate,
         class Map = detail::Map<Predicate>>
Map map(Predicate pred = Predicate()) {
  return Map(std::move(pred));
}

/*
 * member(...) - For extracting a member from each value.
 *
 *  vector<string> strings = ...;
 *  auto sizes = from(strings) | member(&string::size);
 *
 * If a member is const overridden (like 'front()'), pass template parameter
 * 'Const' to select the const version, or 'Mutable' to select the non-const
 * version:
 *
 *  auto heads = from(strings) | member<Const>(&string::front);
 */
enum MemberType {
  Const,
  Mutable
};

template<MemberType Constness = Const,
         class Class,
         class Return,
         class Mem = ConstMemberFunction<Class, Return>,
         class Map = detail::Map<Mem>>
typename std::enable_if<Constness == Const, Map>::type
member(Return (Class::*member)() const) {
  return Map(Mem(member));
}

template<MemberType Constness = Mutable,
         class Class,
         class Return,
         class Mem = MemberFunction<Class, Return>,
         class Map = detail::Map<Mem>>
typename std::enable_if<Constness == Mutable, Map>::type
member(Return (Class::*member)()) {
  return Map(Mem(member));
}

/*
 * field(...) - For extracting a field from each value.
 *
 *  vector<Item> items = ...;
 *  auto names = from(items) | field(&Item::name);
 *
 * Note that if the values of the generator are rvalues, any non-reference
 * fields will be rvalues as well. As an example, the code below does not copy
 * any strings, only moves them:
 *
 *  auto namesVector = from(items)
 *                   | move
 *                   | field(&Item::name)
 *                   | as<vector>();
 */
template<class Class,
         class FieldType,
         class Field = Field<Class, FieldType>,
         class Map = detail::Map<Field>>
Map field(FieldType Class::*field) {
  return Map(Field(field));
}

template<class Predicate,
         class Filter = detail::Filter<Predicate>>
Filter filter(Predicate pred = Predicate()) {
  return Filter(std::move(pred));
}

template<class Predicate,
         class All = detail::All<Predicate>>
All all(Predicate pred = Predicate()) {
  return All(std::move(pred));
}

template<class Predicate,
         class Until = detail::Until<Predicate>>
Until until(Predicate pred = Predicate()) {
  return Until(std::move(pred));
}

template<class Selector,
         class Comparer = Less,
         class Order = detail::Order<Selector, Comparer>>
Order orderBy(Selector selector = Identity(),
              Comparer comparer = Comparer()) {
  return Order(std::move(selector),
               std::move(comparer));
}

template<class Selector,
         class Order = detail::Order<Selector, Greater>>
Order orderByDescending(Selector selector = Identity()) {
  return Order(std::move(selector));
}

template<class Selector,
         class Distinct = detail::Distinct<Selector>>
Distinct distinctBy(Selector selector = Identity()) {
  return Distinct(std::move(selector));
}

template<int n,
         class Get = detail::Map<Get<n>>>
Get get() {
  return Get();
}

// construct Dest from each value
template <class Dest,
          class Cast = detail::Map<Cast<Dest>>>
Cast eachAs() {
  return Cast();
}

// call folly::to on each value
template <class Dest,
          class To = detail::Map<To<Dest>>>
To eachTo() {
  return To();
}

template<class Value>
detail::TypeAssertion<Value> assert_type() {
  return {};
}

/*
 * Sink Factories
 */
template<class Seed,
         class Fold,
         class FoldLeft = detail::FoldLeft<Seed, Fold>>
FoldLeft foldl(Seed seed = Seed(),
               Fold fold = Fold()) {
  return FoldLeft(std::move(seed),
                  std::move(fold));
}

template<class Reducer,
         class Reduce = detail::Reduce<Reducer>>
Reduce reduce(Reducer reducer = Reducer()) {
  return Reduce(std::move(reducer));
}

template<class Selector = Identity,
         class Min = detail::Min<Selector, Less>>
Min minBy(Selector selector = Selector()) {
  return Min(std::move(selector));
}

template<class Selector,
         class MaxBy = detail::Min<Selector, Greater>>
MaxBy maxBy(Selector selector = Selector()) {
  return MaxBy(std::move(selector));
}

template<class Collection,
         class Collect = detail::Collect<Collection>>
Collect as() {
  return Collect();
}

template<template<class, class> class Container = std::vector,
         template<class> class Allocator = std::allocator,
         class Collect = detail::CollectTemplate<Container, Allocator>>
Collect as() {
  return Collect();
}

template<class Collection,
         class Append = detail::Append<Collection>>
Append appendTo(Collection& collection) {
  return Append(&collection);
}

template<class Needle,
         class Contains = detail::Contains<typename std::decay<Needle>::type>>
Contains contains(Needle&& needle) {
  return Contains(std::forward<Needle>(needle));
}

}} // folly::gen

#include "folly/experimental/Gen-inl.h"
