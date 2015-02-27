/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/small_vector.h>

#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <iostream>
#include <limits>

#include <boost/algorithm/string.hpp>

#include <folly/Conv.h>

using folly::small_vector;
using namespace folly::small_vector_policy;

#if FOLLY_X64

static_assert(sizeof(small_vector<int>) == 16,
              "Object size is not what we expect for small_vector<int>");
static_assert(sizeof(small_vector<int32_t,2>) == 16,
              "Object size is not what we expect for "
              "small_vector<int32_t,2>");
static_assert(sizeof(small_vector<int,10>) ==
                10 * sizeof(int) + sizeof(std::size_t),
              "Object size is not what we expect for small_vector<int,10>");

static_assert(sizeof(small_vector<int32_t,1,uint32_t>) ==
                8 + 4,
              "small_vector<int32_t,1,uint32_t> is wrong size");
static_assert(sizeof(small_vector<int32_t,1,uint16_t>) ==
                8 + 2,
              "small_vector<int32_t,1,uint32_t> is wrong size");
static_assert(sizeof(small_vector<int32_t,1,uint8_t>) ==
                8 + 1,
              "small_vector<int32_t,1,uint32_t> is wrong size");

static_assert(sizeof(small_vector<int16_t,4,uint16_t>) == 10,
              "Sizeof unexpectedly large");

#endif

static_assert(!FOLLY_IS_TRIVIALLY_COPYABLE(std::unique_ptr<int>),
              "std::unique_ptr<> is trivially copyable");

namespace {

struct NontrivialType {
  static int ctored;
  explicit NontrivialType() : a(0) {}

  /* implicit */ NontrivialType(int a) : a(a) {
    ++ctored;
  }

  NontrivialType(NontrivialType const& s) {
    ++ctored;
  }

  NontrivialType& operator=(NontrivialType const& o) {
    a = o.a;
    return *this;
  }

  int32_t a;
};
static_assert(!FOLLY_IS_TRIVIALLY_COPYABLE(NontrivialType),
              "NontrivialType is trivially copyable");

int NontrivialType::ctored = 0;

struct TestException {};

int throwCounter = 1;
void MaybeThrow() {
  if (!--throwCounter) {
    throw TestException();
  }
}

const int kMagic = 0xdeadbeef;
struct Thrower {
  static int alive;

  Thrower() : magic(kMagic) {
    EXPECT_EQ(magic, kMagic);
    MaybeThrow();
    ++alive;
  }
  Thrower(Thrower const& other) : magic(other.magic) {
    EXPECT_EQ(magic, kMagic);
    MaybeThrow();
    ++alive;
  }
  ~Thrower() noexcept {
    EXPECT_EQ(magic, kMagic);
    magic = 0;
    --alive;
  }

  Thrower& operator=(Thrower const& other) {
    EXPECT_EQ(magic, kMagic);
    MaybeThrow();
    return *this;
  }

  // This is just to try to make sure we don't get our member
  // functions called on uninitialized memory.
  int magic;
};

int Thrower::alive = 0;

// Type that counts how many exist and doesn't support copy
// construction.
struct NoncopyableCounter {
  static int alive;
  NoncopyableCounter() {
    ++alive;
  }
  ~NoncopyableCounter() {
    --alive;
  }
  NoncopyableCounter(NoncopyableCounter&&) noexcept { ++alive; }
  NoncopyableCounter(NoncopyableCounter const&) = delete;
  NoncopyableCounter& operator=(NoncopyableCounter const&) const = delete;
  NoncopyableCounter& operator=(NoncopyableCounter&&) { return *this; }
};
int NoncopyableCounter::alive = 0;

static_assert(!FOLLY_IS_TRIVIALLY_COPYABLE(NoncopyableCounter),
              "NoncopyableCounter is trivially copyable");

// Check that throws don't break the basic guarantee for some cases.
// Uses the method for testing exception safety described at
// http://www.boost.org/community/exception_safety.html, to force all
// throwing code paths to occur.
struct TestBasicGuarantee {
  folly::small_vector<Thrower,3> vec;
  int const prepopulate;

