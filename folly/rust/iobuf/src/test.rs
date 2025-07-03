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
use std::collections::BTreeMap;

use bytes::Buf;
use bytes::BufMut;
use fbthrift::BinaryProtocol;
use fbthrift::binary_protocol;
use fbthrift::deserialize::Deserialize;
use fbthrift::protocol::Protocol;
use fbthrift_test_if::En;
use fbthrift_test_if::MainStruct;
use fbthrift_test_if::Small;
use fbthrift_test_if::SubStruct;
use fbthrift_test_if::Un;
use fbthrift_test_if::UnOne;
use quickcheck::TestResult;
use quickcheck::quickcheck;

use super::*;

#[test]
fn simple_lifetime() {
    let buf = IOBufMut::with_capacity(100);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 1);
}

#[test]
fn simple_mut_ro() {
    let buf = IOBufMut::with_capacity(100);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 1);

    let buf = IOBufShared::from(buf);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 1);
}

#[test]
fn simple_mut_ro_chain() {
    let buf = IOBufMut::with_chain(100, 50);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 2);

    let buf = IOBufShared::from(buf);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 2);
}

#[test]
fn simple_bufmut() {
    let buf = IOBufMut::with_capacity(100);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 1);

    let buf = {
        let mut cur: IOBufMutCursor = buf.cursor_mut();
        let rem: usize = cur.remaining_mut();
        assert!(rem >= 100, "remaining_mut {} < 100", rem);
        cur.put_slice(b"hello, world");
        assert_eq!(cur.remaining_mut(), rem - 12);
        cur.into_inner()
    };

    assert_eq!(buf.len(), 12);
    assert_eq!(buf.chain_len(), 1);

    {
        let mut cur = buf.cursor();

        assert_eq!(cur.remaining(), 12);
        assert_eq!(b"hello, world", cur.chunk());
        cur.advance(12);
        assert_eq!(cur.remaining(), 0);
    }
}

#[test]
fn simple_bufmut_chain() {
    let buf = IOBufMut::with_chain(100, 50);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 2);

    let buf = {
        let mut cur: IOBufMutCursor = buf.cursor_mut();
        let rem: usize = cur.remaining_mut();
        assert!(rem >= 100, "remaining_mut {} < 100", rem);
        cur.put_slice(b"hello, world");
        assert_eq!(cur.remaining_mut(), rem - 12);
        cur.into_inner()
    };

    assert_eq!(buf.len(), 12);
    assert_eq!(buf.chain_len(), 2);

    {
        let mut cur = buf.cursor();

        assert_eq!(cur.remaining(), 12);
        assert_eq!(b"hello, world", cur.chunk());
        cur.advance(12);
        assert_eq!(cur.remaining(), 0);
    }
}

#[test]
fn fill_bufmut_chain() {
    let buf = IOBufMut::with_chain(100, 50);
    assert_eq!(buf.len(), 0);
    assert_eq!(buf.chain_len(), 2);

    let written_len;
    let buf = {
        let mut cur: IOBufMutCursor = buf.cursor_mut();
        let rem: usize = cur.remaining_mut();
        assert!(rem >= 100, "remaining_mut {} < 100", rem);
        written_len = cur.chunk_mut().len() + 1;
        let vec = vec![5; written_len];
        cur.put_slice(vec.as_ref());
        assert_eq!(cur.remaining_mut(), rem - written_len);
        cur.into_inner()
    };

    {
        let mut cur = buf.cursor();

        assert_eq!(cur.remaining(), written_len);
        cur.advance(written_len);
        assert_eq!(cur.remaining(), 0);
    }
}

#[test]
fn from_vec() {
    let mut v = b"hello, world".to_vec();
    v.reserve(100);

    let buf = IOBufMut::from(v);

    assert_eq!(buf.len(), 12);
    assert_eq!(buf.chain_len(), 1);

    let buf = {
        let cur = buf.cursor();
        assert_eq!(cur.remaining(), 12);
        assert_eq!(cur.chunk(), b"hello, world");
        cur.into_inner()
    };

    {
        let cur = buf.cursor_mut();
        assert!(
            cur.remaining_mut() >= 100,
            "cap missing {}",
            cur.remaining_mut()
        );
    }
}

