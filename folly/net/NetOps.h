/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <cstdint>

#include <folly/Portability.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/IOVec.h>
#include <folly/portability/SysTypes.h>
#include <folly/portability/Time.h>
#include <folly/portability/Windows.h>

#ifdef _WIN32

#include <WS2tcpip.h> // @manual

using nfds_t = int;
using sa_family_t = ADDRESS_FAMILY;

// these are not supported
#define SO_EE_ORIGIN_ZEROCOPY 0
#define SO_ZEROCOPY 0
#define SO_TXTIME 0
#define MSG_ZEROCOPY 0x0
#define SOL_UDP 0x0
#define UDP_SEGMENT 0x0
#define IP_BIND_ADDRESS_NO_PORT 0

// We don't actually support either of these flags
// currently.
#define MSG_DONTWAIT 0x1000
#define MSG_EOR 0
struct msghdr {
  void* msg_name;
  socklen_t msg_namelen;
  struct iovec* msg_iov;
  size_t msg_iovlen;
  void* msg_control;
  size_t msg_controllen;
  int msg_flags;
};

struct mmsghdr {
  struct msghdr msg_hdr;
  unsigned int msg_len;
};

struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[108];
};

#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

// These are the same, but PF_LOCAL
// isn't defined by WinSock.
#define AF_LOCAL PF_UNIX
#define PF_LOCAL PF_UNIX

// This isn't defined by Windows, and we need to
// distinguish it from SO_REUSEADDR
#define SO_REUSEPORT 0x7001

// Someone thought it would be a good idea
// to define a field via a macro...
#undef s_host
#elif defined(__XROS__)
// Stub this out for now.
using nfds_t = int;
using socklen_t = int;
struct sockaddr {};
struct in_addr {};
struct msghdr {
  void* msg_name;
  socklen_t msg_namelen;
  struct iovec* msg_iov;
  size_t msg_iovlen;
  void* msg_control;
  size_t msg_controllen;
  int msg_flags;
};

struct mmsghdr {
  struct msghdr msg_hdr;
  unsigned int msg_len;
};

#else

#include <netdb.h>
#include <poll.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef MSG_ERRQUEUE
#define FOLLY_HAVE_MSG_ERRQUEUE 1
#ifndef FOLLY_HAVE_SO_TIMESTAMPING
#define FOLLY_HAVE_SO_TIMESTAMPING 1
#endif
/* for struct sock_extended_err*/
#include <linux/errqueue.h>
#endif

#ifndef SO_EE_ORIGIN_ZEROCOPY
#define SO_EE_ORIGIN_ZEROCOPY 5
#endif

#ifndef SO_EE_CODE_ZEROCOPY_COPIED
#define SO_EE_CODE_ZEROCOPY_COPIED 1
#endif

#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif

#ifndef SO_TXTIME
#define SO_TXTIME 61
#define SCM_TXTIME SO_TXTIME
#endif

#ifdef FOLLY_HAVE_MSG_ERRQUEUE
namespace folly {
namespace netops {

/* Copied from uapi/linux/net_tstamp.h */
enum txtime_flags {
  SOF_TXTIME_DEADLINE_MODE = (1 << 0),
  SOF_TXTIME_REPORT_ERRORS = (1 << 1),

  SOF_TXTIME_FLAGS_LAST = SOF_TXTIME_REPORT_ERRORS,
  SOF_TXTIME_FLAGS_MASK = (SOF_TXTIME_FLAGS_LAST - 1) | SOF_TXTIME_FLAGS_LAST
};

/* Copied from uapi/linux/net_tstamp.h */
enum timestamping_flags {
  SOF_TIMESTAMPING_TX_HARDWARE = (1 << 0),
  SOF_TIMESTAMPING_TX_SOFTWARE = (1 << 1),
  SOF_TIMESTAMPING_RX_HARDWARE = (1 << 2),
  SOF_TIMESTAMPING_RX_SOFTWARE = (1 << 3),
  SOF_TIMESTAMPING_SOFTWARE = (1 << 4),
  SOF_TIMESTAMPING_SYS_HARDWARE = (1 << 5),
  SOF_TIMESTAMPING_RAW_HARDWARE = (1 << 6),
  SOF_TIMESTAMPING_OPT_ID = (1 << 7),
  SOF_TIMESTAMPING_TX_SCHED = (1 << 8),
  SOF_TIMESTAMPING_TX_ACK = (1 << 9),
  SOF_TIMESTAMPING_OPT_CMSG = (1 << 10),
  SOF_TIMESTAMPING_OPT_TSONLY = (1 << 11),
  SOF_TIMESTAMPING_OPT_STATS = (1 << 12),
  SOF_TIMESTAMPING_OPT_PKTINFO = (1 << 13),
  SOF_TIMESTAMPING_OPT_TX_SWHW = (1 << 14),

