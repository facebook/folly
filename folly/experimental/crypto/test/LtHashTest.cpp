/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/crypto/LtHash.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <folly/io/IOBuf.h>
#include <folly/portability/GTest.h>
#include <sodium.h>
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace ::testing;

using namespace folly::crypto;

namespace {
std::string toHex(const folly::IOBuf& buf) {
  return folly::hexlify({buf.data(), buf.length()});
}

std::unique_ptr<folly::IOBuf> makeRandomData(size_t length) {
  auto data = folly::IOBuf::create(static_cast<uint64_t>(length));
  data->append(length);
  randombytes_buf(data->writableData(), data->length());
  return data;
}

template <typename T>
struct IsLtHash {
  static inline constexpr bool value() {
    return false;
  }
};

template <std::size_t B, std::size_t N>
struct IsLtHash<LtHash<B, N>> {
  static inline constexpr bool value() {
    return true;
  }
};

} // namespace

// Needed so an EXPECT_EQ() or EXPECT_NE() failure prints the LtHash checksum.
template <std::size_t B, std::size_t N>
static std::ostream& operator<<(std::ostream& os, const LtHash<B, N>& h) {
  os << toHex(*h.getChecksum());
  return os;
}

// Note: the template parameter H must be an instance of LtHash<B, N> for some
// valid B and N.
template <typename H>
class LtHashTest : public ::testing::Test {
 protected:
  static_assert(
      IsLtHash<H>::value(),
      "template parameter H is not an instance of LtHash");

  std::array<unsigned char, 1> obj1_{{'a'}};
  std::array<unsigned char, 1> obj2_{{'b'}};

  static size_t kChecksumLength() {
    return H::getElementCount() / H::getElementsPerUint64() * sizeof(uint64_t);
  }

  static const H& kEmptyHash() {
    static const H emptyHash;
    return emptyHash;
  }
};

// Note: to instantiate template-parameterized test cases, use TYPED_TEST()
// instead of TEST_F().

// Some googletest macro magic to make TYPED_TEST work
using LtHashTestTypes =
    ::testing::Types<LtHash<16, 1024>, LtHash<20, 1008>, LtHash<32, 1024>>;
TYPED_TEST_CASE(LtHashTest, LtHashTestTypes);

// Note: in all test cases below, `TypeParam` refers to the H template param.
// Static methods must be prefixed with `TestFixture::`, while class variables
// and instance methods must be accessed through `this->`.
//
// See the "Typed Tests" section of the Advanced Guide at
// https://github.com/google/googletest/blob/master/googletest/docs/AdvancedGuide.md
// for details.

TYPED_TEST(LtHashTest, empty) {
  TypeParam h;
  size_t checksumLength = TestFixture::kChecksumLength();
  EXPECT_EQ(checksumLength, h.getChecksumSizeBytes());
  auto checksum = h.getChecksum();
  EXPECT_EQ(checksumLength, checksum->length());
  EXPECT_EQ(std::string(checksumLength * 2, '0'), toHex(*checksum));
}

TYPED_TEST(LtHashTest, reset) {
  TypeParam h;
  h.addObject(folly::range(this->obj1_));
  EXPECT_NE(TestFixture::kEmptyHash(), h);
  h.reset();
  EXPECT_EQ(TestFixture::kEmptyHash(), h);
}

TYPED_TEST(LtHashTest, copyAssignment) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  TypeParam h2{h1};
  EXPECT_EQ(h1, h2);
  TypeParam h3 = TestFixture::kEmptyHash();
  h3 = h1;
  EXPECT_EQ(h1, h3);
}

TYPED_TEST(LtHashTest, checksumEquals) {
  TypeParam h0;
  auto length = h0.getChecksumSizeBytes();
  // should throw exception for invalid length checksum
  EXPECT_THROW(
      h0.checksumEquals(folly::range(std::string(length - 1, '\0'))),
      std::runtime_error);
  // initial checksum is filled with '0' bytes
  EXPECT_TRUE(h0.checksumEquals(folly::range(std::string(length, '\0'))));
  auto checksum = h0.getChecksum();
  EXPECT_TRUE(h0.checksumEquals({checksum->data(), checksum->length()}));
  // add an object and compare checksums
  h0.addObject(folly::range(this->obj1_));
  checksum = h0.getChecksum();
  EXPECT_TRUE(h0.checksumEquals({checksum->data(), checksum->length()}));
  // compare against moved-out LtHash, should return false
  TypeParam h1 = std::move(h0);
  EXPECT_FALSE(h0.checksumEquals({checksum->data(), checksum->length()}));
}

TYPED_TEST(LtHashTest, moveAssignment) {
  TypeParam h0;
  h0.addObject(folly::range(this->obj1_));
  TypeParam h1 = h0;
  TypeParam h2{std::move(h1)};
  EXPECT_EQ(h0, h2);
  TypeParam h3;
  h3 = std::move(h2);
  EXPECT_EQ(h0, h3);
  // Make sure copying to a moved-from object works
  h2 = h3;
  EXPECT_EQ(h0, h2);
  // Move from h3 to make sure calling destructor of a moved-from object
  // does not crash.
  TypeParam h4 = std::move(h3);
  // This line is here to make sure the previous line is not optimized out.
  EXPECT_EQ(h0, h4);
}

TYPED_TEST(LtHashTest, addObjectCommutative) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  h1.addObject(folly::range(this->obj2_));

  TypeParam h2;
  h2.addObject(folly::range(this->obj2_));
  h2.addObject(folly::range(this->obj1_));

  // add('a'); add('b') == add('b'); add('a')
  EXPECT_EQ(h1, h2);
}

TYPED_TEST(LtHashTest, addTwoLtHashes) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));

  TypeParam h2;
  h2.addObject(folly::range(this->obj2_));

  TypeParam h3 = h1 + h2;
  TypeParam h4;
  h4.addObject(folly::range(this->obj1_));
  h4.addObject(folly::range(this->obj2_));

  EXPECT_EQ(h4, h3);
  // Make sure the move-semantics version works
  h3 = std::move(h1) + std::move(h2);
  EXPECT_EQ(h4, h3);
}

