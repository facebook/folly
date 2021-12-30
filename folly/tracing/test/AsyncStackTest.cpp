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

#include <folly/tracing/AsyncStack.h>

#include <glog/logging.h>

#include <folly/portability/GTest.h>

TEST(AsyncStack, ScopedAsyncStackRoot) {
  void* const stackFramePtr = FOLLY_ASYNC_STACK_FRAME_POINTER();
  void* const returnAddress = FOLLY_ASYNC_STACK_RETURN_ADDRESS();

  CHECK(folly::tryGetCurrentAsyncStackRoot() == nullptr);

  {
    folly::detail::ScopedAsyncStackRoot scopedRoot{
        stackFramePtr, returnAddress};
    auto* root = folly::tryGetCurrentAsyncStackRoot();
    CHECK_NOTNULL(root);

    folly::AsyncStackFrame frame;
    scopedRoot.activateFrame(frame);

    CHECK_EQ(root, frame.getStackRoot());
    CHECK_EQ(stackFramePtr, root->getStackFramePointer());
    CHECK_EQ(returnAddress, root->getReturnAddress());
    CHECK_EQ(&frame, root->getTopFrame());

    folly::deactivateAsyncStackFrame(frame);

    CHECK(frame.getStackRoot() == nullptr);
    CHECK(root->getTopFrame() == nullptr);
  }

  CHECK(folly::tryGetCurrentAsyncStackRoot() == nullptr);
}

TEST(AsyncStack, PushPop) {
  folly::detail::ScopedAsyncStackRoot scopedRoot{nullptr};

  auto& root = folly::getCurrentAsyncStackRoot();

  folly::AsyncStackFrame frame1;
  folly::AsyncStackFrame frame2;
  folly::AsyncStackFrame frame3;

  scopedRoot.activateFrame(frame1);

  CHECK_EQ(&frame1, root.getTopFrame());
  CHECK_EQ(&root, frame1.getStackRoot());

  folly::pushAsyncStackFrameCallerCallee(frame1, frame2);

  CHECK_EQ(&frame2, root.getTopFrame());
  CHECK_EQ(&frame1, frame2.getParentFrame());
  CHECK_EQ(&root, frame2.getStackRoot());
  CHECK(frame1.getStackRoot() == nullptr);

  folly::pushAsyncStackFrameCallerCallee(frame2, frame3);

  CHECK_EQ(&frame3, root.getTopFrame());
  CHECK_EQ(&frame2, frame3.getParentFrame());
  CHECK_EQ(&frame1, frame2.getParentFrame());
  CHECK(frame1.getParentFrame() == nullptr);
  CHECK(frame2.getStackRoot() == nullptr);

  folly::deactivateAsyncStackFrame(frame3);

  CHECK(root.getTopFrame() == nullptr);
  CHECK(frame3.getStackRoot() == nullptr);

  folly::activateAsyncStackFrame(root, frame3);

  CHECK_EQ(&frame3, root.getTopFrame());
  CHECK_EQ(&root, frame3.getStackRoot());

  folly::popAsyncStackFrameCallee(frame3);

  CHECK_EQ(&frame2, root.getTopFrame());
  CHECK_EQ(&root, frame2.getStackRoot());
  CHECK(frame3.getStackRoot() == nullptr);

  folly::popAsyncStackFrameCallee(frame2);

  CHECK_EQ(&frame1, root.getTopFrame());
  CHECK_EQ(&root, frame1.getStackRoot());
  CHECK(frame2.getStackRoot() == nullptr);

  folly::deactivateAsyncStackFrame(frame1);

  CHECK(root.getTopFrame() == nullptr);
  CHECK(frame1.getStackRoot() == nullptr);
}