  SOF_TIMESTAMPING_LAST = SOF_TIMESTAMPING_OPT_TX_SWHW,
  SOF_TIMESTAMPING_MASK = (SOF_TIMESTAMPING_LAST - 1) | SOF_TIMESTAMPING_LAST
};

/* Copied from uapi/linux/net_tstamp.h */
enum tstamp_flags {
  SCM_TSTAMP_SND, /* driver passed skb to NIC, or HW */
  SCM_TSTAMP_SCHED, /* data entered the packet scheduler */
  SCM_TSTAMP_ACK, /* data acknowledged by peer */
};

struct sock_txtime {
  __kernel_clockid_t clockid; /* reference clockid */
  __u32 flags; /* as defined by enum txtime_flags */
};
} // namespace netops
} // namespace folly
#endif

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

#ifndef SOL_UDP
#define SOL_UDP 17
#endif

#ifndef ETH_MAX_MTU
#define ETH_MAX_MTU 0xFFFFU
#endif

#ifndef UDP_NO_CHECK6_TX
#define UDP_NO_CHECK6_TX 101 /* Disable sending checksum for UDP6X */
#endif

#ifndef UDP_NO_CHECK6_RX
#define UDP_NO_CHECK6_RX 102 /* Disable accpeting checksum for UDP6 */
#endif

#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif

#ifndef UDP_GRO
#define UDP_GRO 104
#endif

#ifndef UDP_MAX_SEGMENTS
#define UDP_MAX_SEGMENTS (1 << 6UL)
#endif

#if !defined(MSG_WAITFORONE) && !defined(__wasm32__)
struct mmsghdr {
  struct msghdr msg_hdr;
  unsigned int msg_len;
};
#endif

#ifndef IP_BIND_ADDRESS_NO_PORT
#define IP_BIND_ADDRESS_NO_PORT 24
#endif

#endif

namespace folly {
namespace netops {
// Poll descriptor is intended to be byte-for-byte identical to pollfd,
// except that it is typed as containing a NetworkSocket for sane interactions.
struct PollDescriptor {
  NetworkSocket fd;
  int16_t events;
  int16_t revents;
};

NetworkSocket accept(NetworkSocket s, sockaddr* addr, socklen_t* addrlen);
int bind(NetworkSocket s, const sockaddr* name, socklen_t namelen);
int close(NetworkSocket s);
int connect(NetworkSocket s, const sockaddr* name, socklen_t namelen);
int getpeername(NetworkSocket s, sockaddr* name, socklen_t* namelen);
int getsockname(NetworkSocket s, sockaddr* name, socklen_t* namelen);
int getsockopt(
    NetworkSocket s, int level, int optname, void* optval, socklen_t* optlen);
int inet_aton(const char* cp, in_addr* inp);
int listen(NetworkSocket s, int backlog);
int poll(PollDescriptor fds[], nfds_t nfds, int timeout);
ssize_t recv(NetworkSocket s, void* buf, size_t len, int flags);
ssize_t recvfrom(
    NetworkSocket s,
    void* buf,
    size_t len,
    int flags,
    sockaddr* from,
    socklen_t* fromlen);
ssize_t recvmsg(NetworkSocket s, msghdr* message, int flags);
int recvmmsg(
    NetworkSocket s,
    mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    timespec* timeout);
ssize_t send(NetworkSocket s, const void* buf, size_t len, int flags);
ssize_t sendto(
    NetworkSocket s,
    const void* buf,
    size_t len,
    int flags,
    const sockaddr* to,
    socklen_t tolen);
ssize_t sendmsg(NetworkSocket socket, const msghdr* message, int flags);
int sendmmsg(
    NetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags);
int setsockopt(
    NetworkSocket s,
    int level,
    int optname,
    const void* optval,
    socklen_t optlen);
int shutdown(NetworkSocket s, int how);
NetworkSocket socket(int af, int type, int protocol);
int socketpair(int domain, int type, int protocol, NetworkSocket sv[2]);

// And now we diverge from the Posix way of doing things and just do things
// our own way.
int set_socket_non_blocking(NetworkSocket s);
int set_socket_close_on_exec(NetworkSocket s);
} // namespace netops
} // namespace folly
