/*
 * Copyright 2015 Facebook, Inc.
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
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/SocketAddress.h>

#include <folly/io/IOBuf.h>
#include <folly/io/async/test/AsyncSocketTest.h>
#include <folly/io/async/test/Util.h>
#include <folly/test/SocketAddressTestHelper.h>

#include <gtest/gtest.h>
#include <boost/scoped_array.hpp>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <thread>

using namespace boost;

using std::string;
using std::vector;
using std::min;
using std::cerr;
using std::endl;
using std::unique_ptr;
using std::chrono::milliseconds;
using boost::scoped_array;

using namespace folly;

class DelayedWrite: public AsyncTimeout {
 public:
  DelayedWrite(const std::shared_ptr<AsyncSocket>& socket,
      unique_ptr<IOBuf>&& bufs, AsyncTransportWrapper::WriteCallback* wcb,
      bool cork, bool lastWrite = false):
    AsyncTimeout(socket->getEventBase()),
    socket_(socket),
    bufs_(std::move(bufs)),
    wcb_(wcb),
    cork_(cork),
    lastWrite_(lastWrite) {}

 private:
  void timeoutExpired() noexcept override {
    WriteFlags flags = cork_ ? WriteFlags::CORK : WriteFlags::NONE;
    socket_->writeChain(wcb_, std::move(bufs_), flags);
    if (lastWrite_) {
      socket_->shutdownWrite();
    }
  }

  std::shared_ptr<AsyncSocket> socket_;
  unique_ptr<IOBuf> bufs_;
  AsyncTransportWrapper::WriteCallback* wcb_;
  bool cork_;
  bool lastWrite_;
};

///////////////////////////////////////////////////////////////////////////
// connect() tests
///////////////////////////////////////////////////////////////////////////

/**
 * Test connecting to a server
 */
TEST(AsyncSocketTest, Connect) {
  // Start listening on a local port
  TestServer server;

  // Connect using a AsyncSocket
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback cb;
  socket->connect(&cb, server.getAddress(), 30);

  evb.loop();

  CHECK_EQ(cb.state, STATE_SUCCEEDED);
}

/**
 * Test connecting to a server that isn't listening
 */
TEST(AsyncSocketTest, ConnectRefused) {
  EventBase evb;

  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);

  // Hopefully nothing is actually listening on this address
  folly::SocketAddress addr("127.0.0.1", 65535);
  ConnCallback cb;
  socket->connect(&cb, addr, 30);

  evb.loop();

  CHECK_EQ(cb.state, STATE_FAILED);
  CHECK_EQ(cb.exception.getType(), AsyncSocketException::NOT_OPEN);
}

/**
 * Test connection timeout
 */
TEST(AsyncSocketTest, ConnectTimeout) {
  EventBase evb;

  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);

  // Try connecting to server that won't respond.
  //
  // This depends somewhat on the network where this test is run.
  // Hopefully this IP will be routable but unresponsive.
  // (Alternatively, we could try listening on a local raw socket, but that
  // normally requires root privileges.)
  auto host =
      SocketAddressTestHelper::isIPv6Enabled() ?
      SocketAddressTestHelper::kGooglePublicDnsAAddrIPv6 :
      SocketAddressTestHelper::isIPv4Enabled() ?
      SocketAddressTestHelper::kGooglePublicDnsAAddrIPv4 :
      nullptr;
  SocketAddress addr(host, 65535);
  ConnCallback cb;
  socket->connect(&cb, addr, 1); // also set a ridiculously small timeout

  evb.loop();

  CHECK_EQ(cb.state, STATE_FAILED);
  CHECK_EQ(cb.exception.getType(), AsyncSocketException::TIMED_OUT);

  // Verify that we can still get the peer address after a timeout.
  // Use case is if the client was created from a client pool, and we want
  // to log which peer failed.
  folly::SocketAddress peer;
  socket->getPeerAddress(&peer);
  CHECK_EQ(peer, addr);
}

/**
 * Test writing immediately after connecting, without waiting for connect
 * to finish.
 */
TEST(AsyncSocketTest, ConnectAndWrite) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // write()
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->write(&wcb, buf, sizeof(buf));

  // Loop.  We don't bother accepting on the server socket yet.
  // The kernel should be able to buffer the write request so it can succeed.
  evb.loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  // Make sure the server got a connection and received the data
  socket->close();
  server.verifyConnection(buf, sizeof(buf));
}

/**
 * Test connecting using a nullptr connect callback.
 */
TEST(AsyncSocketTest, ConnectNullCallback) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  socket->connect(nullptr, server.getAddress(), 30);

  // write some data, just so we have some way of verifing
  // that the socket works correctly after connecting
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->write(&wcb, buf, sizeof(buf));

  evb.loop();

  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  // Make sure the server got a connection and received the data
  socket->close();
  server.verifyConnection(buf, sizeof(buf));
}

/**
 * Test calling both write() and close() immediately after connecting, without
 * waiting for connect to finish.
 *
 * This exercises the STATE_CONNECTING_CLOSING code.
 */
TEST(AsyncSocketTest, ConnectWriteAndClose) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // write()
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->write(&wcb, buf, sizeof(buf));

  // close()
  socket->close();

  // Loop.  We don't bother accepting on the server socket yet.
  // The kernel should be able to buffer the write request so it can succeed.
  evb.loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  // Make sure the server got a connection and received the data
  server.verifyConnection(buf, sizeof(buf));
}

/**
 * Test calling close() immediately after connect()
 */
TEST(AsyncSocketTest, ConnectAndClose) {
  TestServer server;

  // Connect using a AsyncSocket
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the close-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                       "of close-during-connect behavior";
    return;
  }

  socket->close();

  // Loop, although there shouldn't be anything to do.
  evb.loop();

  // Make sure the connection was aborted
  CHECK_EQ(ccb.state, STATE_FAILED);
}

/**
 * Test calling closeNow() immediately after connect()
 *
 * This should be identical to the normal close behavior.
 */
TEST(AsyncSocketTest, ConnectAndCloseNow) {
  TestServer server;

  // Connect using a AsyncSocket
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the close-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                       "of closeNow()-during-connect behavior";
    return;
  }

  socket->closeNow();

  // Loop, although there shouldn't be anything to do.
  evb.loop();

  // Make sure the connection was aborted
  CHECK_EQ(ccb.state, STATE_FAILED);
}

/**
 * Test calling both write() and closeNow() immediately after connecting,
 * without waiting for connect to finish.
 *
 * This should abort the pending write.
 */
TEST(AsyncSocketTest, ConnectWriteAndCloseNow) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the close-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                       "of write-during-connect behavior";
    return;
  }

  // write()
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->write(&wcb, buf, sizeof(buf));

  // close()
  socket->closeNow();

  // Loop, although there shouldn't be anything to do.
  evb.loop();

  CHECK_EQ(ccb.state, STATE_FAILED);
  CHECK_EQ(wcb.state, STATE_FAILED);
}

/**
 * Test installing a read callback immediately, before connect() finishes.
 */
TEST(AsyncSocketTest, ConnectAndRead) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  ReadCallback rcb;
  socket->setReadCB(&rcb);

  // Even though we haven't looped yet, we should be able to accept
  // the connection and send data to it.
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  acceptedSocket->write(buf, sizeof(buf));
  acceptedSocket->flush();
  acceptedSocket->close();

  // Loop, although there shouldn't be anything to do.
  evb.loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length, sizeof(buf));
  CHECK_EQ(memcmp(rcb.buffers[0].buffer, buf, sizeof(buf)), 0);
}

