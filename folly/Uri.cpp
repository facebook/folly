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

#include <folly/Uri.h>

#include <algorithm>
#include <cctype>

#include <boost/regex.hpp>

namespace folly {

namespace {

std::string submatch(const boost::cmatch& m, int idx) {
  const auto& sub = m[idx];
  return std::string(sub.first, sub.second);
}

} // namespace

Uri::Uri(StringPiece str) : hasAuthority_(false), port_(0) {
  static const boost::regex uriRegex(
      "([a-zA-Z][a-zA-Z0-9+.-]*):" // scheme:
      "([^?#]*)" // authority and path
      "(?:\\?([^#]*))?" // ?query
      "(?:#(.*))?"); // #fragment
  static const boost::regex authorityAndPathRegex("//([^/]*)(/.*)?");

  boost::cmatch match;
  if (UNLIKELY(!boost::regex_match(str.begin(), str.end(), match, uriRegex))) {
    throw std::invalid_argument(to<std::string>("invalid URI ", str));
  }

  scheme_ = submatch(match, 1);
  std::transform(scheme_.begin(), scheme_.end(), scheme_.begin(), ::tolower);

  StringPiece authorityAndPath(match[2].first, match[2].second);
  boost::cmatch authorityAndPathMatch;
  if (!boost::regex_match(
          authorityAndPath.begin(),
          authorityAndPath.end(),
          authorityAndPathMatch,
          authorityAndPathRegex)) {
    // Does not start with //, doesn't have authority
    hasAuthority_ = false;
    path_ = authorityAndPath.str();
  } else {
    static const boost::regex authorityRegex(
        "(?:([^@:]*)(?::([^@]*))?@)?" // username, password
        "(\\[[^\\]]*\\]|[^\\[:]*)" // host (IP-literal (e.g. '['+IPv6+']',
                                   // dotted-IPv4, or named host)
        "(?::(\\d*))?"); // port

    const auto authority = authorityAndPathMatch[1];
    boost::cmatch authorityMatch;
    if (!boost::regex_match(
            authority.first,
            authority.second,
            authorityMatch,
            authorityRegex)) {
      throw std::invalid_argument(to<std::string>(
          "invalid URI authority ",
          StringPiece(authority.first, authority.second)));
    }

    StringPiece port(authorityMatch[4].first, authorityMatch[4].second);
    if (!port.empty()) {
      try {
        port_ = to<uint16_t>(port);
      } catch (ConversionError const& e) {
        throw std::invalid_argument(
            to<std::string>("invalid URI port: ", e.what()));
      }
    }

    hasAuthority_ = true;
    username_ = submatch(authorityMatch, 1);
    password_ = submatch(authorityMatch, 2);
    host_ = submatch(authorityMatch, 3);
    path_ = submatch(authorityAndPathMatch, 2);
  }

  query_ = submatch(match, 3);
  fragment_ = submatch(match, 4);
}

std::string Uri::authority() const {
  std::string result;

  // Port is 5 characters max and we have up to 3 delimiters.
  result.reserve(host().size() + username().size() + password().size() + 8);

  if (!username().empty() || !password().empty()) {
    result.append(username());

    if (!password().empty()) {
      result.push_back(':');
      result.append(password());
    }

    result.push_back('@');
  }

  result.append(host());

  if (port() != 0) {
    result.push_back(':');
    toAppend(port(), &result);
  }

  return result;
}

std::string Uri::hostname() const {
  if (!host_.empty() && host_[0] == '[') {
    // If it starts with '[', then it should end with ']', this is ensured by
    // regex
    return host_.substr(1, host_.size() - 2);
  }
  return host_;
}

const std::vector<std::pair<std::string, std::string>>& Uri::getQueryParams() {
  if (!query_.empty() && queryParams_.empty()) {
    // Parse query string
    static const boost::regex queryParamRegex(
        "(^|&)" /*start of query or start of parameter "&"*/
        "([^=&]*)=?" /*parameter name and "=" if value is expected*/
        "([^=&]*)" /*parameter value*/
        "(?=(&|$))" /*forward reference, next should be end of query or
                      start of next parameter*/);
    const boost::cregex_iterator paramBeginItr(
        query_.data(), query_.data() + query_.size(), queryParamRegex);
    boost::cregex_iterator paramEndItr;
    for (auto itr = paramBeginItr; itr != paramEndItr; ++itr) {
      if (itr->length(2) == 0) {
        // key is empty, ignore it
        continue;
      }
      queryParams_.emplace_back(
          std::string((*itr)[2].first, (*itr)[2].second), // parameter name
          std::string((*itr)[3].first, (*itr)[3].second) // parameter value
      );
    }
  }
  return queryParams_;
}

} // namespace folly