#[test]
fn append() {
    let buf1 = IOBufMut::with_capacity(100);
    let mut buf1 = {
        let mut cur1 = buf1.cursor_mut();
        cur1.put_slice(b"hello,");
        cur1.into_inner()
    };

    assert_eq!(buf1.len(), 6);
    assert_eq!(buf1.chain_len(), 1);

    let buf2 = IOBufMut::with_capacity(100);
    let buf2 = {
        let mut cur2 = buf2.cursor_mut();
        cur2.put_slice(b" world");
        cur2.into_inner()
    };

    assert_eq!(buf2.len(), 6);
    assert_eq!(buf2.chain_len(), 1);

    buf1.append_to_end(buf2);

    assert_eq!(buf1.len(), 12);
    assert_eq!(buf1.chain_len(), 2);

    {
        let mut cur = buf1.cursor();

        assert_eq!(cur.remaining(), 12);
        assert_eq!(cur.remaining_buf(), 6);
        assert!(cur.has_remaining());
        assert_eq!(cur.chunk(), b"hello,");
        cur.advance(6);

        assert_eq!(cur.remaining(), 6, "cur {:?}", cur);
        assert_eq!(cur.remaining_buf(), 6);
        assert!(cur.has_remaining());
        assert_eq!(cur.chunk(), b" world");
        cur.advance(6);

        assert_eq!(cur.remaining(), 0);
        assert_eq!(cur.remaining_buf(), 0);
        assert!(!cur.has_remaining());
    }
}

#[test]
#[should_panic(expected = "advance out of bounds")]
fn overflow() {
    let buf = IOBufMut::with_capacity(100);

    let d = vec![b'x'; 1000];

    let mut cur = buf.cursor_mut();

    assert!(cur.remaining_mut() < d.len());
    cur.put_slice(d.as_ref());
}

#[test]
fn advance_mut() {
    let buf = IOBufMut::with_capacity(100);

    let mut cur = buf.cursor_mut();
    let cap = cur.remaining_mut();
    unsafe {
        cur.advance_mut(cap);
    }
}

#[test]
#[should_panic(expected = "Buffer too short to advance_mut")]
fn advance_mut_overflow() {
    let buf = IOBufMut::with_capacity(100);

    let mut cur = buf.cursor_mut();
    let cap = cur.remaining_mut();
    unsafe {
        cur.advance_mut(cap + 1);
    }
}

#[test]
fn advance() {
    let buf = IOBufShared::from(b"hello".to_vec());

    let mut cur = buf.cursor();
    cur.advance(5);
    assert_eq!(cur.remaining(), 0);
    assert!(!cur.has_remaining());
}

#[test]
fn advance_with_empty() {
    let mut buf = IOBufShared::from(b"hello".to_vec());
    buf.insert_after(b" world".to_vec());
    buf.insert_after(b"".to_vec());

    let mut cur = buf.cursor();
    cur.advance(5);
    assert_eq!(cur.remaining(), 6);
    assert_eq!(cur.chunk()[0], b' ');
}

#[test]
#[should_panic(expected = "Buffer too short to advance ")]
fn advance_overflow() {
    let buf = IOBufShared::from(b"hello".to_vec());

    let mut cur = buf.cursor();
    cur.advance(5 + 1);
}

#[test]
fn iobuf_eq() {
    let buf = IOBufShared::from(b"hello".to_vec());
    assert_eq!(buf, buf.clone());
}

