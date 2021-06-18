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

#if defined(__linux__) || defined(__APPLE__)
#include <netinet/tcp.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <asm/types.h>
#endif

namespace folly {
namespace tcpinfo {

/**
 *
 * tcp_info structures.
 *
 */

#if defined(__linux__)
#define FOLLY_HAVE_TCP_INFO 1
const int tcp_info_sock_opt = TCP_INFO;
/**
 * tcp_info as of kernel 5.7.
 *
 * The kernel ABI is fully backwards compatible. Thus, if a new structure has
 * been released, this structure can (and should be) upgraded.
 *
 * Having a copy of the latest available structure decouples compilation from
 * whatever is in the header files available to the compiler. These may be
 * very outdated; see discussion of glibc below. WrappedTcpInfo determines which
 * fields are supported by the kernel running on the machine based on the size
 * of the tcp_info object returned and exposes only those fields.
 */
struct tcp_info {
  __u8 tcpi_state;
  __u8 tcpi_ca_state;
  __u8 tcpi_retransmits;
  __u8 tcpi_probes;
  __u8 tcpi_backoff;
  __u8 tcpi_options;
  __u8 tcpi_snd_wscale : 4, tcpi_rcv_wscale : 4;
  __u8 tcpi_delivery_rate_app_limited : 1;

  __u32 tcpi_rto;
  __u32 tcpi_ato;
  __u32 tcpi_snd_mss;
  __u32 tcpi_rcv_mss;

  __u32 tcpi_unacked;
  __u32 tcpi_sacked;
  __u32 tcpi_lost;
  __u32 tcpi_retrans;
  __u32 tcpi_fackets;

  /* Times. */
  __u32 tcpi_last_data_sent;
  __u32 tcpi_last_ack_sent; /* Not remembered, sorry. */
  __u32 tcpi_last_data_recv;
  __u32 tcpi_last_ack_recv;

  /* Metrics. */
  __u32 tcpi_pmtu;
  __u32 tcpi_rcv_ssthresh;
  __u32 tcpi_rtt;
  __u32 tcpi_rttvar;
  __u32 tcpi_snd_ssthresh;
  __u32 tcpi_snd_cwnd;
  __u32 tcpi_advmss;
  __u32 tcpi_reordering;

  __u32 tcpi_rcv_rtt;
  __u32 tcpi_rcv_space;

  __u32 tcpi_total_retrans;

  __u64 tcpi_pacing_rate;
  __u64 tcpi_max_pacing_rate;
  __u64 tcpi_bytes_acked; /* RFC4898 tcpEStatsAppHCThruOctetsAcked */
  __u64 tcpi_bytes_received; /* RFC4898 tcpEStatsAppHCThruOctetsReceived */
  __u32 tcpi_segs_out; /* RFC4898 tcpEStatsPerfSegsOut */
  __u32 tcpi_segs_in; /* RFC4898 tcpEStatsPerfSegsIn */

  __u32 tcpi_notsent_bytes;
  __u32 tcpi_min_rtt;
  __u32 tcpi_data_segs_in; /* RFC4898 tcpEStatsDataSegsIn */
  __u32 tcpi_data_segs_out; /* RFC4898 tcpEStatsDataSegsOut */

  __u64 tcpi_delivery_rate;

  __u64 tcpi_busy_time; /* Time (usec) busy sending data */
  __u64 tcpi_rwnd_limited; /* Time (usec) limited by receive window */
  __u64 tcpi_sndbuf_limited; /* Time (usec) limited by send buffer */

  __u32 tcpi_delivered;
  __u32 tcpi_delivered_ce;

  __u64 tcpi_bytes_sent; /* RFC4898 tcpEStatsPerfHCDataOctetsOut */
  __u64 tcpi_bytes_retrans; /* RFC4898 tcpEStatsPerfOctetsRetrans */
  __u32 tcpi_dsack_dups; /* RFC4898 tcpEStatsStackDSACKDups */
  __u32 tcpi_reord_seen; /* reordering events seen */

