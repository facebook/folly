/*
 * Copyright 2015 Facebook, Inc.
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

/**
 * GCC compatible wrappers around clang attributes.
 *
 * @author Dominik Gabi
 */

#ifndef FOLLY_BASE_ATTRIBUTES_H_
#define FOLLY_BASE_ATTRIBUTES_H_

#ifndef __has_cpp_attribute
#define FOLLY_HAS_CPP_ATTRIBUTE(x) 0
#else
#define FOLLY_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#endif

/**
 * Fallthrough to indicate that `break` was left out on purpose in a switch
 * statement, e.g.
 *
 * switch (n) {
 *   case 22:
 *   case 33:  // no warning: no statements between case labels
 *     f();
 *   case 44:  // warning: unannotated fall-through
 *     g();
 *     FOLLY_FALLTHROUGH; // no warning: annotated fall-through
 * }
 */
#if FOLLY_HAS_CPP_ATTRIBUTE(clang::fallthrough)
#define FOLLY_FALLTHROUGH [[clang::fallthrough]]
#else
#define FOLLY_FALLTHROUGH
#endif

#endif /* FOLLY_BASE_ATTRIBUTES_H_ */
