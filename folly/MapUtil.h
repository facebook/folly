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

#ifndef FOLLY_MAPUTIL_H_
#define FOLLY_MAPUTIL_H_

namespace folly {

/**
 * Given a map and a key, return the value corresponding to the key in the map,
 * or a given default value if the key doesn't exist in the map.
 */
template <class Map>
typename Map::mapped_type get_default(
    const Map& map, const typename Map::key_type& key,
    const typename Map::mapped_type& dflt =
    typename Map::mapped_type()) {
  auto pos = map.find(key);
  return (pos != map.end() ? pos->second : dflt);
}

/**
 * Given a map and a key, return a reference to the value corresponding to the
 * key in the map, or the given default reference if the key doesn't exist in
 * the map.
 */
template <class Map>
const typename Map::mapped_type& get_ref_default(
    const Map& map, const typename Map::key_type& key,
    const typename Map::mapped_type& dflt) {
  auto pos = map.find(key);
  return (pos != map.end() ? pos->second : dflt);
}

/**
 * Given a map and a key, return a pointer to the value corresponding to the
 * key in the map, or nullptr if the key doesn't exist in the map.
 */
template <class Map>
const typename Map::mapped_type* get_ptr(
    const Map& map, const typename Map::key_type& key) {
  auto pos = map.find(key);
  return (pos != map.end() ? &pos->second : nullptr);
}

/**
 * Non-const overload of the above.
 */
template <class Map>
typename Map::mapped_type* get_ptr(
    Map& map, const typename Map::key_type& key) {
  auto pos = map.find(key);
  return (pos != map.end() ? &pos->second : nullptr);
}

}  // namespace folly

#endif /* FOLLY_MAPUTIL_H_ */