  __u32 tcpi_rcv_ooopack; /* Out-of-order packets received */

  __u32 tcpi_snd_wnd; /* peer's advertised receive window after
                       * scaling (bytes)
                       */
};

/**
 * Legacy tcp_info used to confirm backwards compatibility.
 *
 * We use this structure in test cases where the kernel has an older version of
 * tcp_info to verify that the wrapper returns unsupported fields as empty
 * optionals.
 *
 * This tcp_info struct is what shipped in 3.x kernels, and is still shipped
 * with glibc as of 2020 in sysdeps/gnu/netinet/tcp.h. glibc has not updated the
 * tcp_info struct since 2007 due to compatibility concerns; see commit titled
 * "Update netinet/tcp.h from Linux 4.18" in glibc repository.
 */
struct tcp_info_legacy {
  __u8 tcpi_state;
  __u8 tcpi_ca_state;
  __u8 tcpi_retransmits;
  __u8 tcpi_probes;
  __u8 tcpi_backoff;
  __u8 tcpi_options;
  __u8 tcpi_snd_wscale : 4, tcpi_rcv_wscale : 4;

  __u32 tcpi_rto;
  __u32 tcpi_ato;
  __u32 tcpi_snd_mss;
  __u32 tcpi_rcv_mss;

  __u32 tcpi_unacked;
  __u32 tcpi_sacked;
  __u32 tcpi_lost;
  __u32 tcpi_retrans;
  __u32 tcpi_fackets;

  /* Times. */
  __u32 tcpi_last_data_sent;
  __u32 tcpi_last_ack_sent; /* Not remembered, sorry. */
  __u32 tcpi_last_data_recv;
  __u32 tcpi_last_ack_recv;

  /* Metrics. */
  __u32 tcpi_pmtu;
  __u32 tcpi_rcv_ssthresh;
  __u32 tcpi_rtt;
  __u32 tcpi_rttvar;
  __u32 tcpi_snd_ssthresh;
  __u32 tcpi_snd_cwnd;
  __u32 tcpi_advmss;
  __u32 tcpi_reordering;

  __u32 tcpi_rcv_rtt;
  __u32 tcpi_rcv_space;

  __u32 tcpi_total_retrans;
};

#elif defined(__APPLE__)
#define FOLLY_HAVE_TCP_INFO 1
using tcp_info = ::tcp_connection_info;
const int tcp_info_sock_opt = TCP_CONNECTION_INFO;
#endif

/**
 * extra structures used to communicate congestion control information.
 */

#if defined(__linux__) && defined(TCP_CONGESTION) && defined(TCP_CC_INFO)
#define FOLLY_HAVE_TCP_CC_INFO 1
struct tcpvegas_info {
  __u32 tcpv_enabled;
  __u32 tcpv_rttcnt;
  __u32 tcpv_rtt;
  __u32 tcpv_minrtt;
};

struct tcp_dctcp_info {
  __u16 dctcp_enabled;
  __u16 dctcp_ce_state;
  __u32 dctcp_alpha;
  __u32 dctcp_ab_ecn;
  __u32 dctcp_ab_tot;
};

struct tcp_bbr_info {
  /* u64 bw: max-filtered BW (app throughput) estimate in Byte per sec: */
  __u32 bbr_bw_lo; /* lower 32 bits of bw */
  __u32 bbr_bw_hi; /* upper 32 bits of bw */
  __u32 bbr_min_rtt; /* min-filtered RTT in uSec */
  __u32 bbr_pacing_gain; /* pacing gain shifted left 8 bits */
  __u32 bbr_cwnd_gain; /* cwnd gain shifted left 8 bits */
};

union tcp_cc_info {
  struct tcpvegas_info vegas;
  struct tcp_dctcp_info dctcp;
  struct tcp_bbr_info bbr;
};
#endif

} // namespace tcpinfo
} // namespace folly
