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

class ReturnTestVal {
 public:
  ReturnTestVal() = default;
  virtual ~ReturnTestVal() = default;
  virtual int getValue() const { return kTestIntValue; }
};

template <int ToAdd>
class ReturnTestValDerivedWithAdder : public ReturnTestVal {
 public:
  using ReturnTestVal::ReturnTestVal;
  ~ReturnTestValDerivedWithAdder() override = default;
  int getValue() const override { return kTestIntValue + ToAdd; }
};

/**
 * Construct a MaybeManagedPtr<ReturnTestVal> from a ReturnTestVal*.
 */
TEST(MaybeManagedPtrTest, ConstructFromRaw) {
  auto ptr = new ReturnTestVal();
  auto mPtr = folly::MaybeManagedPtr<ReturnTestVal>(ptr);
  EXPECT_EQ(mPtr->getValue(), kTestIntValue);
  delete ptr;
}

/**
 * Construct a MaybeManagedPtr<ReturnTestVal> from a shared_ptr<ReturnTestVal>.
 */
TEST(MaybeManagedPtrTest, ConstructFromShared) {
  auto ptr = std::make_shared<ReturnTestVal>();
  auto mPtr = folly::MaybeManagedPtr<ReturnTestVal>(ptr);
  EXPECT_EQ(mPtr->getValue(), kTestIntValue);
}

/**
 * Construct a MaybeManagedPtr<ReturnTestVal> from a
 * shared_ptr<ReturnTestValDerivedWithAdder>.
 */
TEST(MaybeManagedPtrTest, ConstructFromSharedDerived) {
  auto ptr = std::make_shared<ReturnTestValDerivedWithAdder<1>>();
  auto mPtr = folly::MaybeManagedPtr<ReturnTestVal>(ptr);
  EXPECT_EQ(mPtr->getValue(), kTestIntValue + 1);
}

class ObjectWithMonitoredDestruction {
 public:
  explicit ObjectWithMonitoredDestruction(bool& destroyCalled)
      : destroyCalled_(destroyCalled) {
    EXPECT_TRUE(!destroyCalled_);
  }

  virtual ~ObjectWithMonitoredDestruction() { destroyCalled_ = true; }

 private:
  bool& destroyCalled_;
};

class ObjectWithMonitoredDestructionDerived
    : public ObjectWithMonitoredDestruction {
 public:
  using ObjectWithMonitoredDestruction::ObjectWithMonitoredDestruction;
  ~ObjectWithMonitoredDestructionDerived() override = default;
};

/**
 * Test destruction behavior when using raw pointer at construction.
 *
 * We expect the contained object *not* to be deallocated when the
 * MaybeManagedPtr goes out of scope.
 */
TEST(MaybeManagedPtrDestroyTest, RawPointer) {
  bool destroyCalled{false};
  ObjectWithMonitoredDestruction* monitoredObj{
      new ObjectWithMonitoredDestruction(destroyCalled)};
  EXPECT_FALSE(destroyCalled);

  // Create a MaybeManagedPtr from raw pointer, then verify that destructor is
  // not called when MaybeManagedPtr goes out of scope
  {
    const auto maybeMgdPtr =
        folly::MaybeManagedPtr<ObjectWithMonitoredDestruction>(monitoredObj);

    EXPECT_EQ(maybeMgdPtr, monitoredObj);
    EXPECT_NE(maybeMgdPtr, nullptr);
    EXPECT_FALSE(destroyCalled);
  }
  EXPECT_FALSE(destroyCalled);

  // Verify destructor is called when we explicitly delete the contained object
  delete monitoredObj;
  EXPECT_TRUE(destroyCalled);
}

/**
 * Test destruction behavior when using shared_ptr for construction.
 *
 * Scenario: We expect the contained object *not* to be destroyed when the
 * MaybeManagedPtr goes out of scope.
 */
