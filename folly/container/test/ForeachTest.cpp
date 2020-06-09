/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/container/Foreach.h>

#include <array>
#include <initializer_list>
#include <iterator>
#include <list>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <folly/portability/GTest.h>

using namespace folly;

namespace folly {
namespace test {

class TestRValueConstruct {
 public:
  TestRValueConstruct() = default;
  TestRValueConstruct(TestRValueConstruct&&) noexcept {
    this->constructed_from_rvalue = true;
  }
  TestRValueConstruct(const TestRValueConstruct&) {
    this->constructed_from_rvalue = false;
  }
  TestRValueConstruct& operator=(const TestRValueConstruct&) = delete;
  TestRValueConstruct& operator=(TestRValueConstruct&&) = delete;

  bool constructed_from_rvalue{false};
};

class TestAdlIterable {
 public:
  std::vector<int> vec{0, 1, 2, 3};
};

auto begin(TestAdlIterable& instance) {
  return instance.vec.begin();
}
auto begin(const TestAdlIterable& instance) {
  return instance.vec.begin();
}
auto end(TestAdlIterable& instance) {
  return instance.vec.end();
}
auto end(const TestAdlIterable& instance) {
  return instance.vec.end();
}

class TestBothIndexingAndIter {
 public:
  class Iterator {
   public:
    using difference_type = std::size_t;
    using value_type = int;
    using pointer = int*;
    using reference = int&;
    using iterator_category = std::random_access_iterator_tag;
    int& operator*() {
      return this->val;
    }
    Iterator operator+(int) {
      return *this;
    }
    explicit Iterator(int& val_in) : val{val_in} {}
    int& val;
  };
  auto begin() {
    this->called_begin = true;
    return Iterator{val};
  }
  auto end() {
    return Iterator{val};
  }
  int& operator[](int) {
    return this->val;
  }

  int val{0};
  bool called_begin = false;
};
} // namespace test
} // namespace folly

TEST(Foreach, ForEachFunctionBasic) {
  auto range = std::make_tuple(1, 2, 3);
  auto result_range = std::vector<int>{};
  auto correct_result_range = std::vector<int>{1, 2, 3};

  folly::for_each(range, [&](auto ele) { result_range.push_back(ele); });

  EXPECT_TRUE(std::equal(
      result_range.begin(), result_range.end(), correct_result_range.begin()));
}

TEST(Foreach, ForEachFunctionBasicRuntimeOneArg) {
  auto range = std::vector<int>{1, 2, 3};
  auto current = 0;
  folly::for_each(range, [&](auto ele) {
    if (current == 0) {
      EXPECT_EQ(ele, 1);
    } else if (current == 1) {
      EXPECT_EQ(ele, 2);
    } else {
      EXPECT_EQ(ele, 3);
    }
    ++current;
  });
}

TEST(Foreach, ForEachFunctionBasicRuntimeTwoArg) {
  auto range = std::vector<int>{1, 2, 3};
  folly::for_each(range, [](auto ele, auto index) {
    EXPECT_TRUE(index < 3);
    if (index == 0) {
      EXPECT_EQ(ele, 1);
    } else if (index == 1) {
      EXPECT_EQ(ele, 2);
    } else if (index == 2) {
      EXPECT_EQ(ele, 3);
    }
  });
}

TEST(Foreach, ForEachFunctionBasicRuntimeThreeArg) {
  auto range = std::list<int>{1, 2, 3};
  auto result_range = std::list<int>{1, 3};
  folly::for_each(range, [&](auto ele, auto, auto iter) {
    if (ele == 2) {
      range.erase(iter);
    }
  });
  EXPECT_TRUE(std::equal(range.begin(), range.end(), result_range.begin()));
}

TEST(Foreach, ForEachFunctionBasicTupleOneArg) {
  auto range = std::make_tuple(1, 2, 3);
  auto current = 0;
  folly::for_each(range, [&](auto ele) {
    if (current == 0) {
      EXPECT_EQ(ele, 1);
    } else if (current == 1) {
      EXPECT_EQ(ele, 2);
    } else {
      EXPECT_EQ(ele, 3);
    }
    ++current;
  });
}

TEST(Foreach, ForEachFunctionBasicTupleTwoArg) {
  auto range = std::make_tuple(1, 2, 3);
  folly::for_each(range, [](auto ele, auto index) {
    EXPECT_TRUE(index < 3);
    if (index == 0) {
      EXPECT_EQ(ele, 1);
    } else if (index == 1) {
      EXPECT_EQ(ele, 2);
    } else if (index == 2) {
      EXPECT_EQ(ele, 3);
    }
  });
}

TEST(Foreach, ForEachFunctionBreakRuntimeOneArg) {
  auto range = std::vector<int>{1, 2, 3};
  auto iterations = 0;
  folly::for_each(range, [&](auto) {
    ++iterations;
    if (iterations == 1) {
      return folly::loop_break;
    }
    return folly::loop_continue;
  });
  EXPECT_EQ(iterations, 1);
}

