/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <folly/io/async/test/BlockingSocket.h>

#include <folly/ExceptionWrapper.h>
#include <gflags/gflags.h>

using namespace folly;

DEFINE_string(host, "localhost", "Host");
DEFINE_int32(port, 0, "port");
DEFINE_bool(tfo, false, "enable tfo");
DEFINE_string(msg, "", "Message to send");

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_port == 0) {
    LOG(ERROR) << "Must specify port";
    exit(EXIT_FAILURE);
  }

  // Prep the socket
  EventBase evb;
  AsyncSocket::UniquePtr socket(new AsyncSocket(&evb));
  socket->detachEventBase();

  if (FLAGS_tfo) {
#if FOLLY_ALLOW_TFO
    socket->enableTFO();
#endif
  }

  // Keep this around
  auto sockAddr = socket.get();

  BlockingSocket sock(std::move(socket));
  SocketAddress addr;
  addr.setFromHostPort(FLAGS_host, FLAGS_port);
  sock.setAddress(addr);
  sock.open();
  LOG(INFO) << "connected to " << addr.getAddressStr();

  sock.write((const uint8_t*)FLAGS_msg.data(), FLAGS_msg.size());

  LOG(ERROR) << "TFO attempted: " << sockAddr->getTFOAttempted();
  LOG(ERROR) << "TFO finished: " << sockAddr->getTFOFinished();

  std::array<char, 1024> buf;
  int32_t bytesRead = 0;
  while ((bytesRead = sock.read((uint8_t*)buf.data(), buf.size())) != 0) {
    std::cout << std::string(buf.data(), bytesRead);
  }

  sock.close();
  return 0;
}
