// Copyright 2004-present Facebook.  All rights reserved.
#ifndef BENCHMARK_IMPL_H_
#define BENCHMARK_IMPL_H_

#include <functional>

class TestClass;
class VirtualClass;

void BM_fn_ptr_invoke_impl(int iters, void (*fn)());
void BM_std_function_invoke_impl(int iters, const std::function<void()>& fn);
void BM_mem_fn_invoke_impl(int iters,
                           TestClass* tc,
                           void (TestClass::*memfn)());
void BM_virtual_fn_invoke_impl(int iters, VirtualClass* vc);

// Inlined version of BM_fn_ptr_invoke_impl().
// The compiler could potentially even optimize the call to the function
// pointer if it is a constexpr.
inline void BM_fn_ptr_invoke_inlined_impl(int iters, void (*fn)()) {
  for (int n = 0; n < iters; ++n) {
    fn();
  }
}

// Invoke a function object as a template parameter.
// This can be used to directly invoke lambda functions
template<typename T>
void BM_invoke_fn_template_impl(int iters, const T& fn) {
  for (int n = 0; n < iters; ++n) {
    fn();
  }
}

#endif // BENCHMARK_IMPL_H_
