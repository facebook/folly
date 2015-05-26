/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

namespace folly {

struct dn_char_traits : public std::char_traits<char> {
  static bool eq(char c1, char c2) {
    return ::tolower(c1) == ::tolower(c2);
  }

  static bool ne(char c1, char c2) {
    return ::tolower(c1) != ::tolower(c2);
  }

  static bool lt(char c1, char c2) {
    return ::tolower(c1) < ::tolower(c2);
  }

  static int compare(const char* s1, const char* s2, size_t n) {
    while (n--) {
      if(::tolower(*s1) < ::tolower(*s2) ) {
        return -1;
      }
      if(::tolower(*s1) > ::tolower(*s2) ) {
        return 1;
      }
      ++s1;
      ++s2;
    }
    return 0;
  }

  static const char* find(const char* s, size_t n, char a) {
    char la = ::tolower(a);
    while (n--) {
      if(::tolower(*s) == la) {
        return s;
      } else {
        ++s;
      }
    }
    return nullptr;
  }
};

// Case insensitive string
typedef std::basic_string<char, dn_char_traits> DNString;

struct DNStringHash : public std::hash<std::string> {
  size_t operator()(const DNString& s1) const noexcept {
    std::string s2(s1.data(), s1.size());
    for (char& c : s2)
      c = ::tolower(c);
    return std::hash<std::string>()(s2);
  }
};

} // namespace