TYPED_TEST(LtHashTest, subtractTwoLtHashes) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  h1.addObject(folly::range(this->obj2_));

  TypeParam h2;
  h2.addObject(folly::range(this->obj2_));

  TypeParam h3 = h1 - h2;
  TypeParam h4;
  h4.addObject(folly::range(this->obj1_));

  EXPECT_EQ(h4, h3);
  // Make sure the move-semantics version works
  h3 = std::move(h1) - std::move(h2);
  EXPECT_EQ(h4, h3);
}

TYPED_TEST(LtHashTest, addObjectChaining) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  h1.addObject(folly::range(this->obj2_));
  TypeParam h2;
  h2.addObject(folly::range(this->obj1_)).addObject(folly::range(this->obj2_));

  // add('a'); add('b') == add('a').add('b')
  EXPECT_EQ(h1, h2);
}

TYPED_TEST(LtHashTest, addDifferentObjects) {
  TypeParam h1;
  TypeParam h2;
  h1.addObject(folly::range(this->obj1_));
  h2.addObject(folly::range(this->obj2_));

  // add('a') != add('b')
  EXPECT_NE(h1, h2);
}

TYPED_TEST(LtHashTest, addAndremoveObjectSame) {
  TypeParam h;
  h.addObject(folly::range(this->obj1_));
  h.removeObject(folly::range(this->obj1_));

  // add('a'); delete('a') == 0
  EXPECT_EQ(TestFixture::kEmptyHash(), h);
}

TYPED_TEST(LtHashTest, addObjectWithInitialChecksum) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  h1.addObject(folly::range(this->obj2_));

  TypeParam h2;
  h2.addObject(folly::range(this->obj1_));
  TypeParam h3;
  h3.setChecksum(*h2.getChecksum());
  h3.addObject(folly::range(this->obj2_));

  // add('a'); LtHash(checksum) and add('b') == add('a'); add('b');
  EXPECT_EQ(h1, h3);
}

TYPED_TEST(LtHashTest, addObjectVariadic) {
  std::vector<unsigned char> combinedObj;
  for (size_t i = 0; i < this->obj1_.size(); ++i) {
    combinedObj.push_back(this->obj1_[i]);
  }
  for (size_t i = 0; i < this->obj2_.size(); ++i) {
    combinedObj.push_back(this->obj2_[i]);
  }

  TypeParam h1, h2, h3;
  h1.addObject(folly::range(combinedObj));
  h2.addObject(folly::range(this->obj1_), folly::range(this->obj2_));
  EXPECT_EQ(h1, h2);
  h3.addObject(folly::range(this->obj2_), folly::range(this->obj1_));
  EXPECT_NE(h1, h3);
}

TYPED_TEST(LtHashTest, removeObjectVariadic) {
  std::vector<unsigned char> combinedObj;
  for (size_t i = 0; i < this->obj1_.size(); ++i) {
    combinedObj.push_back(this->obj1_[i]);
  }
  for (size_t i = 0; i < this->obj2_.size(); ++i) {
    combinedObj.push_back(this->obj2_[i]);
  }

  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  h1.addObject(folly::range(combinedObj));

  TypeParam h2 = h1;
  TypeParam h3 = h1;

  h1.removeObject(folly::range(combinedObj));
  h2.removeObject(folly::range(this->obj1_), folly::range(this->obj2_));
  EXPECT_EQ(h1, h2);
  h3.removeObject(folly::range(this->obj2_), folly::range(this->obj1_));
  EXPECT_NE(h1, h3);
}

TYPED_TEST(LtHashTest, getChecksumDeepCopy) {
  // verifies that getChecksum() returns a deep copy of the IOBuf
  // the returned checksum should not change when a new object is added to
  // LtHash later
  TypeParam h;
  h.addObject(folly::range(this->obj1_));
  auto checksum = h.getChecksum();
  auto temp = folly::IOBuf::create(checksum->length());
  std::memcpy(temp->writableData(), checksum->data(), checksum->length());
  EXPECT_EQ(0, std::memcmp(checksum->data(), temp->data(), checksum->length()));

  h.addObject(folly::range(this->obj1_));
  auto c2 = h.getChecksum();
  EXPECT_EQ(0, std::memcmp(checksum->data(), temp->data(), checksum->length()));
  EXPECT_NE(0, std::memcmp(checksum->data(), c2->data(), checksum->length()));
}

TYPED_TEST(LtHashTest, addObjectRandomShuffle) {
  // 1) generates random objects
  // 2) add them to LtHash
  // 3) shuffle the object list
  // 2) add them to a new LtHash
  // 3) then verifies that they reach the same checksum
  int objectCount = 1000;
  std::vector<std::unique_ptr<folly::IOBuf>> objects;
  for (int i = 0; i < objectCount; i++) {
    // object size is between 1 byte and 1 KB
    size_t objectSize = (folly::Random::rand32() % 1024) + 1;
    objects.push_back(makeRandomData(objectSize));
  }
  TypeParam h1;
  for (size_t i = 0; i < objects.size(); i++) {
    h1.addObject({objects[i]->data(), objects[i]->length()});
  }
  auto rng = std::default_random_engine{};
  std::shuffle(std::begin(objects), std::end(objects), rng);
  TypeParam h2;
  for (size_t i = 0; i < objects.size(); i++) {
    h2.addObject({objects[i]->data(), objects[i]->length()});
  }
  // H(o1 + o2 + ...) == H(o_i + o_j + ...)
  EXPECT_EQ(h1, h2);
}

