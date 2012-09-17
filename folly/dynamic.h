/*
 * Copyright 2012 Facebook, Inc.
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

/**
 * This is a runtime dynamically typed value.  It holds types from a
 * specific predetermined set of types (ints, bools, arrays, etc).  In
 * particular, it can be used as a convenient in-memory representation
 * for complete json objects.
 *
 * In general you can try to use these objects as if they were the
 * type they represent (although in some cases with a slightly less
 * complete interface than the raw type), and it'll just throw a
 * TypeError if it is used in an illegal way.
 *
 * Some examples:
 *
 *   dynamic twelve = 12;
 *   dynamic str = "string";
 *   dynamic map = dynamic::object;
 *   map[str] = twelve;
 *   map[str + "another_str"] = { "array", "of", 4, "elements" };
 *   map.insert("null_element", nullptr);
 *   ++map[str];
 *   assert(map[str] == 13);
 *
 *   // Building a complex object with a sub array inline:
 *   dynamic d = dynamic::object
 *     ("key", "value")
 *     ("key2", { "a", "array" })
 *     ;
 *
 * Also see folly/json.h for the serialization and deserialization
 * functions for JSON.
 *
 * Note: dynamic is not DefaultConstructible.  Rationale:
 *
 *   - The intuitive thing to initialize a defaulted dynamic to would
 *     be nullptr.
 *
 *   - However, the expression dynamic d = {} is required to call the
 *     default constructor by the standard, which is confusing
 *     behavior for dynamic unless the default constructor creates an
 *     empty array.
 *
 * Additional documentation is in folly/docs/Dynamic.md.
 *
 * @author Jordan DeLong <delong.j@fb.com>
 */

#ifndef FOLLY_DYNAMIC_H_
#define FOLLY_DYNAMIC_H_

#include <unordered_map>
#include <memory>
#include <string>
#include <utility>
#include <ostream>
#include <type_traits>
#include <initializer_list>
#include <vector>
#include <cstdint>
#include <boost/operators.hpp>

#include "folly/Traits.h"
#include "folly/FBString.h"

namespace folly {

//////////////////////////////////////////////////////////////////////

struct dynamic;
struct TypeError;

//////////////////////////////////////////////////////////////////////

struct dynamic : private boost::operators<dynamic> {
  enum Type {
    NULLT,
    ARRAY,
    BOOL,
    DOUBLE,
    INT64,
    OBJECT,
    STRING,
  };

  /*
   * We support direct iteration of arrays, and indirect iteration of objects.
   * See begin(), end(), keys(), values(), and items() for more.
   *
   * Array iterators dereference as the elements in the array.
   * Object key iterators dereference as the keys in the object.
   * Object value iterators dereference as the values in the object.
   * Object item iterators dereference as pairs of (key, value).
   */
private:
  typedef std::vector<dynamic> Array;
public:
  typedef Array::const_iterator const_iterator;
  struct const_key_iterator;
  struct const_value_iterator;
  struct const_item_iterator;

  /*
   * Creation routines for making dynamic objects.  Objects are maps
   * from key to value (so named due to json-related origins here).
   *
   * Example:
   *
   *   // Make a fairly complex dynamic:
   *   dynamic d = dynamic::object("key", "value1")
   *                              ("key2", { "value", "with", 4, "words" });
   *
   *   // Build an object in a few steps:
   *   dynamic d = dynamic::object;
   *   d["key"] = 12;
   *   d["something_else"] = { 1, 2, 3, nullptr };
   */
private:
  struct ObjectMaker;

public:
  template<class... Args> static ObjectMaker object(Args&&...);

  /*
   * String compatibility constructors.
   */
  /* implicit */ dynamic(char const* val);
  /* implicit */ dynamic(std::string const& val);

  /*
   * This is part of the plumbing for object(), above.  Used to create
   * a new object dynamic.
   */
  /* implicit */ dynamic(ObjectMaker (*)());
  /* implicit */ dynamic(ObjectMaker const&) = delete;
  /* implicit */ dynamic(ObjectMaker&&);

  /*
   * Create a new array from an initializer list.
   *
   * For example:
   *
   *   dynamic v = { 1, 2, 3, "foo" };
   */
  /* implicit */ dynamic(std::initializer_list<dynamic> il);

  /*
   * Conversion constructors from most of the other types.
   */
  template<class T> /* implicit */ dynamic(T t);

  /*
   * Create a dynamic that is an array of the values from the supplied
   * iterator range.
   */
  template<class Iterator> dynamic(Iterator first, Iterator last);

