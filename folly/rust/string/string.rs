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

// This is being included in the bindgen-generated crate
// to add extra methods and traits to the types it generates, and
// add some additional helper types/traits.
//
// It can't be easily split into multiple files because:
// - it takes advantage of being in the same crate/module as the generated code, and
// - the current rust_bindgen_library() rule doesn't allow multiple files to be added as deps

use std::fmt;
use std::fmt::Debug;
use std::marker::PhantomData;
use std::os::raw::c_char;
use std::slice;
use std::str;

use cxx::ExternType;
use cxx::type_id;

#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct StringPiece<'a>(folly_StringPiece, PhantomData<&'a ()>);

impl<'a> StringPiece<'a> {
    pub fn len(&self) -> usize {
        unsafe { facebook_rust_stringpiece_size(&self.0) }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn as_slice(&self) -> &'a [u8] {
        unsafe {
            slice::from_raw_parts(
                facebook_rust_stringpiece_start(&self.0) as *const u8,
                facebook_rust_stringpiece_size(&self.0),
            )
        }
    }

    /// Wrap a raw `folly_StringPiece`
    ///
    /// Since `folly_StringPiece` does not know the lifetime of the data it refers to, the caller
    /// has to manually verify that the passed object refers to valid data for the whole lifetime
    /// of the constructed `StringPiece`.
    pub unsafe fn from_raw(s: folly_StringPiece) -> Self {
        StringPiece(s, PhantomData)
    }

    pub fn as_inner(&self) -> &folly_StringPiece {
        &self.0
    }

    pub fn into_inner(self) -> folly_StringPiece {
        self.0
    }
}

unsafe impl<'a> ExternType for StringPiece<'a> {
    type Id = type_id!("folly::StringPiece");
    type Kind = cxx::kind::Trivial;
}

impl<'a> Debug for StringPiece<'a> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "StringPiece({:?})", self.as_slice())
    }
}

impl<'a> From<&'a str> for StringPiece<'a> {
    fn from(s: &str) -> StringPiece {
        unsafe {
            let sp = facebook_rust_make_stringpiece(s.as_ptr() as *const c_char, s.len());
            StringPiece(sp, PhantomData)
        }
    }
}

impl<'a> From<&'a [u8]> for StringPiece<'a> {
    fn from(s: &[u8]) -> StringPiece {
        unsafe {
            let sp = facebook_rust_make_stringpiece(s.as_ptr() as *const c_char, s.len());
            StringPiece(sp, PhantomData)
        }
    }
}

impl<'a> From<&'a [i8]> for StringPiece<'a> {
    fn from(s: &[i8]) -> StringPiece {
        unsafe {
            let sp = facebook_rust_make_stringpiece(s.as_ptr() as *const c_char, s.len());
            StringPiece(sp, PhantomData)
        }
    }
}

impl<'a> From<&'a String> for StringPiece<'a> {
    fn from(s: &'a String) -> Self {
        Self::from(s.as_str())
    }
}

impl<'a> From<StringPiece<'a>> for &'a [u8] {
    fn from(v: StringPiece<'a>) -> Self {
        v.as_slice()
    }
}

pub struct MutableStringPiece<'a>(folly_MutableStringPiece, PhantomData<&'a ()>);

impl<'a> MutableStringPiece<'a> {
    pub fn len(&self) -> usize {
        unsafe { facebook_rust_mutablestringpiece_size(&self.0) }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe {
            slice::from_raw_parts(
                facebook_rust_mutablestringpiece_start(&self.0) as *const u8,
                facebook_rust_mutablestringpiece_size(&self.0),
            )
        }
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        unsafe {
            slice::from_raw_parts_mut(
                facebook_rust_mutablestringpiece_start_mut(&mut self.0) as *mut u8,
                facebook_rust_mutablestringpiece_size(&self.0),
            )
        }
    }

    pub fn into_inner(self) -> folly_MutableStringPiece {
        self.0
    }
}

unsafe impl<'a> ExternType for MutableStringPiece<'a> {
    type Id = type_id!("folly::MutableStringPiece");
    type Kind = cxx::kind::Trivial;
}

impl<'a> Debug for MutableStringPiece<'a> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "MutableStringPiece({:?})", self.as_slice())
    }
}

impl<'a> From<&'a mut [u8]> for MutableStringPiece<'a> {
    fn from(s: &mut [u8]) -> MutableStringPiece {
        let sp = unsafe {
            facebook_rust_make_mutablestringpiece(s.as_mut_ptr() as *mut c_char, s.len())
        };
        MutableStringPiece(sp, PhantomData)
    }
}

