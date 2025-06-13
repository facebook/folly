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

//! Utilities for controlling the global folly::SingletonVault.

#[cxx::bridge(namespace = "facebook::folly::rust")]
mod ffi {
    enum VaultType {
        /// Singletons can't be created before registration_complete()
        Strict = 0,
        /// Singletons can be created before registration_complete()
        Relaxed = 1,
    }

    unsafe extern "C++" {
        include!("folly/rust/singleton_vault/singleton.h");

        type VaultType;

        fn setSingletonVaultMode(mode: VaultType) -> Result<()>;

        fn registrationComplete() -> Result<()>;

        fn doEagerInit() -> Result<()>;
    }
}

pub use ffi::VaultType as SingletonVaultType;

/// Sets the singleton vault mode for the global vault.
///
/// NOTE: It is not recommended to do this after `initFacebook(Light)` has been called.
///
/// WARNING: DO NOT USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.
pub fn set_singleton_vault_mode(ttype: SingletonVaultType) -> anyhow::Result<()> {
    Ok(ffi::setSingletonVaultMode(ttype)?)
}

/// Mark registration as complete; no more singletons can be registered at this point.
///
/// WARNING: DO NOT USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.
pub fn registration_complete() -> anyhow::Result<()> {
    Ok(ffi::registrationComplete()?)
}

/// Initialize all singletons which were marked as eager-initialized.
///
/// NOTE: Propagates exceptions from constructors / create functions.
///
/// WARNING: DO NOT USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.
pub fn do_eager_init() -> anyhow::Result<()> {
    Ok(ffi::doEagerInit()?)
}