  explicit TestBasicGuarantee(int prepopulate)
    : prepopulate(prepopulate)
  {
    throwCounter = 1000;
    for (int i = 0; i < prepopulate; ++i) {
      vec.push_back(Thrower());
    }
  }

  ~TestBasicGuarantee() {
    throwCounter = 1000;
  }

  template<class Operation>
  void operator()(int insertCount, Operation const& op) {
    bool done = false;

    std::unique_ptr<folly::small_vector<Thrower,3> > workingVec;
    for (int counter = 1; !done; ++counter) {
      throwCounter = 1000;
      workingVec.reset(new folly::small_vector<Thrower,3>(vec));
      throwCounter = counter;
      EXPECT_EQ(Thrower::alive, prepopulate * 2);
      try {
        op(*workingVec);
        done = true;
      } catch (...) {
        // Note that the size of the vector can change if we were
        // inserting somewhere other than the end (it's a basic only
        // guarantee).  All we're testing here is that we have the
        // right amount of uninitialized vs initialized memory.
        EXPECT_EQ(Thrower::alive, workingVec->size() + vec.size());
        continue;
      }

      // If things succeeded.
      EXPECT_EQ(workingVec->size(), prepopulate + insertCount);
      EXPECT_EQ(Thrower::alive, prepopulate * 2 + insertCount);
    }
  }
};

}

TEST(small_vector, BasicGuarantee) {
  for (int prepop = 1; prepop < 30; ++prepop) {
    (TestBasicGuarantee(prepop))( // parens or a mildly vexing parse :(
      1,
      [&] (folly::small_vector<Thrower,3>& v) {
        v.push_back(Thrower());
      }
    );

    EXPECT_EQ(Thrower::alive, 0);

    (TestBasicGuarantee(prepop))(
      1,
      [&] (folly::small_vector<Thrower,3>& v) {
        v.insert(v.begin(), Thrower());
      }
    );

    EXPECT_EQ(Thrower::alive, 0);

    (TestBasicGuarantee(prepop))(
      1,
      [&] (folly::small_vector<Thrower,3>& v) {
        v.insert(v.begin() + 1, Thrower());
      }
    );

    EXPECT_EQ(Thrower::alive, 0);
  }

  TestBasicGuarantee(4)(
    3,
    [&] (folly::small_vector<Thrower,3>& v) {
      std::vector<Thrower> b;
      b.push_back(Thrower());
      b.push_back(Thrower());
      b.push_back(Thrower());

      /*
       * Apparently if you do the following initializer_list instead
       * of the above push_back's, and one of the Throwers throws,
       * g++4.6 doesn't destruct the previous ones.  Heh.
       */
      //b = { Thrower(), Thrower(), Thrower() };
      v.insert(v.begin() + 1, b.begin(), b.end());
    }
  );

  TestBasicGuarantee(2)(
    6,
    [&] (folly::small_vector<Thrower,3>& v) {
      std::vector<Thrower> b;
      for (int i = 0; i < 6; ++i) {
        b.push_back(Thrower());
      }

      v.insert(v.begin() + 1, b.begin(), b.end());
    }
  );

  EXPECT_EQ(Thrower::alive, 0);
  try {
    throwCounter = 4;
    folly::small_vector<Thrower,1> p(14, Thrower());
  } catch (...) {
  }
  EXPECT_EQ(Thrower::alive, 0);
}

// Run this with.
// MALLOC_CONF=prof_leak:true
// LD_PRELOAD=${JEMALLOC_PATH}/lib/libjemalloc.so.1
// LD_PRELOAD="$LD_PRELOAD:"${UNWIND_PATH}/lib/libunwind.so.7
TEST(small_vector, leak_test) {
  for (int j = 0; j < 1000; ++j) {
    folly::small_vector<int, 10> someVec(300);
    for (int i = 0; i < 10000; ++i) {
      someVec.push_back(12);
    }
  }
}

