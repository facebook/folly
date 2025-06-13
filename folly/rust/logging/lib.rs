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

//! A bridge crate between the Folly [log configuration system](https://fburl.com/7i6sea58) and Rust, so that Rust
//! applications can control the log config used by C++ dependencies that they link against.

/// Updates the Folly root logger's configuration by applying the given configuration string. The string is interpreted
/// according to Folly's [logging configuration language](https://fburl.com/rk7eebod).
pub fn update_logging_config(_: fbinit::FacebookInit, config: &str) {
    ffi::updateLoggingConfig(config);
}

#[cxx::bridge]
mod ffi {
    #[namespace = "facebook::rust"]
    unsafe extern "C++" {
        include!("folly/rust/logging/logging.h");

        fn updateLoggingConfig(config: &str);
    }
}
