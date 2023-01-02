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

#include <folly/python/test/IOBufTestUtils.h>

#include <algorithm>
#include <cctype>

#include <folly/python/iobuf.h>

namespace folly::python {

std::string to_uppercase_string_cpp(PyObject* o_iobuf) {
  auto iobuf = iobuf_from_python_iobuf(o_iobuf);

  std::string s;
  iobuf.appendTo(s);
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  return s;
}

} // namespace folly::python
