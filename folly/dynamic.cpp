/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/dynamic.h"

namespace folly {

//////////////////////////////////////////////////////////////////////

#define DEF_TYPE(T, str, typen)                                 \
  template<> char const dynamic::TypeInfo<T>::name[] = str;       \
  template<> dynamic::Type const dynamic::TypeInfo<T>::type = typen

DEF_TYPE(void*,               "null",    dynamic::NULLT);
DEF_TYPE(bool,                "boolean", dynamic::BOOL);
DEF_TYPE(fbstring,            "string",  dynamic::STRING);
DEF_TYPE(dynamic::Array,      "array",   dynamic::ARRAY);
DEF_TYPE(double,              "double",  dynamic::DOUBLE);
DEF_TYPE(int64_t,             "int64",   dynamic::INT64);
DEF_TYPE(dynamic::ObjectImpl, "object",  dynamic::OBJECT);

#undef DEF_TYPE

//////////////////////////////////////////////////////////////////////

}

