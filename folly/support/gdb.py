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


import itertools
import socket

import gdb
import gdb.printing
import gdb.types


def escape_byte(b):
    if b == 10:
        return "\\n"
    elif b == 34:
        return '\\"'
    elif b == 92:
        return "\\\\"
    elif 32 <= b < 127:
        return chr(b)
    else:
        # Escape non-printable bytes with octal, which is what GDB
        # uses when natively printing strings.
        return "\\{0:03o}".format(b)


def repr_string(v, length):
    # GDB API does not support retrieving the value as a byte string,
    # so we need to round-trip through str (unicode) using surrogate
    # escapes for non-decodable sequences.
    decoded = v.string(encoding="utf8", errors="surrogateescape", length=length)
    byte_string = decoded.encode("utf8", errors="surrogateescape")
    return '"' + "".join(escape_byte(b) for b in byte_string) + '"'


class FBStringPrinter:
    """Print an FBString."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size_of_size_t = gdb.lookup_type("size_t").sizeof
        catmask = 0xC0000000 if (size_of_size_t == 4) else 0xC000000000000000
        cap = self.val["store_"]["ml_"]["capacity_"]
        category = cap & catmask

        if category == 0:  # small-string-optimized
            v = self.val["store_"]["small_"]
            sz = v.type.sizeof
            length = sz - v[sz - 1]
            return repr_string(v, length - 1)
        else:
            v = self.val["store_"]["ml_"]
            length = v["size_"]
            return repr_string(v["data_"], length)

    def display_hint(self):
        return "fbstring"


class StringPiecePrinter:
    """Print a (Mutable)StringPiece"""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        ptr = self.val["b_"]
        length = self.val["e_"] - ptr
        return repr_string(ptr, length)

    def display_hint(self):
        return "folly::StringPiece"


class RangePrinter:
    """Print a Range"""

    def __init__(self, val):
        self.val = val

    def children(self):
        count = 0
        item = self.val["b_"]
        end = self.val["e_"]
        while item != end:
            yield "[%d]" % count, item.dereference()
            count += 1
            item += 1

    def to_string(self):
        length = self.val["e_"] - self.val["b_"]
        value_type = self.val.type.template_argument(0)
        return "folly::Range<%s> of length %d" % (value_type, length)

    def display_hint(self):
        return "folly::Range"


class DynamicPrinter:
    """Print a folly::dynamic."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        d = gdb.types.make_enum_dict(self.val["type_"].type)
        names = {v: k for k, v in d.items()}
        type_ = names[int(self.val["type_"])]

        u = self.val["u_"]
        if type_ == "folly::dynamic::NULLT":
            return "NULL"
        elif type_ == "folly::dynamic::ARRAY":
            return u["array"]
        elif type_ == "folly::dynamic::BOOL":
            return u["boolean"]
        elif type_ == "folly::dynamic::DOUBLE":
            return u["doubl"]
        elif type_ == "folly::dynamic::INT64":
            return u["integer"]
        elif type_ == "folly::dynamic::STRING":
            return u["string"]
        elif type_ == "folly::dynamic::OBJECT":
            t = gdb.lookup_type("folly::dynamic::ObjectImpl").pointer()
            raw_v = u["objectBuffer"]["__data"]
            ptr = raw_v.address.reinterpret_cast(t)
            return ptr.dereference()
        else:
            return "{unknown folly::dynamic type}"

    def display_hint(self):
        return "folly::dynamic"


class IPAddressPrinter:
    """Print a folly::IPAddress."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        result = ""
        if self.val["family_"] == socket.AF_INET:
            addr = self.val["addr_"]["ipV4Addr"]["addr_"]["bytes_"]["_M_elems"]
            for i in range(0, 4):
                result += "{:d}.".format(int(addr[i]))
        elif self.val["family_"] == socket.AF_INET6:
            addr = self.val["addr_"]["ipV6Addr"]["addr_"]["bytes_"]["_M_elems"]
            for i in range(0, 8):
                result += "{:02x}{:02x}:".format(int(addr[2 * i]), int(addr[2 * i + 1]))
        else:
            return "unknown address family {}".format(self.val["family_"])
        return result[:-1]

    def display_hint(self):
        return "folly::IPAddress"


class SocketAddressPrinter:
    """Print a folly::SocketAddress."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        result = ""
        if self.val["external_"] != 0:
            return "unix address, printer TBD"
        else:
            ipPrinter = IPAddressPrinter(self.val["storage_"]["addr"])
            if self.val["storage_"]["addr"]["family_"] == socket.AF_INET6:
                result += "[" + ipPrinter.to_string() + "]"
            else:
                result += ipPrinter.to_string()
            result += ":{}".format(self.val["port_"])
        return result

    def display_hint(self):
        return "folly::SocketAddress"