quickcheck! {
    fn qc_chain(v: Vec<Vec<u8>>) -> bool {
        let len: usize = v.iter().map(Vec::len).sum();
        let mut vi = v.clone().into_iter();
        let first = vi.next().unwrap_or_default();
        let buf = vi
            .fold(IOBufShared::from(first), |mut s, v| {
                let b = IOBufShared::from(v); s.append_to_end(b); s
            });

        let ret = buf.len() == len;
        let chain = buf.chain_len() == cmp::max(1, v.len());
        if !ret {
            println!("buf.len {} len {}", buf.len(), len);
        }
        if !chain {
            println!("buf.chain_len {} clen {}", buf.chain_len(), cmp::max(1, v.len()));
        }

        ret && chain
    }

    fn qc_vec_roundtrip(v: Vec<Vec<u8>>) -> bool {
        let mut vi = v.clone().into_iter();
        let first = vi.next().unwrap_or_default();
        let iobuf = vi
            .fold(IOBufShared::from(first), |mut s, v| {
                let b = IOBufShared::from(v); s.append_to_end(b); s
            });

        let flatten: Vec<u8> = v.into_iter().flatten().collect();
        let cvt = Vec::from(iobuf);

        flatten == cvt
    }

    fn qc_remaining(v: Vec<Vec<u8>>) -> bool {
        let mut vi = v.into_iter();
        let first = vi.next().unwrap_or_default();
        let buf = vi
            .fold(IOBufShared::from(first), |mut s, v| {
                let b = IOBufShared::from(v); s.append_to_end(b); s
            });

        let cur = buf.cursor();

        cur.has_remaining() == (cur.remaining() != 0)
    }

    fn qc_advance_with_empty(v: Vec<Vec<u8>>) -> bool {
        // iterate over pieces
        let mut vi = v.into_iter();
        let first = vi.next().unwrap_or_default();
        let buf = vi
            .fold(IOBufShared::from(first), |mut s, v| {
                let b = IOBufShared::from(v); s.append_to_end(b); s
            });

        let len = buf.len();
        let mut idx = 0;
        let mut cur = buf.cursor();

        while idx < len {
            if cur.chunk().is_empty() {
                return false;
            }
            cur.advance(1);
            idx += 1;
        }

        true
    }

    // Given a chain of chunks, step through in step sized steps, making
    // sure we see the same as if the chain is flattened.
    fn qc_advance(v: Vec<Vec<u8>>, step: usize) -> TestResult {
        if step == 0 { return TestResult::discard() }

        // make flat list
        let flat: Vec<_> = v.clone().into_iter().flatten().collect();

        // iterate over pieces
        let mut vi = v.clone().into_iter();
        let first = vi.next().unwrap_or_default();
        let buf = vi
            .fold(IOBufShared::from(first), |mut s, v| {
                let b = IOBufShared::from(v); s.append_to_end(b); s
            });

        let len = buf.len();
        let mut idx = 0;
        let mut cur = buf.cursor();

        while idx < len {
            let s = cmp::min(step, cur.remaining());
            if s == 0 {
                println!("s={} step={} cur={:?}", s, step, cur);
                return TestResult::failed();
            }
            let bl;
            {
                let b = &cur.chunk()[..cmp::min(s, cur.chunk().len())];
                bl = b.len();
                if b != &flat[idx..idx+bl] {
                    println!("s={} step={} cur={:?} b={:?} flat[{}..{}]={:?}",
                        s, step, cur, b, idx, idx+s, &flat[idx..idx+bl]);

                    return TestResult::failed();
                }
                idx += bl;
            }

            cur.advance(bl);
        }
        assert_eq!(idx, len);

        let ret = !cur.has_remaining();
        if !ret {
            println!("v={:?} step={} cur={:?} idx={} len={} ret={} remaining={} has_remaining={}",
                v, step, cur, idx, len, ret, cur.remaining(), cur.has_remaining());
        }
        TestResult::from_bool(ret)
    }
}

#[test]
fn test_send() {
    fn _assert_send<T: Send>() {}

    _assert_send::<IOBufShared>();
}

#[test]
fn test_thrift_framing_roundtrip() {
    // Build the big struct
    let mut m = BTreeMap::new();
    m.insert("m1".to_string(), 1);
    m.insert("m2".to_string(), 2);

    let sub = SubStruct {
        ..Default::default()
    };

    let u = Un::un1(UnOne {
        one: 1,
        ..Default::default()
    });
    let e = En::TWO;

    let mut int_keys = BTreeMap::new();
    int_keys.insert(42, 43);
    int_keys.insert(44, 45);

    let r = MainStruct {
        foo: "foo".to_string(),
        m,
        bar: "test".to_string(),
        s: sub,
        l: vec![
            Small {
                num: 1,
                two: 2,
                ..Default::default()
            },
            Small {
                num: 2,
                two: 3,
                ..Default::default()
            },
        ],
        u,
        e,
        int_keys,
        opt: None,
        ..Default::default()
    };

    // Serialize the struct and put it into an IOBuf
    let response = binary_protocol::serialize(r.clone());
    let as_iobuf = IOBufShared::from(response).cursor();

    // Deserialize the struct back out
    let mut de = BinaryProtocol::<IOBufShared>::deserializer(as_iobuf.into());
    let deserialized =
        MainStruct::rs_thrift_read(&mut de).expect("Failed to deserialize MainStruct");
    assert_eq!(r, deserialized);
}

#[test]
fn test_fast_remaining() {
    let buf = IOBufShared::from(b"hello".to_vec());
    let mut cur: IOBufCursorFastRemaining<IOBufShared> = buf.cursor().into();
    assert_eq!(cur.remaining(), 5);
    cur.advance(1);
    assert_eq!(cur.remaining(), 4);
    cur.advance(4);
    assert_eq!(cur.remaining(), 0);
    assert!(!cur.has_remaining());
}
