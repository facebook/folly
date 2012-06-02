/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_BASE_HASH_H_
#define FOLLY_BASE_HASH_H_

#include <stdint.h>
#include <cstring>
#include <string>

/*
 * Various hashing functions.
 */

namespace folly { namespace hash {

//////////////////////////////////////////////////////////////////////

/*
 * Thomas Wang 64 bit mix hash function
 */

inline uint64_t twang_mix64(uint64_t key) {
  key = (~key) + (key << 21);
  key = key ^ (key >> 24);
  key = (key + (key << 3)) + (key << 8);
  key = key ^ (key >> 14);
  key = (key + (key << 2)) + (key << 4);
  key = key ^ (key >> 28);
  key = key + (key << 31);
  return key;
}

/*
 * Thomas Wang downscaling hash function
 */

inline uint32_t twang_32from64(uint64_t key) {
  key = (~key) + (key << 18);
  key = key ^ (key >> 31);
  key = key * 21;
  key = key ^ (key >> 11);
  key = key + (key << 6);
  key = key ^ (key >> 22);
  return (uint32_t) key;
}

/*
 * Robert Jenkins' reversible 32 bit mix hash function
 */

inline uint32_t jenkins_rev_mix32(uint32_t key) {
  key += (key << 12);
  key ^= (key >> 22);
  key += (key << 4);
  key ^= (key >> 9);
  key += (key << 10);
  key ^= (key >> 2);
  key += (key << 7);
  key += (key << 12);
  return key;
}

/*
 * Fowler / Noll / Vo (FNV) Hash
 *     http://www.isthe.com/chongo/tech/comp/fnv/
 */

const uint32_t FNV_32_HASH_START = 216613626UL;
const uint64_t FNV_64_HASH_START = 14695981039346656037ULL;

inline uint32_t fnv32(const char* s,
                      uint32_t hash = FNV_32_HASH_START) {
  for (; *s; ++s) {
    hash += (hash << 1) + (hash << 4) + (hash << 7) +
            (hash << 8) + (hash << 24);
    hash ^= *s;
  }
  return hash;
}

inline uint32_t fnv32_buf(const void* buf,
                          int n,
                          uint32_t hash = FNV_32_HASH_START) {
  const char* char_buf = reinterpret_cast<const char*>(buf);

  for (int i = 0; i < n; ++i) {
    hash += (hash << 1) + (hash << 4) + (hash << 7) +
            (hash << 8) + (hash << 24);
    hash ^= char_buf[i];
  }

  return hash;
}

inline uint32_t fnv32(const std::string& str,
                      uint64_t hash = FNV_32_HASH_START) {
  return fnv32_buf(str.data(), str.size(), hash);
}

inline uint64_t fnv64(const char* s,
                      uint64_t hash = FNV_64_HASH_START) {
  for (; *s; ++s) {
    hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) +
      (hash << 8) + (hash << 40);
    hash ^= *s;
  }
  return hash;
}

inline uint64_t fnv64_buf(const void* buf,
                          int n,
                          uint64_t hash = FNV_64_HASH_START) {
  const char* char_buf = reinterpret_cast<const char*>(buf);

  for (int i = 0; i < n; ++i) {
    hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) +
      (hash << 8) + (hash << 40);
    hash ^= char_buf[i];
  }
  return hash;
}

inline uint64_t fnv64(const std::string& str,
                      uint64_t hash = FNV_64_HASH_START) {
  return fnv64_buf(str.data(), str.size(), hash);
}

/*
 * Paul Hsieh: http://www.azillionmonkeys.com/qed/hash.html
 */

#define get16bits(d) (*((const uint16_t*) (d)))

inline uint32_t hsieh_hash32_buf(const void* buf, int len) {
  const char* s = reinterpret_cast<const char*>(buf);
  uint32_t hash = len;
  uint32_t tmp;
  int rem;

  if (len <= 0 || buf == 0) {
    return 0;
  }

  rem = len & 3;
  len >>= 2;

  /* Main loop */
  for (;len > 0; len--) {
    hash  += get16bits (s);
    tmp    = (get16bits (s+2) << 11) ^ hash;
    hash   = (hash << 16) ^ tmp;
    s  += 2*sizeof (uint16_t);
    hash  += hash >> 11;
  }

  /* Handle end cases */
  switch (rem) {
  case 3:
    hash += get16bits(s);
    hash ^= hash << 16;
    hash ^= s[sizeof (uint16_t)] << 18;
    hash += hash >> 11;
    break;
  case 2:
    hash += get16bits(s);
    hash ^= hash << 11;
    hash += hash >> 17;
    break;
  case 1:
    hash += *s;
    hash ^= hash << 10;
    hash += hash >> 1;
  }

  /* Force "avalanching" of final 127 bits */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;
};

#undef get16bits

inline uint32_t hsieh_hash32(const char* s) {
  return hsieh_hash32_buf(s, std::strlen(s));
}

inline uint32_t hsieh_hash32_str(const std::string& str) {
  return hsieh_hash32_buf(str.data(), str.size());
}

//////////////////////////////////////////////////////////////////////

} // namespace hash

template<class Key>
struct hasher;

template<> struct hasher<int32_t> {
  size_t operator()(int32_t key) const {
    return hash::jenkins_rev_mix32(uint32_t(key));
  }
};

template<> struct hasher<uint32_t> {
  size_t operator()(uint32_t key) const {
    return hash::jenkins_rev_mix32(key);
  }
};

template<> struct hasher<int64_t> {
  size_t operator()(int64_t key) const {
    return hash::twang_mix64(uint64_t(key));
  }
};

template<> struct hasher<uint64_t> {
  size_t operator()(uint64_t key) const {
    return hash::twang_mix64(key);
  }
};

} // namespace folly

#endif
