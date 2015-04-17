/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/wangle/ssl/SSLUtil.h>

#include <chrono>
#include <netinet/tcp.h>
#include <string>

namespace folly {
class AsyncSocket;

/**
 * A structure that encapsulates byte counters related to the HTTP headers.
 */
struct HTTPHeaderSize {
  /**
   * The number of bytes used to represent the header after compression or
   * before decompression. If header compression is not supported, the value
   * is set to 0.
   */
  size_t compressed{0};

  /**
   * The number of bytes used to represent the serialized header before
   * compression or after decompression, in plain-text format.
   */
  size_t uncompressed{0};
};

struct TransportInfo {
  /*
   * timestamp of when the connection handshake was completed
   */
  std::chrono::steady_clock::time_point acceptTime{};

  /*
   * connection RTT (Round-Trip Time)
   */
  std::chrono::microseconds rtt{0};

#if defined(__linux__) || defined(__FreeBSD__)
  /*
   * TCP information as fetched from getsockopt(2)
   */
  tcp_info tcpinfo {
#if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 17
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 // 32
#else
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 // 29
#endif  // __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 17
  };
#endif  // defined(__linux__) || defined(__FreeBSD__)

  /*
   * time for setting the connection, from the moment in was accepted until it
   * is established.
   */
  std::chrono::milliseconds setupTime{0};

  /*
   * time for setting up the SSL connection or SSL handshake
   */
  std::chrono::milliseconds sslSetupTime{0};

  /*
   * The name of the SSL ciphersuite used by the transaction's
   * transport.  Returns null if the transport is not SSL.
   */
  std::shared_ptr<std::string> sslCipher{nullptr};

  /*
   * The SSL server name used by the transaction's
   * transport.  Returns null if the transport is not SSL.
   */
  std::shared_ptr<std::string> sslServerName{nullptr};

  /*
   * list of ciphers sent by the client
   */
  std::shared_ptr<std::string> sslClientCiphers{nullptr};

  /*
   * list of compression methods sent by the client
   */
  std::shared_ptr<std::string> sslClientComprMethods{nullptr};

  /*
   * list of TLS extensions sent by the client
   */
  std::shared_ptr<std::string> sslClientExts{nullptr};

  /*
   * hash of all the SSL parameters sent by the client
   */
  std::shared_ptr<std::string> sslSignature{nullptr};

  /*
   * list of ciphers supported by the server
   */
  std::shared_ptr<std::string> sslServerCiphers{nullptr};

  /*
   * guessed "(os) (browser)" based on SSL Signature
   */
  std::shared_ptr<std::string> guessedUserAgent{nullptr};

  /**
   * The result of SSL NPN negotiation.
   */
  std::shared_ptr<std::string> sslNextProtocol{nullptr};

  /*
   * total number of bytes sent over the connection
   */
  int64_t totalBytes{0};

  /**
   * If the client passed through one of our L4 proxies (using PROXY Protocol),
   * then this will contain the IP address of the proxy host.
   */
  std::shared_ptr<folly::SocketAddress> clientAddrOriginal;

  /**
   * header bytes read
   */
  HTTPHeaderSize ingressHeader;

  /*
   * header bytes written
   */
  HTTPHeaderSize egressHeader;

  /*
   * Here is how the timeToXXXByte variables are planned out:
   * 1. All timeToXXXByte variables are measuring the ByteEvent from reqStart_
   * 2. You can get the timing between two ByteEvents by calculating their
   *    differences. For example:
   *    timeToLastBodyByteAck - timeToFirstByte
   *    => Total time to deliver the body
   * 3. The calculation in point (2) is typically done outside acceptor
   *
   * Future plan:
   * We should log the timestamps (TimePoints) and allow
   * the consumer to calculate the latency whatever it
   * wants instead of calculating them in wangle, for the sake of flexibility.
   * For example:
   * 1. TimePoint reqStartTimestamp;
   * 2. TimePoint firstHeaderByteSentTimestamp;
   * 3. TimePoint firstBodyByteTimestamp;
   * 3. TimePoint lastBodyByteTimestamp;
   * 4. TimePoint lastBodyByteAckTimestamp;
   */

  /*
   * time to first header byte written to the kernel send buffer
   * NOTE: It is not 100% accurate since TAsyncSocket does not do
   * do callback on partial write.
   */
  int32_t timeToFirstHeaderByte{-1};

  /*
   * time to first body byte written to the kernel send buffer
   */
  int32_t timeToFirstByte{-1};

  /*
   * time to last body byte written to the kernel send buffer
   */
  int32_t timeToLastByte{-1};

  /*
   * time to TCP Ack received for the last written body byte
   */
  int32_t timeToLastBodyByteAck{-1};

  /*
   * time it took the client to ACK the last byte, from the moment when the
   * kernel sent the last byte to the client and until it received the ACK
   * for that byte
   */
  int32_t lastByteAckLatency{-1};

  /*
   * time spent inside wangle
   */
  int32_t proxyLatency{-1};

  /*
   * time between connection accepted and client message headers completed
   */
  int32_t clientLatency{-1};

  /*
   * latency for communication with the server
   */
  int32_t serverLatency{-1};

  /*
   * time used to get a usable connection.
   */
  int32_t connectLatency{-1};

  /*
   * body bytes written
   */
  uint32_t egressBodySize{0};

  /*
   * value of errno in case of getsockopt() error
   */
  int tcpinfoErrno{0};

  /*
   * bytes read & written during SSL Setup
   */
  uint32_t sslSetupBytesWritten{0};
  uint32_t sslSetupBytesRead{0};

  /**
   * SSL error detail
   */
  uint32_t sslError{0};

  /**
   * body bytes read
   */
  uint32_t ingressBodySize{0};

  /*
   * The SSL version used by the transaction's transport, in
   * OpenSSL's format: 4 bits for the major version, followed by 4 bits
   * for the minor version.  Returns zero for non-SSL.
   */
  uint16_t sslVersion{0};

  /*
   * The SSL certificate size.
   */
  uint16_t sslCertSize{0};

  /**
   * response status code
   */
  uint16_t statusCode{0};

  /*
   * The SSL mode for the transaction's transport: new session,
   * resumed session, or neither (non-SSL).
   */
  SSLResumeEnum sslResume{SSLResumeEnum::NA};

  /*
   * true if the tcpinfo was successfully read from the kernel
   */
  bool validTcpinfo{false};

  /*
   * true if the connection is SSL, false otherwise
   */
  bool ssl{false};

  /*
   * get the RTT value in milliseconds
   */
  std::chrono::milliseconds getRttMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(rtt);
  }

  /*
   * initialize the fields related with tcp_info
   */
  bool initWithSocket(const AsyncSocket* sock);

  /*
   * Get the kernel's estimate of round-trip time (RTT) to the transport's peer
   * in microseconds. Returns -1 on error.
   */
  static int64_t readRTT(const AsyncSocket* sock);

#if defined(__linux__) || defined(__FreeBSD__)
  /*
   * perform the getsockopt(2) syscall to fetch TCP info for a given socket
   */
  static bool readTcpInfo(struct tcp_info* tcpinfo,
                          const AsyncSocket* sock);
#endif
};

} // folly