impl<'a> From<&'a mut [i8]> for MutableStringPiece<'a> {
    fn from(s: &mut [i8]) -> MutableStringPiece {
        let sp = unsafe {
            facebook_rust_make_mutablestringpiece(s.as_mut_ptr() as *mut c_char, s.len())
        };
        MutableStringPiece(sp, PhantomData)
    }
}

impl<'a> From<&'a mut MutableStringPiece<'a>> for &'a mut [u8] {
    fn from(v: &'a mut MutableStringPiece<'a>) -> Self {
        v.as_mut_slice()
    }
}

#[derive(Copy, Clone)]
pub struct ByteRange<'a>(folly_ByteRange, PhantomData<&'a ()>);

impl<'a> ByteRange<'a> {
    pub fn len(&self) -> usize {
        unsafe { facebook_rust_byterange_size(&self.0) }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn as_slice(&self) -> &'a [u8] {
        unsafe {
            slice::from_raw_parts(
                facebook_rust_byterange_start(&self.0),
                facebook_rust_byterange_size(&self.0),
            )
        }
    }

    pub fn into_inner(self) -> folly_ByteRange {
        self.0
    }
}

unsafe impl<'a> ExternType for ByteRange<'a> {
    type Id = type_id!("folly::ByteRange");
    type Kind = cxx::kind::Trivial;
}

impl<'a> Debug for ByteRange<'a> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "ByteRange({:?})", self.as_slice())
    }
}

impl<'a> From<&'a str> for ByteRange<'a> {
    fn from(s: &str) -> ByteRange {
        unsafe {
            ByteRange(
                facebook_rust_make_byterange(s.as_ptr(), s.len()),
                PhantomData,
            )
        }
    }
}

impl<'a> From<&'a [u8]> for ByteRange<'a> {
    fn from(s: &[u8]) -> ByteRange {
        unsafe {
            ByteRange(
                facebook_rust_make_byterange(s.as_ptr(), s.len()),
                PhantomData,
            )
        }
    }
}

impl<'a> From<&'a [i8]> for ByteRange<'a> {
    fn from(s: &[i8]) -> ByteRange {
        unsafe {
            ByteRange(
                facebook_rust_make_byterange(s.as_ptr() as *const u8, s.len()),
                PhantomData,
            )
        }
    }
}

impl<'a> From<ByteRange<'a>> for &'a [u8] {
    fn from(v: ByteRange<'a>) -> Self {
        v.as_slice()
    }
}

pub struct MutableByteRange<'a>(folly_MutableByteRange, PhantomData<&'a ()>);

impl<'a> MutableByteRange<'a> {
    pub fn len(&self) -> usize {
        unsafe { facebook_rust_mutablebyterange_size(&self.0) }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe {
            slice::from_raw_parts(
                facebook_rust_mutablebyterange_start(&self.0),
                facebook_rust_mutablebyterange_size(&self.0),
            )
        }
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        unsafe {
            slice::from_raw_parts_mut(
                facebook_rust_mutablebyterange_start_mut(&mut self.0),
                facebook_rust_mutablebyterange_size(&self.0),
            )
        }
    }

    pub fn into_inner(self) -> folly_MutableByteRange {
        self.0
    }
}

unsafe impl<'a> ExternType for MutableByteRange<'a> {
    type Id = type_id!("folly::MutableByteRange");
    type Kind = cxx::kind::Trivial;
}

impl<'a> Debug for MutableByteRange<'a> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "MutableByteRange({:?})", self.as_slice())
    }
}

impl<'a> From<&'a mut [u8]> for MutableByteRange<'a> {
    fn from(s: &mut [u8]) -> MutableByteRange {
        let sp = unsafe { facebook_rust_make_mutablebyterange(s.as_mut_ptr(), s.len()) };
        MutableByteRange(sp, PhantomData)
    }
}

impl<'a> From<&'a mut [i8]> for MutableByteRange<'a> {
    fn from(s: &mut [i8]) -> MutableByteRange {
        let sp = unsafe { facebook_rust_make_mutablebyterange(s.as_mut_ptr() as *mut u8, s.len()) };
        MutableByteRange(sp, PhantomData)
    }
}

impl<'a> From<&'a mut MutableByteRange<'a>> for &'a mut [u8] {
    fn from(v: &'a mut MutableByteRange<'a>) -> Self {
        unsafe {
            slice::from_raw_parts_mut(
                facebook_rust_mutablebyterange_start_mut(&mut v.0),
                facebook_rust_mutablebyterange_size(&v.0),
            )
        }
    }
}

impl<'a> From<&'a mut MutableByteRange<'a>> for &'a mut [i8] {
    fn from(v: &'a mut MutableByteRange<'a>) -> Self {
        unsafe {
            slice::from_raw_parts_mut(
                facebook_rust_mutablebyterange_start(&v.0) as *mut i8,
                facebook_rust_mutablebyterange_size(&v.0),
            )
        }
    }
}
