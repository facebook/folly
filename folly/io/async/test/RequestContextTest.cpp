/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <gtest/gtest.h>
#include <thread>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/Request.h>

using namespace folly;

class TestData : public RequestData {
 public:
  explicit TestData(int data) : data_(data) {}
  ~TestData() override {}
  int data_;
};

TEST(RequestContext, SimpleTest) {
  EventBase base;


  // There should always be a default context with get()
  EXPECT_TRUE(RequestContext::get() != nullptr);


  // but not with saveContext()
  EXPECT_EQ(RequestContext::saveContext(), nullptr);
  RequestContext::create();
  EXPECT_NE(RequestContext::saveContext(), nullptr);
  RequestContext::create();
  EXPECT_NE(RequestContext::saveContext(), nullptr);

  EXPECT_EQ(nullptr, RequestContext::get()->getContextData("test"));

  RequestContext::get()->setContextData(
    "test",
    std::unique_ptr<TestData>(new TestData(10)));
  base.runInEventBaseThread([&](){
      EXPECT_TRUE(RequestContext::get() != nullptr);
      auto data = dynamic_cast<TestData*>(
        RequestContext::get()->getContextData("test"))->data_;
      EXPECT_EQ(10, data);
      base.terminateLoopSoon();
    });
  auto th = std::thread([&](){
      base.loopForever();
  });
  th.join();
  EXPECT_TRUE(RequestContext::get() != nullptr);
  auto a = dynamic_cast<TestData*>(
    RequestContext::get()->getContextData("test"));
  auto data = a->data_;
  EXPECT_EQ(10, data);

  RequestContext::setContext(std::shared_ptr<RequestContext>());
  // There should always be a default context
  EXPECT_TRUE(nullptr != RequestContext::get());
}

TEST(RequestContext, setIfAbsentTest) {
  EXPECT_TRUE(RequestContext::get() != nullptr);

  RequestContext::get()->setContextData(
      "test", std::unique_ptr<TestData>(new TestData(10)));
  EXPECT_FALSE(RequestContext::get()->setContextDataIfAbsent(
      "test", std::unique_ptr<TestData>(new TestData(20))));
  EXPECT_EQ(10,
            dynamic_cast<TestData*>(
                RequestContext::get()->getContextData("test"))->data_);

  EXPECT_TRUE(RequestContext::get()->setContextDataIfAbsent(
      "test2", std::unique_ptr<TestData>(new TestData(20))));
  EXPECT_EQ(20,
            dynamic_cast<TestData*>(
                RequestContext::get()->getContextData("test2"))->data_);

  RequestContext::setContext(std::shared_ptr<RequestContext>());
  EXPECT_TRUE(nullptr != RequestContext::get());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}
