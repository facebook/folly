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

#include <folly/rust/singleton_vault/singleton.h>

#include <folly/Singleton.h>

namespace facebook::folly::rust {

void setSingletonVaultMode(VaultType mode) {
  auto ttype = ::folly::SingletonVault::Type::Strict;
  switch (mode) {
    case VaultType::Strict: {
      ttype = ::folly::SingletonVault::Type::Strict;
      break;
    }
    case VaultType::Relaxed: {
      ttype = ::folly::SingletonVault::Type::Relaxed;
      break;
    }
  }
  ::folly::SingletonVault::singleton()->setType(ttype);
}

void registrationComplete() {
  ::folly::SingletonVault::singleton()->registrationComplete();
}

void doEagerInit() {
  ::folly::SingletonVault::singleton()->doEagerInit();
}

void destroyInstances() {
  ::folly::SingletonVault::singleton()->destroyInstances();
}

} // namespace facebook::folly::rust
