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

#include <folly/portability/GTest.h>
#include <folly/python/error.h>

#include <string>

#include <Python.h>

#include <folly/ScopeGuard.h>

namespace folly {
namespace python {
namespace detail {
namespace test {

struct ErrorTest : public testing::Test {
  void SetUp() override {
    Py_Initialize();
    gstate_ = PyGILState_Ensure();
  }

  void TearDown() override { PyGILState_Release(gstate_); }

 protected:
  template <typename F>
  static void expectThrowsWithMessage(F&& func, std::string message) {
    try {
      func();
      FAIL();
    } catch (const std::runtime_error& e) {
      EXPECT_STREQ(message.c_str(), e.what());
    } catch (...) {
      FAIL();
    }

    EXPECT_EQ(nullptr, PyErr_Occurred());
  }

 private:
  PyGILState_STATE gstate_;
};

TEST_F(ErrorTest, testNoErrorIndicator) {
  expectThrowsWithMessage(
      []() { handlePythonError("fail: "); }, "fail: No error indicator set");
}

TEST_F(ErrorTest, testNullError) {
  expectThrowsWithMessage(
      []() {
        PyErr_SetObject(PyExc_RuntimeError, nullptr);
        handlePythonError("fail: ");
      },
      "fail: Exception of type: <class 'RuntimeError'>");
}

TEST_F(ErrorTest, testStringError) {
  expectThrowsWithMessage(
      []() {
        PyErr_SetString(PyExc_RuntimeError, "test error");
        handlePythonError("fail: ");
      },
      "fail: 'test error'");
}

TEST_F(ErrorTest, testException) {
  expectThrowsWithMessage(
      []() {
        PyObject *args, *exc;
        SCOPE_EXIT {
          Py_XDECREF(args);
          Py_XDECREF(exc);
        };

        args = Py_BuildValue("(s)", "test error");
        if (!args) {
          handlePythonError("fail: ");
        }

        exc = PyObject_CallObject(PyExc_RuntimeError, args);
        if (!exc) {
          handlePythonError("fail: ");
        }

        PyErr_SetObject(PyExc_RuntimeError, exc);
        handlePythonError("fail: ");
      },
      "fail: RuntimeError('test error')");
}

} // namespace test
} // namespace detail
} // namespace python
} // namespace folly
