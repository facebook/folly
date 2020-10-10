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

#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncUDPServerSocket.h>
#include <folly/io/async/EventBase.h>

#ifndef CLOCK_INVALID
#define CLOCK_INVALID -1
#endif

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd) ((clockid_t)((((unsigned int)~fd) << 3) | CLOCKFD))
#define CLOCKID_TO_FD(clk) ((unsigned int)~((clk) >> 3))

DEFINE_bool(ts_client, false, "client mode");
DEFINE_bool(ts_server, false, "client mode");
DEFINE_string(ts_client_addr, "::1", "addr to bind on");
DEFINE_string(ts_server_addr, "::1", "addr to bind on/send packets to");
DEFINE_int32(ts_port, 7879, "port");
DEFINE_string(ts_eth, "eth0", "ethernet interface name");
DEFINE_int32(ts_num, 100, "number of iterations");
DEFINE_int32(ts_gso, 0, "GSO");

std::ostream& operator<<(std::ostream& os, const struct timespec& ts) {
  return os << "(" << ts.tv_sec << "," << ts.tv_nsec << ")";
}

namespace {
class PTPTimeSource {
 public:
  PTPTimeSource(const char* name = "/dev/ptp0") {
    int fd = ::open(name, O_RDWR);
    if (fd >= 0) {
      clkid_ = FD_TO_CLOCKID(fd);
    } else {
      clkid_ = CLOCK_INVALID;
    }
  }

  ~PTPTimeSource() {
    if (clkid_ != CLOCK_INVALID) {
      ::close(CLOCKID_TO_FD(clkid_));
    }
  }

  static PTPTimeSource& getInstance() {
    static PTPTimeSource sInstance;
    return sInstance;
  }

  static struct timespec now() {
    return getInstance().nowInternal();
  }

 private:
  struct timespec nowInternal() {
    struct timespec ts;
    if (clkid_ == CLOCK_INVALID || clock_gettime(clkid_, &ts)) {
      ts.tv_sec = 0;
      ts.tv_nsec = 0;
    }

    return ts;
  }

  clockid_t clkid_;
};

using OnDataAvailableParams =
    folly::AsyncUDPSocket::ReadCallback::OnDataAvailableParams;

class UDPAcceptor : public folly::AsyncUDPServerSocket::Callback {
 public:
  explicit UDPAcceptor(int n) : n_(n) {}

  void onListenStarted() noexcept override {}

  void onListenStopped() noexcept override {}

  void onDataAvailable(
      std::shared_ptr<folly::AsyncUDPSocket> socket,
      const folly::SocketAddress& client,
      std::unique_ptr<folly::IOBuf> data,
      bool truncated,
      OnDataAvailableParams params) noexcept override {
    lastClient_ = client;
    lastMsg_ = data->clone()->moveToFbString().toStdString();
    auto len = data->computeChainDataLength();
    OnDataAvailableParams::Timestamp ts;
    if (params.ts_.has_value()) {
      ts = params.ts_.value();
    } else {
      ::memset(&ts, 0, sizeof(ts));
    }
    auto now = PTPTimeSource::getInstance().now();
    LOG(INFO) << "Worker " << n_ << " read " << len << " bytes "
              << "(trun:" << truncated << ") from " << client.describe()
              << " - " << lastMsg_ << " gro = " << params.gro_
              << " ts = " << params.ts_.has_value() << " " << ts[0] << ts[1]
              << ts[2] << " now:" << now;

    sendPong(socket);
  }

  void sendPong(std::shared_ptr<folly::AsyncUDPSocket> socket) noexcept {
    try {
      auto writeSocket = socket;
      writeSocket->write(lastClient_, folly::IOBuf::copyBuffer(lastMsg_));
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Failed to send PONG " << ex.what();
    }
  }

 private:
  const int n_{-1};

  folly::SocketAddress lastClient_;
  std::string lastMsg_;
};

class UDPServer {
 public:
  UDPServer(folly::EventBase* evb, folly::SocketAddress addr, int n)
      : evb_(evb), addr_(addr), evbs_(n) {}

  void start() {
    CHECK(evb_->isInEventBaseThread());

    socket_ = std::make_unique<folly::AsyncUDPServerSocket>(evb_, 1500);

    try {
      socket_->bind(addr_);
      int val = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE |
          SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_SOFTWARE;
      bool ret = socket_->getSocket()->setTimestamping(val);
      LOG(INFO) << "setTimestamping() returned " << ret;
      ret = socket_->getSocket()->setGRO(true);
      LOG(INFO) << "setGRO() returned " << ret;
      LOG(INFO) << "Server listening on " << socket_->address().describe();
    } catch (const std::exception& ex) {
      LOG(FATAL) << ex.what();
    }

    acceptors_.reserve(evbs_.size());
    threads_.reserve(evbs_.size());

    // Add numWorkers thread
    int i = 0;
    for (auto& evb : evbs_) {
      acceptors_.emplace_back(i);

      std::thread t([&]() { evb.loopForever(); });

      evb.waitUntilRunning();

      socket_->addListener(&evb, &acceptors_[i]);
      threads_.emplace_back(std::move(t));
      ++i;
    }

    socket_->listen();
  }

 private:
  folly::EventBase* const evb_{nullptr};
  const folly::SocketAddress addr_;

