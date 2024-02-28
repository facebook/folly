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

#include <glog/logging.h>
#include <folly/net/TcpInfo.h>
#include <folly/portability/Sockets.h>

#if defined(__linux__)
#include <linux/sockios.h>
#include <sys/ioctl.h>
#endif

namespace folly {
namespace {

constexpr std::array<
    folly::StringPiece,
    static_cast<std::underlying_type_t<TcpInfo::CongestionControlName>>(
        TcpInfo::CongestionControlName::NumCcTypes)>
    kCcNames{
        {"UNKNOWN",
         "CUBIC",
         "BIC",
         "DCTCP",
         "DCTCP_RENO",
         "BBR",
         "RENO",
         "DCTCP_CUBIC",
         "VEGAS"}};
static_assert(
    kCcNames.size() ==
        static_cast<std::underlying_type_t<TcpInfo::CongestionControlName>>(
            TcpInfo::CongestionControlName::NumCcTypes),
    "kCcNames and folly::TcpInfo::CongestionControlName should have "
    "the same number of values");

} // namespace

using ms = std::chrono::milliseconds;
using us = std::chrono::microseconds;

TcpInfo::IoctlDispatcher* TcpInfo::IoctlDispatcher::getDefaultInstance() {
  static TcpInfo::IoctlDispatcher dispatcher = {};
  return &dispatcher;
}

int TcpInfo::IoctlDispatcher::ioctl(
    [[maybe_unused]] int fd,
    [[maybe_unused]] unsigned long request,
    [[maybe_unused]] void* argp) {
#if defined(__linux__)
  return ::ioctl(fd, request, argp);
#else
  return -1; // no cross platform for ioctl operations
#endif
}

Expected<TcpInfo, std::errc> TcpInfo::initFromFd(
    const NetworkSocket& fd,
    const TcpInfo::LookupOptions& options,
    netops::Dispatcher& netopsDispatcher,
    IoctlDispatcher& ioctlDispatcher) {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::makeUnexpected(std::errc::invalid_argument);
#else
  if (NetworkSocket() == fd) {
    return folly::makeUnexpected(std::errc::invalid_argument);
  }

  // try to get TCP_INFO
  TcpInfo info = {};
  socklen_t len = sizeof(TcpInfo::tcp_info);
  auto ret = netopsDispatcher.getsockopt(
      fd,
      IPPROTO_TCP,
      folly::detail::tcp_info_sock_opt,
      (void*)&info.tcpInfo,
      &len);
  if (ret < 0) {
    int errnoCopy = errno;
    VLOG(4) << "Error calling getsockopt(): " << folly::errnoStr(errnoCopy);
    return folly::makeUnexpected(static_cast<std::errc>(errnoCopy));
  }
  info.tcpInfoBytesRead = len;

  // if enabled, try to get information about the congestion control algo
  if (options.getCcInfo) {
    initCcInfoFromFd(fd, info, netopsDispatcher);
  }

  // if enabled, try to get memory buffers
  if (options.getMemInfo) {
    initMemInfoFromFd(fd, info, ioctlDispatcher);
  }

  return info;
#endif
}

/**
 *
 * Accessor definitions.
 *
 */

Optional<std::chrono::microseconds> TcpInfo::minrtt() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  const auto ptr = getFieldAsPtr(&tcp_info::tcpi_min_rtt);
  return (ptr) ? us(*CHECK_NOTNULL(ptr)) : folly::Optional<us>();
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<std::chrono::microseconds> TcpInfo::srtt() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  const auto ptr = getFieldAsPtr(&tcp_info::tcpi_rtt);
  return (ptr) ? us(*CHECK_NOTNULL(ptr)) // __linux__ stores in us
               : folly::Optional<us>();
#elif defined(__APPLE__)
  const auto ptr = getFieldAsPtr(&tcp_info::tcpi_srtt);
  return (ptr) ? us(ms(*CHECK_NOTNULL(ptr))) // __APPLE__ stores in ms
               : folly::Optional<us>();
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bytesSent() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_bytes_sent);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_txbytes);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bytesReceived() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_bytes_received);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_rxbytes);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bytesRetransmitted() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_bytes_retrans);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_txretransmitbytes);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bytesNotSent() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_notsent_bytes);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bytesAcked() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_bytes_acked);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsSent() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_segs_out);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_txpackets);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsWithDataSent() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_data_segs_out);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsReceived() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_segs_in);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_rxpackets);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsWithDataReceived() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_data_segs_in);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsRetransmitted() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_total_retrans);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_txretransmitpackets);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsInFlight() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  // tcp_packets_in_flight is defined in kernel as:
  //    (tp->packets_out - tcp_left_out(tp) + tp->retrans_out)
  //
  // tcp_left_out is defined as:
  //    (tp->sacked_out + tp->lost_out)
  //
  // mapping from tcp_info fields to tcp_sock fields:
  //    info->tcpi_unacked = tp->packets_out;
  //    info->tcpi_retrans = tp->retrans_out;
  //    info->tcpi_sacked = tp->sacked_out;
  //    info->tcpi_lost = tp->lost_out;
  const auto packetsOutOpt = getFieldAsOptUInt64(&tcp_info::tcpi_unacked);
  const auto retransOutOpt = getFieldAsOptUInt64(&tcp_info::tcpi_retrans);
  const auto sackedOutOpt = getFieldAsOptUInt64(&tcp_info::tcpi_sacked);
  const auto lostOutOpt = getFieldAsOptUInt64(&tcp_info::tcpi_lost);
  if (packetsOutOpt && retransOutOpt && sackedOutOpt && lostOutOpt) {
    return (*packetsOutOpt - (*sackedOutOpt + *lostOutOpt) + *retransOutOpt);
  }
  return folly::none;
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsDelivered() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_delivered);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::packetsDeliveredWithCEMarks() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_delivered_ce);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::cwndInPackets() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_snd_cwnd);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_snd_cwnd);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::cwndInBytes() const {
  const auto cwndInPacketsOpt = cwndInPackets();
  const auto mssOpt = mss();
  if (cwndInPacketsOpt && mssOpt) {
    return cwndInPacketsOpt.value() * mssOpt.value();
  }
  return folly::none;
}