TYPED_TEST(LtHashTest, addAndremoveObjectRandom) {
  // 1) generate random objects
  // 2) adds them to LtHash while storing the checksum after each add
  // 3) removes objects in reverse order, verifies that checksum is reversed
  TypeParam h;
  size_t objectCount = 1000;
  std::vector<std::unique_ptr<folly::IOBuf>> objects;
  for (size_t i = 0; i < objectCount; i++) {
    // object size is between 1 byte and 1 KB
    size_t objectSize = (folly::Random::rand32() % 1024) + 1;
    objects.push_back(makeRandomData(objectSize));
  }
  std::vector<std::unique_ptr<folly::IOBuf>> checksums;
  checksums.push_back(h.getChecksum());
  for (size_t i = 0; i < objects.size(); i++) {
    h.addObject({objects[i]->data(), objects[i]->length()});
    checksums.push_back(h.getChecksum());
  }
  EXPECT_TRUE(folly::IOBufEqualTo()(*h.getChecksum(), *checksums.back()));
  for (int i = static_cast<int>(objects.size() - 1); i >= 0; i--) {
    h.removeObject({objects[i]->data(), objects[i]->length()});
    EXPECT_TRUE(folly::IOBufEqualTo()(*h.getChecksum(), *checksums[i]));
  }
  EXPECT_EQ(TestFixture::kEmptyHash(), h);
}

TYPED_TEST(LtHashTest, setChecksum) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  auto checksum = h1.getChecksum();
  TypeParam h2(*checksum); // copy version
  EXPECT_EQ(h1, h2);
  TypeParam h3(h1.getChecksum()); // move version
  EXPECT_EQ(h1, h3);
  TypeParam h4;
  h4.setChecksum(*checksum); // copy version
  EXPECT_EQ(h1, h4);
  TypeParam h5;
  h5.setChecksum(std::move(checksum)); // move version
  EXPECT_EQ(h1, h5);
}

TYPED_TEST(LtHashTest, setChecksumWithChainedIOBuf) {
  TypeParam h;
  h.addObject(folly::range(this->obj1_));
  auto c1 = h.getChecksum();
  auto c2 = folly::IOBuf::create(c1->length() / 2);
  std::memcpy(c2->writableTail(), c1->data(), c1->length() / 2);
  c2->append(c1->length() / 2);
  auto c3 = folly::IOBuf::create(c1->length() - c2->length());
  std::memcpy(
      c3->writableTail(),
      c1->data() + c2->length(),
      c1->length() - c2->length());
  c3->append(c1->length() - c2->length());
  c2->prependChain(std::move(c3));
  TypeParam h2;
  h2.setChecksum(*c2);
  EXPECT_EQ(h, h2);
}

TYPED_TEST(LtHashTest, setChecksumFailure) {
  TypeParam h1;
  h1.addObject(folly::range(this->obj1_));
  auto checksum = h1.getChecksum();
  auto partialChecksum =
      folly::IOBuf::copyBuffer(checksum->data(), checksum->length() - 1);
  TypeParam h2;
  EXPECT_THROW(h2.setChecksum(*partialChecksum), std::runtime_error);
  EXPECT_THROW(h2.setChecksum(std::move(partialChecksum)), std::runtime_error);
  // Trying to set a null checksum should fail
  EXPECT_THROW(
      h2.setChecksum(std::unique_ptr<folly::IOBuf>{}), std::runtime_error);
  // If padding bits are not properly zeroed out, the checksum is invalid.
  if (TypeParam::hasPaddingBits()) {
    uint64_t* ptr = reinterpret_cast<uint64_t*>(checksum->writableData());
    *ptr = 0xFFFFFFFFFFFFFFFFULL;
    EXPECT_THROW(h2.setChecksum(*checksum), std::runtime_error);
    EXPECT_THROW(h2.setChecksum(std::move(checksum)), std::runtime_error);
  }
}

