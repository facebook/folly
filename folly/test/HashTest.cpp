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

#include <folly/Hash.h>
#include <folly/MapUtil.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <unordered_map>
#include <utility>

using namespace folly::hash;

TEST(Hash, Fnv32) {
  const char* s1 = "hello, world!";
  const uint32_t s1_res = 3605494790UL;
  EXPECT_EQ(fnv32(s1), s1_res);
  EXPECT_EQ(fnv32(s1), fnv32_buf(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint32_t s2_res = 1270448334UL;
  EXPECT_EQ(fnv32(s2), s2_res);
  EXPECT_EQ(fnv32(s2), fnv32_buf(s2, strlen(s2)));

  const char* s3 = "";
  const uint32_t s3_res = 2166136261UL;
  EXPECT_EQ(fnv32(s3), s3_res);
  EXPECT_EQ(fnv32(s3), fnv32_buf(s3, strlen(s3)));
}

TEST(Hash, Fnv64) {
  const char* s1 = "hello, world!";
  const uint64_t s1_res = 13991426986746681734ULL;
  EXPECT_EQ(fnv64(s1), s1_res);
  EXPECT_EQ(fnv64(s1), fnv64_buf(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint64_t s2_res = 6091394665637302478ULL;
  EXPECT_EQ(fnv64(s2), s2_res);
  EXPECT_EQ(fnv64(s2), fnv64_buf(s2, strlen(s2)));

  const char* s3 = "";
  const uint64_t s3_res = 14695981039346656037ULL;
  EXPECT_EQ(fnv64(s3), s3_res);
  EXPECT_EQ(fnv64(s3), fnv64_buf(s3, strlen(s3)));

  // note: Use fnv64_buf to make a single hash value from multiple
  // fields/datatypes.
  const char* t4_a = "E Pluribus";
  int64_t t4_b = 0xF1E2D3C4B5A69788;
  int32_t t4_c = 0xAB12CD34;
  const char* t4_d = "Unum";
  uint64_t t4_res = 15571330457339273965ULL;
  uint64_t t4_hash1 = fnv64_buf(t4_a,
                                strlen(t4_a));
  uint64_t t4_hash2 = fnv64_buf(reinterpret_cast<void*>(&t4_b),
                                sizeof(int64_t),
                                t4_hash1);
  uint64_t t4_hash3 = fnv64_buf(reinterpret_cast<void*>(&t4_c),
                                sizeof(int32_t),
                                t4_hash2);
  uint64_t t4_hash4 = fnv64_buf(t4_d,
                                strlen(t4_d),
                                t4_hash3);
  EXPECT_EQ(t4_hash4, t4_res);
  // note: These are probabalistic, not determinate, but c'mon.
  // These hash values should be different, or something's not
  // working.
  EXPECT_NE(t4_hash1, t4_hash4);
  EXPECT_NE(t4_hash2, t4_hash4);
  EXPECT_NE(t4_hash3, t4_hash4);
}

TEST(Hash, Hsieh32) {
  const char* s1 = "hello, world!";
  const uint32_t s1_res = 2918802987ul;
  EXPECT_EQ(hsieh_hash32(s1), s1_res);
  EXPECT_EQ(hsieh_hash32(s1), hsieh_hash32_buf(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint32_t s2_res = 47373213ul;
  EXPECT_EQ(hsieh_hash32(s2), s2_res);
  EXPECT_EQ(hsieh_hash32(s2), hsieh_hash32_buf(s2, strlen(s2)));

  const char* s3 = "";
  const uint32_t s3_res = 0;
  EXPECT_EQ(hsieh_hash32(s3), s3_res);
  EXPECT_EQ(hsieh_hash32(s3), hsieh_hash32_buf(s3, strlen(s3)));
}

TEST(Hash, TWang_Mix64) {
  uint64_t i1 = 0x78a87873e2d31dafULL;
  uint64_t i1_res = 3389151152926383528ULL;
  EXPECT_EQ(i1_res, twang_mix64(i1));
  EXPECT_EQ(i1, twang_unmix64(i1_res));

  uint64_t i2 = 0x0123456789abcdefULL;
  uint64_t i2_res = 3061460455458984563ull;
  EXPECT_EQ(i2_res, twang_mix64(i2));
  EXPECT_EQ(i2, twang_unmix64(i2_res));
}

namespace {
void checkTWang(uint64_t r) {
  uint64_t result = twang_mix64(r);
  EXPECT_EQ(r, twang_unmix64(result));
}
}  // namespace

TEST(Hash, TWang_Unmix64) {
  // We'll try (1 << i), (1 << i) + 1, (1 << i) - 1
  for (int i = 1; i < 64; i++) {
    checkTWang((1U << i) - 1);
    checkTWang(1U << i);
    checkTWang((1U << i) + 1);
  }
}

TEST(Hash, TWang_32From64) {
  uint64_t i1 = 0x78a87873e2d31dafULL;
  uint32_t i1_res = 1525586863ul;
  EXPECT_EQ(twang_32from64(i1), i1_res);

  uint64_t i2 = 0x0123456789abcdefULL;
  uint32_t i2_res = 2918899159ul;
  EXPECT_EQ(twang_32from64(i2), i2_res);
}

TEST(Hash, Jenkins_Rev_Mix32) {
  uint32_t i1 = 3805486511ul;
  uint32_t i1_res = 381808021ul;
  EXPECT_EQ(i1_res, jenkins_rev_mix32(i1));
  EXPECT_EQ(i1, jenkins_rev_unmix32(i1_res));

  uint32_t i2 = 2309737967ul;
  uint32_t i2_res = 1834777923ul;
  EXPECT_EQ(i2_res, jenkins_rev_mix32(i2));
  EXPECT_EQ(i2, jenkins_rev_unmix32(i2_res));
}

namespace {
void checkJenkins(uint32_t r) {
  uint32_t result = jenkins_rev_mix32(r);
  EXPECT_EQ(r, jenkins_rev_unmix32(result));
}
}  // namespace

TEST(Hash, Jenkins_Rev_Unmix32) {
  // We'll try (1 << i), (1 << i) + 1, (1 << i) - 1
  for (int i = 1; i < 32; i++) {
    checkJenkins((1U << i) - 1);
    checkJenkins(1U << i);
    checkJenkins((1U << i) + 1);
  }
}

TEST(Hash, hasher) {
  // Basically just confirms that things compile ok.
  std::unordered_map<int32_t,int32_t,folly::hasher<int32_t>> m;
  m.insert(std::make_pair(4, 5));
  EXPECT_EQ(get_default(m, 4), 5);
}

// Not a full hasher since only handles one type
class TestHasher {
 public:
  static size_t hash(const std::pair<int, int>& p) {
    return p.first + p.second;
  }
};

template <typename T, typename... Ts>
size_t hash_combine_test(const T& t, const Ts&... ts) {
  return hash_combine_generic<TestHasher>(t, ts...);
}

TEST(Hash, pair) {
  auto a = std::make_pair(1, 2);
  auto b = std::make_pair(3, 4);
  auto c = std::make_pair(1, 2);
  auto d = std::make_pair(2, 1);
  EXPECT_EQ(hash_combine(a),
            hash_combine(c));
  EXPECT_NE(hash_combine(b),
            hash_combine(c));
  EXPECT_NE(hash_combine(d),
            hash_combine(c));

  // With composition
  EXPECT_EQ(hash_combine(a, b),
            hash_combine(c, b));
  // Test order dependence
  EXPECT_NE(hash_combine(a, b),
            hash_combine(b, a));

  // Test with custom hasher
  EXPECT_EQ(hash_combine_test(a),
            hash_combine_test(c));
  // 3 + 4 != 1 + 2
  EXPECT_NE(hash_combine_test(b),
            hash_combine_test(c));
  // This time, thanks to a terrible hash function, these are equal
  EXPECT_EQ(hash_combine_test(d),
            hash_combine_test(c));
  // With composition
  EXPECT_EQ(hash_combine_test(a, b),
            hash_combine_test(c, b));
  // Test order dependence
  EXPECT_NE(hash_combine_test(a, b),
            hash_combine_test(b, a));
  // Again, 1 + 2 == 2 + 1
  EXPECT_EQ(hash_combine_test(a, b),
            hash_combine_test(d, b));
}

TEST(Hash, hash_combine) {
  EXPECT_NE(hash_combine(1, 2), hash_combine(2, 1));
}

TEST(Hash, std_tuple) {
  typedef std::tuple<int64_t, std::string, int32_t> tuple3;
  tuple3 t(42, "foo", 1);

  std::unordered_map<tuple3, std::string> m;
  m[t] = "bar";
  EXPECT_EQ("bar", m[t]);
}

namespace {
template <class T>
size_t hash_vector(const std::vector<T>& v) {
  return hash_range(v.begin(), v.end());
}
}

TEST(Hash, hash_range) {
  EXPECT_EQ(hash_vector<int32_t>({1, 2}), hash_vector<int16_t>({1, 2}));
  EXPECT_NE(hash_vector<int>({2, 1}), hash_vector<int>({1, 2}));
  EXPECT_EQ(hash_vector<int>({}), hash_vector<float>({}));
}

TEST(Hash, std_tuple_different_hash) {
  typedef std::tuple<int64_t, std::string, int32_t> tuple3;
  tuple3 t1(42, "foo", 1);
  tuple3 t2(9, "bar", 3);
  tuple3 t3(42, "foo", 3);

  EXPECT_NE(std::hash<tuple3>()(t1),
            std::hash<tuple3>()(t2));
  EXPECT_NE(std::hash<tuple3>()(t1),
            std::hash<tuple3>()(t3));
}