TEST(MaybeManagedPtrDestroyTest, SharedPtrShouldNotBeDestroyed) {
  bool destroyCalled{false};
  std::shared_ptr<ObjectWithMonitoredDestruction> monitoredObj =
      std::make_shared<ObjectWithMonitoredDestruction>(destroyCalled);
  EXPECT_FALSE(destroyCalled);

  // Create a MaybeManagedPtr from shared_ptr, then verify that destructor is
  // not called when MaybeManagedPtr goes out of scope
  {
    const auto maybeMgdPtr =
        folly::MaybeManagedPtr<ObjectWithMonitoredDestruction>(monitoredObj);

    EXPECT_EQ(maybeMgdPtr, monitoredObj);
    EXPECT_NE(maybeMgdPtr, nullptr);
    EXPECT_FALSE(destroyCalled);

    EXPECT_EQ(monitoredObj.use_count(), 2);
    EXPECT_EQ(maybeMgdPtr.useCount(), 2);
  }
  EXPECT_FALSE(destroyCalled);

  // Verify destructor is called when we explicitly delete the contained object
  monitoredObj = nullptr;
  EXPECT_TRUE(destroyCalled);
}

/**
 * Test destruction behavior when using shared_ptr for construction.
 *
 * Scenario: We expect the contained object SHOULD be destroyed when the
 * MaybeManagedPtr goes out of scope.
 */
TEST(MaybeManagedPtrDestroyTest, SharedPtrShouldBeDestroyed) {
  bool destroyCalled{false};
  std::shared_ptr<ObjectWithMonitoredDestruction> monitoredObj =
      std::make_shared<ObjectWithMonitoredDestruction>(destroyCalled);
  EXPECT_FALSE(destroyCalled);

  // Create a MaybeManagedPtr from shared_ptr, then verify that destructor is
  // called when the MaybeManagedPtr goes out of scope, as it's the last thing
  // holding the shared_ptr
  {
    const auto maybeMgdPtr =
        folly::MaybeManagedPtr<ObjectWithMonitoredDestruction>(monitoredObj);

    EXPECT_EQ(maybeMgdPtr, monitoredObj);
    EXPECT_NE(maybeMgdPtr, nullptr);
    EXPECT_FALSE(destroyCalled);

    EXPECT_EQ(monitoredObj.use_count(), 2);
    EXPECT_EQ(maybeMgdPtr.useCount(), 2);

    monitoredObj = nullptr;
    EXPECT_NE(maybeMgdPtr, monitoredObj);
    EXPECT_NE(maybeMgdPtr, nullptr);
    EXPECT_FALSE(destroyCalled);
    EXPECT_EQ(maybeMgdPtr.useCount(), 1);
  }

  // Verify destructor is called when we exit the scope
  EXPECT_TRUE(destroyCalled);
}

/**
 * Test destruction behavior when using shared_ptr for construction.
 *
 * Special case: Initialize from shared_ptr of derived type.
 *
 * Scenario: We expect the contained object SHOULD be destroyed when the
 * MaybeManagedPtr goes out of scope.
 */
TEST(MaybeManagedPtrDestroyTest, SharedPtrShouldBeDestroyed_Derived) {
  bool destroyCalled{false};
  std::shared_ptr<ObjectWithMonitoredDestructionDerived> monitoredObj =
      std::make_shared<ObjectWithMonitoredDestructionDerived>(destroyCalled);
  EXPECT_FALSE(destroyCalled);

  // Create a MaybeManagedPtr from shared_ptr, then verify that destructor is
  // called when the MaybeManagedPtr goes out of scope, as it's the last thing
  // holding the shared_ptr
  {
    const auto maybeMgdPtr =
        folly::MaybeManagedPtr<ObjectWithMonitoredDestruction>(monitoredObj);

    EXPECT_EQ(maybeMgdPtr, monitoredObj);
    EXPECT_NE(maybeMgdPtr, nullptr);
    EXPECT_FALSE(destroyCalled);

    EXPECT_EQ(monitoredObj.use_count(), 2);
    EXPECT_EQ(maybeMgdPtr.useCount(), 2);

    monitoredObj = nullptr;
    EXPECT_NE(maybeMgdPtr, monitoredObj);
    EXPECT_NE(maybeMgdPtr, nullptr);
    EXPECT_FALSE(destroyCalled);
    EXPECT_EQ(maybeMgdPtr.useCount(), 1);
  }

  // Verify destructor is called when we exit the scope
  EXPECT_TRUE(destroyCalled);
}

