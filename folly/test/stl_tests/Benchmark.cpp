/*
 * Copyright 2014 Facebook, Inc.
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

// @author Nicholas Ormrod <njormrod@fb.com>

/******************************************************************************
 *
 * This file is not perfect - benchmarking is a finicky process.
 *
 * TAKE THE NUMBERS YOU SEE TO MIND, NOT TO HEART
 *
 *****************************************************************************/

#include <vector>
#include "OFBVector.h"
#define FOLLY_BENCHMARK_USE_NS_IFOLLY
#include "folly/FBVector.h"

#include <chrono>
#include <deque>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <string>

#include <boost/preprocessor.hpp>

using namespace std;

static const bool enableColors = true;

//=============================================================================
// use the timestamp counter for time measurements

static inline
void clear_icache() {} // placeholder

// return the CPU timestamp counter
static uint64_t readTSC() {
   unsigned reslo, reshi;

   __asm__ __volatile__  (
   "xorl %%eax,%%eax \n cpuid \n"
    ::: "%eax", "%ebx", "%ecx", "%edx");
   __asm__ __volatile__  (
   "rdtsc\n"
    : "=a" (reslo), "=d" (reshi) );
   __asm__ __volatile__  (
   "xorl %%eax,%%eax \n cpuid \n"
    ::: "%eax", "%ebx", "%ecx", "%edx");

   return ((uint64_t)reshi << 32) | reslo;
}

//=============================================================================
// Timer

// The TIME* macros expand to a sequence of functions and classes whose aim
//  is to run a benchmark function several times with different types and
//  sizes.
//
// The first and last thing that TIME* expands to is a function declaration,
//  through the DECL macro. The declared function is templated on the full
//  vector type, its value_type, its allocator_type, and a number N.
// The first DECL is a forward declaration, and is followed by a
//  semicolon. The second DECL ends the macro - a function body is to be
//  supplied after the macro invocation.
// The declared function returns a uint64_t, which is assumed to be a time.
//
// The GETTER macro calls the DECL function repeatedly. It returns the
//  minimum time taken to execute DECL. GETTER runs DECL between 2 and 100
//  times (it will not run the full 100 if the tests take a long time).
//
// The EVALUATOR macro calls the GETTER macro with each of std::vector,
//  the original fbvector (folly::fbvector), and the new fbvector
//  (Ifolly::fbvector). It runs all three twice, and then calls the
//  pretty printer to display the results. Before calling the pretty
//  printer, the EVALUATOR outputs the three message strings.
//
// The EXECUTOR macro calls the EVALUATOR with different values of N.
//  It also defines the string message for N.
//
// The RUNNER macro defines a struct. That struct defines the testname
//  string. The constructor calls the EXECUTOR with each test type, and
//  also defines the test type string.
// The RUNNER class is also instantiated, so the constructor will be run
//  before entering main().
//

#define TIME(str, types)    TIME_I(str, types, (0))
#define TIME_N(str, types)  TIME_I(str, types, (0)(16)(64)(1024)(16384)(262144))

#define TIME_I(str, types, values) \
  TIME_II(str, BOOST_PP_CAT(t_, __LINE__), types, values)

#define TIME_II(str, name, types, values) \
  DECL(name);                             \
  GETTER(name)                            \
  EVALUATOR(name)                         \
  EXECUTOR(name, values)                  \
  RUNNER(str, name, types)                \
  DECL(name)

#define DECL(name)                                                \
  template <class Vector, typename T, typename Allocator, int N>  \
  static inline uint64_t BOOST_PP_CAT(run_, name) ()

#define GETTER(name)                                                          \
  template <class Vector, int N>                                              \
  static uint64_t BOOST_PP_CAT(get_, name) () {                               \
    auto s = chrono::high_resolution_clock::now();                            \
    uint64_t minticks = ~uint64_t(0);                                         \
    int burst = 0;                                                            \
    for (; burst < 100; ++burst) {                                            \
      auto t = BOOST_PP_CAT(run_, name) <Vector,                              \
        typename Vector::value_type, typename Vector::allocator_type, N> ();  \
      minticks = min(minticks, t);                                            \
      if (minticks * burst > 10000000) break;                                 \
    }                                                                         \
    auto e = chrono::high_resolution_clock::now();                            \
    chrono::nanoseconds d(e - s);                                             \
    return minticks;                                                          \
    return d.count() / burst;                                                 \
  }

