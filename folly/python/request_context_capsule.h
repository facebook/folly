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

#pragma once

#include <Python.h>
#include <folly/io/async/Request.h>

namespace folly::python {

namespace {
inline void rc_capsule_destructor(PyObject* capsule) {
  auto ptr = static_cast<std::shared_ptr<RequestContext>*>(
      PyCapsule_GetPointer(capsule, NULL));
  delete ptr;
}
} // namespace

inline PyObject* RequestContextToPyCapsule(std::shared_ptr<RequestContext> rc) {
  // Trun a RequestContext into a PyObject* we can pass around python runtime.
  return PyCapsule_New(
      new std::shared_ptr<RequestContext>(std::move(rc)),
      NULL,
      rc_capsule_destructor);
}

inline std::shared_ptr<RequestContext> PyCapsuleToRequestContext(
    PyObject* capsule) {
  // Return the RequestContext from the PyObject* Capsule
  auto ptr = static_cast<std::shared_ptr<RequestContext>*>(
      PyCapsule_GetPointer(capsule, NULL));
  return *ptr;
}

} // namespace folly::python
