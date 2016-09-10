/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Uri.h>
#include <folly/Benchmark.h>
#include <folly/portability/GTest.h>

#include <boost/algorithm/string.hpp>
#include <glog/logging.h>
#include <map>

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
    EXPECT_EQ("::1", u.hostname());
    EXPECT_EQ(8080, u.port());
    EXPECT_EQ("[::1]:8080", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("fragment", u.fragment());
    EXPECT_EQ(s, u.fbstr());  // canonical
  }

  {
    fbstring s("http://[2401:db00:20:7004:face:0:29:0]:8080/hello/world?query");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("[2401:db00:20:7004:face:0:29:0]", u.host());
    EXPECT_EQ("2401:db00:20:7004:face:0:29:0", u.hostname());
    EXPECT_EQ(8080, u.port());
    EXPECT_EQ("[2401:db00:20:7004:face:0:29:0]:8080", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("", u.fragment());
    EXPECT_EQ(s, u.fbstr());  // canonical
  }

  {
    fbstring s("http://[2401:db00:20:7004:face:0:29:0]/hello/world?query");
    Uri u(s);
    EXPECT_EQ("http", u.scheme());
    EXPECT_EQ("", u.username());
    EXPECT_EQ("", u.password());
    EXPECT_EQ("[2401:db00:20:7004:face:0:29:0]", u.host());
    EXPECT_EQ("2401:db00:20:7004:face:0:29:0", u.hostname());
    EXPECT_EQ(0, u.port());
    EXPECT_EQ("[2401:db00:20:7004:face:0:29:0]", u.authority());
    EXPECT_EQ("/hello/world", u.path());
    EXPECT_EQ("query", u.query());
    EXPECT_EQ("", u.fragment());
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
    EXPECT_EQ("file:/etc/motd", u.fbstr());
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
    // test query parameters
    fbstring s("http://localhost?&key1=foo&key2=&key3&=bar&=bar=&");
    Uri u(s);
    auto paramsList = u.getQueryParams();
    std::map<fbstring, fbstring> params;
    for (auto& param : paramsList) {
      params[param.first] = param.second;
    }
    EXPECT_EQ(3, params.size());
    EXPECT_EQ("foo", params["key1"]);
    EXPECT_NE(params.end(), params.find("key2"));
    EXPECT_EQ("", params["key2"]);
    EXPECT_NE(params.end(), params.find("key3"));
    EXPECT_EQ("", params["key3"]);
  }

  {
    // test query parameters
    fbstring s("http://localhost?&&&&&&&&&&&&&&&");
    Uri u(s);
    auto params = u.getQueryParams();
    EXPECT_TRUE(params.empty());
  }

  {
    // test query parameters
    fbstring s("http://localhost?&=invalid_key&key2&key3=foo");
    Uri u(s);
    auto paramsList = u.getQueryParams();
    std::map<fbstring, fbstring> params;
    for (auto& param : paramsList) {
      params[param.first] = param.second;
    }
    EXPECT_EQ(2, params.size());
    EXPECT_NE(params.end(), params.find("key2"));
    EXPECT_EQ("", params["key2"]);
    EXPECT_EQ("foo", params["key3"]);
  }

  {
    // test query parameters
    fbstring s("http://localhost?&key1=====&&=key2&key3=");
    Uri u(s);
    auto paramsList = u.getQueryParams();
    std::map<fbstring, fbstring> params;
    for (auto& param : paramsList) {
      params[param.first] = param.second;
    }
    EXPECT_EQ(1, params.size());
    EXPECT_NE(params.end(), params.find("key3"));
    EXPECT_EQ("", params["key3"]);
  }

  {
    // test query parameters
    fbstring s("http://localhost?key1=foo=bar&key2=foobar&");
    Uri u(s);
    auto paramsList = u.getQueryParams();
    std::map<fbstring, fbstring> params;
    for (auto& param : paramsList) {
      params[param.first] = param.second;
    }
    EXPECT_EQ(1, params.size());
    EXPECT_EQ("foobar", params["key2"]);
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

  {
    fbstring s("http://[::1:8080/hello/world?query#fragment");

    try {
      Uri u(s);
      CHECK(false) << "Control should not have reached here";
    } catch (const std::invalid_argument& ex) {
      // success
    }
  }

  {
    fbstring s("http://::1]:8080/hello/world?query#fragment");

    try {
      Uri u(s);
      CHECK(false) << "Control should not have reached here";
    } catch (const std::invalid_argument& ex) {
      // success
    }
  }

  {
    fbstring s("http://::1:8080/hello/world?query#fragment");

    try {
      Uri u(s);
      CHECK(false) << "Control should not have reached here";
    } catch (const std::invalid_argument& ex) {
      // success
    }
  }

  {
    fbstring s("http://2401:db00:20:7004:face:0:29:0/hello/world?query");

    try {
      Uri u(s);
      CHECK(false) << "Control should not have reached here";
    } catch (const std::invalid_argument& ex) {
      // success
    }
  }

  // No authority (no "//") is valid
  {
    fbstring s("this:is/a/valid/uri");
    Uri u(s);
    EXPECT_EQ("this", u.scheme());
    EXPECT_EQ("is/a/valid/uri", u.path());
    EXPECT_EQ(s, u.fbstr());
  }
  {
    fbstring s("this:is:another:valid:uri");
    Uri u(s);
    EXPECT_EQ("this", u.scheme());
    EXPECT_EQ("is:another:valid:uri", u.path());
    EXPECT_EQ(s, u.fbstr());
  }
  {
    fbstring s("this:is@another:valid:uri");
    Uri u(s);
    EXPECT_EQ("this", u.scheme());
    EXPECT_EQ("is@another:valid:uri", u.path());
    EXPECT_EQ(s, u.fbstr());
  }
}

/**
 * Result of benchmark varies by the complexity of query.
 * ============================================================================
 * folly/test/UriTest.cpp                          relative  time/iter  iters/s
 * ============================================================================
 * init_uri_simple                                              4.88us  204.80K
 * init_uri_simple_with_query_parsing                          22.46us   44.52K
 * init_uri_complex                                             5.92us  168.85K
 * init_uri_complex_with_query_parsing                         48.70us   20.53K
 * ============================================================================
 */
BENCHMARK(init_uri_simple, iters) {
  const fbstring s("http://localhost?&key1=foo&key2=&key3&=bar&=bar=&");
  for (size_t i = 0; i < iters; ++i) {
    Uri u(s);
  }
}

BENCHMARK(init_uri_simple_with_query_parsing, iters) {
  const fbstring s("http://localhost?&key1=foo&key2=&key3&=bar&=bar=&");
  for (size_t i = 0; i < iters; ++i) {
    Uri u(s);
    u.getQueryParams();
  }
}

BENCHMARK(init_uri_complex, iters) {
  const fbstring s(
      "https://mock.example.com/farm/track.php?TmOxQUDF=uSmTS_VwhjKnh_JME&DI"
      "h=fbbN&GRsoIm=bGshjaUqavZxQai&UMT=36k18N4dn21&3U=CD8o4A4497W152j6m0V%14"
      "%57&Hy=t%05mpr.80JUZ7ne_%23zS8DcA%0qc_%291ymamz096%11Zfb3r%09ZqPD%311ZX"
      "tqJd600ot&5U96U-Rh-VZ=-D_6-9xKYj%1gW6b43s1B9-j21P0oUW5-t46G4kgt&ezgj=mcW"
      "TTQ.c&Oh=%2PblUfuC%7C997048884827569%03xnyJ%2L1pi7irBioQ6D4r7nNHNdo6v7Y%"
      "84aurnSJ%2wCFePHMlGZmIHGfCe7392_lImWsSvN&sBeNN=Nf%80yOE%6X10M64F4gG197aX"
      "R2B4g2533x235A0i4e%57%58uWB%04Erw.60&VMS4=Ek_%02GC0Pkx%6Ov_%207WICUz007%"
      "04nYX8N%46zzpv%999h&KGmBt988y=q4P57C-Dh-Nz-x_7-5oPxz%1gz3N03t6c7-R67N4DT"
      "Y6-f98W1&Lts&%02dOty%8eEYEnLz4yexQQLnL4MGU2JFn3OcmXcatBcabZgBdDdy67hdgW"
      "tYn4");
  for (size_t i = 0; i < iters; ++i) {
    Uri u(s);
  }
}

BENCHMARK(init_uri_complex_with_query_parsing, iters) {
  const fbstring s(
      "https://mock.example.com/farm/track.php?TmOxQUDF=uSmTS_VwhjKnh_JME&DI"
      "h=fbbN&GRsoIm=bGshjaUqavZxQai&UMT=36k18N4dn21&3U=CD8o4A4497W152j6m0V%14"
      "%57&Hy=t%05mpr.80JUZ7ne_%23zS8DcA%0qc_%291ymamz096%11Zfb3r%09ZqPD%311ZX"
      "tqJd600ot&5U96U-Rh-VZ=-D_6-9xKYj%1gW6b43s1B9-j21P0oUW5-t46G4kgt&ezgj=mcW"
      "TTQ.c&Oh=%2PblUfuC%7C997048884827569%03xnyJ%2L1pi7irBioQ6D4r7nNHNdo6v7Y%"
      "84aurnSJ%2wCFePHMlGZmIHGfCe7392_lImWsSvN&sBeNN=Nf%80yOE%6X10M64F4gG197aX"
      "R2B4g2533x235A0i4e%57%58uWB%04Erw.60&VMS4=Ek_%02GC0Pkx%6Ov_%207WICUz007%"
      "04nYX8N%46zzpv%999h&KGmBt988y=q4P57C-Dh-Nz-x_7-5oPxz%1gz3N03t6c7-R67N4DT"
      "Y6-f98W1&Lts&%02dOty%8eEYEnLz4yexQQLnL4MGU2JFn3OcmXcatBcabZgBdDdy67hdgW"
      "tYn4");
  for (size_t i = 0; i < iters; ++i) {
    Uri u(s);
    u.getQueryParams();
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto r = RUN_ALL_TESTS();
  if (r) {
    return r;
  }
  runBenchmarks();
  return 0;
}