/**
 * Test installing a read callback and then closing immediately before the
 * connect attempt finishes.
 */
TEST(AsyncSocketTest, ConnectReadAndClose) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the close-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                       "of read-during-connect behavior";
    return;
  }

  ReadCallback rcb;
  socket->setReadCB(&rcb);

  // close()
  socket->close();

  // Loop, although there shouldn't be anything to do.
  evb.loop();

  CHECK_EQ(ccb.state, STATE_FAILED); // we aborted the close attempt
  CHECK_EQ(rcb.buffers.size(), 0);
  CHECK_EQ(rcb.state, STATE_SUCCEEDED); // this indicates EOF
}

/**
 * Test both writing and installing a read callback immediately,
 * before connect() finishes.
 */
TEST(AsyncSocketTest, ConnectWriteAndRead) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // write()
  char buf1[128];
  memset(buf1, 'a', sizeof(buf1));
  WriteCallback wcb;
  socket->write(&wcb, buf1, sizeof(buf1));

  // set a read callback
  ReadCallback rcb;
  socket->setReadCB(&rcb);

  // Even though we haven't looped yet, we should be able to accept
  // the connection and send data to it.
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();
  uint8_t buf2[128];
  memset(buf2, 'b', sizeof(buf2));
  acceptedSocket->write(buf2, sizeof(buf2));
  acceptedSocket->flush();

  // shut down the write half of acceptedSocket, so that the AsyncSocket
  // will stop reading and we can break out of the event loop.
  shutdown(acceptedSocket->getSocketFD(), SHUT_WR);

  // Loop
  evb.loop();

  // Make sure the connect succeeded
  CHECK_EQ(ccb.state, STATE_SUCCEEDED);

  // Make sure the AsyncSocket read the data written by the accepted socket
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length, sizeof(buf2));
  CHECK_EQ(memcmp(rcb.buffers[0].buffer, buf2, sizeof(buf2)), 0);

  // Close the AsyncSocket so we'll see EOF on acceptedSocket
  socket->close();

  // Make sure the accepted socket saw the data written by the AsyncSocket
  uint8_t readbuf[sizeof(buf1)];
  acceptedSocket->readAll(readbuf, sizeof(readbuf));
  CHECK_EQ(memcmp(buf1, readbuf, sizeof(buf1)), 0);
  uint32_t bytesRead = acceptedSocket->read(readbuf, sizeof(readbuf));
  CHECK_EQ(bytesRead, 0);
}

/**
 * Test writing to the socket then shutting down writes before the connect
 * attempt finishes.
 */
TEST(AsyncSocketTest, ConnectWriteAndShutdownWrite) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the write-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; skipping test";
    return;
  }

  // Ask to write some data
  char wbuf[128];
  memset(wbuf, 'a', sizeof(wbuf));
  WriteCallback wcb;
  socket->write(&wcb, wbuf, sizeof(wbuf));
  socket->shutdownWrite();

  // Shutdown writes
  socket->shutdownWrite();

  // Even though we haven't looped yet, we should be able to accept
  // the connection.
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();

  // Since the connection is still in progress, there should be no data to
  // read yet.  Verify that the accepted socket is not readable.
  struct pollfd fds[1];
  fds[0].fd = acceptedSocket->getSocketFD();
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  int rc = poll(fds, 1, 0);
  CHECK_EQ(rc, 0);

  // Write data to the accepted socket
  uint8_t acceptedWbuf[192];
  memset(acceptedWbuf, 'b', sizeof(acceptedWbuf));
  acceptedSocket->write(acceptedWbuf, sizeof(acceptedWbuf));
  acceptedSocket->flush();

  // Loop
  evb.loop();

  // The loop should have completed the connection, written the queued data,
  // and shutdown writes on the socket.
  //
  // Check that the connection was completed successfully and that the write
  // callback succeeded.
  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  // Check that we can read the data that was written to the socket, and that
  // we see an EOF, since its socket was half-shutdown.
  uint8_t readbuf[sizeof(wbuf)];
  acceptedSocket->readAll(readbuf, sizeof(readbuf));
  CHECK_EQ(memcmp(wbuf, readbuf, sizeof(wbuf)), 0);
  uint32_t bytesRead = acceptedSocket->read(readbuf, sizeof(readbuf));
  CHECK_EQ(bytesRead, 0);

  // Close the accepted socket.  This will cause it to see EOF
  // and uninstall the read callback when we loop next.
  acceptedSocket->close();

  // Install a read callback, then loop again.
  ReadCallback rcb;
  socket->setReadCB(&rcb);
  evb.loop();

  // This loop should have read the data and seen the EOF
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length, sizeof(acceptedWbuf));
  CHECK_EQ(memcmp(rcb.buffers[0].buffer,
                           acceptedWbuf, sizeof(acceptedWbuf)), 0);
}

/**
 * Test reading, writing, and shutting down writes before the connect attempt
 * finishes.
 */
TEST(AsyncSocketTest, ConnectReadWriteAndShutdownWrite) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the write-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; skipping test";
    return;
  }

  // Install a read callback
  ReadCallback rcb;
  socket->setReadCB(&rcb);

  // Ask to write some data
  char wbuf[128];
  memset(wbuf, 'a', sizeof(wbuf));
  WriteCallback wcb;
  socket->write(&wcb, wbuf, sizeof(wbuf));

  // Shutdown writes
  socket->shutdownWrite();

  // Even though we haven't looped yet, we should be able to accept
  // the connection.
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();

  // Since the connection is still in progress, there should be no data to
  // read yet.  Verify that the accepted socket is not readable.
  struct pollfd fds[1];
  fds[0].fd = acceptedSocket->getSocketFD();
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  int rc = poll(fds, 1, 0);
  CHECK_EQ(rc, 0);

  // Write data to the accepted socket
  uint8_t acceptedWbuf[192];
  memset(acceptedWbuf, 'b', sizeof(acceptedWbuf));
  acceptedSocket->write(acceptedWbuf, sizeof(acceptedWbuf));
  acceptedSocket->flush();
  // Shutdown writes to the accepted socket.  This will cause it to see EOF
  // and uninstall the read callback.
  ::shutdown(acceptedSocket->getSocketFD(), SHUT_WR);

  // Loop
  evb.loop();

  // The loop should have completed the connection, written the queued data,
  // shutdown writes on the socket, read the data we wrote to it, and see the
  // EOF.
  //
  // Check that the connection was completed successfully and that the read
  // and write callbacks were invoked as expected.
  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length, sizeof(acceptedWbuf));
  CHECK_EQ(memcmp(rcb.buffers[0].buffer,
                           acceptedWbuf, sizeof(acceptedWbuf)), 0);
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  // Check that we can read the data that was written to the socket, and that
  // we see an EOF, since its socket was half-shutdown.
  uint8_t readbuf[sizeof(wbuf)];
  acceptedSocket->readAll(readbuf, sizeof(readbuf));
  CHECK_EQ(memcmp(wbuf, readbuf, sizeof(wbuf)), 0);
  uint32_t bytesRead = acceptedSocket->read(readbuf, sizeof(readbuf));
  CHECK_EQ(bytesRead, 0);

  // Fully close both sockets
  acceptedSocket->close();
  socket->close();
}