/**
 * Type parameterized test fixture for testing MaybeManagedPtr.
 *
 * Enables the type of object used to construct the MaybeManagedPtr to be
 * templated (e.g., init from raw ptr, or init from shared_ptr).
 */
template <typename T>
class MaybeManagedPtrTypeParamTest : public testing::Test {
 public:
  using TypeParam = T;

  void SetUp() override {
    std::tie(ptrToWrap_, rawPtr_) = T::getNewPtrToWrapWithRaw();
  }

  void TearDown() override { T::destroy(ptrToWrap_); }

 protected:
  typename T::PtrToWrapT ptrToWrap_{nullptr};
  ReturnTestVal* rawPtr_{nullptr};
};

template <typename T, int ExpectedVal>
struct ParamType {
  using Type = T;
  using PtrToWrapT = T;
  static constexpr int expected_val = ExpectedVal;

  static T getNewPtrToWrap() {
    if constexpr (std::is_same_v<Type, ReturnTestVal*>) {
      return new ReturnTestVal();
    } else {
      return std::make_shared<typename T::element_type>();
    }
  }

  static std::tuple<T, ReturnTestVal*> getNewPtrToWrapWithRaw() {
    if constexpr (std::is_same_v<Type, ReturnTestVal*>) {
      auto p = new ReturnTestVal();
      return std::make_tuple(p, p);
    } else {
      T sp = std::make_shared<typename T::element_type>();
      return std::make_tuple(sp, sp.get());
    }
  }

  static void destroy(PtrToWrapT& ptrToWrap) {
    if constexpr (std::is_same<PtrToWrapT, ReturnTestVal*>::value) {
      delete ptrToWrap;
    }
  }

  static void destroy(folly::MaybeManagedPtr<ReturnTestVal>& managedPtr) {
    if constexpr (std::is_same<PtrToWrapT, ReturnTestVal*>::value) {
      delete managedPtr.get();
    }
  }
};

using MaybeManagedPtrTypeParamTestTypes = Types<
    ParamType<ReturnTestVal*, kTestIntValue>,
    ParamType<std::shared_ptr<ReturnTestVal>, kTestIntValue>,
    ParamType<
        std::shared_ptr<ReturnTestValDerivedWithAdder<1>>,
        kTestIntValue + 1>,
    ParamType<
        std::shared_ptr<ReturnTestValDerivedWithAdder<2>>,
        kTestIntValue + 2>>;
TYPED_TEST_SUITE(
    MaybeManagedPtrTypeParamTest, MaybeManagedPtrTypeParamTestTypes);

