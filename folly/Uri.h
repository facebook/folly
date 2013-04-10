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

#ifndef FOLLY_URI_H_
#define FOLLY_URI_H_

#include "folly/String.h"

namespace folly {

/**
 * Class representing a URI.
 *
 * Consider http://www.facebook.com/foo/bar?key=foo#anchor
 *
 * The URI is broken down into its parts: scheme ("http"), authority
 * (ie. host and port, in most cases: "www.facebook.com"), path
 * ("/foo/bar"), query ("key=foo") and fragment ("anchor").  The scheme is
 * lower-cased.
 *
 * If this Uri represents a URL, note that, to prevent ambiguity, the component
 * parts are NOT percent-decoded; you should do this yourself with
 * uriUnescape() (for the authority and path) and uriUnescape(...,
 * UriEscapeMode::QUERY) (for the query, but probably only after splitting at
 * '&' to identify the individual parameters).
 */
class Uri {
 public:
  /**
   * Parse a Uri from a string.  Throws std::invalid_argument on parse error.
   */
  explicit Uri(StringPiece str);

  const fbstring& scheme() const { return scheme_; }
  const fbstring& username() const { return username_; }
  const fbstring& password() const { return password_; }
  const fbstring& host() const { return host_; }
  uint32_t port() const { return port_; }
  const fbstring& path() const { return path_; }
  const fbstring& query() const { return query_; }
  const fbstring& fragment() const { return fragment_; }

  template <class String>
  String toString() const;

  std::string str() const { return toString<std::string>(); }
  fbstring fbstr() const { return toString<fbstring>(); }

 private:
  fbstring scheme_;
  fbstring username_;
  fbstring password_;
  fbstring host_;
  uint32_t port_;
  fbstring path_;
  fbstring query_;
  fbstring fragment_;
};

}  // namespace folly

#include "folly/Uri-inl.h"

#endif /* FOLLY_URI_H_ */
