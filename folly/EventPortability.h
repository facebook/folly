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
#pragma once

#ifdef _MSC_VER

#include <event2/event_compat.h>

namespace folly { namespace event_portability {

void event_set(struct event* e, evutil_socket_t fd, short s, void(*f)(int, short, void*), void* b);
void event_set(struct event* e, int fd, short s, void(*f)(int, short, void*), void* b);

}}

using namespace folly::event_portability;

#endif