TEST(small_vector, Insert) {
  folly::small_vector<int> someVec(3, 3);
  someVec.insert(someVec.begin(), 12, 12);
  EXPECT_EQ(someVec.size(), 15);
  for (size_t i = 0; i < someVec.size(); ++i) {
    if (i < 12) {
      EXPECT_EQ(someVec[i], 12);
    } else {
      EXPECT_EQ(someVec[i], 3);
    }
  }

  auto oldSize = someVec.size();
  someVec.insert(someVec.begin() + 1, 12, 12);
  EXPECT_EQ(someVec.size(), oldSize + 12);

  folly::small_vector<std::string> v1(6, "asd"), v2(7, "wat");
  v1.insert(v1.begin() + 1, v2.begin(), v2.end());
  EXPECT_TRUE(v1.size() == 6 + 7);
  EXPECT_EQ(v1.front(), "asd");
  EXPECT_EQ(v1[1], "wat");
}

TEST(small_vector, Swap) {
  folly::small_vector<int,10> somethingVec, emptyVec;
  somethingVec.push_back(1);
  somethingVec.push_back(2);
  somethingVec.push_back(3);
  somethingVec.push_back(4);

  // Swapping intern'd with intern'd.
  auto vec = somethingVec;
  EXPECT_TRUE(vec == somethingVec);
  EXPECT_FALSE(vec == emptyVec);
  EXPECT_FALSE(somethingVec == emptyVec);

  // Swapping a heap vector with an intern vector.
  folly::small_vector<int,10> junkVec;
  junkVec.assign(12, 12);
  EXPECT_EQ(junkVec.size(), 12);
  for (auto i : junkVec) {
    EXPECT_EQ(i, 12);
  }
  swap(junkVec, vec);
  EXPECT_TRUE(junkVec == somethingVec);
  EXPECT_EQ(vec.size(), 12);
  for (auto i : vec) {
    EXPECT_EQ(i, 12);
  }

  // Swapping two heap vectors.
  folly::small_vector<int,10> moreJunk(15, 15);
  EXPECT_EQ(moreJunk.size(), 15);
  for (auto i : moreJunk) {
    EXPECT_EQ(i, 15);
  }
  swap(vec, moreJunk);
  EXPECT_EQ(moreJunk.size(), 12);
  for (auto i : moreJunk) {
    EXPECT_EQ(i, 12);
  }
  EXPECT_EQ(vec.size(), 15);
  for (auto i : vec) {
    EXPECT_EQ(i, 15);
  }

  // Making a vector heap, then smaller than another non-heap vector,
  // then swapping.
  folly::small_vector<int,5> shrinker, other(4, 10);
  shrinker = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
  shrinker.erase(shrinker.begin() + 2, shrinker.end());
  EXPECT_LT(shrinker.size(), other.size());
  swap(shrinker, other);
  EXPECT_EQ(shrinker.size(), 4);
  EXPECT_TRUE(boost::all(shrinker, boost::is_any_of(std::vector<int>{10})));
  EXPECT_TRUE((other == small_vector<int,5>{ 0, 1 }));
}

TEST(small_vector, Emplace) {
  NontrivialType::ctored = 0;

  folly::small_vector<NontrivialType> vec;
  vec.reserve(1024);
  vec.emplace_back(12);
  EXPECT_EQ(NontrivialType::ctored, 1);
  EXPECT_EQ(vec.front().a, 12);
  vec.emplace_back(13);
  EXPECT_EQ(vec.front().a, 12);
  EXPECT_EQ(vec.back().a, 13);
  EXPECT_EQ(NontrivialType::ctored, 2);

  NontrivialType::ctored = 0;
  for (int i = 0; i < 120; ++i) {
    vec.emplace_back(i);
  }
  EXPECT_EQ(NontrivialType::ctored, 120);
  EXPECT_EQ(vec[0].a, 12);
  EXPECT_EQ(vec[1].a, 13);
  EXPECT_EQ(vec.back().a, 119);

  // We implement emplace() with a temporary (see the implementation
  // for a comment about why), so this should make 2 ctor calls.
  NontrivialType::ctored = 0;
  vec.emplace(vec.begin(), 12);
  EXPECT_EQ(NontrivialType::ctored, 2);
}