/**
 * Test reading, writing, and calling shutdownWriteNow() before the
 * connect attempt finishes.
 */
TEST(AsyncSocketTest, ConnectReadWriteAndShutdownWriteNow) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the write-while-connecting code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; skipping test";
    return;
  }

  // Install a read callback
  ReadCallback rcb;
  socket->setReadCB(&rcb);

  // Ask to write some data
  char wbuf[128];
  memset(wbuf, 'a', sizeof(wbuf));
  WriteCallback wcb;
  socket->write(&wcb, wbuf, sizeof(wbuf));

  // Shutdown writes immediately.
  // This should immediately discard the data that we just tried to write.
  socket->shutdownWriteNow();

  // Verify that writeError() was invoked on the write callback.
  CHECK_EQ(wcb.state, STATE_FAILED);
  CHECK_EQ(wcb.bytesWritten, 0);

  // Even though we haven't looped yet, we should be able to accept
  // the connection.
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();

  // Since the connection is still in progress, there should be no data to
  // read yet.  Verify that the accepted socket is not readable.
  struct pollfd fds[1];
  fds[0].fd = acceptedSocket->getSocketFD();
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  int rc = poll(fds, 1, 0);
  CHECK_EQ(rc, 0);

  // Write data to the accepted socket
  uint8_t acceptedWbuf[192];
  memset(acceptedWbuf, 'b', sizeof(acceptedWbuf));
  acceptedSocket->write(acceptedWbuf, sizeof(acceptedWbuf));
  acceptedSocket->flush();
  // Shutdown writes to the accepted socket.  This will cause it to see EOF
  // and uninstall the read callback.
  ::shutdown(acceptedSocket->getSocketFD(), SHUT_WR);

  // Loop
  evb.loop();

  // The loop should have completed the connection, written the queued data,
  // shutdown writes on the socket, read the data we wrote to it, and see the
  // EOF.
  //
  // Check that the connection was completed successfully and that the read
  // callback was invoked as expected.
  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length, sizeof(acceptedWbuf));
  CHECK_EQ(memcmp(rcb.buffers[0].buffer,
                           acceptedWbuf, sizeof(acceptedWbuf)), 0);

  // Since we used shutdownWriteNow(), it should have discarded all pending
  // write data.  Verify we see an immediate EOF when reading from the accepted
  // socket.
  uint8_t readbuf[sizeof(wbuf)];
  uint32_t bytesRead = acceptedSocket->read(readbuf, sizeof(readbuf));
  CHECK_EQ(bytesRead, 0);

  // Fully close both sockets
  acceptedSocket->close();
  socket->close();
}

// Helper function for use in testConnectOptWrite()
// Temporarily disable the read callback
void tmpDisableReads(AsyncSocket* socket, ReadCallback* rcb) {
  // Uninstall the read callback
  socket->setReadCB(nullptr);
  // Schedule the read callback to be reinstalled after 1ms
  socket->getEventBase()->runInLoop(
      std::bind(&AsyncSocket::setReadCB, socket, rcb));
}

/**
 * Test connect+write, then have the connect callback perform another write.
 *
 * This tests interaction of the optimistic writing after connect with
 * additional write attempts that occur in the connect callback.
 */
void testConnectOptWrite(size_t size1, size_t size2, bool close = false) {
  TestServer server;
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);

  // connect()
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Hopefully the connect didn't succeed immediately.
  // If it did, we can't exercise the optimistic write code path.
  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                       "of optimistic write behavior";
    return;
  }

  // Tell the connect callback to perform a write when the connect succeeds
  WriteCallback wcb2;
  scoped_array<char> buf2(new char[size2]);
  memset(buf2.get(), 'b', size2);
  if (size2 > 0) {
    ccb.successCallback = [&] { socket->write(&wcb2, buf2.get(), size2); };
    // Tell the second write callback to close the connection when it is done
    wcb2.successCallback = [&] { socket->closeNow(); };
  }

  // Schedule one write() immediately, before the connect finishes
  scoped_array<char> buf1(new char[size1]);
  memset(buf1.get(), 'a', size1);
  WriteCallback wcb1;
  if (size1 > 0) {
    socket->write(&wcb1, buf1.get(), size1);
  }

  if (close) {
    // immediately perform a close, before connect() completes
    socket->close();
  }

  // Start reading from the other endpoint after 10ms.
  // If we're using large buffers, we have to read so that the writes don't
  // block forever.
  std::shared_ptr<AsyncSocket> acceptedSocket = server.acceptAsync(&evb);
  ReadCallback rcb;
  rcb.dataAvailableCallback = std::bind(tmpDisableReads,
                                        acceptedSocket.get(), &rcb);
  socket->getEventBase()->tryRunAfterDelay(
      std::bind(&AsyncSocket::setReadCB, acceptedSocket.get(), &rcb),
      10);

  // Loop.  We don't bother accepting on the server socket yet.
  // The kernel should be able to buffer the write request so it can succeed.
  evb.loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  if (size1 > 0) {
    CHECK_EQ(wcb1.state, STATE_SUCCEEDED);
  }
  if (size2 > 0) {
    CHECK_EQ(wcb2.state, STATE_SUCCEEDED);
  }

  socket->close();

  // Make sure the read callback received all of the data
  size_t bytesRead = 0;
  for (vector<ReadCallback::Buffer>::const_iterator it = rcb.buffers.begin();
       it != rcb.buffers.end();
       ++it) {
    size_t start = bytesRead;
    bytesRead += it->length;
    size_t end = bytesRead;
    if (start < size1) {
      size_t cmpLen = min(size1, end) - start;
      CHECK_EQ(memcmp(it->buffer, buf1.get() + start, cmpLen), 0);
    }
    if (end > size1 && end <= size1 + size2) {
      size_t itOffset;
      size_t buf2Offset;
      size_t cmpLen;
      if (start >= size1) {
        itOffset = 0;
        buf2Offset = start - size1;
        cmpLen = end - start;
      } else {
        itOffset = size1 - start;
        buf2Offset = 0;
        cmpLen = end - size1;
      }
      CHECK_EQ(memcmp(it->buffer + itOffset, buf2.get() + buf2Offset,
                               cmpLen),
                        0);
    }
  }
  CHECK_EQ(bytesRead, size1 + size2);
}

TEST(AsyncSocketTest, ConnectCallbackWrite) {
  // Test using small writes that should both succeed immediately
  testConnectOptWrite(100, 200);

  // Test using a large buffer in the connect callback, that should block
  const size_t largeSize = 8*1024*1024;
  testConnectOptWrite(100, largeSize);

  // Test using a large initial write
  testConnectOptWrite(largeSize, 100);

  // Test using two large buffers
  testConnectOptWrite(largeSize, largeSize);

  // Test a small write in the connect callback,
  // but no immediate write before connect completes
  testConnectOptWrite(0, 64);

  // Test a large write in the connect callback,
  // but no immediate write before connect completes
  testConnectOptWrite(0, largeSize);

  // Test connect, a small write, then immediately call close() before connect
  // completes
  testConnectOptWrite(211, 0, true);

  // Test connect, a large immediate write (that will block), then immediately
  // call close() before connect completes
  testConnectOptWrite(largeSize, 0, true);
}

///////////////////////////////////////////////////////////////////////////
// write() related tests
///////////////////////////////////////////////////////////////////////////

/**
 * Test writing using a nullptr callback
 */
