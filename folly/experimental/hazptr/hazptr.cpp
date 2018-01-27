/*
 * Copyright 2017-present Facebook, Inc.
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

#include "hazptr.h"

namespace folly {
namespace hazptr {

FOLLY_STATIC_CTOR_PRIORITY_MAX hazptr_domain default_domain_;

hazptr_stats hazptr_stats_;
#if HAZPTR_ENABLE_TLS
thread_local hazptr_tls_state tls_state_ = TLS_UNINITIALIZED;
thread_local hazptr_tc tls_tc_data_;
thread_local hazptr_priv tls_priv_data_;
thread_local hazptr_tls_life tls_life_; // last
#else
hazptr_tls_state tls_state_ = TLS_UNINITIALIZED;
hazptr_tc tls_tc_data_;
hazptr_priv tls_priv_data_;
hazptr_tls_life tls_life_; // last
#endif

bool hazptr_tc_enabled() {
  return HAZPTR_TC;
}

bool hazptr_priv_enabled() {
  return HAZPTR_PRIV;
}

} // namespace hazptr
} // namespace folly