TEST(small_vector, Erase) {
  folly::small_vector<int,4> notherVec = { 1, 2, 3, 4, 5 };
  EXPECT_EQ(notherVec.front(), 1);
  EXPECT_EQ(notherVec.size(), 5);
  notherVec.erase(notherVec.begin());
  EXPECT_EQ(notherVec.front(), 2);
  EXPECT_EQ(notherVec.size(), 4);
  EXPECT_EQ(notherVec[2], 4);
  EXPECT_EQ(notherVec[3], 5);
  notherVec.erase(notherVec.begin() + 2);
  EXPECT_EQ(notherVec.size(), 3);
  EXPECT_EQ(notherVec[2], 5);

  folly::small_vector<int,2> vec2 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
  vec2.erase(vec2.begin() + 1, vec2.end() - 1);
  folly::small_vector<int,2> expected = { 1, 10 };
  EXPECT_TRUE(vec2 == expected);

  folly::small_vector<std::string,3> v(102, "ASD");
  v.resize(1024, "D");
  EXPECT_EQ(v.size(), 1024);
  EXPECT_EQ(v.back(), "D");
  EXPECT_EQ(v.front(), "ASD");
  v.resize(1);
  EXPECT_EQ(v.front(), "ASD");
  EXPECT_EQ(v.size(), 1);
  v.resize(0);
  EXPECT_TRUE(v.empty());
}

TEST(small_vector, GrowShrinkGrow) {
  folly::small_vector<NontrivialType,7> vec = { 1, 2, 3, 4, 5 };
  std::generate_n(std::back_inserter(vec), 102, std::rand);

  auto capacity = vec.capacity();

  auto oldSize = vec.size();
  for (size_t i = 0; i < oldSize; ++i) {
    vec.erase(vec.begin() + (std::rand() % vec.size()));
    EXPECT_EQ(vec.capacity(), capacity);
  }
  EXPECT_TRUE(vec.empty());

  EXPECT_EQ(vec.capacity(), capacity);
  std::generate_n(std::back_inserter(vec), 102, std::rand);
  EXPECT_EQ(vec.capacity(), capacity);

  std::generate_n(std::back_inserter(vec), 4096, std::rand);
  EXPECT_GT(vec.capacity(), capacity);

  vec.resize(10);
  vec.shrink_to_fit();
  EXPECT_LT(vec.capacity(), capacity);
  vec.resize(4);
  vec.shrink_to_fit();
  EXPECT_EQ(vec.capacity(), 7); // in situ size
}

TEST(small_vector, Iteration) {
  folly::small_vector<std::string,3> vec = { "foo", "bar" };
  vec.push_back("blah");
  vec.push_back("blah2");
  vec.push_back("blah3");
  vec.erase(vec.begin() + 2);

  std::vector<std::string> otherVec;
  for (auto& s : vec) {
    otherVec.push_back(s);
  }
  EXPECT_EQ(otherVec.size(), vec.size());
  if (otherVec.size() == vec.size()) {
    EXPECT_TRUE(std::equal(otherVec.begin(), otherVec.end(), vec.begin()));
  }

  std::reverse(otherVec.begin(), otherVec.end());
  auto oit = otherVec.begin();
  auto rit = vec.crbegin();
  for (; rit != vec.crend(); ++oit, ++rit) {
    EXPECT_EQ(*oit, *rit);
  }
}

TEST(small_vector, NonCopyableType) {
  folly::small_vector<NontrivialType,2> vec;

  for (int i = 0; i < 10; ++i) {
    vec.emplace(vec.begin(), 13);
  }
  EXPECT_EQ(vec.size(), 10);
  auto vec2 = std::move(vec);
  EXPECT_EQ(vec.size(), 0);
  EXPECT_EQ(vec2.size(), 10);
  vec2.clear();

  folly::small_vector<NoncopyableCounter,3> vec3;
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(vec3.size(), i);
    EXPECT_EQ(NoncopyableCounter::alive, i);
    vec3.insert(vec3.begin(), NoncopyableCounter());
  }
  EXPECT_EQ(vec3.size(), 10);
  EXPECT_EQ(NoncopyableCounter::alive, 10);

  vec3.insert(vec3.begin() + 3, NoncopyableCounter());
  EXPECT_EQ(NoncopyableCounter::alive, 11);
  auto vec4 = std::move(vec3);
  EXPECT_EQ(NoncopyableCounter::alive, 11);
  vec4.resize(30);
  EXPECT_EQ(NoncopyableCounter::alive, 30);
  vec4.erase(vec4.begin(), vec4.end());
  EXPECT_EQ(vec4.size(), 0);
  EXPECT_EQ(NoncopyableCounter::alive, 0);
}

