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

#include <folly/Function.h>
#include <folly/Optional.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

namespace folly {

template <typename ObserverContainerT>
class MockObserver : public ObserverContainerT::Observer {
 public:
  using TestSubjectT = typename ObserverContainerT::observed_type;
  using ObserverContainerBase = typename ObserverContainerT::ContainerBase;
  using EventEnum = typename ObserverContainerT::EventEnum;
  using EventSet = typename TestSubjectT::ObserverContainer::Observer::EventSet;
  using EventSetBuilder =
      typename TestSubjectT::ObserverContainer::Observer::EventSetBuilder;

  using ObserverContainerT::Observer::Observer;

  MOCK_METHOD1(attachedMock, void(TestSubjectT*));
  MOCK_METHOD1(detachedMock, void(TestSubjectT*));
  MOCK_METHOD2(
      destroyedMock,
      void(
          TestSubjectT*,
          typename TestSubjectT::ObserverContainer::ObserverBase::
              DestroyContext*));
  MOCK_METHOD3(
      movedMock,
      void(
          TestSubjectT*,
          TestSubjectT*,
          typename TestSubjectT::ObserverContainer::ObserverBase::
              MoveContext*));
  MOCK_METHOD3(
      invokeInterfaceMethodMock,
      void(
          TestSubjectT*,
          folly::Function<void(
              typename TestSubjectT::ObserverContainer::ObserverBase*,
              TestSubjectT*)>&,
          folly::Optional<EventEnum> maybeEvent));
  MOCK_METHOD1(postInvokeInterfaceMethodMock, void(TestSubjectT*));
  MOCK_METHOD1(addedToObserverContainerMock, void(ObserverContainerBase*));
  MOCK_METHOD1(removedFromObserverContainerMock, void(ObserverContainerBase*));
  MOCK_METHOD2(
      movedToObserverContainerMock,
      void(ObserverContainerBase*, ObserverContainerBase*));

  /**
   * Use default handler for invokeInterfaceMethod.
   */
  void useDefaultInvokeMockHandler() { defaultHandlersForInvoke_ = true; }

  /**
   * Use default handlers for postInvokeInterfaceMethod.
   */
  void useDefaultPostInvokeMockHandler() {
    defaultHandlersForPostInvoke_ = true;
  }

 private:
  void attached(TestSubjectT* obj) noexcept override { attachedMock(obj); }
  void detached(TestSubjectT* obj) noexcept override { detachedMock(obj); }
  void destroyed(
      TestSubjectT* obj,
      typename TestSubjectT::ObserverContainer::ManagedObserver::DestroyContext*
          ctx) noexcept override {
    destroyedMock(obj, ctx);
  }
  void moved(
      TestSubjectT* oldObj,
      TestSubjectT* newObj,
      typename TestSubjectT::ObserverContainer::ManagedObserver::MoveContext*
          ctx) noexcept override {
    movedMock(oldObj, newObj, ctx);
  }
  void invokeInterfaceMethod(
      TestSubjectT* obj,
      folly::Function<void(
          typename TestSubjectT::ObserverContainer::ObserverBase*,
          TestSubjectT*)>& fn,
      folly::Optional<EventEnum> maybeEvent) noexcept override {
    if (defaultHandlersForInvoke_) {
      TestSubjectT::ObserverContainer::Observer::invokeInterfaceMethod(
          obj, fn, maybeEvent);
    } else {
      invokeInterfaceMethodMock(obj, fn, maybeEvent);
    }
  }
  void postInvokeInterfaceMethod(TestSubjectT* obj) noexcept override {
    if (defaultHandlersForPostInvoke_) {
      TestSubjectT::ObserverContainer::Observer::postInvokeInterfaceMethod(obj);
    } else {
      postInvokeInterfaceMethodMock(obj);
    }
  }
  void addedToObserverContainer(ObserverContainerBase* ctr) noexcept override {
    addedToObserverContainerMock(ctr);
  }
  void removedFromObserverContainer(
      ObserverContainerBase* ctr) noexcept override {
    removedFromObserverContainerMock(ctr);
  }
  void movedToObserverContainer(
      ObserverContainerBase* oldCtr,
      ObserverContainerBase* newCtr) noexcept override {
    movedToObserverContainerMock(oldCtr, newCtr);
  }

