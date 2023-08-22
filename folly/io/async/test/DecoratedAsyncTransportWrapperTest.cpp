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

#include <folly/io/async/DecoratedAsyncTransportWrapper.h>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace test {

template <auto n>
class DecoratedAsyncTransportWrapperImpl
    : public DecoratedAsyncTransportWrapper<folly::AsyncTransport> {
 public:
  using UniquePtr =
      std::unique_ptr<DecoratedAsyncTransportWrapperImpl, Destructor>;
  explicit DecoratedAsyncTransportWrapperImpl(
      folly::AsyncTransport::UniquePtr t)
      : DecoratedAsyncTransportWrapper<folly::AsyncTransport>(std::move(t)) {}

  static UniquePtr newSocket(folly::AsyncTransport::UniquePtr transport) {
    return UniquePtr{
        new DecoratedAsyncTransportWrapperImpl(std::move(transport))};
  }
};

using DecoratedAsyncTransportWrapperImpl1 =
    DecoratedAsyncTransportWrapperImpl<1>;
using DecoratedAsyncTransportWrapperImpl2 =
    DecoratedAsyncTransportWrapperImpl<2>;
using DecoratedAsyncTransportWrapperImpl3 =
    DecoratedAsyncTransportWrapperImpl<3>;

TEST(DecoratedAsyncTransportWrapperTest, GetWrappingTransport) {
  folly::EventBase eb;
  auto sock = folly::AsyncSocket::newSocket(&eb);
  auto sockRaw = sock.get();
  EXPECT_NE(sockRaw, nullptr);

  auto layer1 = DecoratedAsyncTransportWrapperImpl1::newSocket(std::move(sock));
  auto layer1Raw = layer1.get();
  EXPECT_NE(layer1Raw, nullptr);

  auto layer2 =
      DecoratedAsyncTransportWrapperImpl2::newSocket(std::move(layer1));
  auto layer2Raw = layer2.get();
  EXPECT_NE(layer2Raw, nullptr);

  auto layer3 =
      DecoratedAsyncTransportWrapperImpl3::newSocket(std::move(layer2));
  auto layer3Raw = layer3.get();
  EXPECT_NE(layer3Raw, nullptr);

  // socket -> higher layers
  {
    // socket to layer3
    {
      const auto wrapping3 =
          sockRaw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl3>();
      EXPECT_EQ(wrapping3, layer3Raw);

      // layer3 -> self
      {
        const auto layer3Self =
            wrapping3
                ->getWrappingTransport<DecoratedAsyncTransportWrapperImpl3>();
        EXPECT_EQ(layer3Self, wrapping3);
      }
    }

    // socket to layer2
    {
      const auto wrapping2 =
          sockRaw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl2>();
      EXPECT_EQ(wrapping2, layer2Raw);

      // layer2 up to layer 3
      {
        const auto wrapping3 =
            wrapping2
                ->getWrappingTransport<DecoratedAsyncTransportWrapperImpl3>();
        EXPECT_EQ(wrapping3, layer3Raw);
      }

      // layer2 to self
      {
        const auto layer2Self =
            wrapping2
                ->getWrappingTransport<DecoratedAsyncTransportWrapperImpl2>();
        EXPECT_EQ(layer2Self, wrapping2);
      }
    }

    // socket to layer1
    {
      const auto wrapping1 =
          sockRaw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
      EXPECT_EQ(wrapping1, layer1Raw);

      // layer1 up to layer3
      {
        const auto wrapping3 =
            wrapping1
                ->getWrappingTransport<DecoratedAsyncTransportWrapperImpl3>();
        EXPECT_EQ(wrapping3, layer3Raw);
      }

      // layer1 up to layer2
      {
        const auto wrapping2 =
            wrapping1
                ->getWrappingTransport<DecoratedAsyncTransportWrapperImpl2>();
        EXPECT_EQ(wrapping2, layer2Raw);
      }

      // layer1 to self
      {
        const auto layer1Self =
            wrapping1
                ->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
        EXPECT_EQ(layer1Self, wrapping1);
      }
    }
  }
}

TEST(DecoratedAsyncTransportWrapperTest, ExchangeWrappedTransport) {
  folly::EventBase eb;
  auto sock1 = folly::AsyncSocket::newSocket(&eb);
  auto sock1Raw = sock1.get();

  auto sock2 = folly::AsyncTransport::UniquePtr{new folly::AsyncSocket(&eb)};
  auto sock2Raw = sock2.get();

  auto wrapper =
      DecoratedAsyncTransportWrapperImpl1::newSocket(std::move(sock1));

  // sock2 shouldn't be wrapped
  auto wrapping =
      sock2->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
  EXPECT_EQ(wrapping, nullptr);

  // sock1 should be wrapped
  wrapping =
      sock1Raw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
  EXPECT_EQ(wrapping, wrapper.get());

  // exchange sock1 for sock2
  auto exchanged = wrapper->tryExchangeWrappedTransport(sock2);
  EXPECT_EQ(exchanged.get(), sock1Raw);

  // exchanged should not be wrapped anymore
  wrapping =
      exchanged->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
  EXPECT_EQ(wrapping, nullptr);

  wrapping =
      sock2Raw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
  EXPECT_EQ(wrapping, wrapper.get());
}

TEST(DecoratedAsyncTransportWrapperTest, ExchangeWrappedTransportNullTests) {
  folly::EventBase eb;
  auto sock1 = folly::AsyncTransport::UniquePtr{new folly::AsyncSocket(&eb)};
  auto sock1Raw = sock1.get();
  folly::AsyncTransport::UniquePtr nullSock{nullptr};

  auto wrapper = DecoratedAsyncTransportWrapperImpl1::newSocket(nullptr);

  auto exchanged = wrapper->tryExchangeWrappedTransport(sock1);
  EXPECT_EQ(exchanged, nullptr);

  exchanged = wrapper->tryExchangeWrappedTransport(nullSock);
  EXPECT_EQ(exchanged.get(), sock1Raw);

  exchanged = wrapper->tryExchangeWrappedTransport(nullSock);
  EXPECT_EQ(exchanged, nullptr);
}

TEST(DecoratedAsyncTransportWrapperTest, Destroy) {
  folly::EventBase eb;
  auto sock = folly::AsyncSocket::newSocket(&eb);
  auto sockRaw = sock.get();

  auto wrapper =
      DecoratedAsyncTransportWrapperImpl1::newSocket(std::move(sock));

  auto wrapping =
      sockRaw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
  EXPECT_EQ(wrapping, wrapper.get());

  {
    folly::AsyncSocket::DestructorGuard dg(sockRaw);
    wrapper.reset();
    wrapping =
        sockRaw->getWrappingTransport<DecoratedAsyncTransportWrapperImpl1>();
    EXPECT_EQ(wrapping, nullptr);
  }
}

} // namespace test
} // namespace folly