TEST(small_vector, MoveConstructor) {
  folly::small_vector<std::string,10> v1;
  v1.push_back("asd");
  v1.push_back("bsd");
  auto v2 = std::move(v1);
  EXPECT_EQ(v2.size(), 2);
  EXPECT_EQ(v2[0], "asd");
  EXPECT_EQ(v2[1], "bsd");

  v1 = std::move(v2);
  EXPECT_EQ(v1.size(), 2);
  EXPECT_EQ(v1[0], "asd");
  EXPECT_EQ(v1[1], "bsd");
}

TEST(small_vector, NoHeap) {
  typedef folly::small_vector<std::string,10,
    std::size_t,folly::small_vector_policy::NoHeap> Vector;

  Vector v;
  static_assert(v.max_size() == 10, "max_size is incorrect");

  for (int i = 0; i < 10; ++i) {
    v.push_back(folly::to<std::string>(i));
    EXPECT_EQ(v.size(), i + 1);
  }

  bool caught = false;
  try {
    v.insert(v.begin(), "ha");
  } catch (const std::length_error&) {
    caught = true;
  }
  EXPECT_TRUE(caught);

  // Check max_size works right with various policy combinations.
  folly::small_vector<std::string,32,uint32_t> v4;
  EXPECT_EQ(v4.max_size(), (1ul << 31) - 1);

  /*
   * Test that even when we ask for a small number inlined it'll still
   * inline at least as much as it takes to store the value_type
   * pointer.
   */
  folly::small_vector<char,1,NoHeap> notsosmall;
  static_assert(notsosmall.max_size() == sizeof(char*),
                "max_size is incorrect");
  caught = false;
  try {
    notsosmall.push_back(12);
    notsosmall.push_back(13);
    notsosmall.push_back(14);
  } catch (const std::length_error&) {
    caught = true;
  }
  EXPECT_FALSE(caught);
}

TEST(small_vector, MaxSize) {
  folly::small_vector<int,2,uint8_t> vec;
  EXPECT_EQ(vec.max_size(), 127);
  folly::small_vector<int,2,uint16_t> vec2;
  EXPECT_EQ(vec2.max_size(), (1 << 15) - 1);
}

TEST(small_vector, AllHeap) {
  // Use something bigger than the pointer so it can't get inlined.
  struct SomeObj {
    double a, b, c, d, e; int val;
    SomeObj(int val) : val(val) {}
    bool operator==(SomeObj const& o) const {
      return o.val == val;
    }
  };

  folly::small_vector<SomeObj,0> vec = { 1 };
  EXPECT_EQ(vec.size(), 1);
  if (!vec.empty()) {
    EXPECT_TRUE(vec[0] == 1);
  }
  vec.insert(vec.begin(), { 0, 1, 2, 3 });
  EXPECT_EQ(vec.size(), 5);
  EXPECT_TRUE((vec == folly::small_vector<SomeObj,0>{ 0, 1, 2, 3, 1 }));
}

TEST(small_vector, Basic) {
  typedef folly::small_vector<int,3,uint32_t
  > Vector;

  Vector a;

  a.push_back(12);
  EXPECT_EQ(a.front(), 12);
  EXPECT_EQ(a.size(), 1);
  a.push_back(13);
  EXPECT_EQ(a.size(), 2);
  EXPECT_EQ(a.front(), 12);
  EXPECT_EQ(a.back(), 13);

  a.emplace(a.end(), 32);
  EXPECT_EQ(a.back(), 32);

  a.emplace(a.begin(), 12);
  EXPECT_EQ(a.front(), 12);
  EXPECT_EQ(a.back(), 32);
  a.erase(a.end() - 1);
  EXPECT_EQ(a.back(), 13);

  a.push_back(12);
  EXPECT_EQ(a.back(), 12);
  a.pop_back();
  EXPECT_EQ(a.back(), 13);

  const int s = 12;
  a.push_back(s); // lvalue reference

  Vector b, c;
  b = a;
  EXPECT_TRUE(b == a);
  c = std::move(b);
  EXPECT_TRUE(c == a);
  EXPECT_TRUE(c != b && b != a);

  EXPECT_GT(c.size(), 0);
  c.resize(1);
  EXPECT_EQ(c.size(), 1);

  Vector intCtor(12);
}

