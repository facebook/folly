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

#include <folly/io/async/test/AsyncSocketTest.h>
#include <folly/io/async/test/MockAsyncSocketObserver.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace folly::test;
using namespace ::testing;

TEST(AsyncSocketObserver, ConstructorCallback) {
  EventBase evb;
  // create socket and verify that w/o a ctor callback, nothing happens
  auto socket1 = AsyncSocket::UniquePtr(new AsyncSocket(&evb));
  EXPECT_EQ(socket1->numObservers(), 0);

  // Then register a constructor callback that registers a mock observer
  // NB: use nicemock instead of strict b/c the actual lifecycle testing
  // is done below and this simplifies the test
  auto observer = std::make_shared<NiceMock<MockAsyncSocketObserver>>();
  auto observerRawPtr = observer.get();
  ConstructorCallbackList<AsyncSocket>::addCallback(
      [observerRawPtr](AsyncSocket* s) { s->addObserver(observerRawPtr); });
  auto socket2 = AsyncSocket::UniquePtr(new AsyncSocket(&evb));
  EXPECT_EQ(socket2->numObservers(), 1);
  EXPECT_THAT(socket2->findObservers(), UnorderedElementsAre(observer.get()));
  Mock::VerifyAndClearExpectations(observer.get());
}

TEST(AsyncSocketObserver, AttachObserverThenDetachAndAttachEvb) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  EventBase evb2;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  // Detach the evb and attach a new evb2
  EXPECT_CALL(*observer, evbDetach(socket.get(), &evb));
  socket->detachEventBase();
  EXPECT_EQ(nullptr, socket->getEventBase());
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, evbAttach(socket.get(), &evb2));
  socket->attachEventBase(&evb2);
  EXPECT_EQ(&evb2, socket->getEventBase());
  Mock::VerifyAndClearExpectations(observer.get());

  // detach the new evb2 and re-attach the old evb.
  EXPECT_CALL(*observer, evbDetach(socket.get(), &evb2));
  socket->detachEventBase();
  EXPECT_EQ(nullptr, socket->getEventBase());
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, evbAttach(socket.get(), &evb));
  socket->attachEventBase(&evb);
  EXPECT_EQ(&evb, socket->getEventBase());
  Mock::VerifyAndClearExpectations(observer.get());
}

TEST(AsyncSocketObserver, AttachObserverThenConnectAndCloseSocket) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  TestServer server;
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket.get()));
  EXPECT_CALL(*observer, fdAttach(socket.get()));
  EXPECT_CALL(*observer, connectSuccess(socket.get()));
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, close(socket.get()));
  socket->closeNow();
  Mock::VerifyAndClearExpectations(observer.get());

  socket = nullptr;
}

TEST(AsyncSocketObserver, AttachObserverThenConnectError) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  // port =1 is unreachble on localhost
  folly::SocketAddress unreachable{"::1", 1};
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket.get()));
  EXPECT_CALL(*observer, fdAttach(socket.get()));
  EXPECT_CALL(*observer, close(socket.get()));
  // the current state machine calls AsyncSocket::invokeConnectionError() twice
  // for this use-case...
  EXPECT_CALL(*observer, connectError(socket.get(), _)).Times(2);
  socket->connect(nullptr, unreachable, 1);
  evb.loop();
  socket = nullptr;
  Mock::VerifyAndClearExpectations(observer.get());
}

TEST(AsyncSocketObserver, AttachMultipleObserversThenConnectAndCloseSocket) {
  auto observer1 = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  auto observer2 = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  TestServer server;
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  socket->addObserver(observer1.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer1.get()));

  socket->addObserver(observer2.get());
  EXPECT_EQ(socket->numObservers(), 2);
  EXPECT_THAT(
      socket->findObservers(),
      UnorderedElementsAre(observer1.get(), observer2.get()));

  InSequence s;
  EXPECT_CALL(*observer1, connectAttempt(socket.get()));
  EXPECT_CALL(*observer2, connectAttempt(socket.get()));
  EXPECT_CALL(*observer1, fdAttach(socket.get()));
  EXPECT_CALL(*observer2, fdAttach(socket.get()));
  EXPECT_CALL(*observer1, connectSuccess(socket.get()));
  EXPECT_CALL(*observer2, connectSuccess(socket.get()));
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();

  EXPECT_CALL(*observer1, close(socket.get()));
  EXPECT_CALL(*observer2, close(socket.get()));
  socket->closeNow();
  Mock::VerifyAndClearExpectations(observer1.get());
  Mock::VerifyAndClearExpectations(observer2.get());

  socket = nullptr;
}

TEST(AsyncSocketObserver, AttachThenRemoveObserver) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  EXPECT_TRUE(socket->removeObserver(observer.get()));
  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  Mock::VerifyAndClearExpectations(observer.get());
}

TEST(AsyncSocketObserver, AttachThenRemoveMultipleObservers) {
  auto observer1 = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  auto observer2 = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  socket->addObserver(observer1.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer1.get()));

  socket->addObserver(observer2.get());
  EXPECT_EQ(socket->numObservers(), 2);
  EXPECT_THAT(
      socket->findObservers(),
      UnorderedElementsAre(observer1.get(), observer2.get()));

  EXPECT_TRUE(socket->removeObserver(observer1.get()));
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer2.get()));

  EXPECT_TRUE(socket->removeObserver(observer2.get()));
  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());
}