# For Node and Value containers, i.e., kEnableItemIteration == false
class F14HashtableIterator:
    def __init__(self, ht, is_node_container):
        item_type = gdb.lookup_type("{}::{}".format(ht.type.name, "Item"))
        self.item_ptr_type = item_type.pointer()
        self.chunk_ptr = ht["chunks_"]
        pair = ht["sizeAndChunkShiftAndPackedBegin_"]["sizeAndChunkShift_"]
        size_of_size_t = gdb.lookup_type("size_t").sizeof
        if size_of_size_t == 4:
            chunk_count = 1 << pair["chunkShift_"]
        else:
            chunk_count = 1 << (pair["packedSizeAndChunkShift_"] & ((1 << 8) - 1))
        self.chunk_end = self.chunk_ptr + chunk_count
        self.current_chunk = self.chunk_iter(self.chunk_ptr)
        self.is_node_container = is_node_container

    def __iter__(self):
        return self

    # generator to enumerate items in a given chunk
    def chunk_iter(self, chunk_ptr):
        chunk = chunk_ptr.dereference()
        tags = chunk["tags_"]["_M_elems"]
        raw_items = chunk["rawItems_"]["_M_elems"]

        # Enumerate over slots in the chunk
        for i in range(tags.type.sizeof):
            # full items have the top bit set in the tag
            if tags[i] & 0x80:
                item_ptr = raw_items[i]["__data"].cast(self.item_ptr_type)
                item = item_ptr.dereference()
                # node containers stores a pointer to value_type whereas value
                # containers store values inline
                yield item.dereference() if self.is_node_container else item

    def __next__(self):
        while True:
            try:
                # exhaust the current chunk
                return next(self.current_chunk)
            except StopIteration:
                # find the next chunk
                self.chunk_ptr += 1
                if self.chunk_ptr == self.chunk_end:
                    raise StopIteration
                self.current_chunk = self.chunk_iter(self.chunk_ptr)
                pass


# For Vector containers, i.e., kEnableItemIteration == true
class F14HashtableItemIterator:
    def __init__(self, start, size):
        self.item = start
        self.end = self.item + size

    def __iter__(self):
        return self

    def __next__(self):
        # To iterator the first item, gdb will call __next__
        if self.item == self.end:
            raise StopIteration
        val = self.item.dereference()
        self.item = self.item + 1
        return val


class F14Printer:
    """Print an F14 hash map or hash set"""

    @staticmethod
    def get_container_type_name(type):
        name = type.unqualified().strip_typedefs().name
        # strip template arguments
        template_start = name.find("<")
        if template_start < 0:
            return name
        return name[:template_start]

    def __init__(self, val):
        self.val = val
        self.type = val.type
        self.short_typename = self.get_container_type_name(self.type)
        self.is_map = "Map" in self.short_typename

        iter_type = gdb.lookup_type(
            f"{self.type.unqualified().strip_typedefs()}::iterator"
        )
        self.is_node_container = "NodeContainer" in iter_type.name
        self.enable_item_iteration = "VectorContainer" in iter_type.name

    def hashtable(self):
        return self.val["table_"]

    def size(self):
        pair = self.hashtable()["sizeAndChunkShiftAndPackedBegin_"][
            "sizeAndChunkShift_"
        ]
        size_of_size_t = gdb.lookup_type("size_t").sizeof
        if size_of_size_t == 4:
            return pair["size_"]
        else:
            return pair["packedSizeAndChunkShift_"] >> 8

    def to_string(self):
        return "%s with %d elements" % (
            self.short_typename,
            self.size(),
        )

    @staticmethod
    def format_one_map(elt):
        return (elt["first"], elt["second"])

    @staticmethod
    def format_count(i):
        return "[%d]" % i

    @staticmethod
    def flatten(list):
        for elt in list:
            for i in elt:
                yield i

    def children(self):
        counter = map(self.format_count, itertools.count())
        iter = (
            F14HashtableItemIterator(self.hashtable()["values_"], self.size())
            if self.enable_item_iteration
            else F14HashtableIterator(self.hashtable(), self.is_node_container)
        )
        data = self.flatten(map(self.format_one_map, iter)) if self.is_map else iter
        return zip(counter, data)

    def display_hint(self):
        return "map" if self.is_map else "array"


