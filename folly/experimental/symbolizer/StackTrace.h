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

#ifndef FOLLY_SYMBOLIZER_STACKTRACE_H_
#define FOLLY_SYMBOLIZER_STACKTRACE_H_

#include <cstddef>
#include <cstdint>

namespace folly { namespace symbolizer {

/**
 * Get the current stack trace into addresses, which has room for at least
 * maxAddresses frames.
 *
 * Returns the number of frames written in the array.
 * Returns -1 on failure.
 */
ssize_t getStackTrace(uintptr_t* addresses, size_t maxAddresses);

}}  // namespaces

#endif /* FOLLY_SYMBOLIZER_STACKTRACE_H_ */

