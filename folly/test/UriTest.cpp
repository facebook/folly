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

#include "folly/Uri.h"

#include <boost/algorithm/string.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;

namespace {

}  // namespace

TEST(Uri, Simple) {
  {
    fbstring s("http://www.facebook.com/hello/world?query#fragment");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("www.facebook.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("www.facebook.com", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("fragment", u.fragment());
    EXPECT_EQ(s, u.fbstr());  // canonical
  }

  {
    fbstring s("http://www.facebook.com:8080/hello/world?query#fragment");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("www.facebook.com", u.host());
    EXPECT_EQ(8080, u.port());
    EXPECT_EQ("www.facebook.com:8080", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("fragment", u.fragment());
    EXPECT_EQ(s, u.fbstr());  // canonical
  }

  {
    fbstring s("http://127.0.0.1:8080/hello/world?query#fragment");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("127.0.0.1", u.host());
    EXPECT_EQ(8080, u.port());
    EXPECT_EQ("127.0.0.1:8080", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("fragment", u.fragment());
    EXPECT_EQ(s, u.fbstr());  // canonical
  }

  {
    fbstring s("http://[::1]:8080/hello/world?query#fragment");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("[::1]", u.host());
    EXPECT_EQ(8080, u.port());
    EXPECT_EQ("[::1]:8080", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("fragment", u.fragment());
    EXPECT_EQ(s, u.fbstr());  // canonical
  }

  {
    fbstring s("http://user:pass@host.com/");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("user", u.username());
    EXPECT_EQ("pass", u.password());
    EXPECT_EQ("host.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("user:pass@host.com", u.authority());
    EXPECT_EQ("/", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ(s, u.fbstr());
  }

  {
    fbstring s("http://user@host.com/");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("user", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("host.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("user@host.com", u.authority());
    EXPECT_EQ("/", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ(s, u.fbstr());
  }

  {
    fbstring s("http://user:@host.com/");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("user", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("host.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("user@host.com", u.authority());
    EXPECT_EQ("/", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ("http://user@host.com/", u.fbstr());
  }

  {
    fbstring s("http://:pass@host.com/");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("pass", u.password());
    EXPECT_EQ("host.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ(":pass@host.com", u.authority());
    EXPECT_EQ("/", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ(s, u.fbstr());
  }

  {
    fbstring s("http://@host.com/");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("host.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("host.com", u.authority());
    EXPECT_EQ("/", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ("http://host.com/", u.fbstr());
  }

  {
    fbstring s("http://:@host.com/");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("host.com", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("host.com", u.authority());
    EXPECT_EQ("/", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ("http://host.com/", u.fbstr());
  }

  {
    fbstring s("file:///etc/motd");
    Uri u(s);
    EXPECT_EQ("file", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("", u.authority());
    EXPECT_EQ("/etc/motd", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ(s, u.fbstr());
  }

  {
    fbstring s("file:/etc/motd");
    Uri u(s);
    EXPECT_EQ("file", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("", u.authority());
    EXPECT_EQ("/etc/motd", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ("file:///etc/motd", u.fbstr());
  }

  {
    fbstring s("file://etc/motd");
    Uri u(s);
    EXPECT_EQ("file", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("etc", u.host());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("etc", u.authority());
    EXPECT_EQ("/motd", u.path());
    EXPECT_EQ("", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ(s, u.fbstr());
  }

  {
    fbstring s("2http://www.facebook.com");

    try {
      Uri u(s);
      CHECK(false) << "Control should not have reached here";
    } catch (const std::invalid_argument& ex) {
      EXPECT_TRUE(boost::algorithm::ends_with(ex.what(), s));
    }
  }

  {
    fbstring s("www[facebook]com");

    try {
      Uri u("http://" + s);
      CHECK(false) << "Control should not have reached here";
    } catch (const std::invalid_argument& ex) {
      EXPECT_TRUE(boost::algorithm::ends_with(ex.what(), s));
    }
  }
}
