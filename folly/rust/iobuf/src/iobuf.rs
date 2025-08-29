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

use std::os::raw::c_void;

use bytes::Buf;
use bytes::Bytes;
use cxx::UniquePtr;
use fbthrift::BufExt;
use iobuf_sys::root::facebook::rust::*;
use iobuf_sys::root::folly::IOBuf as IOBufSys;

use crate::IOBuf;
use crate::bridge;
use crate::cursor::IOBufCursor;
use crate::iobufmut::IOBufMut;

// Not really public - hidden in this non-public module
pub unsafe trait RawIOBufPtr {
    fn get_ptr(&self) -> *const IOBufSys;
}

/// Our owned head of an immutable IOBuf chain
#[derive(Debug)]
pub struct IOBufShared(pub(crate) UniquePtr<IOBuf>);

unsafe impl RawIOBufPtr for IOBufShared {
    #[inline]
    fn get_ptr(&self) -> *const IOBufSys {
        &self.0.binding
    }
}

impl IOBufShared {
    /// Total amount of readable bytes in the whole buffer chain.
    pub fn len(&self) -> usize {
        self.0.computeChainDataLength()
    }

    /// Whether the whole buffer chain has readable bytes (false) or not (true)
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Total chain length
    pub fn chain_len(&self) -> usize {
        self.0.countChainElements()
    }

    /// Unshare all parts of the buffer chain so they're available for modification
    pub fn unshare(mut self) -> IOBufMut {
        self.0.pin_mut().unshare();
        IOBufMut(self.0)
    }