Optional<uint64_t> TcpInfo::ssthresh() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_snd_ssthresh);
#elif defined(__APPLE__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_snd_ssthresh);
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::mss() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return tcpInfo.tcpi_snd_mss;
#elif defined(__APPLE__)
  return tcpInfo.tcpi_maxseg;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::deliveryRateBitsPerSecond() const {
  return bytesPerSecondToBitsPerSecond(deliveryRateBytesPerSecond());
}

Optional<uint64_t> TcpInfo::deliveryRateBytesPerSecond() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&tcp_info::tcpi_delivery_rate);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<bool> TcpInfo::deliveryRateAppLimited() const {
#ifndef FOLLY_HAVE_TCP_INFO
  return folly::none;
#elif defined(__linux__)
  // have to check if delivery rate is available for two reasons
  //  (1) can't use getTcpInfoFieldAsPtr on bit-field
  //  (2) tcpi_delivery_rate_app_limited was added in earlier part of tcp_info
  //      to take advantage of 1-byte gap; must check if we have the delivery
  //      rate field to determine if the app limited field is available
  if (deliveryRateBytesPerSecond().has_value()) {
    return tcpInfo.tcpi_delivery_rate_app_limited;
  }
  return folly::none;
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<std::string> TcpInfo::ccNameRaw() const {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return folly::none;
#elif defined(__linux__)
  return maybeCcNameRaw;
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<TcpInfo::CongestionControlName> TcpInfo::ccNameEnum() const {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return folly::none;
#elif defined(__linux__)
  return maybeCcEnum;
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<folly::StringPiece> TcpInfo::ccNameEnumAsStr() const {
  const auto maybeCcNameEnum = ccNameEnum();
  if (!maybeCcNameEnum.has_value()) {
    return folly::none;
  }
  const auto ccEnumAsInt =
      static_cast<std::underlying_type_t<CongestionControlName>>(
          maybeCcNameEnum.value());
  CHECK_GE(
      static_cast<std::underlying_type_t<TcpInfo::CongestionControlName>>(
          TcpInfo::CongestionControlName::NumCcTypes),
      ccEnumAsInt);
  CHECK_GE(kCcNames.size(), ccEnumAsInt);
  return kCcNames[ccEnumAsInt];
}

Optional<uint64_t> TcpInfo::bbrBwBitsPerSecond() const {
  return bytesPerSecondToBitsPerSecond(bbrBwBytesPerSecond());
}

Optional<uint64_t> TcpInfo::bbrBwBytesPerSecond() const {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return folly::none;
#elif defined(__linux__)
  auto bbrBwLoOpt =
      getFieldAsOptUInt64(&folly::TcpInfo::tcp_bbr_info::bbr_bw_lo);
  auto bbrBwHiOpt =
      getFieldAsOptUInt64(&folly::TcpInfo::tcp_bbr_info::bbr_bw_hi);
  if (bbrBwLoOpt && bbrBwHiOpt) {
    return ((int64_t)*bbrBwHiOpt << 32) + *bbrBwLoOpt;
  }
  return folly::none;
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<std::chrono::microseconds> TcpInfo::bbrMinrtt() const {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return folly::none;
#elif defined(__linux__)
  auto opt = getFieldAsOptUInt64(&folly::TcpInfo::tcp_bbr_info::bbr_min_rtt);
  return (opt) ? us(*opt) : folly::Optional<us>();
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bbrPacingGain() const {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&folly::TcpInfo::tcp_bbr_info::bbr_pacing_gain);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<uint64_t> TcpInfo::bbrCwndGain() const {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return folly::none;
#elif defined(__linux__)
  return getFieldAsOptUInt64(&folly::TcpInfo::tcp_bbr_info::bbr_cwnd_gain);
#elif defined(__APPLE__)
  return folly::none;
#else
  return folly::none;
#endif
}

Optional<size_t> TcpInfo::sendBufInUseBytes() const {
  return maybeSendBufInUseBytes;
}

Optional<size_t> TcpInfo::recvBufInUseBytes() const {
  return maybeRecvBufInUseBytes;
}

void TcpInfo::initCcInfoFromFd(
    [[maybe_unused]] const NetworkSocket& fd,
    [[maybe_unused]] TcpInfo& wrappedInfo,
    [[maybe_unused]] netops::Dispatcher& netopsDispatcher) {
#ifndef FOLLY_HAVE_TCP_CC_INFO
  return; // platform not supported
#elif defined(__linux__)
  if (NetworkSocket() == fd) {
    return;
  }

  // identification strings returned by Linux Kernel for TCP_CONGESTION
  static constexpr auto kLinuxCcNameStrReno = "reno";
  static constexpr auto kLinuxCcNameStrCubic = "cubic";
  static constexpr auto kLinuxCcNameStrBic = "bic";
  static constexpr auto kLinuxCcNameStrBbr = "bbr";
  static constexpr auto kLinuxCcNameStrVegas = "vegas";
  static constexpr auto kLinuxCcNameStrDctcp = "dctcp";
  static constexpr auto kLinuxCcNameStrDctcpReno = "dctcp_reno";
  static constexpr auto kLinuxCcNameStrDctcpCubic = "dctcp_cubic";

  std::array<char, (unsigned int)kLinuxTcpCaNameMax> tcpCongestion{{0}};
  socklen_t optlen = tcpCongestion.size();
  if (netopsDispatcher.getsockopt(
          fd, IPPROTO_TCP, TCP_CONGESTION, tcpCongestion.data(), &optlen) < 0) {
    VLOG(4) << "Error calling getsockopt(): " << folly::errnoStr(errno);
    return;
  }

  {
    auto ccStr = std::string(tcpCongestion.data());
    if (ccStr == kLinuxCcNameStrReno) {
      wrappedInfo.maybeCcEnum = CongestionControlName::RENO;
    } else if (ccStr == kLinuxCcNameStrCubic) {
      wrappedInfo.maybeCcEnum = CongestionControlName::CUBIC;
    } else if (ccStr == kLinuxCcNameStrBic) {
      wrappedInfo.maybeCcEnum = CongestionControlName::BIC;
    } else if (ccStr == kLinuxCcNameStrBbr) {
      wrappedInfo.maybeCcEnum = CongestionControlName::BBR;
    } else if (ccStr == kLinuxCcNameStrVegas) {
      wrappedInfo.maybeCcEnum = CongestionControlName::VEGAS;
    } else if (ccStr == kLinuxCcNameStrDctcp) {
      wrappedInfo.maybeCcEnum = CongestionControlName::DCTCP;
    } else if (ccStr == kLinuxCcNameStrDctcpReno) {
      wrappedInfo.maybeCcEnum = CongestionControlName::DCTCP_RENO;
    } else if (ccStr == kLinuxCcNameStrDctcpCubic) {
      wrappedInfo.maybeCcEnum = CongestionControlName::DCTCP_CUBIC;
    } else {
      wrappedInfo.maybeCcEnum = CongestionControlName::UNKNOWN;
    }
    wrappedInfo.maybeCcNameRaw.emplace(std::move(ccStr));
  }

  // get TCP_CC_INFO if supported for the congestion control algorithm
  switch (wrappedInfo.maybeCcEnum.value_or(CongestionControlName::UNKNOWN)) {
    case CongestionControlName::UNKNOWN:
    case CongestionControlName::RENO:
    case CongestionControlName::CUBIC:
    case CongestionControlName::BIC:
      return; // no TCP_CC_INFO for these congestion controls, exit out
    case CongestionControlName::BBR:
    case CongestionControlName::VEGAS:
    case CongestionControlName::DCTCP:
    case CongestionControlName::DCTCP_RENO:
    case CongestionControlName::DCTCP_CUBIC:
      break; // supported, proceed
    case CongestionControlName::NumCcTypes:
      LOG(FATAL) << "CongestionControlName::NumCcTypes is not a valid CC type";
  }

  tcp_cc_info ccInfo = {};
  socklen_t len = sizeof(tcp_cc_info);
  const int ret = netopsDispatcher.getsockopt(
      fd, IPPROTO_TCP, TCP_CC_INFO, (void*)&ccInfo, &len);
  if (ret < 0) {
    int errnoCopy = errno;
    VLOG(4) << "Error calling getsockopt(): " << folly::errnoStr(errnoCopy);
    return;
  }
  wrappedInfo.maybeCcInfo = ccInfo;
  wrappedInfo.tcpCcInfoBytesRead = len;
#else
  return;
#endif
}

void TcpInfo::initMemInfoFromFd(
    [[maybe_unused]] const NetworkSocket& fd,
    [[maybe_unused]] TcpInfo& wrappedInfo,
    [[maybe_unused]] IoctlDispatcher& ioctlDispatcher) {
#if defined(__linux__)
  if (NetworkSocket() == fd) {
    return;
  }

  size_t val = 0;
  if (ioctlDispatcher.ioctl(fd.toFd(), SIOCOUTQ, &val) == 0) {
    wrappedInfo.maybeSendBufInUseBytes = val;
  }
  if (ioctlDispatcher.ioctl(fd.toFd(), SIOCINQ, &val) == 0) {
    wrappedInfo.maybeRecvBufInUseBytes = val;
  }
#endif
}

} // namespace folly
