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

#include <folly/python/error.h>

#include <stdexcept>
#include <string>

#include <Python.h>

#include <folly/Conv.h>
#include <folly/ScopeGuard.h>

namespace folly {
namespace python {
namespace {
// Best effort c-api implementation of repr(obj)
std::string pyObjectToString(PyObject* obj) {
  constexpr StringPiece kConversionFail = "Error conversion failed";
  PyObject *pyStr, *pyBytes;
  FOLLY_SCOPE_EXIT {
    Py_XDECREF(pyStr);
    Py_XDECREF(pyBytes);
    // Swallow any errors that arise in this function
    PyErr_Clear();
  };

  pyStr = PyObject_Repr(obj);
  if (pyStr == nullptr) {
    return std::string(kConversionFail);
  }

  char* cStr = nullptr;
#if PY_VERSION_HEX < 0x0300000
  cStr = PyString_AsString(pyStr);
#else
  pyBytes = PyUnicode_AsEncodedString(pyStr, "utf-8", "strict");
  if (pyBytes == nullptr) {
    return std::string(kConversionFail);
  }

  cStr = PyBytes_AsString(pyBytes);
#endif

  if (cStr == nullptr) {
    return std::string(kConversionFail);
  }

  return std::string(cStr);
}

} // namespace

void handlePythonError(StringPiece errPrefix) {
  PyObject *ptype, *pvalue, *ptraceback;
  FOLLY_SCOPE_EXIT {
    Py_XDECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptraceback);
  };
  /**
   * PyErr_Fetch will clear the error indicator (which *should* be set here, but
   * let's not assume it is)
   */
  PyErr_Fetch(&ptype, &pvalue, &ptraceback);

  if (ptype == nullptr) {
    throw std::runtime_error(
        to<std::string>(errPrefix, "No error indicator set"));
  }

  if (pvalue == nullptr) {
    throw std::runtime_error(to<std::string>(
        errPrefix, "Exception of type: ", pyObjectToString(ptype)));
  }

  throw std::runtime_error(
      to<std::string>(errPrefix, pyObjectToString(pvalue)));
}
} // namespace python
} // namespace folly
