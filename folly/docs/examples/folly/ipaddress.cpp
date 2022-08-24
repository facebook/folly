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

#include <folly/IPAddress.h>
#include <folly/portability/GTest.h>

using folly::IPAddress;

TEST(IPAddress, demo) {
  IPAddress v4addr("192.0.2.129");
  IPAddress v6map("::ffff:192.0.2.129");
  ASSERT_EQ(
      v4addr.inSubnet("192.0.2.0/24"),
      v4addr.inSubnet(IPAddress("192.0.2.0"), 24));
  ASSERT_TRUE(v4addr.inSubnet("192.0.2.128/30"));
  ASSERT_FALSE(v4addr.inSubnet("192.0.2.128/32"));
  ASSERT_EQ(v4addr.asV4().toLong(), 2164392128);
  ASSERT_EQ(v4addr.asV4().toLongHBO(), 3221226113);
  ASSERT_TRUE(v4addr.isV4());
  ASSERT_TRUE(v6map.isV6());
  ASSERT_EQ(v4addr, v6map);
  ASSERT_TRUE(v6map.isIPv4Mapped());
  ASSERT_EQ(v4addr.asV4(), IPAddress::createIPv4(v6map));
  ASSERT_EQ(IPAddress::createIPv6(v4addr), v6map.asV6());
}
