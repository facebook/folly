/*
 * Copyright 2013 Facebook, Inc.
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

#include <ctype.h>
#include <boost/regex.hpp>

namespace folly {

namespace {

fbstring submatch(const boost::cmatch& m, size_t idx) {
  auto& sub = m[idx];
  return fbstring(sub.first, sub.second);
}

template <class String>
void toLower(String& s) {
  for (auto& c : s) {
    c = tolower(c);
  }
}

}  // namespace

Uri::Uri(StringPiece str) : port_(0) {
  static const boost::regex uriRegex(
      "([a-zA-Z][a-zA-Z0-9+.-]*):"  // scheme:
      "([^?#]*)"                    // authority and path
      "(?:\\?([^#]*))?"             // ?query
      "(?:#(.*))?");                // #fragment
  static const boost::regex authorityAndPathRegex("//([^/]*)(/.*)?");

  boost::cmatch match;
  if (UNLIKELY(!boost::regex_match(str.begin(), str.end(), match, uriRegex))) {
    throw std::invalid_argument("invalid URI");
  }

  scheme_ = submatch(match, 1);
  toLower(scheme_);

  StringPiece authorityAndPath(match[2].first, match[2].second);
  boost::cmatch authorityAndPathMatch;
  if (!boost::regex_match(authorityAndPath.begin(),
                          authorityAndPath.end(),
                          authorityAndPathMatch,
                          authorityAndPathRegex)) {
    // Does not start with //, doesn't have authority
    path_ = authorityAndPath.fbstr();
  } else {
    static const boost::regex authorityRegex(
        "(?:([^@:]*)(?::([^@]*))?@)?"  // username, password
        "(\\[[^\\]]*\\]|[^\\[:]*)"     // host (IP-literal, dotted-IPv4, or
                                       // named host)
        "(?::(\\d*))?");               // port

    auto authority = authorityAndPathMatch[1];
    boost::cmatch authorityMatch;
    if (!boost::regex_match(authority.first,
                            authority.second,
                            authorityMatch,
                            authorityRegex)) {
      throw std::invalid_argument("invalid URI authority");
    }

    StringPiece port(authorityMatch[4].first, authorityMatch[4].second);
    if (!port.empty()) {
      port_ = to<uint32_t>(port);
    }

    username_ = submatch(authorityMatch, 1);
    password_ = submatch(authorityMatch, 2);
    host_ = submatch(authorityMatch, 3);
    path_ = submatch(authorityAndPathMatch, 2);
  }

  query_ = submatch(match, 3);
  fragment_ = submatch(match, 4);
}

}  // namespace folly