  bool defaultHandlersForInvoke_{false};
  bool defaultHandlersForPostInvoke_{false};
};

template <typename ObserverContainerT>
class MockManagedObserver : public ObserverContainerT::ManagedObserver {
 public:
  using TestSubjectT = typename ObserverContainerT::observed_type;
  using ObserverContainerBase = typename ObserverContainerT::ContainerBase;
  using EventEnum = typename ObserverContainerT::EventEnum;
  using EventSet = typename TestSubjectT::ObserverContainer::Observer::EventSet;
  using EventSetBuilder =
      typename TestSubjectT::ObserverContainer::Observer::EventSetBuilder;

  using ObserverContainerT::ManagedObserver::ManagedObserver;

  MOCK_METHOD1(attachedMock, void(TestSubjectT*));
  MOCK_METHOD1(detachedMock, void(TestSubjectT*));
  MOCK_METHOD2(
      destroyedMock,
      void(
          TestSubjectT*,
          typename TestSubjectT::ObserverContainer::ManagedObserver::
              DestroyContext*));
  MOCK_METHOD3(
      movedMock,
      void(
          TestSubjectT*,
          TestSubjectT*,
          typename TestSubjectT::ObserverContainer::ManagedObserver::
              MoveContext*));
  MOCK_METHOD3(
      invokeInterfaceMethodMock,
      void(
          TestSubjectT*,
          folly::Function<void(
              typename TestSubjectT::ObserverContainer::ObserverBase*,
              TestSubjectT*)>&,
          folly::Optional<EventEnum> maybeEvent));
  MOCK_METHOD1(postInvokeInterfaceMethodMock, void(TestSubjectT*));

  /**
   * Use default handler for invokeInterfaceMethod.
   */
  void useDefaultInvokeMockHandler() { defaultHandlersForInvoke_ = true; }

  /**
   * Use default handlers for postInvokeInterfaceMethod.
   */
  void useDefaultPostInvokeMockHandler() {
    defaultHandlersForPostInvoke_ = true;
  }

 private:
  void attached(TestSubjectT* obj) noexcept override { attachedMock(obj); }
  void detached(TestSubjectT* obj) noexcept override { detachedMock(obj); }
  void destroyed(
      TestSubjectT* obj,
      typename TestSubjectT::ObserverContainer::ManagedObserver::DestroyContext*
          ctx) noexcept override {
    destroyedMock(obj, ctx);
  }
  void moved(
      TestSubjectT* oldObj,
      TestSubjectT* newObj,
      typename TestSubjectT::ObserverContainer::ManagedObserver::MoveContext*
          ctx) noexcept override {
    movedMock(oldObj, newObj, ctx);
  }
  void invokeInterfaceMethod(
      TestSubjectT* obj,
      folly::Function<void(
          typename TestSubjectT::ObserverContainer::ObserverBase*,
          TestSubjectT*)>& fn,
      folly::Optional<EventEnum> maybeEvent) noexcept override {
    if (defaultHandlersForInvoke_) {
      TestSubjectT::ObserverContainer::Observer::invokeInterfaceMethod(
          obj, fn, maybeEvent);
    } else {
      invokeInterfaceMethodMock(obj, fn, maybeEvent);
    }
  }
  void postInvokeInterfaceMethod(TestSubjectT* obj) noexcept override {
    if (defaultHandlersForPostInvoke_) {
      TestSubjectT::ObserverContainer::Observer::postInvokeInterfaceMethod(obj);
    } else {
      gmock_postInvokeInterfaceMethodMock(obj);
    }
  }

  bool defaultHandlersForInvoke_{false};
  bool defaultHandlersForPostInvoke_{false};
};

} // namespace folly
