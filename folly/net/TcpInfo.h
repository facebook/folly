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

#include <chrono>
#include <system_error>

#include <folly/Expected.h>
#include <folly/Optional.h>
#include <folly/String.h>
#include <folly/net/NetOpsDispatcher.h>
#include <folly/net/NetworkSocket.h>
#include <folly/net/TcpInfoTypes.h>

namespace folly {

/**
 * Abstraction layer for capturing current TCP and congestion control state.
 *
 * Fetches information from four different resources:
 *   - TCP_INFO (state of TCP)
 *   - TCP_CONGESTION (name of congestion control algorithm)
 *   - TCP_CC_INFO (details for a given congestion control algorithm)
 *   - SIOCOUTQ/SIOCINQ (socket buffers)
 *
 * To save space, the structure only allocates fields for which the underlying
 * platform supports lookups. For instance, if TCP_CONGESTION is not supported,
 * then the TcpInfo structure will not have fields to store the CC name / type.
 *
 * This abstraction layer solves two problems:
 *
 *   1. It unblocks use of the latest tcp_info structs and related structs.
 *
 *      As of 2020, the tcp_info struct shipped with glibc
 *      (sysdeps/gnu/netinet/tcp.h) has not been updated since 2007 due to
 *      compatibility concerns; see commit titled "Update netinet/tcp.h from
 *      Linux 4.18" in glibc repository. This creates scenarios where fields
 *      that have long been available in the kernel ABI cannot be accessed.
 *      Even if glibc does eventually update the tcp_info shipped, we don't
 *      want to be limited to their update cycle.
 *
 *      folly::TcpInfo solves this in two ways:
 *         - First, TcpInfoTypes.h contains a copy of the latest tcp_info struct
 *           for Linux, and folly::TcpInfo always uses this struct for lookups;
 *           this decouples TcpInfo from glibc's / the platform's tcp_info.
 *
 *         - Second, folly::TcpInfo determines which fields in the struct the
 *           kernel ABI populated (and thus which fields are valid) based on the
 *           number of bytes the kernel ABI copies into the struct during the
 *           corresponding getsockopt operation. When a field is accessed
 *           through getFieldAsOptUInt64 or through an accessor, folly::TcpInfo
 *           returns an empty optional if the field is unavailable at run-time.
 *           In this manner, folly::TcpInfo enables the latest struct to always
 *           be used while ensuring that programs can determine at run-time
 *           which fields are available for use --- there's no risk of a program
 *           assuming that a field is valid when it in fact was never
 *           initialized/set by the ABI.
 *
 *   2. Eliminates platform differences while still retaining details.
 *
 *      The tcp_info structure varies significantly between Apple and Linux.
 *      folly::TcpInfo exposes a subset of tcp_info and other fields through
 *      accessors that abstract these differences, and reduce potential errors
 *      (e.g., Apple stores srtt in milliseconds, Linux stores in microseconds).
 *      When a field is unavailable on a platform, the accessor returns an empty
 *      optional.
 *
 *      In parallel, the underlying structures remain accessible and can be
 *      safely accessed through the appropriate getFieldAsOptUInt64(...). This
 *      enables platform-specific code to have full access to the structure
 *      while also benefiting from folly::TcpInfo's knowledge of whether a
 *      given field was populated by the ABI at run-time.
 */
struct TcpInfo {
  enum class CongestionControlName {
    UNKNOWN = 0,
    CUBIC = 1,
    BIC = 2,
    DCTCP = 3,
    DCTCP_RENO = 4,
    BBR = 5,
    RENO = 6,
    DCTCP_CUBIC = 7,
    VEGAS = 8,
    NumCcTypes,
  };

  /**
   * Structure specifying options for TcpInfo::initFromFd.
   */
  struct LookupOptions {
    // On supported platforms, whether to fetch the name of the  congestion
    // control algorithm and any information exposed via TCP_CC_INFO.
    bool getCcInfo{false};

