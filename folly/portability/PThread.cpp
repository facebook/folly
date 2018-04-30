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

#include <folly/portability/PThread.h>

#if !FOLLY_HAVE_PTHREAD && _WIN32
#include <boost/thread/tss.hpp> // @manual

namespace folly {
namespace portability {
namespace pthread {

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
  try {
    auto newKey = new boost::thread_specific_ptr<void>(destructor);
    *key = newKey;
    return 0;
  } catch (boost::thread_resource_error) {
    return -1;
  }
}

int pthread_key_delete(pthread_key_t key) {
  try {
    auto realKey = reinterpret_cast<boost::thread_specific_ptr<void>*>(key);
    delete realKey;
    return 0;
  } catch (boost::thread_resource_error) {
    return -1;
  }
}

void* pthread_getspecific(pthread_key_t key) {
  auto realKey = reinterpret_cast<boost::thread_specific_ptr<void>*>(key);
  // This can't throw as-per the documentation.
  return realKey->get();
}

int pthread_setspecific(pthread_key_t key, const void* value) {
  try {
    auto realKey = reinterpret_cast<boost::thread_specific_ptr<void>*>(key);
    // We can't just call reset here because that would invoke the cleanup
    // function, which we don't want to do.
    boost::detail::set_tss_data(
        realKey,
        boost::shared_ptr<boost::detail::tss_cleanup_function>(),
        const_cast<void*>(value),
        false);
    return 0;
  } catch (boost::thread_resource_error) {
    return -1;
  }
}
}
}
}
#endif
