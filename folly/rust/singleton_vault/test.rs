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

#[cxx::bridge]
mod ffi {
    #[namespace = "facebook::folly::rust::test"]
    unsafe extern "C++" {
        include!("folly/rust/singleton_vault/singleton_test.h");

        fn initNormalSingleton() -> Result<()>;

        fn getNormalSingleton() -> Result<i64>;

        fn getEagerSingleton() -> Result<i32>;
    }
}

pub use ffi::*;

#[cfg(test)]
mod tests {
    use singleton_vault::SingletonVaultType;
    use singleton_vault::do_eager_init;
    use singleton_vault::registration_complete;
    use singleton_vault::set_singleton_vault_mode;

    use super::*;

    #[test]
    fn test_singleton_vault() -> anyhow::Result<()> {
        eprintln!("Setting vault mode to strict");
        set_singleton_vault_mode(SingletonVaultType::Strict)?;

        eprintln!("Forcing registration complete");
        registration_complete()?;

        eprintln!("Doing eager init");
        do_eager_init()?;

        eprintln!("Init normal singleton");
        ffi::initNormalSingleton()?;

        eprintln!("Get normal singleton");
        assert_eq!(ffi::getNormalSingleton()?, 58);

        eprintln!("Get eager singleton");
        assert_eq!(ffi::getEagerSingleton()?, 14);

        Ok(())
    }
}