TEST(Foreach, ForEachFunctionBreakRuntimeTwoArg) {
  auto range = std::vector<int>{1, 2, 3};
  auto iterations = 0;
  folly::for_each(range, [&](auto, auto index) {
    ++iterations;
    if (index == 1) {
      return folly::loop_break;
    }
    return folly::loop_continue;
  });
  EXPECT_EQ(iterations, 2);
}

TEST(Foreach, ForEachFunctionBreakRuntimeThreeArg) {
  auto range = std::vector<int>{1, 2, 3};
  auto iterations = 0;
  folly::for_each(range, [&](auto, auto index, auto) {
    ++iterations;
    if (index == 1) {
      return folly::loop_break;
    }
    return folly::loop_continue;
  });
  EXPECT_EQ(iterations, 2);
}

TEST(Foreach, ForEachFunctionBreakTupleOneArg) {
  auto range = std::vector<int>{1, 2, 3};
  auto iterations = 0;
  folly::for_each(range, [&](auto) {
    ++iterations;
    if (iterations == 1) {
      return folly::loop_break;
    }
    return folly::loop_continue;
  });
  EXPECT_EQ(iterations, 1);
}

TEST(Foreach, ForEachFunctionBreakTupleTwoArg) {
  auto range = std::vector<int>{1, 2, 3};
  auto iterations = 0;
  folly::for_each(range, [&](auto, auto index) {
    ++iterations;
    if (index == 1) {
      return folly::loop_break;
    }
    return folly::loop_continue;
  });
  EXPECT_EQ(iterations, 2);
}

TEST(Foreach, ForEachFunctionArray) {
  auto range = std::array<int, 3>{{1, 2, 3}};
  auto iterations = 0;
  folly::for_each(range, [&](auto, auto index) {
    ++iterations;
    if (index == 1) {
      return folly::loop_break;
    }
    return folly::loop_continue;
  });
  EXPECT_EQ(iterations, 2);
}

TEST(Foreach, ForEachFunctionInitializerListBasic) {
  folly::for_each(std::initializer_list<int>{1, 2, 3}, [](auto ele) { ++ele; });
}

TEST(Foreach, ForEachFunctionTestForward) {
  using folly::test::TestRValueConstruct;
  auto range_one = std::vector<TestRValueConstruct>{};
  range_one.resize(3);

  folly::for_each(std::move(range_one), [](auto ele) {
    EXPECT_FALSE(ele.constructed_from_rvalue);
  });

  folly::for_each(
      std::make_tuple(TestRValueConstruct{}, TestRValueConstruct{}),
      [](auto ele) { EXPECT_TRUE(ele.constructed_from_rvalue); });
}

TEST(Foreach, ForEachFunctionAdlIterable) {
  auto range = test::TestAdlIterable{};
  auto iterations = 0;
  folly::for_each(range, [&](auto ele, auto index) {
    ++iterations;
    EXPECT_EQ(ele, index);
  });
  EXPECT_EQ(iterations, 4);
}

TEST(ForEach, FetchRandomAccessIterator) {
  auto vec = std::vector<int>{1, 2, 3};
  auto& second = folly::fetch(vec, 1);
  EXPECT_EQ(second, 2);
  second = 3;
  EXPECT_EQ(second, 3);
}

TEST(ForEach, FetchIndexing) {
  auto mp = std::map<int, int>{{1, 2}};
  auto& ele = folly::fetch(mp, 1);
  EXPECT_EQ(ele, 2);
  ele = 3;
  EXPECT_EQ(ele, 3);
}

TEST(ForEach, FetchTuple) {
  auto mp = std::make_tuple(1, 2, 3);
  auto& ele = folly::fetch(mp, std::integral_constant<int, 1>{});
  EXPECT_EQ(ele, 2);
  ele = 3;
  EXPECT_EQ(ele, 3);
}

TEST(ForEach, FetchTestPreferIterator) {
  auto range = test::TestBothIndexingAndIter{};
  auto& ele = folly::fetch(range, 0);
  EXPECT_TRUE(range.called_begin);
  EXPECT_EQ(ele, 0);
  ele = 2;
  EXPECT_EQ(folly::fetch(range, 0), 2);
}

TEST(Foreach, ForEachRvalue) {
  const char* const hello = "hello";
  int n = 0;
  FOR_EACH (it, std::string(hello)) { ++n; }
  EXPECT_EQ(strlen(hello), n);
  FOR_EACH_R (it, std::string(hello)) {
    --n;
    EXPECT_EQ(hello[n], *it);
  }
  EXPECT_EQ(0, n);
}

TEST(Foreach, ForEachNested) {
  const std::string hello = "hello";
  size_t n = 0;
  FOR_EACH (i, hello) {
    FOR_EACH (j, hello) { ++n; }
  }
  auto len = hello.size();
  EXPECT_EQ(len * len, n);
}