  dynamic(dynamic const&);
  dynamic(dynamic&&);
  ~dynamic();

  /*
   * "Deep" equality comparison.  This will compare all the way down
   * an object or array, and is potentially expensive.
   */
  bool operator==(dynamic const& o) const;

  /*
   * For all types except object this returns the natural ordering on
   * those types.  For objects, we throw TypeError.
   */
  bool operator<(dynamic const& o) const;

  /*
   * General operators.
   *
   * These throw TypeError when used with types or type combinations
   * that don't support them.
   *
   * These functions may also throw if you use 64-bit integers with
   * doubles when the integers are too big to fit in a double.
   */
  dynamic& operator+=(dynamic const&);
  dynamic& operator-=(dynamic const&);
  dynamic& operator*=(dynamic const&);
  dynamic& operator/=(dynamic const&);
  dynamic& operator%=(dynamic const&);
  dynamic& operator|=(dynamic const&);
  dynamic& operator&=(dynamic const&);
  dynamic& operator^=(dynamic const&);
  dynamic& operator++();
  dynamic& operator--();

  /*
   * Assignment from other dynamics.  Because of the implicit conversion
   * to dynamic from its potential types, you can use this to change the
   * type pretty intuitively.
   *
   * Basic guarantee only.
   */
  dynamic& operator=(dynamic const&);
  dynamic& operator=(dynamic&&);

  /*
   * For simple dynamics (not arrays or objects), this prints the
   * value to an std::ostream in the expected way.  Respects the
   * formatting manipulators that have been sent to the stream
   * already.
   *
   * If the dynamic holds an object or array, this prints them in a
   * format very similar to JSON.  (It will in fact actually be JSON
   * as long as the dynamic validly represents a JSON object---i.e. it
   * can't have non-string keys.)
   */
  friend std::ostream& operator<<(std::ostream&, dynamic const&);

  /*
   * Returns true if this dynamic is of the specified type.
   */
  bool isString() const;
  bool isObject() const;
  bool isBool() const;
  bool isNull() const;
  bool isArray() const;
  bool isDouble() const;
  bool isInt() const;

  /*
   * Returns: isInt() || isDouble().
   */
  bool isNumber() const;

  /*
   * Returns the type of this dynamic.
   */
  Type type() const;

  /*
   * Extract a value while trying to convert to the specified type.
   * Throws exceptions if we cannot convert from the real type to the
   * requested type.
   *
   * Note you can only use this to access integral types or strings,
   * since arrays and objects are generally best delt with as a
   * dynamic.
   */
  fbstring asString() const;
  double   asDouble() const;
  int64_t  asInt() const;
  bool     asBool() const;

  /*
   * Returns: true if this dynamic is null, an empty array, an empty
   * object, or an empty string.
   */
  bool empty() const;

  /*
   * If this is an array or an object, returns the number of elements
   * contained.  If it is a string, returns the length.  Otherwise
   * throws TypeError.
   */
  std::size_t size() const;

  /*
   * You can iterate over the values of the array.  Calling these on
   * non-arrays will throw a TypeError.
   */
  const_iterator begin()  const;
  const_iterator end()    const;

private:
  /*
   * Helper object returned by keys(), values(), and items().
   */
  template <class T> struct IterableProxy;

public:
  /*
   * You can iterate over the keys, values, or items (std::pair of key and
   * value) in an object.  Calling these on non-objects will throw a TypeError.
   */
  IterableProxy<const_key_iterator> keys() const;
  IterableProxy<const_value_iterator> values() const;
  IterableProxy<const_item_iterator> items() const;

  /*
   * AssociativeContainer-style find interface for objects.  Throws if
   * this is not an object.
   *
   * Returns: end() if the key is not present, or an iterator pointing
   * to the item.
   */
  const_item_iterator find(dynamic const&) const;

  /*
   * If this is an object, returns whether it contains a field with
   * the given name.  Otherwise throws TypeError.
   */
  std::size_t count(dynamic const&) const;

  /*
   * For objects or arrays, provides access to sub-fields by index or
   * field name.
   *
   * Using these with dynamic objects that are not arrays or objects
   * will throw a TypeError.  Using an index that is out of range or
   * object-element that's not present throws std::out_of_range.
   */
  dynamic const& at(dynamic const&) const;
  dynamic&       at(dynamic const&);

  /*
   * This works for access to both objects and arrays.
   *
   * In the case of an array, the index must be an integer, and this will throw
   * std::out_of_range if it is less than zero or greater than size().
   *
   * In the case of an object, the non-const overload inserts a null
   * value if the key isn't present.  The const overload will throw
   * std::out_of_range if the key is not present.
   *
   * These functions do not invalidate iterators.
   */
  dynamic&       operator[](dynamic const&);
  dynamic const& operator[](dynamic const&) const;