TEST(AsyncSocketTest, WriteNullCallback) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
    AsyncSocket::newSocket(&evb, server.getAddress(), 30);
  evb.loop(); // loop until the socket is connected

  // write() with a nullptr callback
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  socket->write(nullptr, buf, sizeof(buf));

  evb.loop(); // loop until the data is sent

  // Make sure the server got a connection and received the data
  socket->close();
  server.verifyConnection(buf, sizeof(buf));
}

/**
 * Test writing with a send timeout
 */
TEST(AsyncSocketTest, WriteTimeout) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
    AsyncSocket::newSocket(&evb, server.getAddress(), 30);
  evb.loop(); // loop until the socket is connected

  // write() a large chunk of data, with no-one on the other end reading
  size_t writeLength = 8*1024*1024;
  uint32_t timeout = 200;
  socket->setSendTimeout(timeout);
  scoped_array<char> buf(new char[writeLength]);
  memset(buf.get(), 'a', writeLength);
  WriteCallback wcb;
  socket->write(&wcb, buf.get(), writeLength);

  TimePoint start;
  evb.loop();
  TimePoint end;

  // Make sure the write attempt timed out as requested
  CHECK_EQ(wcb.state, STATE_FAILED);
  CHECK_EQ(wcb.exception.getType(), AsyncSocketException::TIMED_OUT);

  // Check that the write timed out within a reasonable period of time.
  // We don't check for exactly the specified timeout, since AsyncSocket only
  // times out when it hasn't made progress for that period of time.
  //
  // On linux, the first write sends a few hundred kb of data, then blocks for
  // writability, and then unblocks again after 40ms and is able to write
  // another smaller of data before blocking permanently.  Therefore it doesn't
  // time out until 40ms + timeout.
  //
  // I haven't fully verified the cause of this, but I believe it probably
  // occurs because the receiving end delays sending an ack for up to 40ms.
  // (This is the default value for TCP_DELACK_MIN.)  Once the sender receives
  // the ack, it can send some more data.  However, after that point the
  // receiver's kernel buffer is full.  This 40ms delay happens even with
  // TCP_NODELAY and TCP_QUICKACK enabled on both endpoints.  However, the
  // kernel may be automatically disabling TCP_QUICKACK after receiving some
  // data.
  //
  // For now, we simply check that the timeout occurred within 160ms of
  // the requested value.
  T_CHECK_TIMEOUT(start, end, milliseconds(timeout), milliseconds(160));
}

/**
 * Test writing to a socket that the remote endpoint has closed
 */
TEST(AsyncSocketTest, WritePipeError) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
    AsyncSocket::newSocket(&evb, server.getAddress(), 30);
  socket->setSendTimeout(1000);
  evb.loop(); // loop until the socket is connected

  // accept and immediately close the socket
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();
  acceptedSocket.reset();

  // write() a large chunk of data
  size_t writeLength = 8*1024*1024;
  scoped_array<char> buf(new char[writeLength]);
  memset(buf.get(), 'a', writeLength);
  WriteCallback wcb;
  socket->write(&wcb, buf.get(), writeLength);

  evb.loop();

  // Make sure the write failed.
  // It would be nice if AsyncSocketException could convey the errno value,
  // so that we could check for EPIPE
  CHECK_EQ(wcb.state, STATE_FAILED);
  CHECK_EQ(wcb.exception.getType(),
                    AsyncSocketException::INTERNAL_ERROR);
}

/**
 * Test writing a mix of simple buffers and IOBufs
 */
TEST(AsyncSocketTest, WriteIOBuf) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Accept the connection
  std::shared_ptr<AsyncSocket> acceptedSocket = server.acceptAsync(&evb);
  ReadCallback rcb;
  acceptedSocket->setReadCB(&rcb);

  // Write a simple buffer to the socket
  size_t simpleBufLength = 5;
  char simpleBuf[simpleBufLength];
  memset(simpleBuf, 'a', simpleBufLength);
  WriteCallback wcb;
  socket->write(&wcb, simpleBuf, simpleBufLength);

  // Write a single-element IOBuf chain
  size_t buf1Length = 7;
  unique_ptr<IOBuf> buf1(IOBuf::create(buf1Length));
  memset(buf1->writableData(), 'b', buf1Length);
  buf1->append(buf1Length);
  unique_ptr<IOBuf> buf1Copy(buf1->clone());
  WriteCallback wcb2;
  socket->writeChain(&wcb2, std::move(buf1));

  // Write a multiple-element IOBuf chain
  size_t buf2Length = 11;
  unique_ptr<IOBuf> buf2(IOBuf::create(buf2Length));
  memset(buf2->writableData(), 'c', buf2Length);
  buf2->append(buf2Length);
  size_t buf3Length = 13;
  unique_ptr<IOBuf> buf3(IOBuf::create(buf3Length));
  memset(buf3->writableData(), 'd', buf3Length);
  buf3->append(buf3Length);
  buf2->appendChain(std::move(buf3));
  unique_ptr<IOBuf> buf2Copy(buf2->clone());
  buf2Copy->coalesce();
  WriteCallback wcb3;
  socket->writeChain(&wcb3, std::move(buf2));
  socket->shutdownWrite();

  // Let the reads and writes run to completion
  evb.loop();

  CHECK_EQ(wcb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb2.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb3.state, STATE_SUCCEEDED);

  // Make sure the reader got the right data in the right order
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length,
      simpleBufLength + buf1Length + buf2Length + buf3Length);
  CHECK_EQ(
      memcmp(rcb.buffers[0].buffer, simpleBuf, simpleBufLength), 0);
  CHECK_EQ(
      memcmp(rcb.buffers[0].buffer + simpleBufLength,
          buf1Copy->data(), buf1Copy->length()), 0);
  CHECK_EQ(
      memcmp(rcb.buffers[0].buffer + simpleBufLength + buf1Length,
          buf2Copy->data(), buf2Copy->length()), 0);

  acceptedSocket->close();
  socket->close();
}

TEST(AsyncSocketTest, WriteIOBufCorked) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Accept the connection
  std::shared_ptr<AsyncSocket> acceptedSocket = server.acceptAsync(&evb);
  ReadCallback rcb;
  acceptedSocket->setReadCB(&rcb);

  // Do three writes, 100ms apart, with the "cork" flag set
  // on the second write.  The reader should see the first write
  // arrive by itself, followed by the second and third writes
  // arriving together.
  size_t buf1Length = 5;
  unique_ptr<IOBuf> buf1(IOBuf::create(buf1Length));
  memset(buf1->writableData(), 'a', buf1Length);
  buf1->append(buf1Length);
  size_t buf2Length = 7;
  unique_ptr<IOBuf> buf2(IOBuf::create(buf2Length));
  memset(buf2->writableData(), 'b', buf2Length);
  buf2->append(buf2Length);
  size_t buf3Length = 11;
  unique_ptr<IOBuf> buf3(IOBuf::create(buf3Length));
  memset(buf3->writableData(), 'c', buf3Length);
  buf3->append(buf3Length);
  WriteCallback wcb1;
  socket->writeChain(&wcb1, std::move(buf1));
  WriteCallback wcb2;
  DelayedWrite write2(socket, std::move(buf2), &wcb2, true);
  write2.scheduleTimeout(100);
  WriteCallback wcb3;
  DelayedWrite write3(socket, std::move(buf3), &wcb3, false, true);
  write3.scheduleTimeout(200);

  evb.loop();
  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb1.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb2.state, STATE_SUCCEEDED);
  if (wcb3.state != STATE_SUCCEEDED) {
    throw(wcb3.exception);
  }
  CHECK_EQ(wcb3.state, STATE_SUCCEEDED);

  // Make sure the reader got the data with the right grouping
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 2);
  CHECK_EQ(rcb.buffers[0].length, buf1Length);
  CHECK_EQ(rcb.buffers[1].length, buf2Length + buf3Length);

  acceptedSocket->close();
  socket->close();
}