class ConcurrentHashMapIterator:
    """Iterator for folly's ConcurrentHashMap"""

    def __init__(self, hashmap):
        self._segments = hashmap["segments_"]
        # Iterator state across segments
        self.segment_idx = 0
        self.segment = None
        self.num_segments = int(hashmap["NumShards"])
        # Iterator state across buckets within a segment
        self.bucket_idx = -1
        self.segment_buckets = None
        self.segment_num_buckets = None

    def __iter__(self):
        return self

    def __next__(self):
        if self.segment_idx >= self.num_segments:
            raise StopIteration
        # Update cached segment
        if self.segment is None:
            self.bucket_idx = -1
            _segment_ptr = self._segments[self.segment_idx]["_M_b"]["_M_p"]
            # Check if the segment has been initialized or setp over it
            if int(_segment_ptr) == 0:
                self.segment_idx += 1
                return self.__next__()
            self.segment = _segment_ptr.dereference()["impl_"]
            _buckets = self.segment["buckets_"]["_M_b"]["_M_p"]
            # Can this even happen?
            if int(_buckets) == 0:
                self.segment_idx += 1
                return self.__next__()
            self.segment_buckets = _buckets.dereference()["buckets_"]
            self.segment_num_buckets = int(self.segment["bucket_count_"]["_M_i"])

        self.bucket_idx += 1
        # Check if we're done with this segment
        if self.bucket_idx >= self.segment_num_buckets:
            self.segment_idx += 1
            self.segment = None
            return self.__next__()

        # Skip null items
        ptr = self.segment_buckets[self.bucket_idx]["link_"]["_M_b"]["_M_p"]
        return self.__next__() if int(ptr) == 0 else ptr["item_"]["item_"]


class ConcurrentHashMapPrinter:
    """Print a ConcurrentHashMap"""

    def __init__(self, val):
        self.val = val
        self.key_type = val.type.template_argument(0)
        self.val_type = val.type.template_argument(1)

    def to_string(self):
        # DEBUG: consume iterator to get map size and print it
        # mapsize = len(list(self.children()))
        return f"folly::ConcurrentHashMap<{self.key_type}, {self.val_type}, ...>"

    def children(self):
        "Returns an iterator of tuple(name, value)"
        return (
            (f'[{str(item["kv_"]["first"])}]', item["kv_"]["second"])
            for idx, item in enumerate(ConcurrentHashMapIterator(self.val))
        )

    def display_hint(self):
        return "folly::ConcurrentHashMap"


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("folly")
    pp.add_printer("fbstring", "^folly::basic_fbstring<char,.*$", FBStringPrinter)
    pp.add_printer(
        "StringPiece", r"^folly::Range<(\w+ )*char( \w+)*\*>$", StringPiecePrinter
    )
    pp.add_printer("Range", r"^folly::Range<.*", RangePrinter)
    pp.add_printer("dynamic", "^folly::dynamic$", DynamicPrinter)
    pp.add_printer("IPAddress", "^folly::IPAddress$", IPAddressPrinter)
    pp.add_printer("SocketAddress", "^folly::SocketAddress$", SocketAddressPrinter)

    pp.add_printer("F14NodeMap", "^folly::F14NodeMap<.*$", F14Printer)
    pp.add_printer("F14ValueMap", "^folly::F14ValueMap<.*$", F14Printer)
    pp.add_printer("F14VectorMap", "^folly::F14VectorMap<.*$", F14Printer)
    pp.add_printer("F14FastMap", "^folly::F14FastMap<.*$", F14Printer)

    pp.add_printer("F14NodeSet", "^folly::F14NodeSet<.*$", F14Printer)
    pp.add_printer("F14ValueSet", "^folly::F14ValueSet<.*$", F14Printer)
    pp.add_printer("F14VectorSet", "^folly::F14VectorSet<.*$", F14Printer)
    pp.add_printer("F14FastSet", "^folly::F14FastSet<.*$", F14Printer)

    pp.add_printer(
        "ConcurrentHashMap", "^folly::ConcurrentHashMap<.*$", ConcurrentHashMapPrinter
    )
    return pp


def load():
    gdb.printing.register_pretty_printer(gdb, build_pretty_printer())


def info():
    return "Pretty printers for folly containers"
