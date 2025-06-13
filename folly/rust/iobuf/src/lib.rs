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

//! Rust binding for `folly::IOBuf`
//!
//! This primarily intended for interop with C++ APIs which use `folly::IOBuf` as part of their
//! interface, rather than as a full-featured Rust API.
//!
//! The basic types are:
//!
//! `IOBufShared` - this Rust type owns a specific `folly::IOBuf` node in a circular chain,
//!    which is considered to be the head of the chain. The individual buffers can point to
//!    distinct or common underlying memory, which may also be shared with other IOBuf chains.
//!    This type only presents a read-only API. Data is read via `IOBufCursor` (implementing
//!    `bytes::Buf`) or `IOBufIter` (implementing an iterator over byte slices).
//!
//! `IOBufMut` - Structurally identical to `IOBufShared`, but has the further guarantee that
//!    the underlying memory is not shared, and can therefore be safely modified. Data can be
//!    extended via `IOBufMutCursor`'s `bytes::BufMut` implementation, or data can be modified
//!    in place via `IOBufMutIter`'s iterator over `&mut [u8]`.
//!
//! Normally IOBuf instances will either be allocated by C++ code, but it also supports wrapping
//! `Vec<u8>` in an `IOBuf` to allow zero-copy operations from Rust `Vec<u8>` or `Bytes` instances.

#![deny(warnings)]
#![allow(clippy::upper_case_acronyms)] // We use the same IOBuf capitalization as C++.

#[cfg(test)]
mod test;

mod cursor;
mod iobuf;
mod iobufmut;

use std::fmt;
use std::fmt::Debug;

use cxx::ExternType;
use cxx::type_id;
use iobuf_sys::root::folly::IOBuf as IOBufSys;

pub use crate::cursor::IOBufCursor;
pub use crate::cursor::IOBufCursorFastRemaining;
pub use crate::cursor::IOBufMutCursor;
pub use crate::iobuf::IOBufShared;
pub use crate::iobufmut::IOBufMut;

// In downstream Rust code "folly::IOBuf" is more evocative than "iobuf::IOBuf".
pub mod folly {
    pub use crate::IOBuf;
}

#[repr(transparent)]
pub struct IOBuf {
    // Important: the fields of the C++ struct are not exposed outside of this
    // crate. This makes it sound to expose conversions between
    // IOBufShared/IOBufMut and UniquePtr<folly::IOBuf>.
    pub(crate) binding: IOBufSys,
}

unsafe impl ExternType for IOBuf {
    type Id = type_id!("folly::IOBuf");
    // Opaque because it must not be passed around by value in
    // Rust. folly::IOBuf relies on a move constructor to rewire
    // the backpointers of the next and prev nodes.
    type Kind = cxx::kind::Opaque;
}

impl Debug for IOBuf {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        Debug::fmt(&self.binding, formatter)
    }
}

impl IOBuf {
    fn ptr(ptr: *mut IOBufSys) -> *mut Self {
        ptr.cast()
    }
}

impl Eq for IOBuf {}
impl PartialEq for IOBuf {
    fn eq(&self, other: &Self) -> bool {
        crate::bridge::iobuf_cmp_equal(self, other)
    }
}

#[cxx::bridge]
pub mod bridge {
    #[namespace = "folly"]
    unsafe extern "C++" {
        include!("folly/io/IOBuf.h");

        type IOBuf = crate::IOBuf;

        fn countChainElements(self: &IOBuf) -> usize;
        fn computeChainDataLength(self: &IOBuf) -> usize;
        fn unshare(self: Pin<&mut IOBuf>);
        // Name is used for uniformity with C++
        #[allow(clippy::should_implement_trait)]
        fn clone(self: &IOBuf) -> UniquePtr<IOBuf>;
        fn cloneOne(self: &IOBuf) -> UniquePtr<IOBuf>;
    }

    #[namespace = "facebook::rust"]
    unsafe extern "C++" {
        include!("folly/rust/iobuf/iobuf.h");

        fn iobuf_create(cap: usize) -> UniquePtr<IOBuf>;
        fn iobuf_create_combined(cap: usize) -> UniquePtr<IOBuf>;
        fn iobuf_create_chain(total_cap: usize, buf_lim: usize) -> UniquePtr<IOBuf>;
        fn iobuf_prepend_chain(iobuf: Pin<&mut IOBuf>, other: UniquePtr<IOBuf>);
        fn iobuf_append_chain(iobuf: Pin<&mut IOBuf>, other: UniquePtr<IOBuf>);
        fn iobuf_cmp_equal(a: &IOBuf, b: &IOBuf) -> bool;
    }

    impl UniquePtr<IOBuf> {}
    impl CxxVector<IOBuf> {}
}