#define EVALUATOR(name)                                                       \
  template <typename T, typename Allocator, int N>                            \
  void BOOST_PP_CAT(evaluate_, name)                                          \
  ( string& part1, string& part2, string& part3 ) {                           \
    cout << setw(25) << left << part1                                         \
         << setw(4) << left << part2                                          \
         << setw(6) << right << part3;                                        \
    part1.clear(); part2.clear(); part3.clear();                              \
    auto v1 = BOOST_PP_CAT(get_, name)                                        \
      <Ifolly::fbvector<T, Allocator>, N> ();                                 \
    auto v2 = BOOST_PP_CAT(get_, name)                                        \
      < folly::fbvector<T, Allocator>, N> ();                                 \
    auto v3 = BOOST_PP_CAT(get_, name)                                        \
      <   std::  vector<T, Allocator>, N> ();                                 \
    auto v1b = BOOST_PP_CAT(get_, name)                                       \
      <Ifolly::fbvector<T, Allocator>, N> ();                                 \
    auto v2b = BOOST_PP_CAT(get_, name)                                       \
      < folly::fbvector<T, Allocator>, N> ();                                 \
    auto v3b = BOOST_PP_CAT(get_, name)                                       \
      <   std::  vector<T, Allocator>, N> ();                                 \
    prettyPrint(min(v1, v1b), min(v2, v2b), min(v3, v3b));                    \
    cout << endl;                                                             \
  }

#define EXECUTOR(name, values)                                                \
  template <typename T, typename Allocator>                                   \
  void BOOST_PP_CAT(execute_, name) ( string& part1, string& part2 ) {        \
    BOOST_PP_SEQ_FOR_EACH(EVALUATE, name, values)                             \
  }

#define EVALUATE(r, name, value)                                              \
  { string part3(BOOST_PP_STRINGIZE(value));                                  \
    BOOST_PP_CAT(evaluate_, name) <T, Allocator, value>                       \
    ( part1, part2, part3 ); }

#define RUNNER(str, name, types)                                              \
  struct BOOST_PP_CAT(Runner_, name) {                                        \
    BOOST_PP_CAT(Runner_, name) () {                                          \
      string part1(str);                                                      \
      BOOST_PP_SEQ_FOR_EACH(EXECUTE, (part1, name), types)                    \
    }                                                                         \
  } BOOST_PP_CAT(runner_, name);

#define EXECUTE(r, pn, type)                                                  \
  { string part2(BOOST_PP_STRINGIZE(type));                                   \
    BOOST_PP_CAT(execute_, BOOST_PP_TUPLE_ELEM(2, 1, pn))                     \
    <typename type::first_type, typename type::second_type>                   \
    ( BOOST_PP_TUPLE_ELEM(2, 0, pn), part2 ); }

//=============================================================================
// pretty printer

// The pretty printer displays the times for each of the three vectors.
// The fastest time (or times, if there is a tie) is highlighted in green.
// Additionally, if the new fbvector (time v1) is not the fastest, then
//  it is highlighted with red or blue. It is highlighted with blue only
//  if it lost by a small margin (5 clock cycles or 2%, whichever is
//  greater).

