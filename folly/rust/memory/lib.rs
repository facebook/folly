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

//! Utilities for controlling the facebook-specific build of JEMalloc that fbcode-platform binaries link against in
//! production builds.
//!
//! JEMalloc provides a non-standard interface, `mallctl`, which is an extremely flexible way to query for performance
//! statistics, influence the behavior of JEMalloc, dump the heap, and do a variety of other things. It's an ioctl for
//! JEMalloc. This crate provides a typed interface around `folly`'s implementation of a library that talks to mallctl.
//!
//! The list of `mallctl` commands, their arguments, and effects can be seen [here](http://jemalloc.net/jemalloc.3.html).

use anyhow::Result;

#[cxx::bridge]
mod ffi {
    #[namespace = "facebook::rust"]
    unsafe extern "C++" {
        include!("folly/rust/memory/memory.h");

        fn mallctlReadBool(cmd: &str, value: &mut bool) -> Result<()>;
        fn mallctlWriteBool(cmd: &str, value: bool) -> Result<()>;
        fn mallctlWriteString(cmd: &str, value: &str) -> Result<()>;
        fn mallctlCall(cmd: &str) -> Result<()>;

        #[namespace = "folly"]
        fn usingJEMalloc() -> bool;
    }
}

/// Trait for types that can be read from `mallctl`.
pub trait MallctlValueRead: Sized {
    /// Invokes the given `mallctl` command and reads a value of type `Self` from the result.
    fn read(cmd: &str) -> Result<Self>;
}

/// Trait for types that can be written into `mallctl`.
pub trait MallctlValueWrite {
    /// Invokes the given `mallctl` command and writes the given value to it.
    fn write(&self, cmd: &str) -> Result<()>;
}

impl MallctlValueWrite for bool {
    fn write(&self, cmd: &str) -> Result<()> {
        ffi::mallctlWriteBool(cmd, *self)?;
        Ok(())
    }
}

impl MallctlValueRead for bool {
    fn read(cmd: &str) -> Result<Self> {
        let mut value = false;
        ffi::mallctlReadBool(cmd, &mut value)?;
        Ok(value)
    }
}

impl MallctlValueWrite for &str {
    fn write(&self, cmd: &str) -> Result<()> {
        ffi::mallctlWriteString(cmd, self)?;
        Ok(())
    }
}

/// Returns whether or not this binary is currently using JEMalloc as the main allocator.
pub fn is_using_jemalloc() -> bool {
    ffi::usingJEMalloc()
}

/// Writes a given value to the given `mallctl` command, returning whether or not the write was successful.
///
/// # Examples
///
/// ```no_run
/// # use memory::mallctl_write;
/// #
/// // Write a heap dump to `heapdump.dmp`
/// mallctl_write("prof.dump", "heapdump.dmp").unwrap();
/// ```
pub fn mallctl_write<T: MallctlValueWrite>(cmd: &str, value: T) -> Result<()> {
    value.write(cmd)
}

/// Reads a value of the given type from the given `mallctl` command, returning the value and whether it was
/// successful.
///
/// # Examples
///
/// ```no_run
/// # use memory::mallctl_read;
/// #
/// // Read whether or not profiling is enabled in this build of jemalloc.
/// let profiling_enabled = mallctl_read::<bool>("config.prof").unwrap();
/// ```
pub fn mallctl_read<T: MallctlValueRead>(cmd: &str) -> Result<T> {
    <T as MallctlValueRead>::read(cmd)
}

/// Invokes a mallctl command purely for side-effects.
///
/// # Examples
///
/// ```no_run
/// # use memory::mallctl_call;
/// #
/// // Advance the JEMalloc epoch to flush in-flight data to stats.
/// mallctl_call("epoch").unwrap();
/// ```
pub fn mallctl_call(cmd: &str) -> Result<()> {
    Ok(ffi::mallctlCall(cmd)?)
}

#[cfg(test)]
mod tests {
    #[test]
    fn ctl_read() {
        if super::is_using_jemalloc() {
            assert!(super::mallctl_read::<bool>("config.prof").is_ok())
        }
    }

    #[test]
    fn ctl_call() {
        if super::is_using_jemalloc() {
            super::mallctl_call("epoch").unwrap();
        }
    }
}
