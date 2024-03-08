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

#include <folly/MaybeManagedPtr.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace ::testing;

const int kTestIntValue = 55;

/**
 * This will be used to set up a typed test suite for both possible constructor
 * inputs of raw and shared pointer.
 */
template <typename T>
class MaybeManagedPtrInterfaceTest : public testing::Test {
 public:
  ~MaybeManagedPtrInterfaceTest() {
    // Clean up allocated int object in case of raw pointer. Shared pointer will
    // do the deallocation for us when it goes out of scope.
    if (std::is_same<T, int*>::value) {
      delete intPtr_;
    }
  }

 protected:
  // int object that will be pointed to via raw or shared ptr in tests
  int* intPtr_{new int(kTestIntValue)};
  // create raw or shared_ptr to be used with folly::MaybeManagedPtr in tests
  T somePtr_ = T(intPtr_);
};

using MaybeManagedPtrInterfaceTestTypes = Types<int*, std::shared_ptr<int>>;
TYPED_TEST_SUITE(
    MaybeManagedPtrInterfaceTest, MaybeManagedPtrInterfaceTestTypes);

/**
 * Test that we can construct a MaybeManagedPtr from a raw pointer as well as
 * from a shared_ptr
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, Constructor) {
  folly::MaybeManagedPtr<int>(this->somePtr_);
}

/**
 * Test that the pointer contained in shared pointer points to same address as
 * the initial value.
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, GetMethod) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);

  EXPECT_EQ(maybeManagedPtr.get(), this->intPtr_);
  EXPECT_FALSE(
      std::is_const_v<std::remove_pointer_t<decltype(maybeManagedPtr.get())>>);
}

/**
 * Test that member of pointer operator -> returns a pointer to the same
 * address.
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, MemberOfPointerOperator) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);
  EXPECT_EQ(maybeManagedPtr.operator->(), this->intPtr_);
}

/**
 * Test that indirection operator * returns the value the contained pointer it
 * pointing to, as well as making sure it is pointing to the same address.
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, IndirectionOperator) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);

  EXPECT_EQ(&(*maybeManagedPtr), this->intPtr_);
}

/**
 * Test equal to operator with raw pointers.
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, EqualToOperatorRawPointer) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);
  int* otherIntPtr{new int(kTestIntValue + 1)};

  EXPECT_EQ(maybeManagedPtr, this->intPtr_);
  EXPECT_NE(maybeManagedPtr, otherIntPtr);

  delete otherIntPtr;
}

/**
 * Test equal to operator with shared pointers
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, EqualToOperatorSharedPointer) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);

  // prevent second shared pointer from deallocating value
  auto sameSharedPtr =
      std::shared_ptr<int>(std::shared_ptr<void>(), this->intPtr_);

  auto otherSharedPtr = std::shared_ptr<int>(new int(kTestIntValue));

  EXPECT_EQ(maybeManagedPtr, sameSharedPtr);
  EXPECT_NE(maybeManagedPtr, otherSharedPtr);
}

/**
 * Test equal to operator with MaybeManagedPtr
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, EqualToOperatorMaybeManagedPointer) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);

  auto sameMaybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);
  auto otherMaybeManagedPtr =
      folly::MaybeManagedPtr<int>(std::shared_ptr<int>(new int(kTestIntValue)));

  EXPECT_EQ(maybeManagedPtr, sameMaybeManagedPtr);
  EXPECT_NE(maybeManagedPtr, otherMaybeManagedPtr);
}

/**
 * Test bool type conversion operator
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, BoolTypeConversionOperator) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(nullptr);
  EXPECT_FALSE(maybeManagedPtr);

  maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);
  EXPECT_TRUE(maybeManagedPtr);
}

/**
 * Test implicit type conversion operator
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, ImplicitTypeConversionOperator) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);

  int* rawPtr = maybeManagedPtr;
  EXPECT_EQ(rawPtr, this->intPtr_);
}

/**
 * Test explicit type conversion operator
 */
TYPED_TEST(MaybeManagedPtrInterfaceTest, ExplicitTypeConversionOperator) {
  auto maybeManagedPtr = folly::MaybeManagedPtr<int>(this->somePtr_);

  auto rawPtr = (int*)maybeManagedPtr;
  EXPECT_EQ(rawPtr, this->intPtr_);
}

class ContainedObjectMock {
 public:
  // mock method to ensure object is still alive
  MOCK_METHOD(void, checkpoint, ());
  // mock method to be called in destructor to verify object destruction
  MOCK_METHOD(void, dtor, ());

  ~ContainedObjectMock() { dtor(); }
};

/**
 * Test behavior of MaybeManagedPtr when using raw pointer at construction. We
 * expect the contained object *not* to be deallocated when the MaybeManagedPtr
 * goes out of scope.
 */
TEST(MaybeManagedPtrBehaviourTest, RawPointer) {
  ContainedObjectMock* containedObject{new ContainedObjectMock()};

  /**
   * We call checkpoint once in enclosed scope and outside of scope to ensure
   * contained object is still alive after scope exit. We expect destructor to
   * be called once to verify object destruction.
   */
  testing::InSequence s;
  EXPECT_CALL(*containedObject, checkpoint).Times(2);
  EXPECT_CALL(*containedObject, dtor).Times(1);

  {
    auto rawPtr = containedObject;
    auto maybeManagedPtr = folly::MaybeManagedPtr<ContainedObjectMock>(rawPtr);

    EXPECT_NE(maybeManagedPtr, nullptr);

    containedObject->checkpoint();
  }

  // ensure object is still alive after scope exit
  containedObject->checkpoint();
  delete containedObject;
}

/**
 * Test behavior of MaybeManagedPtr when using smart pointer at construction. We
 * expect the contained object to be deallocated when the MaybeManagedPtr goes
 * out of scope.
 */
TEST(MaybeManagedPtrBehaviourTest, SharedPointer) {
  ContainedObjectMock* containedObject{new ContainedObjectMock()};

  /**
   * We call checkpoint once in enclosed scope to ensure
   * contained object is still alive after scope exit. We expect destructor to
   * be called once to verify object destruction. Note that we do not explicitly
   * delete the contained object here, since maybeManagedPtr will take care of
   * this for us on scope exit.
   */
  testing::InSequence s;
  EXPECT_CALL(*containedObject, checkpoint).Times(2);
  EXPECT_CALL(*containedObject, dtor).Times(1);

  {
    auto maybeManagedPtr = folly::MaybeManagedPtr<ContainedObjectMock>(nullptr);

    {
      auto sharedPtr = std::shared_ptr<ContainedObjectMock>(containedObject);
      EXPECT_EQ(sharedPtr.use_count(), 1);

      maybeManagedPtr = folly::MaybeManagedPtr<ContainedObjectMock>(sharedPtr);
      EXPECT_EQ(sharedPtr.use_count(), 2);
      EXPECT_NE(maybeManagedPtr, nullptr);
      EXPECT_EQ(maybeManagedPtr, sharedPtr);
      EXPECT_EQ(maybeManagedPtr.useCount(), 2);
    }

    /**
     * Contained object is still alive, even though we destroyed the initial
     * shared pointer. Destructor has not been called yet since this would
     * violate required sequencing.
     */
    containedObject->checkpoint();
    EXPECT_EQ(maybeManagedPtr.useCount(), 1);

    /**
     * Contained object is still alive, but will be destroyed by maybeManagedPtr
     * on scope exit.
     */
    containedObject->checkpoint();
  }
}
