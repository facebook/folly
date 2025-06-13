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

use std::mem;
use std::os::raw::c_void;

use cxx::UniquePtr;
use iobuf_sys::root::facebook::rust::*;
use iobuf_sys::root::folly::IOBuf as IOBufSys;

use crate::IOBuf;
use crate::bridge;
use crate::cursor::IOBufCursor;
use crate::cursor::IOBufMutCursor;
use crate::iobuf::IOBufShared;
use crate::iobuf::RawIOBufPtr;

/// Owned head of a mutable IOBuf chain
#[derive(Debug)]
pub struct IOBufMut(pub(crate) UniquePtr<IOBuf>);

unsafe impl RawIOBufPtr for IOBufMut {
    fn get_ptr(&self) -> *const IOBufSys {
        &self.0.binding
    }
}

impl IOBufMut {
    /// Allocate a buffer chain with a single node with the given capacity
    pub fn with_capacity(cap: usize) -> IOBufMut {
        IOBufMut(bridge::iobuf_create(cap))
    }

    /// Allocate a single node with the data and metadata in a single allocation
    pub fn with_capacity_combined(cap: usize) -> IOBufMut {
        IOBufMut(bridge::iobuf_create_combined(cap))
    }

    /// Allocate a whole chain with a given overall capacity and a max buffer size.
    pub fn with_chain(total_cap: usize, max_buffer: usize) -> IOBufMut {
        IOBufMut(bridge::iobuf_create_chain(total_cap, max_buffer))
    }

    /// Total amount of readable bytes in the whole buffer chain.
    pub fn len(&self) -> usize {
        self.0.computeChainDataLength()
    }

    /// Whether there are any readable bytes in the whole buffer chain (false) or not (true)
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Total chain length
    pub fn chain_len(&self) -> usize {
        self.0.countChainElements()
    }

    /// Clone the underlying buffer chain, returning the two chains sharing the same data.
    /// This creates sharing, so it has to revert to `IOBufShared`.
    pub fn clone(self) -> (IOBufShared, IOBufShared) {
        let b1 = IOBufShared(self.0);
        let b2 = b1.clone();
        (b1, b2)
    }

    /// Clone a single buffer, returning the two IOBufs sharing the same data.
    /// This creates sharing, so it has to revert to `IOBufShared`.
    pub fn clone_one(self) -> (IOBufShared, IOBufShared) {
        let b1 = IOBufShared(self.0);
        let b2 = b1.clone_one();
        (b1, b2)
    }

    /// Clear entire chain. This retains the chain's nodes and allocations, but sets the
    /// valid data length in each node to 0.
    pub fn clear(&mut self) {
        let head: *mut IOBufSys = &mut self.0.binding;
        let mut ptr = head;

        loop {
            unsafe {
                (*ptr).data_ = (*ptr).buf_;
                (*ptr).length_ = 0;
                ptr = (*ptr).next_;
            }
            if ptr == head {
                break;
            }
        }
    }

    /// Insert chain `other` immediately after this iobuf, logically inserting its entire contents
    pub fn insert_after<B: Into<Self>>(&mut self, other: B) {
        bridge::iobuf_append_chain(self.0.pin_mut(), other.into().0);
    }

    /// Append chain `other` to the end of the chain, logically appending its entire contents
    pub fn append_to_end<B: Into<Self>>(&mut self, other: B) {
        // IOBufs are on a circular list, so prepending the head is appending the tail
        bridge::iobuf_prepend_chain(self.0.pin_mut(), other.into().0);
    }

    /// Get a cursor implementing `bytes::Buf`.
    pub fn cursor(self) -> IOBufCursor<IOBufMut> {
        IOBufCursor::from(self)
    }

    /// Get a cursor implementing `bytes::BufMut`.
    pub fn cursor_mut(self) -> IOBufMutCursor {
        IOBufMutCursor::new(self)
    }
}

/// Convert from `IOBufShared` to `IOBufMut` by unsharing any buffers.
impl From<IOBufShared> for IOBufMut {
    fn from(buf: IOBufShared) -> Self {
        buf.unshare()
    }
}

/// Take ownership of underlying bindgen-generated raw iobuf.
impl From<UniquePtr<IOBuf>> for IOBufMut {
    fn from(repr: UniquePtr<IOBuf>) -> Self {
        // The conversion from IOBufShared to IOBufMut performs the unshare to
        // ensure we have exclusive mutable access to this iobuf head.
        IOBufMut::from(IOBufShared::from(repr))
    }
}

impl From<IOBufMut> for UniquePtr<IOBuf> {
    fn from(wrapper: IOBufMut) -> Self {
        wrapper.0
    }
}

/// Equality is by `IOBufEqualTo`
impl Eq for IOBufMut {}
impl PartialEq for IOBufMut {
    fn eq(&self, other: &Self) -> bool {
        self.0.as_ref() == other.0.as_ref()
    }
}

/// Convert `Vec<u8>` into `IOBufMut`. Since the vector can't be shared, we can use it as
/// a `IOBufMut` from the outset.
impl From<Vec<u8>> for IOBufMut {
    fn from(mut vec: Vec<u8>) -> Self {
        // Decompose vec into ptr/len/cap and stop its Drop impl from running.
        let ptr = vec.as_mut_ptr();
        let len = vec.len();
        let cap = vec.capacity();
        mem::forget(vec);

        // Turn the ptr/cap back into a Vec and run its Drop impl. Len doesn't
        // matter; assume 0 because there isn't a Drop impl for u8.
        unsafe extern "C" fn free(ptr: *mut c_void, userdata: *mut c_void) {
            unsafe {
                let ptr = ptr as *mut u8;
                let len = 0;
                let cap = userdata as usize;
                let _ = Vec::from_raw_parts(ptr, len, cap);
            }
        }

        let free_on_error = true;

        let iobuf: *mut IOBufSys = unsafe {
            iobuf_take_ownership(
                ptr as *mut c_void, // payload
                cap,                // capacity
                len,                // valid length of payload
                Some(free),         // freeing function
                cap as *mut c_void, // capacity as userdata
                free_on_error,
            )
        };
        assert!(!iobuf.is_null());

        IOBufMut(unsafe { UniquePtr::from_raw(IOBuf::ptr(iobuf)) })
    }
}
