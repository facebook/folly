/*
 * Copyright 2016-present Facebook, Inc.
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

// Bring in MAXPATHLEN so kerberos doesn't make up its
// own defintion of it.
#include <folly/portability/Stdlib.h>

#include <folly/portability/Windows.h>

#include <gssapi/gssapi_generic.h>
#include <gssapi/gssapi_krb5.h>
#include <krb5.h> // @manual

// Kerberos defines a bunch of things that we implement as actual
// functions, so undefine whatever mess kerberos has done.
#undef strcasecmp
#undef strncasecmp
#undef strdup
#undef strtok_r