void prettyPrint(uint64_t v1, uint64_t v2, uint64_t v3) {
  // rdtsc takes some time to run; about 157 clock cycles
  // if we see a smaller positive number, adjust readtsc
  uint64_t readtsc_time = 157;
  if (v1 != 0 && v1 < readtsc_time) readtsc_time = v1;
  if (v2 != 0 && v2 < readtsc_time) readtsc_time = v2;
  if (v3 != 0 && v3 < readtsc_time) readtsc_time = v3;

  if (v1 == 0) v1 = ~uint64_t(0); else v1 -= readtsc_time;
  if (v2 == 0) v2 = ~uint64_t(0); else v2 -= readtsc_time;
  if (v3 == 0) v3 = ~uint64_t(0); else v3 -= readtsc_time;

  auto least = min({ v1, v2, v3 });
  // a good time is less than 2% or 5 clock cycles slower
  auto good = max(least + 5, (uint64_t)(least * 1.02));

  string w("\x1b[1;;42m"); // green
  string g("\x1b[1;;44m"); // blue
  string b("\x1b[1;;41m"); // red
  string e("\x1b[0m");     // reset

  if (!enableColors) {
    w = b = e = "";
  }

  cout << " ";

  if (v1 == least) cout << w;
  else if (v1 <= good) cout << g;
  else cout << b;
  cout << setw(13) << right;
  if (v1 == ~uint64_t(0)) cout << "-"; else cout << v1;
  cout << " "  << e << " ";

  if (v2 == least) cout << w;
  cout << setw(13) << right;
  if (v2 == ~uint64_t(0)) cout << "-"; else cout << v2;
  cout << " " << e << " ";

  if (v3 == least) cout << w;
  cout << setw(13) << right;
  if (v3 == ~uint64_t(0)) cout << "-"; else cout << v3;
  cout << " " << e << " ";
}

//=============================================================================
// table formatting

// Much like the TIME macros, the Leader and Line struct/macros
//  instantiate a class before main, and work is done inside the
//  constructors. The Leader and Line struct print out pretty
//  table boundaries and titles.

uint64_t leader_elapsed() {
  static auto t = chrono::high_resolution_clock::now();
  chrono::nanoseconds d(chrono::high_resolution_clock::now() - t);
  return d.count() / 1000000000;
}

struct Leader {
  Leader() {
    leader_elapsed();
    std::cout.imbue(std::locale(""));

    cout << endl;
    cout << "========================================"
         << "========================================" << endl;
    cout << setw(35) << left << "Test";
    cout << setw(15) << right << "new fbvector ";
    cout << setw(15) << right << "old fbvector ";
    cout << setw(15) << right << "std::vector ";
    cout << endl;
    cout << "========================================"
         << "========================================" << endl;
  }
  ~Leader() {
    cout << endl;
    cout << "========================================"
         << "========================================" << endl;
    cout << setw(78) << right << leader_elapsed() << " s" << endl;
  }
} leader;

struct Line {
  explicit Line(string text) {
    cout << "\n--- " << text  << " ---" << endl;
  }
};
#define SECTION(str) Line BOOST_PP_CAT(l_, __LINE__) ( str )

//=============================================================================
// Test types

typedef pair<int, std::allocator<int>> T1;
typedef pair<vector<int>, std::allocator<vector<int>>> T2;

uint64_t v1_T1 = 0, v2_T1 = 0, v3_T1 = 0;
uint64_t v1_T2 = 0, v2_T2 = 0, v3_T2 = 0;

#define BASICS (T1)(T2)

//=============================================================================
// prevent optimizing

std::vector<int> O_vi(10000000);

void O(int i) {
  O_vi.push_back(i);
}
template <class V>
void O(const V& v) {
  int s = v.size();
  for (int i = 0; i < s; ++i) O(v[i]);
}

//=============================================================================
// Benchmarks

// #if 0
//-----------------------------------------------------------------------------
//SECTION("Dev");
//#undef BASICS
//#define BASICS (T1)


// #else

//-----------------------------------------------------------------------------
SECTION("Essentials");

TIME_N("~Vector()", BASICS) {
  Vector a(N);
  O(a);
  clear_icache(); auto b = readTSC();
  a.~Vector();
  auto e = readTSC();
  new (&a) Vector();
  O(a);
  return e - b;
}

TIME_N("a.clear()", BASICS) {
  Vector a(N);
  O(a);
  clear_icache(); auto b = readTSC();
  a.clear();
  auto e = readTSC();
  O(a);
  return e - b;
}