    // On supported platforms, whether to fetch socket buffer utilization.
    bool getMemInfo{false};

    LookupOptions() {}
  };

  /**
   * Dispatcher that enables calls to ioctl to be intercepted for tests.
   *
   * Also enables ioctl calls to be disabled for unsupported platforms.
   */
  class IoctlDispatcher {
   public:
    static IoctlDispatcher* getDefaultInstance();
    virtual int ioctl(int fd, unsigned long request, void* argp);

   protected:
    IoctlDispatcher() = default;
    virtual ~IoctlDispatcher() = default;
  };

  /**
   * Initializes and returns TcpInfo struct.
   *
   * @param fd          Socket file descriptor encapsulated in NetworkSocket.
   * @param options     Options for lookup.
   * @param netopsDispatcher  Dispatcher to use for netops calls;
   *                          facilitates mocking during unit tests.
   * @param ioctlDispatcher   Dispatcher to use for ioctl calls;
   *                          facilitates mocking during unit tests.
   */
  static Expected<TcpInfo, std::errc> initFromFd(
      const NetworkSocket& fd,
      const TcpInfo::LookupOptions& options = TcpInfo::LookupOptions(),
      netops::Dispatcher& netopsDispatcher =
          *netops::Dispatcher::getDefaultInstance(),
      IoctlDispatcher& ioctlDispatcher =
          *IoctlDispatcher::getDefaultInstance());

  /**
   * Accessors for tcp_info.
   *
   * These accessors are always available regardless of platform, they return
   * folly::none if the underlying field is unavailable.
   */

  Optional<std::chrono::microseconds> minrtt() const;
  Optional<std::chrono::microseconds> srtt() const;

  Optional<uint64_t> bytesSent() const;
  Optional<uint64_t> bytesReceived() const;
  Optional<uint64_t> bytesRetransmitted() const;
  Optional<uint64_t> bytesNotSent() const;
  Optional<uint64_t> bytesAcked() const;

  Optional<uint64_t> packetsSent() const;
  Optional<uint64_t> packetsWithDataSent() const;
  Optional<uint64_t> packetsReceived() const;
  Optional<uint64_t> packetsWithDataReceived() const;
  Optional<uint64_t> packetsRetransmitted() const;
  Optional<uint64_t> packetsInFlight() const;

  Optional<uint64_t> cwndInPackets() const;
  Optional<uint64_t> cwndInBytes() const;
  Optional<uint64_t> ssthresh() const;
  Optional<uint64_t> mss() const;

  Optional<uint64_t> deliveryRateBitsPerSecond() const;
  Optional<uint64_t> deliveryRateBytesPerSecond() const;
  Optional<bool> deliveryRateAppLimited() const;

  /**
   * Accessors for congestion control information.
   *
   * These accessors are always available regardless of platform, they return
   * folly::none if the underlying field is unavailable.
   */
  Optional<std::string> ccNameRaw() const;
  Optional<CongestionControlName> ccNameEnum() const;
  Optional<folly::StringPiece> ccNameEnumAsStr() const;

  Optional<uint64_t> bbrBwBitsPerSecond() const;
  Optional<uint64_t> bbrBwBytesPerSecond() const;
  Optional<std::chrono::microseconds> bbrMinrtt() const;
  Optional<uint64_t> bbrPacingGain() const;
  Optional<uint64_t> bbrCwndGain() const;

  /**
   * Accessors for memory info information.
   *
   * These accessors are always available regardless of platform, they return
   * folly::none if the underlying field is unavailable.
   */

  Optional<size_t> sendBufInUseBytes() const;
  Optional<size_t> recvBufInUseBytes() const;

 private:
  /**
   * Returns pointer containing requested field from passed struct.
   *
   * If field is unavailable, returns a nullptr.
   */
  template <typename T1, typename T2>
  static const T1* getFieldAsPtr(
      const T2& tgtStruct, const int tgtBytesRead, T1 T2::*field) {
    if (field != nullptr && tgtBytesRead > 0 &&
        getFieldOffset(field) + sizeof(tgtStruct.*field) <=
            (unsigned long)tgtBytesRead) {
      return &(tgtStruct.*field);
    }
    return nullptr;
  }

