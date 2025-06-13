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

use std::cmp;
use std::ops::Deref;
use std::slice;

use bytes::Buf;
use bytes::BufMut;
use bytes::buf::UninitSlice;
use fbthrift::BufExt;
use fbthrift::BufMutExt;
use fbthrift::DeserializeSource;
use fbthrift::framing::Framing;
use iobuf_sys::root::folly::IOBuf as IOBufSys;

use crate::iobuf::IOBufShared;
use crate::iobuf::RawIOBufPtr;
use crate::iobufmut::IOBufMut;

/// Read-only cursor for IOBuf and IOBufMut
///
/// Takes ownership of the underlying buffer.
#[derive(Debug)]
pub struct IOBufCursor<T> {
    head: T,              // head buffer
    cur: *const IOBufSys, // current buffer
    off: usize,           // offset from data into buffer
}

impl<T: RawIOBufPtr + Clone> Clone for IOBufCursor<T> {
    /// Create an independent cursor starting at the current position. Whether
    /// data is copied depends on the underlying IOBuf.
    fn clone(&self) -> Self {
        // Clone the chain and slide the current buffer down to mirror the
        // source's current buffer.
        let new_head = self.head.clone();
        let mut new_cur = new_head.get_ptr();
        let mut old_cur = self.head.get_ptr();
        while old_cur != self.cur {
            unsafe {
                old_cur = (*old_cur).next_;
                new_cur = (*new_cur).next_;
            }
        }
        Self {
            head: new_head,
            cur: new_cur,
            off: self.off,
        }
    }
}

unsafe impl<T> Send for IOBufCursor<T> {}

impl<T: RawIOBufPtr> From<T> for IOBufCursor<T> {
    fn from(head: T) -> Self {
        let ptr = head.get_ptr();
        let mut cur = IOBufCursor {
            head,
            cur: ptr,
            off: 0,
        };

        cur.skip_empty_bufs();
        cur
    }
}

impl<T: RawIOBufPtr> IOBufCursor<T> {
    fn cur_len(&self) -> usize {
        unsafe { (*self.cur).length_ }
    }

    pub fn remaining_buf(&self) -> usize {
        debug_assert!(
            self.off <= self.cur_len(),
            "off={} cur_len={}",
            self.off,
            self.cur_len()
        );
        self.cur_len() - self.off
    }

    pub(crate) fn data(&self) -> &[u8] {
        unsafe {
            let data = (*self.cur).data_;
            slice::from_raw_parts(data, self.cur_len())
        }
    }

    fn skip_empty_bufs(&mut self) {
        loop {
            if self.remaining_buf() > 0 {
                break;
            }
            if !self.step() {
                break;
            }
        }
    }

    fn next(&self) -> *const IOBufSys {
        unsafe { (*self.cur).next_ }
    }

    // move cur to next if possible, returns true if we stepped
    pub(crate) fn step(&mut self) -> bool {
        if self.next() != self.head.get_ptr() {
            self.cur = self.next();
            self.off = 0;
            true
        } else {
            false
        }
    }
}

impl IOBufCursor<IOBufShared> {
    pub fn into_inner(self) -> IOBufShared {
        self.head
    }

    pub fn reset(self) -> Self {
        self.into_inner().cursor()
    }
}

impl BufExt for IOBufCursor<IOBufShared> {
    fn can_advance(&self, n: usize) -> bool {
        self.shortfall(n) == 0
    }

    fn shortfall(&self, n: usize) -> usize {
        let head_ptr = self.head.get_ptr();
        let mut ptr: *const IOBufSys = unsafe { (*self.cur).next_ };

        // Compute number of remaining bytes in buffer chain, but terminate
        // early as soon as it is known to be >=n.
        let mut remaining = self.remaining_buf();
        while remaining < n {
            if ptr == head_ptr {
                return n - remaining;
            }
            remaining += unsafe { (*ptr).length_ };
            ptr = unsafe { (*ptr).next_ };
        }

        // We know self.remaining() >= n.
        0
    }
}

impl From<IOBufCursor<IOBufShared>> for DeserializeSource<IOBufCursor<IOBufShared>> {
    fn from(buf: IOBufCursor<IOBufShared>) -> Self {
        DeserializeSource::new(buf)
    }
}

impl IOBufCursor<IOBufMut> {
    pub fn into_inner(self) -> IOBufMut {
        self.head
    }
}

impl<T: RawIOBufPtr> Buf for IOBufCursor<T> {
    fn remaining(&self) -> usize {
        let mut total = self.remaining_buf();

        unsafe {
            let head_ptr = self.head.get_ptr();
            let mut ptr: *const IOBufSys = (*self.cur).next_;
            while ptr != head_ptr {
                total += (*ptr).length_;
                ptr = (*ptr).next_;
            }
        }

        total
    }

