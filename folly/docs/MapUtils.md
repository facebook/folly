`folly/container/MapUtil.h`
---------------------------

MapUtil provides convenience functions to get a value from a map.

### Motivation & Comparisons

Fetching a value in a map can be tedious. For example, `map.at()` will throw an exception if the key is not present:
```cpp
try {
    auto& v = m.at(1);
} catch (...) {
    // do something!
}
```

We can avoid the exception handling by using `find`:
```cpp
auto it = m.find(1);
if (it != m.end()) {
    auto& v = it->second;
}
```

Both of these cases can be written more succinctly and without exception handling using folly/MapUtil.h
```cpp
if (const auto* v = folly::get_ptr(m, 1)) {
    // do stuff with `v` which is guaranteed to be present in this block
}
```

We can also easily lookup values in nested maps:
```cpp
std::map<int, std::map<int, int>> m {{1, {{2, 3}}}};
// using find
auto it1 = m.find(1);
if (it1 != m.end()) {
    auto it2 = it1->second.find(2);
    if (it2 != it1->second.end()) {
        auto& v = it2->second;
    }
}
// using folly/MapUtil.h
if (const auto* v = folly::get_ptr(m, 1, 2)) {
    // do stuff with `v`!
}
```

### Sample
The corresponding unit test suite has a very thorough collection of examples: `folly/container/test/MapUtilTest.cpp`,
for brevity, here is a small glimpse:

***
```cpp
std::map<int, int> m {{1, 2}};

// Return a pointer (nullptr if the key is not in the map)
EXPECT_EQ(2, *get_ptr(m, 1));
EXPECT_TRUE(get_ptr(m, 2) == nullptr);

// Return a value (or default)
EXPECT_EQ(2, get_default(m, 1, 42));
EXPECT_EQ(42, get_default(m, 2, 42));

// Return an optional (empty if the key is not in the map)
EXPECT_TRUE(get_optional(m, 1).has_value());
EXPECT_EQ(2, get_optional(m, 1).value());

// Return a reference
const int i = 42;
EXPECT_EQ(2, get_ref_default(m, 1, i));
EXPECT_EQ(42, get_ref_default(m, 2, i));

// Even works for nested maps!
std::map<int, map<int, map<int, map<int, int>>>> nested{{1, {{2, {{3, {{4, 5}}}}}}}};
EXPECT_EQ(5, *get_ptr(nested, 1, 2, 3, 4));
EXPECT_TRUE(get_ptr(nested, 1, 2, 3, 4));
EXPECT_FALSE(get_ptr(nested, 1, 2, 3, 0));
```
***