  std::unique_ptr<folly::AsyncUDPServerSocket> socket_;
  std::vector<std::thread> threads_;
  std::vector<folly::EventBase> evbs_;
  std::vector<UDPAcceptor> acceptors_;
};

class UDPClient : private folly::AsyncUDPSocket::ReadCallback,
                  private folly::AsyncTimeout {
 public:
  using folly::AsyncUDPSocket::ReadCallback::OnDataAvailableParams;

  ~UDPClient() override = default;

  explicit UDPClient(folly::EventBase* evb)
      : folly::AsyncTimeout(evb), evb_(evb) {}

  void start(
      const folly::SocketAddress& client,
      const folly::SocketAddress& server,
      int n) {
    CHECK(evb_->isInEventBaseThread());
    server_ = server;
    socket_ = std::make_unique<folly::AsyncUDPSocket>(evb_);

    try {
      socket_->bind(client);
      LOG(INFO) << "Client bound to " << socket_->address().describe();
      socket_->setGSO(FLAGS_ts_gso);
      auto ret = socket_->getGSO();
      LOG(INFO) << "GSO = " << ret;
    } catch (const std::exception& ex) {
      LOG(FATAL) << ex.what();
    }

    socket_->resumeRead(this);

    n_ = n;

    sendPing();
  }

  void shutdown() {
    CHECK(evb_->isInEventBaseThread());
    socket_->pauseRead();
    socket_->close();
    socket_.reset();
    evb_->terminateLoopSoon();
  }

  void sendPing() {
    if (n_ == 0) {
      shutdown();
      return;
    }

    --n_;
    scheduleTimeout(1000);
    writePing(folly::IOBuf::copyBuffer(folly::to<std::string>("PING ", n_)));
  }

  virtual void writePing(std::unique_ptr<folly::IOBuf> buf) {
    socket_->write(server_, std::move(buf));
  }

  void getReadBuffer(void** buf, size_t* len) noexcept override {
    *buf = buf_;
    *len = 1024;
  }

  void onDataAvailable(
      const folly::SocketAddress& client,
      size_t len,
      bool truncated,
      OnDataAvailableParams) noexcept override {
    VLOG(4) << "Read " << len << " bytes (trun:" << truncated << ") from "
            << client.describe() << " - " << std::string(buf_, len);
    VLOG(4) << n_ << " left";

    ++pongRecvd_;

    scheduleTimeout(1000);
  }

  void onReadError(const folly::AsyncSocketException& ex) noexcept override {
    LOG(ERROR) << ex.what();

    // Start listening for next PONG
    socket_->resumeRead(this);
  }

  void onReadClosed() noexcept override {
    CHECK(false) << "We unregister reads before closing";
  }

  void timeoutExpired() noexcept override {
    LOG(INFO) << "Timeout expired";
    sendPing();
  }

 protected:
  folly::EventBase* const evb_{nullptr};

  folly::SocketAddress server_;
  std::unique_ptr<folly::AsyncUDPSocket> socket_;

 private:
  int pongRecvd_{0};

  int n_{0};
  char buf_[1024];
};
} // namespace

int main(int argc, char* argv[]) {
  folly::init(&argc, &argv, false);

  if (!FLAGS_ts_eth.empty()) {
    int fd = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, FLAGS_ts_eth.c_str(), sizeof(ifr.ifr_name) - 1);

    struct hwtstamp_config hwc;
    hwc.flags = 0;
    hwc.tx_type = HWTSTAMP_TX_OFF;
    hwc.rx_filter = HWTSTAMP_FILTER_ALL;

    ifr.ifr_data = (char*)&hwc;

    int ret = ::ioctl(fd, SIOCSHWTSTAMP, &ifr);

    if (ret) {
      auto copy = errno;
      LOG(ERROR) << "ioctl(SIOCSHWTSTAMP) failed with " << copy << ":"
                 << folly::errnoStr(copy) << std::endl;
    } else {
      LOG(INFO) << "ioctl(SIOCSHWTSTAMP) success";
    }
    ::close(fd);
  }

  std::unique_ptr<std::thread> serverThread;
  std::unique_ptr<std::thread> clientThread;

  std::unique_ptr<UDPServer> server;
  std::unique_ptr<UDPClient> client;

  std::unique_ptr<folly::EventBase> serverEvb;
  std::unique_ptr<folly::EventBase> clientEvb;

  folly::SocketAddress clientAddr(FLAGS_ts_client_addr, 0);
  folly::SocketAddress serverAddr(FLAGS_ts_server_addr, FLAGS_ts_port);

  if (FLAGS_ts_server) {
    serverEvb = std::make_unique<folly::EventBase>();
    server = std::make_unique<UDPServer>(serverEvb.get(), serverAddr, 4);
    serverThread =
        std::make_unique<std::thread>([&]() { serverEvb->loopForever(); });
    serverEvb->waitUntilRunning();
    serverEvb->runInEventBaseThreadAndWait([&]() { server->start(); });
  }

  if (FLAGS_ts_client) {
    clientEvb = std::make_unique<folly::EventBase>();
    client = std::make_unique<UDPClient>(clientEvb.get());
    clientThread =
        std::make_unique<std::thread>([&]() { clientEvb->loopForever(); });
    clientEvb->waitUntilRunning();
    clientEvb->runInEventBaseThread(
        [&]() { client->start(clientAddr, serverAddr, FLAGS_ts_num); });
    clientThread->join();
  }

  if (serverThread) {
    serverThread->join();
  }

  return 0;
}