TIME("Vector u", BASICS) {
  clear_icache(); auto b = readTSC();
  Vector u;
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("Vector u(a)", BASICS) {
  static const Vector a(N);
  clear_icache(); auto b = readTSC();
  Vector u(a);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("Vector u(move(a))", BASICS) {
  Vector a(N);
  clear_icache(); auto b = readTSC();
  Vector u(move(a));
  auto e = readTSC();
  O(u);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Constructors");

TIME_N("Vector u(n)", BASICS) {
  clear_icache(); auto b = readTSC();
  Vector u(N);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("Vector u(n, t)", BASICS) {
  static const T t(1);
  clear_icache(); auto b = readTSC();
  Vector u(N, t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("Vector u(first, last)", BASICS) {
  static const deque<T> d(N);
  clear_icache(); auto b = readTSC();
  Vector u(d.begin(), d.end());
  auto e = readTSC();
  O(u);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Assignment");

TIME_N("a = b", BASICS) {
  Vector a(N);
  static const Vector c(N/2 + 10);
  O(a);
  clear_icache(); auto b = readTSC();
  a = c;
  auto e = readTSC();
  O(a);
  return e - b;
}

TIME_N("a = move(b)", BASICS) {
  Vector a(N);
  Vector c(N/2 + 10);
  O(a);
  O(c);
  clear_icache(); auto b = readTSC();
  a = move(c);
  auto e = readTSC();
  O(a);
  O(c);
  return e - b;
}

TIME_N("a = destructive_move(b)", BASICS) {
  Vector a(N);
  Vector c(N/2 + 10);
  O(a);
  O(c);
  clear_icache(); auto b = readTSC();
  a = move(c);
  c.clear();
  auto e = readTSC();
  O(a);
  O(c);
  return e - b;
}

TIME_N("a.assign(N, t)", BASICS) {
  Vector a(N/2 + 10);
  const T t(1);
  O(a);
  clear_icache(); auto b = readTSC();
  a.assign(N, t);
  auto e = readTSC();
  O(a);
  return e - b;
}

TIME_N("a.assign(first, last)", BASICS) {
  static const deque<T> d(N);
  Vector a(N/2 + 10);
  clear_icache(); auto b = readTSC();
  a.assign(d.begin(), d.end());
  auto e = readTSC();
  O(a);
  return e - b;
}

TIME_N("a.swap(b)", BASICS) {
  Vector a(N/2 + 10);
  Vector c(N);
  O(a);
  O(c);
  clear_icache(); auto b = readTSC();
  a.swap(c);
  auto e = readTSC();
  O(a);
  O(c);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Iterators");

TIME("a.begin()", BASICS) {
  static Vector a(1);
  clear_icache(); auto b = readTSC();
  auto r = a.begin();
  auto e = readTSC();
  O(*r);
  return e - b;
}

TIME("a.cbegin()", BASICS) {
  static Vector a(1);
  clear_icache(); auto b = readTSC();
  auto r = a.cbegin();
  auto e = readTSC();
  O(*r);
  return e - b;
}

TIME("a.rbegin()", BASICS) {
  static Vector a(1);
  clear_icache(); auto b = readTSC();
  auto r = a.rbegin();
  auto e = readTSC();
  O(*r);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Capacity");

TIME_N("a.size()", BASICS) {
  static const Vector a(N);
  clear_icache(); auto b = readTSC();
  int n = a.size();
  auto e = readTSC();
  O(n);
  return e - b;
}

TIME("a.max_size()", BASICS) {
  static Vector a;
  clear_icache(); auto b = readTSC();
  int n = a.max_size();
  auto e = readTSC();
  O(n);
  return e - b;
}

TIME_N("a.capacity()", BASICS) {
  static const Vector a(N);
  clear_icache(); auto b = readTSC();
  int n = a.capacity();
  auto e = readTSC();
  O(n);
  return e - b;
}

TIME_N("a.empty()", BASICS) {
  static const Vector a(N);
  clear_icache(); auto b = readTSC();
  int n = a.empty();
  auto e = readTSC();
  O(n);
  return e - b;
}

TIME_N("reserve(n)", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.reserve(N);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("resize(n)", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.resize(N);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("resize(n, t)", BASICS) {
  static const T t(1);
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.resize(N, t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("staged reserve", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.reserve(500);
  u.reserve(1000);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("staged resize", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.resize(500);
  u.resize(1000);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("resize then reserve", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.resize(500);
  u.reserve(1000);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("shrink", BASICS) {
  Vector u;
  O(u);
  u.resize(500);
  u.reserve(1000);
  clear_icache(); auto b = readTSC();
  u.shrink_to_fit();
  auto e = readTSC();
  O(u);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Access");

TIME("operator[]", BASICS) {
  static const Vector a(10);
  clear_icache(); auto b = readTSC();
  const auto& v = a[8];
  auto e = readTSC();
  O(v);
  return e - b;
}

TIME("at()", BASICS) {
  static const Vector a(10);
  clear_icache(); auto b = readTSC();
  const auto& v = a.at(8);
  auto e = readTSC();
  O(v);
  return e - b;
}

TIME("front()", BASICS) {
  static const Vector a(10);
  clear_icache(); auto b = readTSC();
  const auto& v = a.front();
  auto e = readTSC();
  O(v);
  return e - b;
}

TIME("back()", BASICS) {
  static const Vector a(10);
  clear_icache(); auto b = readTSC();
  const auto& v = a.back();
  auto e = readTSC();
  O(v);
  return e - b;
}

TIME("data()", BASICS) {
  static const Vector a(10);
  clear_icache(); auto b = readTSC();
  const auto& v = a.data();
  auto e = readTSC();
  O(*v);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Append");

TIME("reserved emplace", BASICS) {
  Vector u;
  u.reserve(1);
  O(u);
  clear_icache(); auto b = readTSC();
  u.emplace_back(0);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("full emplace", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.emplace_back(0);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("reserved push_back", BASICS) {
  static T t(0);
  Vector u;
  u.reserve(1);
  O(u);
  clear_icache(); auto b = readTSC();
  u.push_back(t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("full push_back", BASICS) {
  static T t(0);
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.push_back(t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("reserved push_back&&", BASICS) {
  T t(0);
  Vector u;
  u.reserve(1);
  O(u);
  clear_icache(); auto b = readTSC();
  u.push_back(std::move(t));
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("full push_back&&", BASICS) {
  T t(0);
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.push_back(std::move(t));
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("reserved push emplace", BASICS) {
  Vector u;
  u.reserve(1);
  O(u);
  clear_icache(); auto b = readTSC();
  u.push_back(T(0));
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("full push emplace", BASICS) {
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  u.push_back(T(0));
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("bulk append", BASICS) {
  static deque<T> d(N);
  Vector u(N/2 + 10);
  O(u);
  clear_icache(); auto b = readTSC();
  u.insert(u.end(), d.begin(), d.end());
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("erase end", BASICS) {
  Vector u(N);
  O(u);
  auto it = u.begin();
  it += u.size() / 2;
  if (it != u.end()) O(*it);
  clear_icache(); auto b = readTSC();
  u.erase(it, u.end());
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("pop_back", BASICS) {
  Vector u(1);
  O(u);
  clear_icache(); auto b = readTSC();
  u.pop_back();
  auto e = readTSC();
  O(u);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Insert/Erase - Bad Ops");

TIME("insert", BASICS) {
  Vector u(100);
  T t(1);
  auto it = u.begin();
  it += 50;
  O(u);
  O(*it);
  O(t);
  clear_icache(); auto b = readTSC();
  u.insert(it, t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("insert&&", BASICS) {
  Vector u(100);
  T t(1);
  auto it = u.begin();
  it += 50;
  O(u);
  O(*it);
  O(t);
  clear_icache(); auto b = readTSC();
  u.insert(it, std::move(t));
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("insert n few", BASICS) {
  Vector u(100);
  T t(1);
  auto it = u.begin();
  it += 50;
  O(u);
  O(*it);
  O(t);
  clear_icache(); auto b = readTSC();
  u.insert(it, 10, t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("insert n many", BASICS) {
  Vector u(100);
  T t(1);
  auto it = u.begin();
  it += 50;
  O(u);
  O(*it);
  O(t);
  clear_icache(); auto b = readTSC();
  u.insert(it, 200, t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("iterator insert few", BASICS) {
  static deque<T> d(10);
  Vector u(100);
  T t(1);
  auto it = u.begin();
  it += 50;
  O(u);
  O(*it);
  O(t);
  clear_icache(); auto b = readTSC();
  u.insert(it, d.begin(), d.end());
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("iterator insert many", BASICS) {
  static deque<T> d(200);
  Vector u(100);
  T t(1);
  auto it = u.begin();
  it += 50;
  O(u);
  O(*it);
  O(t);
  clear_icache(); auto b = readTSC();
  u.insert(it, d.begin(), d.end());
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("erase", BASICS) {
  Vector u(100);
  O(u);
  auto it = u.begin();
  it += 50;
  O(*it);
  clear_icache(); auto b = readTSC();
  u.erase(it);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("erase many", BASICS) {
  Vector u(100);
  O(u);
  auto it1 = u.begin();
  it1 += 33;
  O(*it1);
  auto it2 = u.begin();
  it2 += 66;
  O(*it2);
  clear_icache(); auto b = readTSC();
  u.erase(it1, it2);
  auto e = readTSC();
  O(u);
  return e - b;
}

//-----------------------------------------------------------------------------
SECTION("Large Tests");

TIME_N("reserved bulk push_back", BASICS) {
  Vector u;
  u.reserve(N);
  T t(0);
  O(u);
  clear_icache(); auto b = readTSC();
  for (int i = 0; i < N; ++i) u.emplace_back(t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("reserved bulk emplace", BASICS) {
  Vector u;
  u.reserve(N);
  O(u);
  clear_icache(); auto b = readTSC();
  for (int i = 0; i < N; ++i) u.emplace_back(0);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("populate", BASICS) {
  static T t(0);
  Vector u;
  O(u);
  clear_icache(); auto b = readTSC();
  for (int i = 0; i < N; ++i) u.push_back(t);
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("jigsaw growth", BASICS) {
  int sizes[] =
    { 1, 5, 2, 80, 17, 8, 9, 8, 140, 130, 1000, 130, 10000, 0, 8000, 2000 };
  clear_icache(); auto b = readTSC();
  Vector u;
  for (auto s : sizes) {
    if (s < u.size()) {
      int toAdd = u.size() - s;
      for (int i = 0; i < toAdd / 2; ++i) u.emplace_back(0);
      u.insert(u.end(), (toAdd + 1) / 2, T(1));
    } else {
      int toRm = u.size() - s;
      for (int i = 0; i < toRm / 2; ++i) u.pop_back();
      auto it = u.begin();
      std::advance(it, s);
      if (it < u.end()) u.erase(it, u.end());
    }
  }
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME("random access and modify", (T1)) {
  static const int n = 1024 * 1024 * 16;
  Vector u(n);
  O(u);
  clear_icache(); auto b = readTSC();
  int j = 7;
  for (int i = 0; i < 100000; ++i) {
    j = (j * 2 + j) ^ 0xdeadbeef;
    j = j & (n - 1);
    u[j] = i;
    u.at(n - j) = -i;
  }
  auto e = readTSC();
  O(u);
  return e - b;
}

TIME_N("iterate", (T1)) {
  static Vector u(N);
  O(u);
  clear_icache(); auto b = readTSC();
  int acc = 0;
  for (auto& e : u) {
    acc += e;
    e++;
  }
  auto e = readTSC();
  O(acc);
  O(u);
  return e - b;
}

TIME("emplace massive", BASICS) {
  clear_icache(); auto b = readTSC();
  Vector u;
  for (int i = 0; i < 10000000; ++i) {
    u.emplace_back(0);
  }
  auto e = readTSC();
  O(u);
  return e - b;
}

// #endif

//=============================================================================

int main() {
  return 0;
}