  /**
   * Get the offset of a field in a struct.
   *
   * Requires that struct (T1) be POD (else undefined behavior).
   *
   * Alternative to `offsetof` that enables us to avoid use of macro.
   * Approach from:
   *    https://gist.github.com/graphitemaster/494f21190bb2c63c5516
   */
  template <typename T1, typename T2>
  static size_t constexpr getFieldOffset(T1 T2::*field) {
    static_assert(std::is_standard_layout<T1>() && std::is_trivial<T1>());
    constexpr T2 dummy{};
    return size_t(&(dummy.*field)) - size_t(&dummy);
  }

  /**
   * Converts an optional containing a value with units bits/s to byte/s.
   *
   * If input optional is empty, returns empty.
   */
  static Optional<uint64_t> bytesPerSecondToBitsPerSecond(
      const Optional<uint64_t>& bytesPerSecondOpt) {
    if (bytesPerSecondOpt.hasValue()) {
      return bytesPerSecondOpt.value() * 8;
    }
    return folly::none;
  }

  /**
   * Initializes the congestion control fields in passed WrappedTcpInfo.
   */
  static void initCcInfoFromFd(
      const NetworkSocket& fd,
      TcpInfo& tcpInfo,
      netops::Dispatcher& netopsDispatcher =
          *netops::Dispatcher::getDefaultInstance());

  /**
   * Initializes the socker buffer memory fields in passed WrappedTcpInfo.
   */
  static void initMemInfoFromFd(
      const NetworkSocket& fd,
      TcpInfo& tcpInfo,
      IoctlDispatcher& ioctlDispatcher =
          *IoctlDispatcher::getDefaultInstance());

#if defined(FOLLY_HAVE_TCP_INFO)
 public:
  using tcp_info = folly::detail::tcp_info;

  TcpInfo() = default;
  explicit TcpInfo(const tcp_info& tInfo)
      : tcpInfo(tInfo), tcpInfoBytesRead{sizeof(TcpInfo::tcp_info)} {}

  /**
   * Returns pointer containing requested field from tcp_info struct.
   *
   * The tcp_info struct type is platform specific (and thus templated). If
   * no tcp_info is supprted for this platform, this accessor is unavailable.
   *
   * To access tcp_info fields without needing to consider platform specifics,
   * use accessors, such as bytesSent().
   */
  template <typename T1>
  const T1* getFieldAsPtr(T1 tcp_info::*field) const {
    return getFieldAsPtr(tcpInfo, tcpInfoBytesRead, field);
  }

  /**
   * Returns Optional<uint64_t> containing requested field from tcp_info struct.
   *
   * The tcp_info struct type is platform specific (and thus templated). If
   * no tcp_info is supprted for this platform, this accessor is unavailable.
   *
   * To access tcp_info fields without needing to consider platform specifics,
   * use accessors such as bytesSent().
   */
  template <typename T1>
  folly::Optional<uint64_t> getFieldAsOptUInt64(T1 tcp_info::*field) const {
    if (auto ptr = getFieldAsPtr(field)) {
      return *ptr;
    }
    return folly::none;
  }

 private:
  // tcp_info struct for this system, may be backported.
  tcp_info tcpInfo = {};

  // number of bytes read during getsockopt for TCP_INFO.
  int tcpInfoBytesRead{0};
#endif

#if defined(FOLLY_HAVE_TCP_CC_INFO)
 public:
  using tcp_cc_info = folly::detail::tcp_cc_info;
  using tcp_bbr_info = folly::detail::tcp_bbr_info;
  using tcpvegas_info = folly::detail::tcpvegas_info;
  using tcp_dctcp_info = folly::detail::tcp_dctcp_info;

