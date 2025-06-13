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

pub use string_sys::ByteRange;
pub use string_sys::MutableByteRange;
pub use string_sys::MutableStringPiece;
pub use string_sys::StringPiece;

/// Raw mangled names
pub mod ffi {
    pub use string_sys::folly_ByteRange;
    pub use string_sys::folly_MutableByteRange;
    pub use string_sys::folly_MutableStringPiece;
    pub use string_sys::folly_StringPiece;
}

/// folly namespace
pub mod folly {
    pub use string_sys::folly_ByteRange as ByteRange;
    pub use string_sys::folly_MutableByteRange as MutableByteRange;
    pub use string_sys::folly_MutableStringPiece as MutableStringPiece;
    pub use string_sys::folly_StringPiece as StringPiece;
}

/// Rust helper namespace
pub mod facebook {
    pub mod rust {
        pub use string_sys::facebook_rust_byterange_end as byterange_end;
        pub use string_sys::facebook_rust_byterange_size as byterange_size;
        pub use string_sys::facebook_rust_byterange_start as byterange_start;
        pub use string_sys::facebook_rust_make_byterange as make_byterange;
        pub use string_sys::facebook_rust_make_mutablebyterange as make_mutablebyterange;
        pub use string_sys::facebook_rust_make_mutablestringpiece as make_mutablestringpiece;
        pub use string_sys::facebook_rust_make_stringpiece as make_stringpiece;
        pub use string_sys::facebook_rust_mutablebyterange_end as mutablebyterange_end;
        pub use string_sys::facebook_rust_mutablebyterange_size as mutablebyterange_size;
        pub use string_sys::facebook_rust_mutablebyterange_start as mutablebyterange_start;
        pub use string_sys::facebook_rust_mutablestringpiece_end as mutablestringpiece_end;
        pub use string_sys::facebook_rust_mutablestringpiece_size as mutablestringpiece_size;
        pub use string_sys::facebook_rust_mutablestringpiece_start as mutablestringpiece_start;
        pub use string_sys::facebook_rust_stringpiece_end as stringpiece_end;
        pub use string_sys::facebook_rust_stringpiece_size as stringpiece_size;
        pub use string_sys::facebook_rust_stringpiece_start as stringpiece_start;
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn str_simple() {
        let s = "hello, world";

        let sp = StringPiece::from(s);
        assert_eq!(sp.len(), 12);

        assert_eq!(s.as_bytes(), sp.as_slice());
    }

    #[test]
    fn vec_simple() {
        let s = [1u8, 2, 3, 4, 5];

        let sp = StringPiece::from(&s[..]);
        assert_eq!(sp.len(), 5);
        assert_eq!(&s[..], sp.as_slice());
    }

    #[test]
    fn vec_mut() {
        let mut s = vec![1u8, 2, 3, 4, 5];

        {
            let mut sp = MutableStringPiece::from(&mut s[..]);
            assert_eq!(sp.len(), 5);
            let spl = sp.as_mut_slice();
            spl[0] = 0xff;
        }

        assert_eq!(s, vec![0xff, 2, 3, 4, 5]);
    }
}
