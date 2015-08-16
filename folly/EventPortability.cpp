/*
* Copyright 2015 Facebook, Inc.
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

#ifdef _MSC_VER
#include <folly/EventPortability.h>

#include <fcntl.h>
#include <io.h>
#include <functional>

namespace folly { namespace event_portability {

// The indirect manner of casting to/from evutil_socket_t is done so
// that we can still compile if we've adjusted evutil_socket_t to be
// defined as a structure to break any places that are incorrectly
// using the API. (ABI-wise, they are actually the exact same)

void event_set(struct event* e, evutil_socket_t fd, short s, void(*f)(int, short, void*), void* b) {
  // We need to dynamically wrap a function pointer, so we get this fun :(
  auto fp = std::function<void(evutil_socket_t, short, void*)>([&](evutil_socket_t a, short b, void* c) {
    f(_open_osfhandle(*(intptr_t*)&a, O_RDWR | O_BINARY), b, c);
  });
  auto fp2 = fp.target<void(evutil_socket_t, short, void*)>();
  ::event_set(e, fd, s, fp2, b);
}

void event_set(struct event* e, int fd, short s, void(*f)(int, short, void*), void* b) {
  intptr_t fh;
  if (fd == -1)
    fh = (intptr_t)INVALID_HANDLE_VALUE;
  else
    fh = _get_osfhandle(fd);
  event_set(e, *(evutil_socket_t*)&fh, s, f, b);
}

}}

#endif
