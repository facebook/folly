#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import array
import struct
import unittest

from folly.iobuf import IOBuf

from .iobuf_helper import (
    get_empty_chain,
    make_chain,
    to_uppercase_string,
    to_uppercase_string_heap,
)


class IOBufTests(unittest.TestCase):
    def test_empty_chain(self) -> None:
        ebuf = get_empty_chain()
        self.assertFalse(ebuf)
        self.assertTrue(ebuf.is_chained)
        self.assertEqual(len(ebuf), 0)
        self.assertEqual(ebuf.chain_size(), 0)
        self.assertEqual(ebuf.chain_count(), 8)
        self.assertEqual(b"".join(ebuf), b"")
        self.assertEqual(b"", bytes(ebuf))

    def test_chain(self) -> None:
        control = [b"facebook", b"thrift", b"python3", b"cython"]
        chain = make_chain([IOBuf(x) for x in control])
        self.assertTrue(chain.is_chained)
        self.assertTrue(chain)
        self.assertEqual(bytes(chain), control[0])
        self.assertEqual(len(chain), len(control[0]))
        self.assertEqual(chain.chain_size(), sum(len(x) for x in control))
        self.assertEqual(chain.chain_count(), len(control))
        # pyre-fixme[6]: For 1st argument expected `Buffer` but got `Optional[IOBuf]`.
        self.assertEqual(memoryview(chain.next), control[1])
        self.assertEqual(b"".join(chain), b"".join(control))

    def test_cyclic_chain(self) -> None:
        control = [b"aaa", b"aaaa"]
        chain = make_chain([IOBuf(x) for x in control])
        self.assertTrue(chain.is_chained)
        self.assertTrue(chain)
        self.assertEqual(bytes(chain), control[0])
        self.assertEqual(len(chain), len(control[0]))
        self.assertEqual(chain.chain_size(), sum(len(x) for x in control))
        self.assertEqual(chain.chain_count(), len(control))
        # pyre-fixme[6]: For 1st argument expected `Buffer` but got `Optional[IOBuf]`.
        self.assertEqual(memoryview(chain.next), control[1])
        self.assertEqual(b"".join(chain), b"".join(control))

    def test_hash(self) -> None:
        x = b"omg"
        y = b"wtf"
        xb = IOBuf(x)
        yb = IOBuf(y)
        hash(xb)
        self.assertNotEqual(hash(xb), hash(yb))
        self.assertEqual(hash(xb), hash(IOBuf(x)))

    def test_empty(self) -> None:
        x = b""
        xb = IOBuf(x)
        # pyre-fixme[6]: For 1st argument expected `Buffer` but got `IOBuf`.
        self.assertEqual(memoryview(xb), x)
        self.assertEqual(bytes(xb), x)
        self.assertFalse(xb)
        self.assertEqual(len(xb), len(x))

    def test_iter(self) -> None:
        x = b"testtest"
        xb = IOBuf(x)
        self.assertEqual(b"".join(iter(xb)), x)

    def test_bytes(self) -> None:
        x = b"omgwtfbbq"
        xb = IOBuf(x)
        self.assertEqual(bytes(xb), x)

    def test_cmp(self) -> None:
        x = IOBuf(b"abc")
        y = IOBuf(b"def")
        z = IOBuf(b"abc")
        self.assertEqual(x, z)
        self.assertNotEqual(x, y)
        self.assertLess(x, y)
        self.assertLessEqual(x, y)
        self.assertLessEqual(x, z)
        self.assertGreater(y, x)
        self.assertGreaterEqual(y, x)

    def test_typed(self) -> None:
        # pyre-fixme[6]: Expected `Union[IOBuf, bytearray, bytes, memoryview]` for
        #  1st param but got `array[int]`.
        x = IOBuf(array.array("l", [1, 2, 3, 4, 5]))
        self.assertEqual(x.chain_size(), 5 * struct.calcsize("l"))

    def test_unshaped(self) -> None:
        x = IOBuf(memoryview(b"a").cast("B", shape=[]))
        self.assertEqual(x.chain_size(), 1)

    def test_multidimensional(self) -> None:
        x = IOBuf(memoryview(b"abcdef").cast("B", shape=[3, 2]))
        self.assertEqual(x.chain_size(), 6)

    def test_conversion_from_python_to_cpp(self) -> None:
        iobuf = make_chain(
            [
                IOBuf(memoryview(b"abc")),
                IOBuf(memoryview(b"def")),
                IOBuf(memoryview(b"ghi")),
            ]
        )
        self.assertEqual(to_uppercase_string(iobuf), "ABCDEFGHI")
        self.assertEqual(to_uppercase_string_heap(iobuf), "ABCDEFGHI")

    def test_conversion_from_python_to_cpp_with_wrong_type(self) -> None:
        not_an_iobuf = [1, 2, 3]
        with self.assertRaises(TypeError):
            to_uppercase_string(not_an_iobuf)
        with self.assertRaises(TypeError):
            to_uppercase_string_heap(not_an_iobuf)