  // TCP_CA_NAME_MAX from <net/tcp.h> (Linux) or <netinet/tcp.h> (FreeBSD)
  static constexpr socklen_t kLinuxTcpCaNameMax = 16;

  /**
   * Returns Optional<uint64_t> containing requested field from BBR struct.
   *
   * If tcp_cc_info is unavailable or the congestion controller is not BBR,
   * returns folly::none for all fields.
   *
   * To access tcp_cc_info fields without needing to consider platform
   * specifics, use accessors such as bbrBwBitsPerSecond().
   */
  template <typename T1>
  folly::Optional<uint64_t> getFieldAsOptUInt64(T1 tcp_bbr_info::*field) const {
    if (maybeCcInfo.has_value() && ccNameEnum() == CongestionControlName::BBR) {
      return getFieldAsOptUInt64(maybeCcInfo.value().bbr, field);
    }
    return folly::none;
  }

  /**
   * Returns Optional<uint64_t> containing requested field from Vegas struct.
   *
   * If tcp_cc_info is unavailable or the congestion controller is not Vegas,
   * returns folly::none for all fields.
   */
  template <typename T1>
  folly::Optional<uint64_t> getFieldAsOptUInt64(
      T1 tcpvegas_info::*field) const {
    if (maybeCcInfo.hasValue() &&
        ccNameEnum() == CongestionControlName::VEGAS) {
      return getFieldAsOptUInt64(maybeCcInfo.value().vegas, field);
    }
    return folly::none;
  }

  /**
   * Returns Optional<uint64_t> containing requested field from DCTCP struct.
   *
   * If tcp_cc_info is unavailable or the congestion controller is not DCTCP,
   * returns folly::none for all fields.
   */
  template <typename T1>
  const folly::Optional<uint64_t> getFieldAsOptUInt64(
      T1 tcp_dctcp_info::*field) const {
    if (maybeCcInfo.has_value() &&
        (ccNameEnum() == CongestionControlName::DCTCP ||
         ccNameEnum() == CongestionControlName::DCTCP_CUBIC ||
         ccNameEnum() == CongestionControlName::DCTCP_RENO)) {
      return getFieldAsOptUInt64(maybeCcInfo.value().dctcp, field);
    }
    return folly::none;
  }

 private:
  /**
   * Returns Optional<uint64_t> containing requested field from tcp_cc_info.
   *
   * The tcp_cc_info struct type is platform specific (and thus templated). If
   * no tcp_cc_info is supprted for this platform, this accessor is unavailable.
   *
   * To access tcp_cc_info fields without needing to consider platform
   * specifics, use accessors such as bbrBwBitsPerSecond().
   */
  template <typename T1, typename T2>
  folly::Optional<uint64_t> getFieldAsOptUInt64(
      const T2& tgtStruct, T1 T2::*field) const {
    if (field != nullptr && tcpCcInfoBytesRead > 0 &&
        getFieldOffset(field) + sizeof(tgtStruct.*field) <=
            (unsigned long)tcpCcInfoBytesRead) {
      return folly::Optional<uint64_t>(tgtStruct.*field);
    }
    return folly::none;
  }

  // raw congestion control algorithm name returned by underlying platform
  folly::Optional<std::string> maybeCcNameRaw;

  // enum for congestion control algorithm.
  folly::Optional<CongestionControlName> maybeCcEnum;

  // additional information from congestion control algorithm.
  folly::Optional<tcp_cc_info> maybeCcInfo;

  // number of bytes read during getsockopt for TCP_CC_INFO.
  //
  // if TCP_CC_INFO was not able to be fetched, will be 0.
  int tcpCcInfoBytesRead{0};

#endif // #if defined(FOLLY_HAVE_TCP_CC_INFO)

 private:
  folly::Optional<size_t> maybeSendBufInUseBytes;
  folly::Optional<size_t> maybeRecvBufInUseBytes;
};

} // namespace folly