/**
 * Construct a MaybeManagedPtr.
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, Constructor) {
  folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
}

/**
 * Check value returned by MaybeManagedPtr.
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, Value) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
  EXPECT_EQ(TestFixture::TypeParam::expected_val, maybeMgdPtr->getValue());
}

/**
 * Check address pointed to by MaybeManagedPtr.
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, GetMethod) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
  EXPECT_EQ(maybeMgdPtr.get(), this->rawPtr_);
  EXPECT_FALSE(
      std::is_const_v<std::remove_pointer_t<decltype(maybeMgdPtr.get())>>);
}

/**
 * Check address returned by operator ->.
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, MemberOfPointerOperator) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
  EXPECT_EQ(maybeMgdPtr.operator->(), this->rawPtr_);
}

/**
 * Check address returned by operator *.
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, IndirectionOperator) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
  EXPECT_EQ(&(*maybeMgdPtr), this->rawPtr_);
}

/**
 * Test equal to operator with raw pointers.
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, EqualToOperatorRawPointer) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
  ReturnTestVal* otherRawPtr1{new ReturnTestVal()};
  ReturnTestVal* otherRawPtr2{nullptr};

  EXPECT_EQ(maybeMgdPtr, this->rawPtr_);
  EXPECT_NE(maybeMgdPtr, otherRawPtr1);
  EXPECT_NE(maybeMgdPtr, otherRawPtr2);
  EXPECT_NE(maybeMgdPtr, nullptr);

  delete otherRawPtr1;
}

/**
 * Test equal to operator with shared pointers
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, EqualToOperatorSharedPointer) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);

  // prevent second shared pointer from deallocating value
  auto sameSharedPtr =
      std::shared_ptr<ReturnTestVal>(std::shared_ptr<void>(), this->rawPtr_);
  auto otherSharedPtr1 = std::make_shared<ReturnTestVal>();
  std::shared_ptr<ReturnTestVal> otherSharedPtr2;

  EXPECT_EQ(maybeMgdPtr, sameSharedPtr);
  EXPECT_NE(maybeMgdPtr, otherSharedPtr1);
  EXPECT_NE(maybeMgdPtr, otherSharedPtr2);
}

/**
 * Test equal to operator with MaybeManagedPtr
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, EqualToOperatorMaybeManagedPointer) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);

  auto sameMaybeManagedPtr1 =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
  auto sameMaybeManagedPtr2 =
      folly::MaybeManagedPtr<ReturnTestVal>(maybeMgdPtr);
  auto sameMaybeManagedPtr3 = maybeMgdPtr;
  auto differentMaybeManagedPtr = folly::MaybeManagedPtr<ReturnTestVal>(
      TestFixture::TypeParam::getNewPtrToWrap());

  EXPECT_EQ(
      maybeMgdPtr, folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_));
  EXPECT_EQ(maybeMgdPtr, maybeMgdPtr);
  EXPECT_EQ(maybeMgdPtr, sameMaybeManagedPtr1);
  EXPECT_EQ(maybeMgdPtr, sameMaybeManagedPtr2);
  EXPECT_EQ(maybeMgdPtr, sameMaybeManagedPtr3);
  EXPECT_NE(maybeMgdPtr, differentMaybeManagedPtr);

  TestFixture::TypeParam::destroy(differentMaybeManagedPtr);
}

/**
 * Test bool type conversion operator
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, BoolTypeConversionOperator) {
  EXPECT_FALSE(folly::MaybeManagedPtr<ReturnTestVal>(nullptr));
  EXPECT_FALSE(
      folly::MaybeManagedPtr<ReturnTestVal>(std::shared_ptr<ReturnTestVal>()));
  EXPECT_TRUE(folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_));

  {
    auto maybeMgdPtr = folly::MaybeManagedPtr<ReturnTestVal>(nullptr);
    EXPECT_FALSE(maybeMgdPtr);
  }

  {
    auto maybeMgdPtr =
        folly::MaybeManagedPtr<ReturnTestVal>(std::shared_ptr<ReturnTestVal>());
    EXPECT_FALSE(maybeMgdPtr);
  }

  {
    auto maybeMgdPtr = folly::MaybeManagedPtr<ReturnTestVal>(nullptr);
    EXPECT_FALSE(maybeMgdPtr);
  }

  // finally, actually populate the MaybeManagedPtr with a non-null pointer
  {
    auto maybeMgdPtr = folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);
    EXPECT_TRUE(maybeMgdPtr);
  }
}

/**
 * Test implicit type conversion operator
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, ImplicitTypeConversionOperator) {
  const auto maybeMgdPtr =
      folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);

  const ReturnTestVal* rawPtr = maybeMgdPtr;
  EXPECT_EQ(rawPtr, this->rawPtr_);
}

/**
 * Test explicit type conversion operator
 */
TYPED_TEST(MaybeManagedPtrTypeParamTest, ExplicitTypeConversionOperator) {
  auto maybeMgdPtr = folly::MaybeManagedPtr<ReturnTestVal>(this->ptrToWrap_);

  const auto rawPtr = (ReturnTestVal*)maybeMgdPtr;
  EXPECT_EQ(rawPtr, this->rawPtr_);
}
