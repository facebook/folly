/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {
namespace test {

std::vector<std::string> getCiphersFromSSL(SSL* s);

std::vector<std::string> getNonTLS13CipherList(SSL* s);

std::vector<std::string> getTLS13Ciphersuites(SSL* s);

} // namespace test
} // namespace folly
