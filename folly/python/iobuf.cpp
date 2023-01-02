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

#include <folly/python/iobuf.h>

#include <folly/python/import.h>
#include <folly/python/iobuf_api.h> // @manual

namespace folly::python {

FOLLY_CONSTINIT static python::import_cache import_folly__iobuf_{
    import_folly__iobuf, "import_folly__iobuf"};

folly::IOBuf iobuf_from_python_iobuf(PyObject* iobuf) {
  import_folly__iobuf_();
  return from_python_iobuf(iobuf);
}

} // namespace folly::python
