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
#include <unordered_map>
#include <utility>

namespace folly {
namespace portability {
namespace pthread {
static thread_local struct PThreadLocalMap {
  PThreadLocalMap() = default;
  ~PThreadLocalMap() {
    for (auto kv : keyMap) {
      // Call destruction callbacks if they exist.
      if (kv.second.second != nullptr) {
        kv.second.second(kv.second.first);
      }
    }
  }

  int createKey(pthread_key_t* key, void (*destructor)(void*)) {
    auto ret = TlsAlloc();
    if (ret == TLS_OUT_OF_INDEXES) {
      return -1;
    }
    *key = ret;
    keyMap.emplace(*key, std::make_pair(nullptr, destructor));
    return 0;
  }

  int deleteKey(pthread_key_t key) {
    if (!TlsFree(key)) {
      return -1;
    }
    keyMap.erase(key);
    return 0;
  }

  void* getKey(pthread_key_t key) {
    return TlsGetValue(key);
  }

  int setKey(pthread_key_t key, void* value) {
    if (!TlsSetValue(key, value)) {
      return -1;
    }
    keyMap[key].first = value;
    return 0;
  }

  std::unordered_map<pthread_key_t, std::pair<void*, void (*)(void*)>> keyMap{};
} s_tls_key_map;

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
  return s_tls_key_map.createKey(key, destructor);
}

int pthread_key_delete(pthread_key_t key) {
  return s_tls_key_map.deleteKey(key);
}

void* pthread_getspecific(pthread_key_t key) {
  return s_tls_key_map.getKey(key);
}

int pthread_setspecific(pthread_key_t key, const void* value) {
  // Yes, the PThread API really is this bad -_-...
  return s_tls_key_map.setKey(key, const_cast<void*>(value));
}
}
}
}
#endif