    /// Clone a single buffer, resulting in a single unchained buffer.
    pub fn clone_one(&self) -> Self {
        IOBufShared(self.0.cloneOne())
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

    /// Cursor for reading data via `bytes::Buf`.
    pub fn cursor(self) -> IOBufCursor<IOBufShared> {
        IOBufCursor::from(self)
    }

    /// Converts a `BufExt` into a potentially chained `IOBufShared`. Whether
    /// this conversion copies is dependent on whether
    /// `BufExt::copy_or_reuse_bytes` copies.
    pub fn from_bufext(mut b: impl BufExt) -> Self {
        let mut head = {
            let length = b.chunk().len();
            let bytes = b.copy_or_reuse_bytes(length);
            Self::from(bytes)
        };
        while let chunk @ [_, ..] = b.chunk() {
            let length = chunk.len();
            let bytes = b.copy_or_reuse_bytes(length);
            let tail = Self::from(bytes);
            head.append_to_end(tail);
        }
        head
    }
}

/// Take ownership of underlying bindgen-generated raw iobuf.
impl From<UniquePtr<IOBuf>> for IOBufShared {
    #[inline]
    fn from(repr: UniquePtr<IOBuf>) -> Self {
        IOBufShared(repr)
    }
}

/// Borrow an inner IOBuf from IOBufShared.
impl AsRef<IOBuf> for IOBufShared {
    #[inline]
    fn as_ref(&self) -> &IOBuf {
        &self.0
    }
}

/// Extract raw IOBuf from IOBufShared.
impl From<IOBufShared> for UniquePtr<IOBuf> {
    #[inline]
    fn from(wrapper: IOBufShared) -> Self {
        wrapper.0
    }
}

/// Convert a `Vec<u8>` into an `IOBufShared` via `IOBufMut`.
impl From<Vec<u8>> for IOBufShared {
    fn from(vec: Vec<u8>) -> Self {
        Self::from(IOBufMut::from(vec))
    }
}

impl From<String> for IOBufShared {
    fn from(s: String) -> Self {
        Self::from(s.into_bytes())
    }
}

impl<'a> From<&'a [u8]> for IOBufShared {
    fn from(slice: &'a [u8]) -> Self {
        let v = slice.to_vec();
        Self::from(v)
    }
}

impl<'a> From<&'a str> for IOBufShared {
    fn from(slice: &'a str) -> Self {
        let v = slice.as_bytes();
        Self::from(v)
    }
}

/// SAFETY: implementers of this trait must guarantee that the pointer returned by Deref::as_ptr is
/// valid for the lifetime of this object, and that it is never written to.
pub unsafe trait BytesToIOBufSharedExt: std::ops::Deref<Target = [u8]> {}

unsafe impl BytesToIOBufSharedExt for bytes::Bytes {}
unsafe impl BytesToIOBufSharedExt for memmap2::Mmap {}

/// Convert a `Bytes` into an `IOBufShared`.
impl<T> From<T> for IOBufShared
where
    T: BytesToIOBufSharedExt,
{
    // This takes the Bytes, boxes it up, and passes ownership to the IOBuf
    // with `iobuf_take_ownership`. We box it up so that we can pass it to
    // a freeing function, called by C++ when it wants to release the memory.
    fn from(bytes: T) -> Self {
        // safety: `T` satisfies the contract of `from_owner` because the `BytesToIOBufSharedExt`
        // trait bound.
        unsafe { Self::from_owner(bytes) }
    }
}

impl IOBufShared {
    /// This takes the Bytes, boxes it up, and passes ownership to the IOBuf
    /// with `iobuf_take_ownership`. We box it up so that we can pass it to
    /// a freeing function, called by C++ when it wants to release the memory.
    ///
    /// SAFETY: Similar to `BytesToIOBufSharedExt`, `T` must ensure that its
    /// deref's slice is valid for the lifetime of `T`.
    pub unsafe fn from_owner<T: std::ops::Deref<Target = [u8]>>(bytes: T) -> Self {
        let len = bytes.len();
        let b = Box::new(bytes);

        // Release the Box simply by constructing it then dropping it on the floor.
        // We don't care about the payload pointer, just our raw Box pointer in the
        // userdata.
        unsafe extern "C" fn box_free<TT>(_ptr: *mut c_void, userdata: *mut c_void) {
            unsafe {
                let _ = Box::from_raw(userdata as *mut TT);
            }
        }

        let ptr: *mut IOBufSys = unsafe {
            iobuf_take_ownership(
                b.as_ptr() as *mut c_void,       // actual payload
                len,                             // capacity is the same as len - none extra
                len,                             // size of the payload (same for Bytes)
                Some(box_free::<T>),             // freeing function
                Box::into_raw(b) as *mut c_void, // Boxed Bytes as userdata
                true,
            )
        };
        assert!(!ptr.is_null());

        IOBufShared(unsafe { UniquePtr::from_raw(IOBuf::ptr(ptr)) })
    }
}

impl From<IOBufShared> for Vec<u8> {
    fn from(iob: IOBufShared) -> Self {
        let mut vec = Vec::with_capacity(iob.len());
        let mut cur = iob.cursor();

        while cur.has_remaining() {
            let len = {
                let b = cur.chunk();
                vec.extend_from_slice(b);
                b.len()
            };
            cur.advance(len);
        }

        vec
    }
}

impl From<IOBufShared> for Bytes {
    fn from(iob: IOBufShared) -> Self {
        Bytes::from(Vec::from(iob))
    }
}

/// Make a `IOBufMut` immutable again
impl From<IOBufMut> for IOBufShared {
    fn from(buf: IOBufMut) -> Self {
        IOBufShared(buf.0)
    }
}

/// Clone an entire buffer chain
impl Clone for IOBufShared {
    fn clone(&self) -> Self {
        IOBufShared(self.0.clone())
    }
}

/// Equality is by `IOBufEqualTo`
impl Eq for IOBufShared {}
impl PartialEq for IOBufShared {
    fn eq(&self, other: &Self) -> bool {
        let foo = self.0.as_ref();
        foo == other.0.as_ref()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    /// An iobuf created from an empty buffer should also be empty.
    #[test]
    fn test_from_empty_bufext() {
        let buf = Bytes::new();
        let iobuf = IOBufShared::from_bufext(buf);
        assert_eq!(iobuf.chain_len(), 1);
        let cursor = iobuf.cursor();
        assert!(!cursor.has_remaining());
    }

    /// An iobuf created from a (potentially non-contiguous) buffer should
    /// contain the same data as the buffer.
    #[test]
    fn test_from_bufext() {
        let buf = {
            let first = Bytes::from(vec![1, 2, 3]);
            let second = Bytes::from(vec![4, 5]);
            let third = Bytes::new();
            first.chain(second).chain(third)
        };
        let iobuf = IOBufShared::from_bufext(buf);
        // It's not stricly required to have a chain length of 2, but it's
        // (usually) more efficient to chain iobufs corresponding to the source
        // chunks than it is to concatenate/copy the source chunks, and the
        // "third chunk" is empty.
        assert_eq!(iobuf.chain_len(), 2);
        let mut cursor = iobuf.cursor();
        for x in 1..=5 {
            assert_eq!(cursor.get_u8(), x);
        }
        assert!(!cursor.has_remaining());
    }
}