TEST(AsyncSocketObserver, AttachThenRemoveMultipleObserversReverse) {
  auto observer1 = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  auto observer2 = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  socket->addObserver(observer1.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer1.get()));

  socket->addObserver(observer2.get());
  EXPECT_EQ(socket->numObservers(), 2);
  EXPECT_THAT(
      socket->findObservers(),
      UnorderedElementsAre(observer1.get(), observer2.get()));

  EXPECT_TRUE(socket->removeObserver(observer2.get()));
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer1.get()));

  EXPECT_TRUE(socket->removeObserver(observer1.get()));
  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());
}

TEST(AsyncSocketObserver, RemoveMissingObserver) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));
  EXPECT_FALSE(socket->removeObserver(observer.get()));
}

TEST(AsyncSocketObserver, AttachObserverThenRemoveThenConnect) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  TestServer server;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  EXPECT_TRUE(socket->removeObserver(observer.get()));
  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  Mock::VerifyAndClearExpectations(observer.get());

  // keep going to ensure no further callbacks
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();
}

TEST(AsyncSocketObserver, AttachObserverThenConnectThenRemoveObserver) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  TestServer server;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket.get()));
  EXPECT_CALL(*observer, fdAttach(socket.get()));
  EXPECT_CALL(*observer, connectSuccess(socket.get()));
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_TRUE(socket->removeObserver(observer.get()));
  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  Mock::VerifyAndClearExpectations(observer.get());

  socket = nullptr;
}

TEST(AsyncSocketObserver, AttachObserverThenConnectThenDestroySocket) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  TestServer server;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket.get()));
  EXPECT_CALL(*observer, fdAttach(socket.get()));
  EXPECT_CALL(*observer, connectSuccess(socket.get()));
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, close(socket.get()));
  socket = nullptr;
}

TEST(
    AsyncSocketObserver, AttachObserverThenConnectThenCloseThenRemoveObserver) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  TestServer server;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket.get()));
  EXPECT_CALL(*observer, fdAttach(socket.get()));
  EXPECT_CALL(*observer, connectSuccess(socket.get()));
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, close(socket.get()));
  socket->closeNow();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_TRUE(socket->removeObserver(observer.get()));
  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  Mock::VerifyAndClearExpectations(observer.get());

  socket = nullptr;
}

TEST(
    AsyncSocketObserver,
    AttachObserverThenConnectThenRemoveObserverDuringClose) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  TestServer server;
  auto socket = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket->numObservers(), 0);
  EXPECT_THAT(socket->findObservers(), IsEmpty());

  socket->addObserver(observer.get());
  EXPECT_EQ(socket->numObservers(), 1);
  EXPECT_THAT(socket->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket.get()));
  EXPECT_CALL(*observer, fdAttach(socket.get()));
  EXPECT_CALL(*observer, connectSuccess(socket.get()));
  socket->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, close(socket.get()))
      .WillOnce(Invoke([&observer](AsyncTransport* transport) {
        if (auto sock =
                transport->getUnderlyingTransport<folly::AsyncSocket>()) {
          EXPECT_TRUE(sock->removeObserver(observer.get()));
        }
      }));
  socket = nullptr;
}

TEST(AsyncSocketObserver, AttachObserverThenConnectAndDetachFd) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  EventBase evb;
  TestServer server;
  auto socket1 = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket1->numObservers(), 0);
  EXPECT_THAT(socket1->findObservers(), IsEmpty());

  socket1->addObserver(observer.get());
  EXPECT_EQ(socket1->numObservers(), 1);
  EXPECT_THAT(socket1->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket1.get()));
  EXPECT_CALL(*observer, fdAttach(socket1.get()));
  EXPECT_CALL(*observer, connectSuccess(socket1.get()));
  socket1->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, fdDetach(socket1.get()));
  auto fd = socket1->detachNetworkSocket();
  Mock::VerifyAndClearExpectations(observer.get());

  // create socket2, then immediately destroy it, should get no callbacks
  auto socket2 = AsyncSocket::UniquePtr(new AsyncSocket(&evb, fd));
  socket2 = nullptr;
}

TEST(AsyncSocketObserver, AttachObserverThenConnectAndMoveSocket) {
  auto observer = std::make_unique<StrictMock<MockAsyncSocketObserver>>();
  TestServer server;
  EventBase evb;
  auto socket1 = AsyncSocket::UniquePtr(new AsyncSocket(&evb));

  EXPECT_EQ(socket1->numObservers(), 0);
  EXPECT_THAT(socket1->findObservers(), IsEmpty());

  socket1->addObserver(observer.get());
  EXPECT_EQ(socket1->numObservers(), 1);
  EXPECT_THAT(socket1->findObservers(), UnorderedElementsAre(observer.get()));

  InSequence s;
  EXPECT_CALL(*observer, connectAttempt(socket1.get()));
  EXPECT_CALL(*observer, fdAttach(socket1.get()));
  EXPECT_CALL(*observer, connectSuccess(socket1.get()));
  socket1->connect(nullptr, server.getAddress(), 30);
  evb.loop();
  Mock::VerifyAndClearExpectations(observer.get());

  EXPECT_CALL(*observer, fdDetach(socket1.get()));
  EXPECT_CALL(*observer, moved(socket1.get(), _, _));

  auto socket2 = AsyncSocket::UniquePtr(new AsyncSocket(std::move(socket1)));
  Mock::VerifyAndClearExpectations(observer.get());
  EXPECT_EQ(socket2->numObservers(), 1);

  EXPECT_CALL(*observer, close(socket2.get()));
}
