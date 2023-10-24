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

#pragma once

namespace folly {

/*
 * flags given by the application for write* calls
 */
enum class WriteFlags : uint32_t {
  NONE = 0x00,
  /*
   * Whether to delay the output until a subsequent non-corked write.
   * (Note: may not be supported in all subclasses or on all platforms.)
   */
  CORK = 0x01,
  /*
   * Set MSG_EOR flag when writing the last byte of the buffer to the socket.
   *
   * EOR tracking may need to be enabled to ensure that the MSG_EOR flag is only
   * set when the final byte is being written.
   *
   *  - If the MSG_EOR flag is set, it is marked in the corresponding
   *    tcp_skb_cb; this can be useful when debugging.
   *  - The kernel uses it to decide whether socket buffers can be collapsed
   *    together (see tcp_skb_can_collapse_to).
   */
  EOR = 0x02,
  /*
   * this indicates that only the write side of socket should be shutdown
   */
  WRITE_SHUTDOWN = 0x04,
  /*
   * use msg zerocopy if allowed
   */
  WRITE_MSG_ZEROCOPY = 0x08,
  /*
   * Request timestamp when entire buffer transmitted by the NIC.
   *
   * How timestamping is performed is implementation specific and may rely on
   * software or hardware timestamps
   */
  TIMESTAMP_TX = 0x10,
  /*
   * Request timestamp when entire buffer ACKed by remote endpoint.
   *
   * How timestamping is performed is implementation specific and may rely on
   * software or hardware timestamps
   */
  TIMESTAMP_ACK = 0x20,
  /*
   * Request timestamp when entire buffer has entered packet scheduler.
   */
  TIMESTAMP_SCHED = 0x40,
  /*
   * Request timestamp when entire buffer has been written to system socket.
   */
  TIMESTAMP_WRITE = 0x80,
};

/*
 * union operator
 */
constexpr WriteFlags operator|(WriteFlags a, WriteFlags b) {
  return static_cast<WriteFlags>(
      static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/*
 * compound assignment union operator
 */
constexpr WriteFlags& operator|=(WriteFlags& a, WriteFlags b) {
  a = a | b;
  return a;
}

/*
 * intersection operator
 */
constexpr WriteFlags operator&(WriteFlags a, WriteFlags b) {
  return static_cast<WriteFlags>(
      static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/*
 * compound assignment intersection operator
 */
constexpr WriteFlags& operator&=(WriteFlags& a, WriteFlags b) {
  a = a & b;
  return a;
}

/*
 * exclusion parameter
 */
constexpr WriteFlags operator~(WriteFlags a) {
  return static_cast<WriteFlags>(~static_cast<uint32_t>(a));
}

/*
 * unset operator
 */
constexpr WriteFlags unSet(WriteFlags a, WriteFlags b) {
  return a & ~b;
}

/*
 * inclusion operator
 */
constexpr bool isSet(WriteFlags a, WriteFlags b) {
  return (a & b) == b;
}

/**
 * Write flags that are related to timestamping.
 */
constexpr WriteFlags kWriteFlagsForTimestamping = WriteFlags::TIMESTAMP_SCHED |
    WriteFlags::TIMESTAMP_TX | WriteFlags::TIMESTAMP_ACK;

} // namespace folly
