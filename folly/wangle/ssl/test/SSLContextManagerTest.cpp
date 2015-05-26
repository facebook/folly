/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <folly/wangle/ssl/SSLContextManager.h>
#include <folly/wangle/acceptor/DomainNameMisc.h>

using std::shared_ptr;

namespace folly {

TEST(SSLContextManagerTest, Test1)
{
  EventBase eventBase;
  SSLContextManager sslCtxMgr(&eventBase, "vip_ssl_context_manager_test_",
                              true, nullptr);
  auto www_facebook_com_ctx = std::make_shared<SSLContext>();
  auto start_facebook_com_ctx = std::make_shared<SSLContext>();
  auto start_abc_facebook_com_ctx = std::make_shared<SSLContext>();

  sslCtxMgr.insertSSLCtxByDomainName(
    "www.facebook.com",
    strlen("www.facebook.com"),
    www_facebook_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "www.facebook.com",
    strlen("www.facebook.com"),
    www_facebook_com_ctx);
  try {
    sslCtxMgr.insertSSLCtxByDomainName(
      "www.facebook.com",
      strlen("www.facebook.com"),
      std::make_shared<SSLContext>());
  } catch (const std::exception& ex) {
  }
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.facebook.com",
    strlen("*.facebook.com"),
    start_facebook_com_ctx);
  sslCtxMgr.insertSSLCtxByDomainName(
    "*.abc.facebook.com",
    strlen("*.abc.facebook.com"),
    start_abc_facebook_com_ctx);
  try {
    sslCtxMgr.insertSSLCtxByDomainName(
      "*.abc.facebook.com",
      strlen("*.abc.facebook.com"),
      std::make_shared<SSLContext>());
    FAIL();
  } catch (const std::exception& ex) {
  }

  shared_ptr<SSLContext> retCtx;
  retCtx = sslCtxMgr.getSSLCtx(DNString("www.facebook.com"));
  EXPECT_EQ(retCtx, www_facebook_com_ctx);
  retCtx = sslCtxMgr.getSSLCtx(DNString("WWW.facebook.com"));
  EXPECT_EQ(retCtx, www_facebook_com_ctx);
  EXPECT_FALSE(sslCtxMgr.getSSLCtx(DNString("xyz.facebook.com")));

  retCtx = sslCtxMgr.getSSLCtxBySuffix(DNString("xyz.facebook.com"));
  EXPECT_EQ(retCtx, start_facebook_com_ctx);
  retCtx = sslCtxMgr.getSSLCtxBySuffix(DNString("XYZ.facebook.com"));
  EXPECT_EQ(retCtx, start_facebook_com_ctx);

  retCtx = sslCtxMgr.getSSLCtxBySuffix(DNString("www.abc.facebook.com"));
  EXPECT_EQ(retCtx, start_abc_facebook_com_ctx);

  // ensure "facebook.com" does not match "*.facebook.com"
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(DNString("facebook.com")));
  // ensure "Xfacebook.com" does not match "*.facebook.com"
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(DNString("Xfacebook.com")));
  // ensure wildcard name only matches one domain up
  EXPECT_FALSE(sslCtxMgr.getSSLCtxBySuffix(DNString("abc.xyz.facebook.com")));

  eventBase.loop(); // Clean up events before SSLContextManager is destructed
}

}