/**
 * Test performing a zero-length write
 */
TEST(AsyncSocketTest, ZeroLengthWrite) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
    AsyncSocket::newSocket(&evb, server.getAddress(), 30);
  evb.loop(); // loop until the socket is connected

  auto acceptedSocket = server.acceptAsync(&evb);
  ReadCallback rcb;
  acceptedSocket->setReadCB(&rcb);

  size_t len1 = 1024*1024;
  size_t len2 = 1024*1024;
  std::unique_ptr<char[]> buf(new char[len1 + len2]);
  memset(buf.get(), 'a', len1);
  memset(buf.get(), 'b', len2);

  WriteCallback wcb1;
  WriteCallback wcb2;
  WriteCallback wcb3;
  WriteCallback wcb4;
  socket->write(&wcb1, buf.get(), 0);
  socket->write(&wcb2, buf.get(), len1);
  socket->write(&wcb3, buf.get() + len1, 0);
  socket->write(&wcb4, buf.get() + len1, len2);
  socket->close();

  evb.loop(); // loop until the data is sent

  CHECK_EQ(wcb1.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb2.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb3.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb4.state, STATE_SUCCEEDED);
  rcb.verifyData(buf.get(), len1 + len2);
}

TEST(AsyncSocketTest, ZeroLengthWritev) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket =
    AsyncSocket::newSocket(&evb, server.getAddress(), 30);
  evb.loop(); // loop until the socket is connected

  auto acceptedSocket = server.acceptAsync(&evb);
  ReadCallback rcb;
  acceptedSocket->setReadCB(&rcb);

  size_t len1 = 1024*1024;
  size_t len2 = 1024*1024;
  std::unique_ptr<char[]> buf(new char[len1 + len2]);
  memset(buf.get(), 'a', len1);
  memset(buf.get(), 'b', len2);

  WriteCallback wcb;
  size_t iovCount = 4;
  struct iovec iov[iovCount];
  iov[0].iov_base = buf.get();
  iov[0].iov_len = len1;
  iov[1].iov_base = buf.get() + len1;
  iov[1].iov_len = 0;
  iov[2].iov_base = buf.get() + len1;
  iov[2].iov_len = len2;
  iov[3].iov_base = buf.get() + len1 + len2;
  iov[3].iov_len = 0;

  socket->writev(&wcb, iov, iovCount);
  socket->close();
  evb.loop(); // loop until the data is sent

  CHECK_EQ(wcb.state, STATE_SUCCEEDED);
  rcb.verifyData(buf.get(), len1 + len2);
}

///////////////////////////////////////////////////////////////////////////
// close() related tests
///////////////////////////////////////////////////////////////////////////

/**
 * Test calling close() with pending writes when the socket is already closing.
 */
TEST(AsyncSocketTest, ClosePendingWritesWhileClosing) {
  TestServer server;

  // connect()
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&evb);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // accept the socket on the server side
  std::shared_ptr<BlockingSocket> acceptedSocket = server.accept();

  // Loop to ensure the connect has completed
  evb.loop();

  // Make sure we are connected
  CHECK_EQ(ccb.state, STATE_SUCCEEDED);

  // Schedule pending writes, until several write attempts have blocked
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  typedef vector< std::shared_ptr<WriteCallback> > WriteCallbackVector;
  WriteCallbackVector writeCallbacks;

  writeCallbacks.reserve(5);
  while (writeCallbacks.size() < 5) {
    std::shared_ptr<WriteCallback> wcb(new WriteCallback);

    socket->write(wcb.get(), buf, sizeof(buf));
    if (wcb->state == STATE_SUCCEEDED) {
      // Succeeded immediately.  Keep performing more writes
      continue;
    }

    // This write is blocked.
    // Have the write callback call close() when writeError() is invoked
    wcb->errorCallback = std::bind(&AsyncSocket::close, socket.get());
    writeCallbacks.push_back(wcb);
  }

  // Call closeNow() to immediately fail the pending writes
  socket->closeNow();

  // Make sure writeError() was invoked on all of the pending write callbacks
  for (WriteCallbackVector::const_iterator it = writeCallbacks.begin();
       it != writeCallbacks.end();
       ++it) {
    CHECK_EQ((*it)->state, STATE_FAILED);
  }
}

///////////////////////////////////////////////////////////////////////////
// ImmediateRead related tests
///////////////////////////////////////////////////////////////////////////

/* AsyncSocket use to verify immediate read works */
class AsyncSocketImmediateRead : public folly::AsyncSocket {
 public:
  bool immediateReadCalled = false;
  explicit AsyncSocketImmediateRead(folly::EventBase* evb) : AsyncSocket(evb) {}
 protected:
  void checkForImmediateRead() noexcept override {
    immediateReadCalled = true;
    AsyncSocket::handleRead();
  }
};

TEST(AsyncSocket, ConnectReadImmediateRead) {
  TestServer server;

  const size_t maxBufferSz = 100;
  const size_t maxReadsPerEvent = 1;
  const size_t expectedDataSz = maxBufferSz * 3;
  char expectedData[expectedDataSz];
  memset(expectedData, 'j', expectedDataSz);

  EventBase evb;
  ReadCallback rcb(maxBufferSz);
  AsyncSocketImmediateRead socket(&evb);
  socket.connect(nullptr, server.getAddress(), 30);

  evb.loop(); // loop until the socket is connected

  socket.setReadCB(&rcb);
  socket.setMaxReadsPerEvent(maxReadsPerEvent);
  socket.immediateReadCalled = false;

  auto acceptedSocket = server.acceptAsync(&evb);

  ReadCallback rcbServer;
  WriteCallback wcbServer;
  rcbServer.dataAvailableCallback = [&]() {
    if (rcbServer.dataRead() == expectedDataSz) {
      // write back all data read
      rcbServer.verifyData(expectedData, expectedDataSz);
      acceptedSocket->write(&wcbServer, expectedData, expectedDataSz);
      acceptedSocket->close();
    }
  };
  acceptedSocket->setReadCB(&rcbServer);

  // write data
  WriteCallback wcb1;
  socket.write(&wcb1, expectedData, expectedDataSz);
  evb.loop();
  CHECK_EQ(wcb1.state, STATE_SUCCEEDED);
  rcb.verifyData(expectedData, expectedDataSz);
  CHECK_EQ(socket.immediateReadCalled, true);
}

