// Copyright 2004-present Facebook.  All rights reserved.
#include "folly/test/function_benchmark/benchmark_impl.h"

#include "folly/test/function_benchmark/test_functions.h"

/*
 * These functions are defined in a separate file so that gcc won't be able to
 * inline them and optimize away the indirect calls.
 */

void BM_fn_ptr_invoke_impl(int iters, void (*fn)()) {
  for (int n = 0; n < iters; ++n) {
    fn();
  }
}

void BM_std_function_invoke_impl(int iters,
                                 const std::function<void()>& fn) {
  for (int n = 0; n < iters; ++n) {
    fn();
  }
}

void BM_mem_fn_invoke_impl(int iters,
                           TestClass* tc,
                           void (TestClass::*memfn)()) {
  for (int n = 0; n < iters; ++n) {
    (tc->*memfn)();
  }
}

void BM_virtual_fn_invoke_impl(int iters, VirtualClass* vc) {
  for (int n = 0; n < iters; ++n) {
    vc->doNothing();
  }
}
