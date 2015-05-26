/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/acceptor/TransportInfo.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <folly/io/async/AsyncSocket.h>

using std::chrono::microseconds;
using std::map;
using std::string;

namespace folly {

bool TransportInfo::initWithSocket(const AsyncSocket* sock) {
#if defined(__linux__) || defined(__FreeBSD__)
  if (!TransportInfo::readTcpInfo(&tcpinfo, sock)) {
    tcpinfoErrno = errno;
    return false;
  }
  rtt = microseconds(tcpinfo.tcpi_rtt);
  validTcpinfo = true;
#else
  tcpinfoErrno = EINVAL;
  rtt = microseconds(-1);
#endif
  return true;
}

int64_t TransportInfo::readRTT(const AsyncSocket* sock) {
#if defined(__linux__) || defined(__FreeBSD__)
  struct tcp_info tcpinfo;
  if (!TransportInfo::readTcpInfo(&tcpinfo, sock)) {
    return -1;
  }
  return tcpinfo.tcpi_rtt;
#else
  return -1;
#endif
}

#if defined(__linux__) || defined(__FreeBSD__)
bool TransportInfo::readTcpInfo(struct tcp_info* tcpinfo,
                                const AsyncSocket* sock) {
  socklen_t len = sizeof(struct tcp_info);
  if (!sock) {
    return false;
  }
  if (getsockopt(sock->getFd(), IPPROTO_TCP,
                 TCP_INFO, (void*) tcpinfo, &len) < 0) {
    VLOG(4) << "Error calling getsockopt(): " << strerror(errno);
    return false;
  }
  return true;
}
#endif

} // folly