TEST(small_vector, Capacity) {
  folly::small_vector<unsigned long, 1> vec;
  EXPECT_EQ(vec.size(), 0);
  EXPECT_EQ(vec.capacity(), 1);

  vec.push_back(0);
  EXPECT_EQ(vec.size(), 1);
  EXPECT_EQ(vec.capacity(), 1);

  vec.push_back(1);
  EXPECT_EQ(vec.size(), 2);
  EXPECT_GT(vec.capacity(), 1);


  folly::small_vector<unsigned long, 2> vec2;
  EXPECT_EQ(vec2.size(), 0);
  EXPECT_EQ(vec2.capacity(), 2);

  vec2.push_back(0);
  vec2.push_back(1);
  EXPECT_EQ(vec2.size(), 2);
  EXPECT_EQ(vec2.capacity(), 2);

  vec2.push_back(2);
  EXPECT_EQ(vec2.size(), 3);
  EXPECT_GT(vec2.capacity(), 2);

  // Test capacity heapifying logic
  folly::small_vector<unsigned char, 1> vec3;
  const size_t hc_size = 1000000;
  for (size_t i = 0; i < hc_size; ++i) {
    auto v = (unsigned char)i;
    vec3.push_back(v);
    EXPECT_EQ(vec3[i], v);
    EXPECT_EQ(vec3.size(), i + 1);
    EXPECT_GT(vec3.capacity(), i);
  }
  for (auto i = hc_size; i > 0; --i) {
    auto v = (unsigned char)(i - 1);
    EXPECT_EQ(vec3.back(), v);
    vec3.pop_back();
    EXPECT_EQ(vec3.size(), i - 1);
  }
}

TEST(small_vector, SelfPushBack) {
  for (int i = 1; i < 33; ++i) {
    folly::small_vector<std::string> vec;
    for (int j = 0; j < i; ++j) {
      vec.push_back("abc");
    }
    EXPECT_EQ(vec.size(), i);
    vec.push_back(std::move(vec[0]));
    EXPECT_EQ(vec.size(), i + 1);

    EXPECT_EQ(vec[i], "abc");
  }
}

TEST(small_vector, SelfEmplaceBack) {
  for (int i = 1; i < 33; ++i) {
    folly::small_vector<std::string> vec;
    for (int j = 0; j < i; ++j) {
      vec.emplace_back("abc");
    }
    EXPECT_EQ(vec.size(), i);
    vec.emplace_back(std::move(vec[0]));
    EXPECT_EQ(vec.size(), i + 1);

    EXPECT_EQ(vec[i], "abc");
  }
}

TEST(small_vector, SelfInsert) {
  // end insert
  for (int i = 1; i < 33; ++i) {
    folly::small_vector<std::string> vec;
    for (int j = 0; j < i; ++j) {
      vec.push_back("abc");
    }
    EXPECT_EQ(vec.size(), i);
    vec.insert(vec.end(), std::move(vec[0]));
    EXPECT_EQ(vec.size(), i + 1);

    EXPECT_EQ(vec[i], "abc");
    EXPECT_EQ(vec[vec.size() - 1], "abc");
  }

  // middle insert
  for (int i = 2; i < 33; ++i) {
    folly::small_vector<std::string> vec;
    for (int j = 0; j < i; ++j) {
      vec.push_back("abc");
    }
    EXPECT_EQ(vec.size(), i);
    vec.insert(vec.end()-1, std::move(vec[0]));
    EXPECT_EQ(vec.size(), i + 1);

    EXPECT_EQ(vec[i-1], "abc");
    EXPECT_EQ(vec[i], "abc");
  }
}