    fn has_remaining(&self) -> bool {
        self.remaining_buf() > 0
            || unsafe {
                let head_ptr = self.head.get_ptr();
                let mut ptr: *const IOBufSys = (*self.cur).next_;
                loop {
                    if ptr == head_ptr {
                        break false;
                    }
                    if (*ptr).length_ > 0 {
                        break true;
                    }
                    ptr = (*ptr).next_;
                }
            }
    }

    fn chunk(&self) -> &[u8] {
        &self.data()[self.off..]
    }

    fn advance(&mut self, cnt: usize) {
        let orig = cnt;
        let mut cnt = cnt;

        loop {
            let delta = cmp::min(self.remaining_buf(), cnt);
            self.off += delta;
            cnt -= delta;

            if cnt == 0 {
                // If we've consumed this buffer, move to the next one (if possible) so that
                // `bytes()` returns a non-empty slice.
                if self.remaining_buf() == 0 {
                    self.skip_empty_bufs();
                }
                // all done
                break;
            }

            // next buffer
            if !self.step() {
                // run out of buffers
                panic!("Buffer too short to advance {} by {} bytes", orig, cnt);
            }
        }
    }
}

impl<T: RawIOBufPtr> std::io::Read for IOBufCursor<T> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.skip_empty_bufs();

        let n = cmp::min(buf.len(), self.remaining_buf());
        buf[..n].copy_from_slice(&self.data()[self.off..self.off + n]);
        self.off += n;

        Ok(n)
    }
}

impl<T: RawIOBufPtr> std::io::BufRead for IOBufCursor<T> {
    fn fill_buf(&mut self) -> std::io::Result<&[u8]> {
        self.skip_empty_bufs();
        Ok(&self.data()[self.off..])
    }

    fn consume(&mut self, amt: usize) {
        self.advance(amt);
    }
}

/// Version of IOBufCursor with the remaining length cached,
/// to avoid walking the whole buffer chain on each remaining call.
#[derive(Debug)]
pub struct IOBufCursorFastRemaining<T> {
    inner: IOBufCursor<T>,
    remaining: usize,
}

impl<T> Clone for IOBufCursorFastRemaining<T>
where
    IOBufCursor<T>: Clone,
{
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
            remaining: self.remaining,
        }
    }
}

impl<T: RawIOBufPtr> From<IOBufCursor<T>> for IOBufCursorFastRemaining<T> {
    fn from(inner: IOBufCursor<T>) -> Self {
        let remaining = inner.remaining();
        Self { inner, remaining }
    }
}

impl BufExt for IOBufCursorFastRemaining<IOBufShared> {}

impl<T: RawIOBufPtr> Buf for IOBufCursorFastRemaining<T> {
    fn remaining(&self) -> usize {
        self.remaining
    }

    fn chunk(&self) -> &[u8] {
        self.inner.chunk()
    }

    fn advance(&mut self, cnt: usize) {
        self.inner.advance(cnt);
        self.remaining -= cnt;
    }
}

/// Cursor for writing an IOBufMut chain.
///
/// This appends to the end of the data on the first
/// IOBufMut. Once the first buffer is full, it moves onto the later ones.
/// # Panics
/// If a buffer in the chain is not empty when we advance to it, panic rather than
/// overwriting it. It is up to the caller to make sure the IOBufMut chain is empty.
/// (Maybe revisit this, depending on how it works out in practice.)
#[derive(Debug)]
pub struct IOBufMutCursor(IOBufCursor<IOBufMut>);

impl IOBufMutCursor {
    pub(crate) fn new(buf: IOBufMut) -> IOBufMutCursor {
        // Note - we don't use IOBufMutCursor(IOBufCursor::from(buf)) because
        // we _don't_ want to skip 0-length buffers, we expect to have no
        // 0-capacity buffers in a mutable chain.
        let ptr = buf.get_ptr();
        IOBufMutCursor(IOBufCursor {
            head: buf,
            cur: ptr,
            off: 0,
        })
    }

    pub fn into_inner(self) -> IOBufMut {
        self.0.into_inner()
    }

    fn cur_capacity(&self) -> usize {
        unsafe { (*self.0.cur).capacity_ }
    }

    fn cur_tail(&self) -> usize {
        self.cur_capacity() - self.0.cur_len()
    }

    pub(crate) fn data_mut(&mut self) -> &mut [u8] {
        unsafe { slice::from_raw_parts_mut((*self.0.cur).data_, self.cur_capacity()) }
    }

    pub(crate) fn step_mut(&mut self) -> bool {
        let ret = self.0.step();

        if ret && self.0.remaining_buf() != 0 {
            panic!("Stepped into non-empty IOBuf");
        }

        ret
    }
}

impl Deref for IOBufMutCursor {
    type Target = IOBufCursor<IOBufMut>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// SAFETY: Only calls advance_mut can influence the return value of remaining_mut.
unsafe impl BufMut for IOBufMutCursor {
    fn remaining_mut(&self) -> usize {
        let mut rem = self.cur_tail();

        unsafe {
            let head_ptr = self.0.head.get_ptr();
            let mut ptr = (*self.0.cur).next_ as *const IOBufSys;
            while ptr != head_ptr {
                rem += (*ptr).capacity_;
                ptr = (*ptr).next_ as *const IOBufSys;
            }
        }

        rem
    }