TEST(AsyncSocket, ConnectReadUninstallRead) {
  TestServer server;

  const size_t maxBufferSz = 100;
  const size_t maxReadsPerEvent = 1;
  const size_t expectedDataSz = maxBufferSz * 3;
  char expectedData[expectedDataSz];
  memset(expectedData, 'k', expectedDataSz);

  EventBase evb;
  ReadCallback rcb(maxBufferSz);
  AsyncSocketImmediateRead socket(&evb);
  socket.connect(nullptr, server.getAddress(), 30);

  evb.loop(); // loop until the socket is connected

  socket.setReadCB(&rcb);
  socket.setMaxReadsPerEvent(maxReadsPerEvent);
  socket.immediateReadCalled = false;

  auto acceptedSocket = server.acceptAsync(&evb);

  ReadCallback rcbServer;
  WriteCallback wcbServer;
  rcbServer.dataAvailableCallback = [&]() {
    if (rcbServer.dataRead() == expectedDataSz) {
      // write back all data read
      rcbServer.verifyData(expectedData, expectedDataSz);
      acceptedSocket->write(&wcbServer, expectedData, expectedDataSz);
      acceptedSocket->close();
    }
  };
  acceptedSocket->setReadCB(&rcbServer);

  rcb.dataAvailableCallback = [&]() {
    // we read data and reset readCB
    socket.setReadCB(nullptr);
  };

  // write data
  WriteCallback wcb;
  socket.write(&wcb, expectedData, expectedDataSz);
  evb.loop();
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  /* we shoud've only read maxBufferSz data since readCallback_
   * was reset in dataAvailableCallback */
  CHECK_EQ(rcb.dataRead(), maxBufferSz);
  CHECK_EQ(socket.immediateReadCalled, false);
}

// TODO:
// - Test connect() and have the connect callback set the read callback
// - Test connect() and have the connect callback unset the read callback
// - Test reading/writing/closing/destroying the socket in the connect callback
// - Test reading/writing/closing/destroying the socket in the read callback
// - Test reading/writing/closing/destroying the socket in the write callback
// - Test one-way shutdown behavior
// - Test changing the EventBase
//
// - TODO: test multiple threads sharing a AsyncSocket, and detaching from it
//   in connectSuccess(), readDataAvailable(), writeSuccess()


///////////////////////////////////////////////////////////////////////////
// AsyncServerSocket tests
///////////////////////////////////////////////////////////////////////////

/**
 * Helper AcceptCallback class for the test code
 * It records the callbacks that were invoked, and also supports calling
 * generic std::function objects in each callback.
 */
class TestAcceptCallback : public AsyncServerSocket::AcceptCallback {
 public:
  enum EventType {
    TYPE_START,
    TYPE_ACCEPT,
    TYPE_ERROR,
    TYPE_STOP
  };
  struct EventInfo {
    EventInfo(int fd, const folly::SocketAddress& addr)
      : type(TYPE_ACCEPT),
        fd(fd),
        address(addr),
        errorMsg() {}
    explicit EventInfo(const std::string& msg)
      : type(TYPE_ERROR),
        fd(-1),
        address(),
        errorMsg(msg) {}
    explicit EventInfo(EventType et)
      : type(et),
        fd(-1),
        address(),
        errorMsg() {}

    EventType type;
    int fd;  // valid for TYPE_ACCEPT
    folly::SocketAddress address;  // valid for TYPE_ACCEPT
    string errorMsg;  // valid for TYPE_ERROR
  };
  typedef std::deque<EventInfo> EventList;

  TestAcceptCallback()
    : connectionAcceptedFn_(),
      acceptErrorFn_(),
      acceptStoppedFn_(),
      events_() {}

  std::deque<EventInfo>* getEvents() {
    return &events_;
  }

  void setConnectionAcceptedFn(
      const std::function<void(int, const folly::SocketAddress&)>& fn) {
    connectionAcceptedFn_ = fn;
  }
  void setAcceptErrorFn(const std::function<void(const std::exception&)>& fn) {
    acceptErrorFn_ = fn;
  }
  void setAcceptStartedFn(const std::function<void()>& fn) {
    acceptStartedFn_ = fn;
  }
  void setAcceptStoppedFn(const std::function<void()>& fn) {
    acceptStoppedFn_ = fn;
  }

  void connectionAccepted(
      int fd, const folly::SocketAddress& clientAddr) noexcept override {
    events_.emplace_back(fd, clientAddr);

    if (connectionAcceptedFn_) {
      connectionAcceptedFn_(fd, clientAddr);
    }
  }
  void acceptError(const std::exception& ex) noexcept override {
    events_.emplace_back(ex.what());

    if (acceptErrorFn_) {
      acceptErrorFn_(ex);
    }
  }
  void acceptStarted() noexcept override {
    events_.emplace_back(TYPE_START);

    if (acceptStartedFn_) {
      acceptStartedFn_();
    }
  }
  void acceptStopped() noexcept override {
    events_.emplace_back(TYPE_STOP);

    if (acceptStoppedFn_) {
      acceptStoppedFn_();
    }
  }

 private:
  std::function<void(int, const folly::SocketAddress&)> connectionAcceptedFn_;
  std::function<void(const std::exception&)> acceptErrorFn_;
  std::function<void()> acceptStartedFn_;
  std::function<void()> acceptStoppedFn_;

  std::deque<EventInfo> events_;
};

/**
 * Make sure accepted sockets have O_NONBLOCK and TCP_NODELAY set
 */