TYPED_TEST(LtHashTest, addObjectWithKnownValue) {
  std::array<unsigned char, 5> obj3{{'h', 'e', 'l', 'l', 'o'}};
  TypeParam h;
  h.addObject(folly::range(this->obj1_));
  h.addObject(folly::range(this->obj2_));
  h.addObject(folly::range(obj3));
  h.removeObject(folly::range(this->obj2_));
  if (h.getElementSizeInBits() == 16 && h.getElementCount() == 1024) {
    std::string expectedChecksum =
        "353cae6169e519eb9cf80edd2c5b33810276227e77a09030e3ac3b00299c9716c6b592"
        "b262b2b05ad82db539f23fc03baa1ffacc9704fe078219307c02c0f501c810895c19a7"
        "71934855d091e30db8eba564596f071400fcca93b69115055c55e0b333b5583ec0068a"
        "219289b557be5b24cfa679ae8e20b9084c77eadab966e4f94239d5f671371aa17c41f0"
        "510aaaeb6e28fed0eb37b57c5ff8f6c64a0395ddb32d2948abee9ae84930ee0d43d015"
        "b2f577cadb558eef33e715f349114c1937817ff26b606f1f33a1f3b4a72eaa3b24573a"
        "78d06b315857a8295675ec2bfc9897b644f60d401c4315bea8a6ad410f77e3969aaa03"
        "2d31526df0c271665647c98f1e4d3946b659e47f45480c3eac9b0e0b742501595b24d5"
        "362d3f6f4ba8a4fcda7d87951ade9ec184a45c2fd5bff5282835c29071551e96d940c3"
        "ed19bb3124c3b37080dc3c80bc22f61b431195b9489bed3244e0e522bf8f8c752145b0"
        "1ee47701085ffa1238f3a1d5e778052b393330fff8b586d9399cced75d4d15697f9015"
        "174d3302d97b1cc55ae20cdb573d4061d2940b213a35808122e7d55bd53e2c9ba1779c"
        "8a19532ff1e65a440e871f96e086dce6693efba86e033f7e3b04069f9eeccc0f5c5947"
        "af0b04f5528be1b57bba0912eeb52fdd11f0cac0e40ae641bbc40207188adbfe13463c"
        "880e84016476facae56f7f6de26e7f508a277a409988aabec7f9bb552000e3f7a44f51"
        "ec5c7c98979a227403464797a06fae0d7aa951bb429cb9df4ed65a430a98e0c88f7d4e"
        "47e1256f17c4b126f05b885154507b3b80a2a1b6e1f43eea48b4b93cab0622bd002a25"
        "dd5d1b69fe05c11619837eba6edfac493d663409f5ce82762584205fe49e8f718fbc5a"
        "92823cd9a17c1c9a07ce9f2535c918c6ee0f0729b67eb0be8b5e0edc990260679fdf5a"
        "9991a6d62ec1d72f5e5a478dbf0e5cbd1703daf5f170411d0d7aca4921cb644ec1d86e"
        "02711d09359b0f2b45a5b9fe57e122add8b5ae27aaeb44aa77a9fe187a67ea7447b27d"
        "02b4bf41fc5024350dc8838fb8f977535ba4481569a74d90306e0c9979a9d149be9502"
        "23d1ca5d425b9ec281ee3884d8e8a1ad0d00504f0f57ff35e0ee33d184f35fd28dfa34"
        "8686fb926da95597fb947acc509a5cb7cfe1eeb33dfdf9b4b384346c862cfb198f6948"
        "a6f6d53a74848043c8b647076b0a90151bd40c58d32434ebf549aa92f4a5b7581b7ec6"
        "821ca3485cf8e2a6ce0f5e204ed5a92c84618c2828e5c6f222ec3c48e37dcada7ce28b"
        "ba5c09740170d32aa004b43cde46d45f9912528a3a7a7f30fb6019548dd174b4d7b0ba"
        "fb232920b972362db4d863a5e0a9e30a041ecb874a7acbd378ccb11ffbffcd086ad797"
        "be5b4de07859d0b1fb3e4835a84ea224940482a3849cf392528dfcf8920d4b4bfc4060"
        "6e852d85b7bfd1f2723214969dab6adfb8c26dc5f51b1b043b8a25df1eadd90d1a2324"
        "0b735943841ae4e13564ddb6f0f7dcac1db82a34ab9ca042f8c4690727c7a0fac98c10"
        "dac065a57dff8010e9d49ba3b801622e8b786bb44079ceecf61ff7cc07be8672c647b5"
        "25ffea7c3fab95d40d9d36e220bb3a5292880faf05a8dd94e60a4ff0ccfc124d2dca03"
        "a85d0864bfa28cddb7bdcc83ff717239dae979596691b6e3062068e6ea442ebd354bc6"
        "53b0e5b750bcbfaec275c77ab82bd3452e4776734df686d6bae946855a4659dd3566f4"
        "8d0879a00c06a7ce81c0f234e0203ce68ffc9434f3f10281d76110887a4b460514f761"
        "b517f1d151d88724160fbeff7f69a5a23eae2bf48916ad55c084b908d955519a67096b"
        "94638fa10d8d153a60d0c44f2d9148ad549fb1e64ac423aac1fdc754bc44a69573578c"
        "6b881bca177698e68d6ffdc2d7d89469f2e1039e8e3b955581a56c15519590b65bd9bc"
        "c3b3b1a95d1d484c2585ebdfd8a15c737b436456934d9b8439d92d1212bf8799028780"
        "d9f35d208c093ba6506aff74979faa10fa807398e8fb769be070318caee6b4f5091d8d"
        "9254656d0a1e838ba73ed0f0c8e8d4a0d19f9e91340578baa7ce5aec9f73f8e26db927"
        "3c544f11d6b8e5e142f4a8ad70a9e21c3dc2c7b4403073c4722e0af775f98c37ae0645"
        "e1829dec574de3108f62965a5354aaa7695c1e4feab1fc8ccf9a5e2a7ed0758e411ea9"
        "ea25f4f659a36cc5aa0bed2a9ce4518cd1aa1b4ed94c2d62596059d20cd948a058b78e"
        "f9ad3c9e7c7c9fd433c42701aad7aff74fb14ea39812c3e68b6ca8585432ecd53a7dfe"
        "ece8e6a73b0ecddabc8c9da37b140adee6308c540bedbcf77d49762e7efaeededf5196"
        "6503315fb287b69a08854ec58fd41c2f214c3273cc48bc71718b801c27936c7fd339b7"
        "f78c2eda6835e7d532ef6496cbbc7b018cc48ed49e33c16e16d2bdc98f47f376208770"
        "b5d5b6e789b30ca55ad4e8cd09b6bd90b66e8d4abfd0fbc3e98fc28f3913e476161d0f"
        "0f7477d3ca066adce7567d1af90dc3415970199ada286e22ea90892da107c34d3745c5"
        "d5d3f290fbd2d0b64942117955e94b343517d76959f0764216ce27bc33e772fefe4a48"
        "20315d67b43302f73e8002cc6144c3f3ad66c307eb2f9e0192a00da9ddf262b600dd0a"
        "49721da25ca71997d17c3441cd9588c5469f6927966d06676f89243387f01ebd2094be"
        "ac0716a2e486d85524c51ba2898abb8b8df3a169f93fe6333f4f6a868738969905ba55"
        "176f1b8055d19749c0c5122ab1eaf34b0eb458fc10a656811fe4a7bb588eac3450b6d6"
        "1c7f634588996ef2d903478cde206f58c1a93069c7df80a28394d05e9a8ed99b5312e9"
        "cbec0a2dba2e3e3e24e854798e9dc8c09922fadb987e945a765e2f614993f2b5605548"
        "1e3702371d4eb86c872ca65269125be61d86";
    EXPECT_EQ(expectedChecksum, toHex(*h.getChecksum()));
  } else if (h.getElementSizeInBits() == 20 && h.getElementCount() == 1008) {
    std::string expectedChecksum =
        "aec6e81142ad553e9eae2f5ea2c9d20dec91881229418b20024b05a235b5f50adb98ed"
        "276aa9443a7be0047ed20d3f31c8f80ba3d5048719d5174076ac1db9298ec1e62d6b74"
        "0e3dd825427e4d1dcf1270eb656e8cddfc16f549cf769e8dc30fec35039986e41e0230"
        "a063e4ca145c162766404257f89c2d43f4eb45beddfc036d552dcf94b5fe3780514217"
        "52797a3d67d909529399c31ffdb2ce05f725f03fb0892d45ba84602818af60832ea1f1"
        "0b7489ee8ce0c8eb01478ca884b4602f197858a0a95d71e83f4796ee61a2a130297691"
        "203342e49820f3ab2e78cdedc0190268a81102d08026851eeeeed5600333b6b6cff6df"
        "2c7511c1cfc0cb7b85f7283933abffe308d42779698c3f10b8c42058ec4a60ec78270b"
        "68104d6dc7fd042124a32bd2c0b4422d3d9ea3c0d878b71ca12d6778a068db04f49aed"
        "a11b31160283abef5143e84111c2502012e490f33488ee6098c809aa2870bd63e1dcc9"
        "081925fb4622af39ad02322d24de7311fd1dfaa3eb543ec0a5086f6a4f849281322124"
        "d7650430409c3cfff6a2d7d2259937ee21eef320657c13cc1fe1689a35a81ee51802a7"
        "54ad1b1a8242ed98f18d033550922eb6b191123f803c68a249d9ef1818290085a00870"
        "3db27fad9985f42d36469be3edfa450a290194230f2ad9350304ee8cb3e97cb40ca773"
        "6e2953c106256f08c4d7972c611cc134049e431c032d91ff0d5cbf549c006f9042cb79"
        "3ca7128ca90faf42212f1e1e02877d91f5ec14884440b3df70352ff73ea75c4768541c"
        "de0f03f85ef9a824d1a64834ddd9b0303aa543e959813a07950e235a8b1d9e11c7e26e"
        "6e42a8c80a89c58f590561be0dd70a6e2f820c9c2c0af3478eecb47513bcef8ff1ccdd"
        "9c1830a92b25c518513033856391c009ec37919fa6d5de6ddc32c4cca1a267257b0184"
        "17adb3d1e0d9341f6de3143c98d61d625f8aa39fd0723f9773cb84a6c4c2140c0fa0d7"
        "ca5d7d15058c0acdd0cd703b19920b2c42c9c30e71392cb6d39c7b1d5794c44af309c6"
        "1db784415a80940a129135abd79bac822f067b4631db656d07a53a8f3a212d510f3724"
        "c031cab0a51632792ef2477c061fbef3855725a9f90fbe1608498918392ba532c7927a"
        "90a3140b0d4309bd31131381270ef120b51720936184f4c4494e05d10082efe281f936"
        "10184ef84c198c3ccb77a2e76fd10f31b9afc33a0e51ff2f707842f590719a1368e589"
        "69bbc9db0e208a20c957fd663079f2e347184d081abd5382965ca59817d7f40df44e98"
        "f42c668169a79fbc0324a36a86e78c2d4507d2672889760577009261e494cc9d3638b9"
        "81697bbc71373ed31149804e3497184bcd2e5f2b11310a360b812431792a1a509560e1"
        "eb597c3eb6ec09ea32e1823a3400ec0e8af96f38a35c0f623271cb3fee83461da71823"
        "288e1a47de8a79f4284ac24daa8bb9493d65d3051198740600774a63e7cbe4b6125aba"
        "678c7d94c920e93cab8fc239be312fb2aa214b91fb3dca17a0f4acd1e732838e09c255"
        "615c2d9a2d066a705ce334c0cacc3625fcd413b0c5276da3d935393d0a896861a0e415"
        "86da248985ed4626fdde66dacdbc97384cb724b11c51d42947dd686376b19004bdc26b"
        "b677543d309b4feb60bd88e922fb5227aed9489130d7954b1b65e152337fb545ad83a8"
        "6b2ca548e439c268741dbec80181190d232c4fc8c67399e5900b269463069f6c4506f1"
        "7e4f507f40ee294e12eb2973b48022d2b7293a6b9501100bf9aa58ce01c82813bfc79e"
        "99e80514ce5b6d92f899800edbf34b87f52c121d8de5cc16429072167eee4c7cfcd050"
        "3fa42cace2ad2c641f6187a5d42cc8f60cf70d81b2019182274dc64540cf85c62ba229"
        "09d82284fe1899fdc9f13761d408c7072563c5400224a3b9ad73127582311691e03ca7"
        "9cf71ff31f226fe6cc8a0807494b976854bf0db4dfe02975a0ee2ee66d414aaaada809"
        "855fa4bdea98880a7adfa4e81a099a1ad718cee89fa88b0640fbadfbb3e47d00b075a2"
        "f5b0e50f34d09c8ef98fa5462559b50f17b9a44e36e9e949c2c4ccac0e2467a7431adc"
        "812dc3c2087119f1521bc8080760db2dfa1f4928c97a2b88ce01011921250cf8ca3ba8"
        "39461b7868030ec410e2463659ea29ea540bdcde606304504180a147ed052a56150a75"
        "2f045320d34ec0fd469c621d29408af4b861e20dda2262b1fdbd4d02739cadd16f64e2"
        "339f62c14ef2a5371e1b4de553e5309528e51d262744896132dda9c48983a801021ff3"
        "634ec1bd070f5dd5c8ade63de51b348d0c0fb00d003d17720d4673cd5d05e18f69c90f"
        "e82c0b1c6661fe60f41a16a285accbf0f47539d8986cf1d78961321a9564ac9b11221e"
        "e3be45d9e1dd1a00a35301d968ccf521230ac5aa85a97f35bafea0d27a918d258c1e0d"
        "cca7041b2a598a86e291493c140c3e0cbee51c120bdcf1e8ff4ac1ad07f35663f1f1ac"
        "be3c8ae58baccb598a127293232631a878351764046d09494914b6e7a7e11b98d427bd"
        "d4aa0b0da9f1271ed78d6ea20dbb1a77974b9b16e9b013cc0f85bf6ac5ed23ec048bb5"
        "17f4133a98a1a3279375cd0faecc86ba317dfe184177294e430c3d0fb81a8904a3c9a6"
        "2d790566069dbcee14b79882268eb88b37becb23791f218b3743b66c281ca0912c380f"
        "8d40acedb72885174640d0f824157af0ef2968c93e25679ae4ade784322f31034c5d4f"
        "9c2538ea4a800900a55027d2b1239a5bf09209e91a83a402654f06fc0f43800dd46029"
        "61a4c58180a0e2252de900de3eb1873ad74707bb962cbf0dd1c7e0672ae9da0a14bbef"
        "125408343478dee0549119431b01d9aa8ae850ff133f8a89bde69d8914a316850ee409"
        "970a5383a8d491c806307d9082dfa1b9f03f9cbb40889ff509121c9fa1dfda80f52803"
        "0de018a1a8c517797b08065d90bd30c8dc67d1dee50d17885a8e0b6ef57a23e290634d"
        "1589d0158f9c8a6a7b14471172cbcab20ae5e024918faf959ab8a80c729141f294b8ff"
        "07b6af09666aad7c3ebc6ca88e95c1da3057efcd124d7d123bc2d0474d36c4783c1619"
        "45181d856007df8dc040d331d2207262a01cd63d7514279def9d4d98243d132e6554c8"
        "611b3adfa683d9a3a8a61cca40cb8af3058e3683fc0bdfc1d9bd0f62cd2265db18542b"
        "17d9a08671bc6d381d6ee1aa27c5400b4112a0c15d087306581d67006ac1343861ae4d"
        "01d938732deae4e9efabf44f1102aa6fbbce2d111f3edb4ac0bc2c0533b186e6632e2c"
        "631edde5e4dc9ebce8158f4ee0fa10cd5e046a78c2d03b91bf1a608cad8c248c5e1cb0"
        "62847e09850e13fa2187be526d883cdcc168555b1c081ffe8b4d3e1764971bb7b9255c"
        "07a4361a857a88d4e059b7300d3e0c769e3ce304ce7048163f7d7b3f16738987faa4ed"
        "21d1cb853997341d193947484317584d39e3882691bd29773937184ca49265e93e3fca"
        "2cedd84cd0353eac027e456d1c26050661a5444d7b36be4fcd38720c3637e5326e759e"
        "9d3838729e0fcc49b4540989ed2e7534a956209b6da9b7f470ff033f23a7f1a6e9101d"
        "00d00e9cdcb8482a5b06c18cc6993f29f8d2a7ddcb91e804b82a417c404d761edc6ae6"
        "0b81d884268e41c161fcf18f3dec7061e86b046e3831afe93ca869fe06404f4e174d75"
        "420686108a011129790b1d4046eae7714e11d980c271eae5f13858d1427caaec9c023e"
        "73afdcd76d593922d30a6f04d41f2d8d8a820e0e48171f92ee4ee455700b381fac06e5"
        "70c58b0a81bc499a835826117f1146877c90b011ea0ca4db40a17d29";
    EXPECT_EQ(expectedChecksum, toHex(*h.getChecksum()));
  } else if (h.getElementSizeInBits() == 32 && h.getElementCount() == 1024) {
    std::string expectedChecksum =
        "68c13c13c663d66f6f834f309cc293115b5c2026dab510ce1c6dae6b13763954ad4771"
        "4f93a997a80ca52922770c4e64e3bbf10f67cc87a51d183ec0c66dc9ff751fdc5723cc"
        "f8bb6636e0d34e1c7b3a38adcaa2b22e1e9f68bc1524ae89c62a601b1fd1ae544ede9b"
        "a826e2153ef1c406d5e6807018e635e677a676031d94a798281d6801792a2aeb22d5ab"
        "3082fe6b90b0bb5ffcc71727eb662fb0996e374b4a50074c47f8d68725986e142c98e1"
        "38d4c934e17c17ce8ac9b554ed3d0e5016aa8d34976104ccdf63f5309f9a38de29a45f"
        "14cd08041f7dd27882891307346495eb98348f06999fae96eba531e7837fcf868ae438"
        "2200051f4f5c6ada89584a4a301faa5943ca28c3c0d850fe4754818674fc5a41efdd5b"
        "6860c23ad851a5f35ed54dfc70603023b02ae43a8d211dcf7857764559b50aa6cbaa01"
        "9fea1485aa0b597efc968e969f1e88add067022a695836fb5da6baacbf4f2278544acd"
        "a0b22182788b872a2a75f70fbd2b36ae7dd8c5a291e2e2d2657f583b3852e5183d4bf1"
        "5a2b6084cd3f5ef66f2457673aeb13a366ae31ec2cb8c36bf265ffe34af45e8e4e6782"
        "0730952b527a955af1fa2dd5a3c4ba3bf94847627e3d049cce0c44c976be8f197f41da"
        "a61fa9cf4f95fdc130585d30d897c0364e7a1b162841f7af267bf76888b376dfe7790e"
        "14b3d1786e3369c0270bc3d0130c3acfd6a4638b72bd39a9e68e50b16d00a6ba74cf12"
        "ccf3ab028e4d728d25c3f99323ffe5c158054b9581cd2a897d24468bb0001337b68b14"
        "94fc4ba2f407d2df0d49a593daa31993b4ac16d880efbb94feca0728bd284e0acea435"
        "4e386327a086e2e1b991ec31690e0b3eb4a8dffe5c982cf368597980553e2b12e87ccf"
        "2e6994c2ea74546a3029a62a9851c5a23b89c2053519ae6304704a19284d8930fa7cca"
        "2ab6231e5d620d5743594f11e1248b6b362ba8b0015ca9c7d6bc36d0ae9104221e75c9"
        "519e34b0d0888a672cadaca3198d79c214cc275ffee909e600d949e3353cb24e033e19"
        "596b4a6d7c299de11337f4d46704f6ad5292fa3eb72535bfd6a27c106a457eca485dd5"
        "fdea051666affc73d0e051db5aaa894735c403e2456d0594c42cb0bd1ec325d30ac2dc"
        "6aa698a2c1e499a809f9c25f88101d1307b339c7251fc4f455d5d52d1cc08d17ec626a"
        "be8ff10ae10b61c0d96b786357dc1385b84f76e1a0fc2fd38ed67f3021ebff3c1655fe"
        "20056434a7e68f2569c262f5c1bca2ecc0f9c8160e6b9bfed5fb6d8cf0e74f9df39453"
        "9e5a9f04d4a0f2da01023d92ea2e8d2c36ce0b87d07fe6c996bba8795dc81c3c9b7c07"
        "26879351817464e4626694aa5ce33ae6ee988f81412c74a8216901fbe46a4adaa75b78"
        "39ab01d933bd77b43d8bd4736adbb0507318801f024a044a6a3308cccf4438b6865ecd"
        "59521505806a935871c26cfcd4bf3955b2b573eabfbecaf53eb52e92f6fd3e397d4c2a"
        "63090ef8b9e2434290e4184aff69bc64dc1246a5e4acf3f02b663c723404911ae4863b"
        "fa3222af924647fb47e6ca45dc51bc5e20f771865e73652607006df18ff0d62f688a4d"
        "3ed986fa8e79d7f400253d28028f79770d84daa2b14d3aee4a2d4153882f56d693ec7d"
        "d9dbca9e1496464e0772abe54c0610dc969952189649a52ceb910cb52a61f989bb993a"
        "e948899edd5a08dc4b10eceb47cf68cc3747304dcdde918c4fe60d336af06b0e84afbe"
        "e81f39cd3bdfc054f421a12a0d0c8c89b451b3dc385e1ea94aece03cd692ad01237425"
        "98fc638ac688c15d1738ef89c07bd19384ea0acbf8e97342c33c71b10993df7c28a36e"
        "14278d352a02599478a30a942069dfbb10c7dfc6b921d75eb47f5e6296e17c3a79ef39"
        "3d764f167b8c1c6598482497ffe7dd3103ea0c682d15565fe2d75357500482b6dc2848"
        "f5a0ef2b7ab697fff5878ec016aa674cf21666cd83b88720ccb6fcfaf9ca9d85f42409"
        "471718ec28c85717369f1d9c130b00e18ae7bfab24994bcb582c8e5e9bae67aabf5306"
        "8f8d3ae2860c24250f1388b6cda75f2218b674084ee131823b8a1bf2bf70d1028cb99f"
        "db3cbb3758e0f03efd6d928900d8ea73f3c9600a593d146ee6d10d437021699ba3fede"
        "c2e7942200bfd968a4744b477078b2479f223264e4c845e994558424ebfebfea0cc4af"
        "9975b0e59792a28b972d81e2391ad73970842fc7ceab6e217fb40338d107fb5522b0ba"
        "c9ae4346600a7444c97f488db697a4a3d63c2b762b1a48eb036a2464bcf5def2c20919"
        "bb2bd8ad4f5e00e5a8fc3f77ba8489069b415ed84ff767c6b3e80130688459770bb1bd"
        "870d1861f043270ea20a31545a1734134ff406a00d47c65d7e09fb06f714f7a70f20ce"
        "cc39bf5e4d4fca8e083fcfc61d8c2212561bc0633dc5c32ff0d922f583ef2b1aa21d83"
        "4767708c257d65123d7280f4508bad4c36e162554dfb3a913454b94a4fd78573dc2499"
        "f11092ba5b02549f0abf5c325e57d82e749d1e78809cd6484edf8e9b71e1a9b1e58c86"
        "b6d82b11ffd1c36a19bb31a6fb38857c4811bd184ae1658b6a3a86175c8d0567ff08cc"
        "e74c281b47557a48a1c92a3eee6a903f730ee62e0ef37882dbeb1526a6181f3681ed48"
        "529a8af5efdf4c898e84546a18e084223b8077cea0ba9d2fd3d43a2a8560945167e8ef"
        "d293a839d086c9b324c3da7a082576fc5e7bbe80b3b2a4057571714eeaf28e5e927fa1"
        "f046759dcb5c7bd0b33f1b109ea40b5afaded8c72cfa555f558a2cb581aa4d41216587"
        "e9bc03ddfda78471b26aae2b12a265f6f0d1b5a5a61a3e0af9efb96aa606b4d8c6bdaf"
        "bab903fb553da5088e7dcebd2666b9c02fc287ec89bfd49a2107f5eac4f60a105c590c"
        "a6fba3c9bdce8d385ea8f4b2ee0117f8bf29fe855f27e3ae6f205c43854410a8a5f6b5"
        "3c9474d2eeb14bc050df8b8c9b697a510b77f93fe5c41ac0ccf2f13512768e6db2e43f"
        "e78e5f8bf61129a91cad0bff6a25d96b568696ea5920f2a4bcc3902df8f929a1d7af3f"
        "1e7e37aacf22488dd63a5f33c290d3d57a30a17740210e7f949cc244042fa43f75f25f"
        "1da2210cb28e255d57111a647b7b1e2a209545a288fca4f19a1d628c0f5a03e680560f"
        "80e7561478cfe222a10844cd5a9257b0ec8bc388d18d6bfbf2cf020448e20c4efaeaea"
        "4441c8b82685533ad6c2e657e18d22038fc1fac7e2500b29e86080a54d85661c326646"
        "7af9fd33fdc84aa7ce4b5297c9477eec2b45ebaf3c324629f2bfd9fd38eb62c11da2d1"
        "abf6291005da15d12910e27cbf02dbe2c02093923668d0dac192c402dd874f7f3692c5"
        "fca4b2d9850a6c28dbf0b1d13690cb077174fbaa14c9638d773f5e6ad43a891f0783ac"
        "b90da25c894d5b33075ef153a477f76934380dbf2a6b39f3039f43a403387b33137348"
        "8cbbcc5534df67c3b1c4323d20d98b4a68e266ce8eff85cc49053449d254e1f8eace18"
        "9597731c9b8a5780437c53755706b72a959ce57a2baa10e559e577527c9dacb89fc6bc"
        "cbbe250cf6ad229fa68186e7c0687992990d1fac6dfb0af7e6f10f600717d8ece368b9"
        "ffb7460f50f8ba1ea58849781ea7f4c369df359c9d08f1e401fe31aafd0ba9a7af0550"
        "599c33a1452a7d35dd7ebd241f93d725cb53864c6b0ecae5383d36ed1a1baee0d9a4fb"
        "8191091eb68113210a7d2fb7441127987300677eddeb0b3d4104d4e70f40ae0f67468e"
        "cf1f7c56e38737ab32dc4e3adbf8bff02cd9c730eab344bb48d4a87625cfbf8b01090a"
        "c935e5b63835202945904281659fb1091b3c9ecb48bd2a1192552006f6f6d32d779b86"
        "fe3aafb4927972b99be92cc229ef6b0008ac2f8ac782e499442accf4e27eeb19b8e53b"
        "46669ee9a53fa78d7e5e03bb40195fec07673f7dcf21c4b71ff1eae78a8728ba7c6658"
        "be16cbcfb8bebd400367ffac62b3e5a64a25becea53c62f0dfe3a62273d83dd97bdf82"
        "8f4ad6b8c240d4043cf5f8a472e0d5a6caebd3915f68cfe82f30eafd499c6aec35199a"
        "bc76fddf895320d5cebd24600532588aa35adc3c117ab71b528b80064b55bf6c7f107a"
        "e5465a41a24cc0d59bc3608c101d93c52fd614d37b9a80e912285859d470e4d9815d49"
        "9250cf4bd0a85a0d8fd825f37128e62ef37b64a2c0648badd3ccbd448b5ab57e92baee"
        "949d2ec36d3c1512316a972f570bb1e44fc8e4d07ab417af6771de2fa66677323b8cbe"
        "587f978e5a463f56b0b76e9ce5871d24c80f2a0ef3c57c28338de45871a3632ae1cf7b"
        "4437bb1f148ef8bba02911147aa536d140d09e456f41db3151d23c1037a812e4c37a64"
        "d95297170729ad8e7b3ed13794e78f16f40c11c50ea3af8723b142fb53e22a9ed9ad47"
        "a4a9a919614cf72311f6566cb3b3dfe046530524787e773181e734e86728dbfae34a04"
        "5041c83958c509ac68664870a1c7334614506da4692a29e9cf4cb9ac99ac94b3e63dbe"
        "f58f80d55863a4a13b3543efff58295f8c690e5fe71534860f67b19c73013c0e6ff52f"
        "e0d4c6830023dc56d0473a9797417df439d63d83779097b9f6899a42d6b8a63312aef6"
        "f6e75b66efccfcbb63fe4e14002aeb043ec467b68b6bb7fb3ed2e0e850d1c5da0e0015"
        "7d8ad7a12c3ec9b6fff04ae03ade0f00ad6d6d76d8bf50f7f9309bcff6184aa3161eee"
        "7ffe48b24145389b08a892d2f71efcc9b8be2db9309efd419073519c39993fff210de3"
        "ced7eb2e62c6356fc936db012cb2e29b0724e3da7ec9a74042daaf476e150eb0227334"
        "0994673cf019525eb3a341e7bb92424ac66c0af6c00a4c94a54bbd65c7de052a6c4d5c"
        "cb6e905e609d9c9d93cd28a3993b0df86a7097411e91c46db8471ce2e4645ed7901116"
        "fcfd72a117158752b02dfae2e4b2b7be13b8eb01b49066a359fbcb013607d9623fb275"
        "711576e948acbacba72fba1c385799b908d14d7ecf57b9d83a59b628c092b686fcf6a9"
        "4a5330d95aa8f204a34c4502350564e3657219c914029139fb0fc95a536183e40c1d96"
        "d6e1f75073b833c51170cca1a32ca1937fc7e46af88e81346f10aaf88a8e2473444cf6"
        "dcef0a178d6f88ec82e8cc79eb6a162d0f177ff4e6ab3b738210f57995a57800dbbe73"
        "89349484e6d71813a93c074c55279ad87a64cc0c9e33203a2a275ed59cdc0d8a03aa74"
        "6793a93e83868b42fd781d091181455aa28d1d93850766a0b58cdd4fb6d4fd30966da7"
        "ab99c42f6c337f8654869803dc372839e3b0e21ec959e59ec07a50e8d11642c4a82b73"
        "d07064d265a33f030d397133306d0d69caa3e93238fdc7f78432eea40851a05f683924"
        "d7f959c353a66fa11da2c27090505529474ec9f8220794725ab25575e4562f6f94dafc"
        "107d5c9fd808471dacf6ec06faa727d7960fb81648551dc248deea3b13bbb941d3d485"
        "dec2b60d81c9d8378f25f88dba0d3611137fa0181e8d2b73dddb24aa2f904b97e207cf"
        "df4fba7faf36dcb6e7bb857b20751dbaf85608181bcc04d6f23e661ca8595cef86c837"
        "75a67825dc7274924e81ef31a01b9a9067ed16cb8c0f2ccd92123b2dc11f847e1467a8"
        "9e09259cb63c25b35fb3cf8f20e53667ca44a95f5d7bbe00c50f92cdd970e6c60ae2d6"
        "232bc7720c128ef4a4a5e3913be0ae1c5e1c64d10b1b8f24e493c9f543a7cc2ca5eaa9"
        "ea208de5ce933340c4f7df1151694c8f24edf4abd8ca09db389b14f4afa2a3be7be8ea"
        "0f384384a324c75569278ab5a9a29d48714db5525e63c7bdde225a59d36b9786fc6e09"
        "686a79a29884ae47e0cb69a020f9e796cf13fcc53ef08ba265d121a9f7bb35021eb098"
        "09";
    EXPECT_EQ(expectedChecksum, toHex(*h.getChecksum()));
  } else {
    FAIL() << "Unexpected element size and/or count: B="
           << h.getElementSizeInBits() << ", N=" << h.getElementCount()
           << ", checksum=" << toHex(*h.getChecksum());
  }
}