    fn has_remaining_mut(&self) -> bool {
        self.cur_tail() > 0 || self.0.next() != self.0.head.get_ptr()
    }

    unsafe fn advance_mut(&mut self, cnt: usize) {
        unsafe {
            let orig = cnt;
            let mut cnt = cnt;

            loop {
                let delta = cmp::min(self.cur_tail(), cnt);
                (*(self.0.cur as *mut IOBufSys)).length_ += delta;
                cnt -= delta;

                if cnt == 0 {
                    // If we've consumed this buffer, move to the next one (if possible) so that
                    // `bytes()` returns a non-empty slice.
                    if self.cur_tail() == 0 {
                        let _ = self.step_mut();
                    }
                    // all done
                    break;
                }

                // next buffer
                if !self.step_mut() {
                    // run out of buffers
                    panic!("Buffer too short to advance_mut {} by {} bytes", orig, cnt);
                }
            }
        }
    }

    fn chunk_mut(&mut self) -> &mut UninitSlice {
        let cur_len = self.0.cur_len();
        let rest = &mut self.data_mut()[cur_len..];
        unsafe { UninitSlice::from_raw_parts_mut(rest.as_mut_ptr(), rest.len()) }
    }
}

impl BufMutExt for IOBufMutCursor {
    type Final = IOBufMut;

    fn finalize(self) -> Self::Final {
        self.into_inner()
    }
}

impl Framing for IOBufShared {
    type EncBuf = IOBufMutCursor;
    type DecBuf = IOBufCursorFastRemaining<IOBufShared>;

    fn enc_with_capacity(cap: usize) -> Self::EncBuf {
        let inner = IOBufMut::with_capacity(cap);
        IOBufMutCursor::new(inner)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    /// A cloned cursor should read independently from its source and start at
    /// the source's position when it was cloned.
    #[test]
    fn test_cursor_clone() {
        let mut cursor = {
            let mut iobuf = IOBufShared::from(vec![1, 2]);
            iobuf.append_to_end(IOBufShared::from(vec![3, 4]));
            iobuf.cursor()
        };
        assert_eq!(cursor.get_u8(), 1);
        cursor.advance(2);
        let mut cloned_cursor = cursor.clone();
        assert_eq!(cursor.get_u8(), 4);
        assert_eq!(cloned_cursor.get_u8(), 4);
    }

    #[test]
    fn test_cursor_read() {
        use std::io::Read;

        let mut cursor = {
            let mut iobuf = IOBufShared::from(vec![1, 2]);
            iobuf.append_to_end(IOBufShared::from(vec![3, 4]));
            iobuf.append_to_end(IOBufShared::from(vec![]));
            iobuf.append_to_end(IOBufShared::from(vec![5, 6, 7]));
            iobuf.cursor()
        };

        let mut buf = [0; 3];
        assert_eq!(cursor.read(&mut buf).unwrap(), 2);
        assert_eq!(buf, [1, 2, 0]);
        assert_eq!(cursor.read(&mut buf).unwrap(), 2);
        assert_eq!(buf, [3, 4, 0]);
        assert_eq!(cursor.read(&mut buf).unwrap(), 3);
        assert_eq!(buf, [5, 6, 7]);
    }

    #[test]
    fn test_cursor_bufread() {
        use std::io::BufRead;

        let mut cursor = {
            let mut iobuf = IOBufShared::from(vec![1, 2]);
            iobuf.append_to_end(IOBufShared::from(vec![3, 4]));
            iobuf.append_to_end(IOBufShared::from(vec![]));
            iobuf.append_to_end(IOBufShared::from(vec![5, 6, 7]));
            iobuf.cursor()
        };

        {
            let mut buf = vec![];
            assert_eq!(cursor.clone().read_until(6, &mut buf).unwrap(), 6);
            assert_eq!(buf, [1, 2, 3, 4, 5, 6]);
        }

        {
            let mut buf = vec![];
            assert_eq!(cursor.skip_until(3).unwrap(), 3);
            assert_eq!(cursor.read_until(6, &mut buf).unwrap(), 3);
            assert_eq!(buf, [4, 5, 6]);
        }
    }

    /// Cloned fast cursors should behave independently, like normal cursors,
    /// even while caching the number of bytes remaining.
    #[test]
    fn test_fast_cursor_clone() {
        let mut cursor = {
            let mut iobuf = IOBufShared::from(vec![1, 2]);
            iobuf.append_to_end(IOBufShared::from(vec![3, 4]));
            iobuf.cursor()
        };
        cursor.advance(1);
        let mut cloned_cursor = cursor.clone();
        assert_eq!(cursor.get_u8(), 2);
        assert_eq!(cloned_cursor.get_u8(), 2);
        assert_eq!(cursor.remaining(), 2);
        assert_eq!(cloned_cursor.remaining(), 2);
    }
}
