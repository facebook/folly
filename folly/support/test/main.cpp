/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define FOLLY_F14_PERTURB_INSERTION_ORDER 0

#include <folly/IPAddress.h>
#include <folly/Range.h>
#include <folly/SocketAddress.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/json/dynamic.h>
#include <folly/small_vector.h>
#include <folly/support/test/GdbUtil.h>

#pragma GCC diagnostic ignored "-Wunused-variable"

int main() {
  using namespace folly;

  // FBString
  fbstring empty = "";
  fbstring small = "small";
  fbstring maxsmall = "12345678901234567890123";
  fbstring minmed = "123456789012345678901234";
  fbstring large =
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456"
      "abcdefghijklmnopqrstuvwxyz123456";

  // StringPiece
  auto emptypiece = StringPiece("");
  auto otherpiece = StringPiece("strings. Strings! STRINGS!!");

  // Range
  std::array<int, 6> nums = {{1, 2, 3, 4, 5, 6}};
  auto num_range = Range<const int*>(nums);

  // Dynamic
  dynamic dynamic_null = nullptr;
  dynamic dynamic_array = dynamic::array("A string", 1, 2, 3, 4, 5);
  dynamic dynamic_bool = true;
  dynamic dynamic_double = 0.25;
  dynamic dynamic_int64 = 8675309;
  dynamic dynamic_string = "Hi!";
  dynamic dynamic_object = dynamic::object;
  dynamic_object["one"] = "two";
  dynamic_object["eight"] = "ten";

  // IPAddress
  auto ipv4 = IPAddress("0.0.0.0");
  auto ipv6 = IPAddress("2a03:2880:fffe:c:face:b00c:0:35");

  // SocketAddress
  auto ipv4socket = SocketAddress("127.0.0.1", 8080);
  auto ipv6socket = SocketAddress("2a03:2880:fffe:c:face:b00c:0:35", 8080);

  // F14 containers
  F14NodeMap<std::string, int> m_node = {{"foo", 0}, {"bar", 1}, {"baz", 2}};
  F14ValueMap<std::string, int> m_val = {{"foo", 0}};
  F14VectorMap<std::string, int> m_vec = {{"foo", 0}, {"bar", 1}};
  F14FastMap<int, std::string> m_fvec = {{42, "foo"}, {43, "bar"}, {44, "baz"}};
  F14FastMap<int, int> m_fval = {{9, 0}, {8, 1}, {7, 2}};

  F14NodeSet<std::string> s_node = {"foo", "bar", "baz"};
  F14NodeSet<int> s_node_large;
  for (auto i = 0; i < 20; ++i) {
    s_node_large.emplace(i);
  }
  F14ValueSet<std::string> s_val = {"foo", "bar", "baz"};
  F14ValueSet<uint32_t> s_val_i;
  for (uint32_t i = 0; i < 20; ++i) {
    s_val_i.emplace(i);
  }
  F14VectorSet<std::string> s_vec = {"foo", "bar", "baz"};
  F14FastSet<std::string> s_fvec = {"foo", "bar", "baz"};
  F14FastSet<int> s_fval = {42, 43, 44};
  typedef F14FastSet<int> F14FastSetTypedef;
  F14FastSetTypedef s_fval_typedef = {45, 46, 47};

  const F14FastSet<int>& const_ref = s_fval;

  small_vector<uint32_t, 7> smol_vec_inline = {1, 2, 3};
  small_vector<std::string, 1> smol_vec_outline = {"four", "five"};

  asm_gdb_breakpoint();

  return 0;
}