TEST(AsyncSocketTest, ServerAcceptOptions) {
  EventBase eventBase;

  // Create a server socket
  std::shared_ptr<AsyncServerSocket> serverSocket(
      AsyncServerSocket::newSocket(&eventBase));
  serverSocket->bind(0);
  serverSocket->listen(16);
  folly::SocketAddress serverAddress;
  serverSocket->getAddress(&serverAddress);

  // Add a callback to accept one connection then stop the loop
  TestAcceptCallback acceptCallback;
  acceptCallback.setConnectionAcceptedFn(
    [&](int fd, const folly::SocketAddress& addr) {
      serverSocket->removeAcceptCallback(&acceptCallback, nullptr);
    });
  acceptCallback.setAcceptErrorFn([&](const std::exception& ex) {
    serverSocket->removeAcceptCallback(&acceptCallback, nullptr);
  });
  serverSocket->addAcceptCallback(&acceptCallback, nullptr);
  serverSocket->startAccepting();

  // Connect to the server socket
  std::shared_ptr<AsyncSocket> socket(
      AsyncSocket::newSocket(&eventBase, serverAddress));

  eventBase.loop();

  // Verify that the server accepted a connection
  CHECK_EQ(acceptCallback.getEvents()->size(), 3);
  CHECK_EQ(acceptCallback.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(acceptCallback.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(acceptCallback.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_STOP);
  int fd = acceptCallback.getEvents()->at(1).fd;

  // The accepted connection should already be in non-blocking mode
  int flags = fcntl(fd, F_GETFL, 0);
  CHECK_EQ(flags & O_NONBLOCK, O_NONBLOCK);

#ifndef TCP_NOPUSH
  // The accepted connection should already have TCP_NODELAY set
  int value;
  socklen_t valueLength = sizeof(value);
  int rc = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &valueLength);
  CHECK_EQ(rc, 0);
  CHECK_EQ(value, 1);
#endif
}

/**
 * Test AsyncServerSocket::removeAcceptCallback()
 */
TEST(AsyncSocketTest, RemoveAcceptCallback) {
  // Create a new AsyncServerSocket
  EventBase eventBase;
  std::shared_ptr<AsyncServerSocket> serverSocket(
      AsyncServerSocket::newSocket(&eventBase));
  serverSocket->bind(0);
  serverSocket->listen(16);
  folly::SocketAddress serverAddress;
  serverSocket->getAddress(&serverAddress);

  // Add several accept callbacks
  TestAcceptCallback cb1;
  TestAcceptCallback cb2;
  TestAcceptCallback cb3;
  TestAcceptCallback cb4;
  TestAcceptCallback cb5;
  TestAcceptCallback cb6;
  TestAcceptCallback cb7;

  // Test having callbacks remove other callbacks before them on the list,
  // after them on the list, or removing themselves.
  //
  // Have callback 2 remove callback 3 and callback 5 the first time it is
  // called.
  int cb2Count = 0;
  cb1.setConnectionAcceptedFn([&](int fd, const folly::SocketAddress& addr){
      std::shared_ptr<AsyncSocket> sock2(
        AsyncSocket::newSocket(&eventBase, serverAddress)); // cb2: -cb3 -cb5
      });
  cb3.setConnectionAcceptedFn([&](int fd, const folly::SocketAddress& addr){
    });
  cb4.setConnectionAcceptedFn([&](int fd, const folly::SocketAddress& addr){
      std::shared_ptr<AsyncSocket> sock3(
        AsyncSocket::newSocket(&eventBase, serverAddress)); // cb4
    });
  cb5.setConnectionAcceptedFn([&](int fd, const folly::SocketAddress& addr){
  std::shared_ptr<AsyncSocket> sock5(
      AsyncSocket::newSocket(&eventBase, serverAddress)); // cb7: -cb7

    });
  cb2.setConnectionAcceptedFn(
    [&](int fd, const folly::SocketAddress& addr) {
      if (cb2Count == 0) {
        serverSocket->removeAcceptCallback(&cb3, nullptr);
        serverSocket->removeAcceptCallback(&cb5, nullptr);
      }
      ++cb2Count;
    });
  // Have callback 6 remove callback 4 the first time it is called,
  // and destroy the server socket the second time it is called
  int cb6Count = 0;
  cb6.setConnectionAcceptedFn(
    [&](int fd, const folly::SocketAddress& addr) {
      if (cb6Count == 0) {
        serverSocket->removeAcceptCallback(&cb4, nullptr);
        std::shared_ptr<AsyncSocket> sock6(
          AsyncSocket::newSocket(&eventBase, serverAddress)); // cb1
        std::shared_ptr<AsyncSocket> sock7(
          AsyncSocket::newSocket(&eventBase, serverAddress)); // cb2
        std::shared_ptr<AsyncSocket> sock8(
          AsyncSocket::newSocket(&eventBase, serverAddress)); // cb6: stop

      } else {
        serverSocket.reset();
      }
      ++cb6Count;
    });
  // Have callback 7 remove itself
  cb7.setConnectionAcceptedFn(
    [&](int fd, const folly::SocketAddress& addr) {
      serverSocket->removeAcceptCallback(&cb7, nullptr);
    });

  serverSocket->addAcceptCallback(&cb1, nullptr);
  serverSocket->addAcceptCallback(&cb2, nullptr);
  serverSocket->addAcceptCallback(&cb3, nullptr);
  serverSocket->addAcceptCallback(&cb4, nullptr);
  serverSocket->addAcceptCallback(&cb5, nullptr);
  serverSocket->addAcceptCallback(&cb6, nullptr);
  serverSocket->addAcceptCallback(&cb7, nullptr);
  serverSocket->startAccepting();

  // Make several connections to the socket
  std::shared_ptr<AsyncSocket> sock1(
      AsyncSocket::newSocket(&eventBase, serverAddress)); // cb1
  std::shared_ptr<AsyncSocket> sock4(
      AsyncSocket::newSocket(&eventBase, serverAddress)); // cb6: -cb4

  // Loop until we are stopped
  eventBase.loop();

  // Check to make sure that the expected callbacks were invoked.
  //
  // NOTE: This code depends on the AsyncServerSocket operating calling all of
  // the AcceptCallbacks in round-robin fashion, in the order that they were
  // added.  The code is implemented this way right now, but the API doesn't
  // explicitly require it be done this way.  If we change the code not to be
  // exactly round robin in the future, we can simplify the test checks here.
  // (We'll also need to update the termination code, since we expect cb6 to
  // get called twice to terminate the loop.)
  CHECK_EQ(cb1.getEvents()->size(), 4);
  CHECK_EQ(cb1.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb1.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb1.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb1.getEvents()->at(3).type,
                    TestAcceptCallback::TYPE_STOP);

  CHECK_EQ(cb2.getEvents()->size(), 4);
  CHECK_EQ(cb2.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb2.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb2.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb2.getEvents()->at(3).type,
                    TestAcceptCallback::TYPE_STOP);

  CHECK_EQ(cb3.getEvents()->size(), 2);
  CHECK_EQ(cb3.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb3.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_STOP);

  CHECK_EQ(cb4.getEvents()->size(), 3);
  CHECK_EQ(cb4.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb4.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb4.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_STOP);

  CHECK_EQ(cb5.getEvents()->size(), 2);
  CHECK_EQ(cb5.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb5.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_STOP);

  CHECK_EQ(cb6.getEvents()->size(), 4);
  CHECK_EQ(cb6.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb6.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb6.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb6.getEvents()->at(3).type,
                    TestAcceptCallback::TYPE_STOP);

  CHECK_EQ(cb7.getEvents()->size(), 3);
  CHECK_EQ(cb7.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb7.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb7.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_STOP);
}

/**
 * Test AsyncServerSocket::removeAcceptCallback()
 */
TEST(AsyncSocketTest, OtherThreadAcceptCallback) {
  // Create a new AsyncServerSocket
  EventBase eventBase;
  std::shared_ptr<AsyncServerSocket> serverSocket(
      AsyncServerSocket::newSocket(&eventBase));
  serverSocket->bind(0);
  serverSocket->listen(16);
  folly::SocketAddress serverAddress;
  serverSocket->getAddress(&serverAddress);

  // Add several accept callbacks
  TestAcceptCallback cb1;
  auto thread_id = pthread_self();
  cb1.setAcceptStartedFn([&](){
    CHECK_NE(thread_id, pthread_self());
    thread_id = pthread_self();
  });
  cb1.setConnectionAcceptedFn([&](int fd, const folly::SocketAddress& addr){
    CHECK_EQ(thread_id, pthread_self());
    serverSocket->removeAcceptCallback(&cb1, nullptr);
  });
  cb1.setAcceptStoppedFn([&](){
    CHECK_EQ(thread_id, pthread_self());
  });

  // Test having callbacks remove other callbacks before them on the list,
  serverSocket->addAcceptCallback(&cb1, nullptr);
  serverSocket->startAccepting();

  // Make several connections to the socket
  std::shared_ptr<AsyncSocket> sock1(
      AsyncSocket::newSocket(&eventBase, serverAddress)); // cb1

  // Loop in another thread
  auto other = std::thread([&](){
    eventBase.loop();
  });
  other.join();

  // Check to make sure that the expected callbacks were invoked.
  //
  // NOTE: This code depends on the AsyncServerSocket operating calling all of
  // the AcceptCallbacks in round-robin fashion, in the order that they were
  // added.  The code is implemented this way right now, but the API doesn't
  // explicitly require it be done this way.  If we change the code not to be
  // exactly round robin in the future, we can simplify the test checks here.
  // (We'll also need to update the termination code, since we expect cb6 to
  // get called twice to terminate the loop.)
  CHECK_EQ(cb1.getEvents()->size(), 3);
  CHECK_EQ(cb1.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(cb1.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(cb1.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_STOP);

}

void serverSocketSanityTest(AsyncServerSocket* serverSocket) {
  // Add a callback to accept one connection then stop accepting
  TestAcceptCallback acceptCallback;
  acceptCallback.setConnectionAcceptedFn(
    [&](int fd, const folly::SocketAddress& addr) {
      serverSocket->removeAcceptCallback(&acceptCallback, nullptr);
    });
  acceptCallback.setAcceptErrorFn([&](const std::exception& ex) {
    serverSocket->removeAcceptCallback(&acceptCallback, nullptr);
  });
  serverSocket->addAcceptCallback(&acceptCallback, nullptr);
  serverSocket->startAccepting();

  // Connect to the server socket
  EventBase* eventBase = serverSocket->getEventBase();
  folly::SocketAddress serverAddress;
  serverSocket->getAddress(&serverAddress);
  AsyncSocket::UniquePtr socket(new AsyncSocket(eventBase, serverAddress));

  // Loop to process all events
  eventBase->loop();

  // Verify that the server accepted a connection
  CHECK_EQ(acceptCallback.getEvents()->size(), 3);
  CHECK_EQ(acceptCallback.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(acceptCallback.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(acceptCallback.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_STOP);
}

/* Verify that we don't leak sockets if we are destroyed()
 * and there are still writes pending
 *
 * If destroy() only calls close() instead of closeNow(),
 * it would shutdown(writes) on the socket, but it would
 * never be close()'d, and the socket would leak
 */
TEST(AsyncSocketTest, DestroyCloseTest) {
  TestServer server;

  // connect()
  EventBase clientEB;
  EventBase serverEB;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::newSocket(&clientEB);
  ConnCallback ccb;
  socket->connect(&ccb, server.getAddress(), 30);

  // Accept the connection
  std::shared_ptr<AsyncSocket> acceptedSocket = server.acceptAsync(&serverEB);
  ReadCallback rcb;
  acceptedSocket->setReadCB(&rcb);

  // Write a large buffer to the socket that is larger than kernel buffer
  size_t simpleBufLength = 5000000;
  char* simpleBuf = new char[simpleBufLength];
  memset(simpleBuf, 'a', simpleBufLength);
  WriteCallback wcb;

  // Let the reads and writes run to completion
  int fd = acceptedSocket->getFd();

  acceptedSocket->write(&wcb, simpleBuf, simpleBufLength);
  socket.reset();
  acceptedSocket.reset();

  // Test that server socket was closed
  ssize_t sz = read(fd, simpleBuf, simpleBufLength);
  CHECK_EQ(sz, -1);
  CHECK_EQ(errno, 9);
  delete[] simpleBuf;
}

/**
 * Test AsyncServerSocket::useExistingSocket()
 */
TEST(AsyncSocketTest, ServerExistingSocket) {
  EventBase eventBase;

  // Test creating a socket, and letting AsyncServerSocket bind and listen
  {
    // Manually create a socket
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_GE(fd, 0);

    // Create a server socket
    AsyncServerSocket::UniquePtr serverSocket(
        new AsyncServerSocket(&eventBase));
    serverSocket->useExistingSocket(fd);
    folly::SocketAddress address;
    serverSocket->getAddress(&address);
    address.setPort(0);
    serverSocket->bind(address);
    serverSocket->listen(16);

    // Make sure the socket works
    serverSocketSanityTest(serverSocket.get());
  }

  // Test creating a socket and binding manually,
  // then letting AsyncServerSocket listen
  {
    // Manually create a socket
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_GE(fd, 0);
    // bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = INADDR_ANY;
    CHECK_EQ(bind(fd, reinterpret_cast<struct sockaddr*>(&addr),
                             sizeof(addr)), 0);
    // Look up the address that we bound to
    folly::SocketAddress boundAddress;
    boundAddress.setFromLocalAddress(fd);

    // Create a server socket
    AsyncServerSocket::UniquePtr serverSocket(
        new AsyncServerSocket(&eventBase));
    serverSocket->useExistingSocket(fd);
    serverSocket->listen(16);

    // Make sure AsyncServerSocket reports the same address that we bound to
    folly::SocketAddress serverSocketAddress;
    serverSocket->getAddress(&serverSocketAddress);
    CHECK_EQ(boundAddress, serverSocketAddress);

    // Make sure the socket works
    serverSocketSanityTest(serverSocket.get());
  }

  // Test creating a socket, binding and listening manually,
  // then giving it to AsyncServerSocket
  {
    // Manually create a socket
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_GE(fd, 0);
    // bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = INADDR_ANY;
    CHECK_EQ(bind(fd, reinterpret_cast<struct sockaddr*>(&addr),
                             sizeof(addr)), 0);
    // Look up the address that we bound to
    folly::SocketAddress boundAddress;
    boundAddress.setFromLocalAddress(fd);
    // listen
    CHECK_EQ(listen(fd, 16), 0);

    // Create a server socket
    AsyncServerSocket::UniquePtr serverSocket(
        new AsyncServerSocket(&eventBase));
    serverSocket->useExistingSocket(fd);

    // Make sure AsyncServerSocket reports the same address that we bound to
    folly::SocketAddress serverSocketAddress;
    serverSocket->getAddress(&serverSocketAddress);
    CHECK_EQ(boundAddress, serverSocketAddress);

    // Make sure the socket works
    serverSocketSanityTest(serverSocket.get());
  }
}

TEST(AsyncSocketTest, UnixDomainSocketTest) {
  EventBase eventBase;

  // Create a server socket
  std::shared_ptr<AsyncServerSocket> serverSocket(
      AsyncServerSocket::newSocket(&eventBase));
  string path(1, 0);
  path.append("/anonymous");
  folly::SocketAddress serverAddress;
  serverAddress.setFromPath(path);
  serverSocket->bind(serverAddress);
  serverSocket->listen(16);

  // Add a callback to accept one connection then stop the loop
  TestAcceptCallback acceptCallback;
  acceptCallback.setConnectionAcceptedFn(
    [&](int fd, const folly::SocketAddress& addr) {
      serverSocket->removeAcceptCallback(&acceptCallback, nullptr);
    });
  acceptCallback.setAcceptErrorFn([&](const std::exception& ex) {
    serverSocket->removeAcceptCallback(&acceptCallback, nullptr);
  });
  serverSocket->addAcceptCallback(&acceptCallback, nullptr);
  serverSocket->startAccepting();

  // Connect to the server socket
  std::shared_ptr<AsyncSocket> socket(
      AsyncSocket::newSocket(&eventBase, serverAddress));

  eventBase.loop();

  // Verify that the server accepted a connection
  CHECK_EQ(acceptCallback.getEvents()->size(), 3);
  CHECK_EQ(acceptCallback.getEvents()->at(0).type,
                    TestAcceptCallback::TYPE_START);
  CHECK_EQ(acceptCallback.getEvents()->at(1).type,
                    TestAcceptCallback::TYPE_ACCEPT);
  CHECK_EQ(acceptCallback.getEvents()->at(2).type,
                    TestAcceptCallback::TYPE_STOP);
  int fd = acceptCallback.getEvents()->at(1).fd;

  // The accepted connection should already be in non-blocking mode
  int flags = fcntl(fd, F_GETFL, 0);
  CHECK_EQ(flags & O_NONBLOCK, O_NONBLOCK);
}