  /*
   * Only defined for objects, throws TypeError otherwise.
   *
   * getDefault will return the value associated with the supplied key, the
   * supplied default otherwise. setDefault will set the key to the supplied
   * default if it is not yet set, otherwise leaving it. setDefault returns
   * a reference to the existing value if present, the new value otherwise.
   */
  dynamic
  getDefault(const dynamic& k, const dynamic& v = dynamic::object) const;
  dynamic&& getDefault(const dynamic& k, dynamic&& v) const;
  template<class K, class V = dynamic>
  dynamic& setDefault(K&& k, V&& v = dynamic::object);

  /*
   * Resizes an array so it has at n elements, using the supplied
   * default to fill new elements.  Throws TypeError if this dynamic
   * is not an array.
   *
   * May invalidate iterators.
   *
   * Post: size() == n
   */
  void resize(std::size_t n, dynamic const& = nullptr);

  /*
   * Inserts the supplied key-value pair to an object, or throws if
   * it's not an object.
   *
   * Invalidates iterators.
   */
  template<class K, class V> void insert(K&&, V&& val);

  /*
   * Erase an element from a dynamic object, by key.
   *
   * Invalidates iterators to the element being erased.
   *
   * Returns the number of elements erased (i.e. 1 or 0).
   */
  std::size_t erase(dynamic const& key);

  /*
   * Erase an element from a dynamic object or array, using an
   * iterator or an iterator range.
   *
   * In arrays, invalidates iterators to elements after the element
   * being erased.  In objects, invalidates iterators to the elements
   * being erased.
   *
   * Returns a new iterator to the first element beyond any elements
   * removed, or end() if there are none.  (The iteration order does
   * not change.)
   */
  const_iterator erase(const_iterator it);
  const_iterator erase(const_iterator first, const_iterator last);

  const_key_iterator erase(const_key_iterator it);
  const_key_iterator erase(const_key_iterator first, const_key_iterator last);

  const_value_iterator erase(const_value_iterator it);
  const_value_iterator erase(const_value_iterator first,
                             const_value_iterator last);

  const_item_iterator erase(const_item_iterator it);
  const_item_iterator erase(const_item_iterator first,
                            const_item_iterator last);
  /*
   * Append elements to an array.  If this is not an array, throws
   * TypeError.
   *
   * Invalidates iterators.
   */
  void push_back(dynamic const&);
  void push_back(dynamic&&);

  /*
   * Remove an element from the back of an array.  If this is not an array,
   * throws TypeError.
   *
   * Does not invalidate iterators.
   */
  void pop_back();

  /*
   * Get a hash code.  This function is called by a std::hash<>
   * specialization, also.
   *
   * Throws TypeError if this is an object, array, or null.
   */
  std::size_t hash() const;

private:
  friend struct TypeError;
  struct ObjectImpl;
  struct ObjectMaker;
  template<class T> struct TypeInfo;
  template<class T> struct CompareOp;
  template<class T> struct GetAddrImpl;
  template<class T> struct PrintImpl;

  template<class T> T const& get() const;
  template<class T> T&       get();
  template<class T> T*       get_nothrow();
  template<class T> T const* get_nothrow() const;
  template<class T> T*       getAddress();
  template<class T> T const* getAddress() const;

  template<class T> T asImpl() const;

  static char const* typeName(Type);
  void destroy();
  void print(std::ostream&) const;
  void print_as_pseudo_json(std::ostream&) const; // see json.cpp

private:
  Type type_;
  union Data {
    explicit Data() : nul(nullptr) {}
    ~Data() {}

    // XXX: gcc does an ICE if we use std::nullptr_t instead of void*
    // here.  See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=50361
    void* nul;
    Array array;
    bool boolean;
    double doubl;
    int64_t integer;
    fbstring string;

    /*
     * Objects are placement new'd here.  We have to use a char buffer
     * because we don't know the type here (std::unordered_map<> with
     * dynamic would be parameterizing a std:: template with an
     * incomplete type right now).  (Note that in contrast we know it
     * is ok to do this with fbvector because we own it.)
     */
    typename std::aligned_storage<
      sizeof(std::unordered_map<int,int>),
      alignof(std::unordered_map<int,int>)
    >::type objectBuffer;
  } u_;
};

//////////////////////////////////////////////////////////////////////

}

#include "folly/dynamic-inl.h"

#endif
