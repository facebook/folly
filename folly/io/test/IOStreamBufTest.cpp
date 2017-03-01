#include <istream>
#include <memory>
#include <string>

#include <folly/io/IOBuf.h>
#include <folly/io/IOStreamBuf.h>

#include <folly/portability/GTest.h>

using folly::IOBuf;
using folly::IOStreamBuf;

static std::unique_ptr<const IOBuf> sampledata() {
  auto hello = IOBuf::copyBuffer(std::string("hello "));
  auto world = IOBuf::copyBuffer(std::string("world"));

  hello->prependChain(std::move(world));
  return std::move(hello);
}

// Convenience function:
// Create a basic_string<T> from a basic_string<char>
template <typename T>
static std::basic_string<T> typedString(const std::string& in) {
  // Simply cast the string instead of widening since we only support
  // 1-octet types for now.
  static_assert(sizeof(T) == 1,
      "Casting without widening only works when sizeof(T) == 1");

  std::basic_string<T> out;
  out.append(reinterpret_cast<const T*>(in.data()), in.size());
  return out;
}

template <typename T>
class IOStreamBufTest : public ::testing::Test {
 public:
  IOStreamBufTest():
    streambuf_(IOStreamBuf<T>(data.get())),
    in_(&streambuf_)
  {}

  static const T newline = IOStreamBuf<T>::traits_type::to_char_type('\n');
  static const std::unique_ptr<const IOBuf> data;

 private:
  IOStreamBuf<T> streambuf_;
 protected:
  std::basic_istream<T> in_;
};

template <typename T>
const std::unique_ptr<const IOBuf> IOStreamBufTest<T>::data = sampledata();

typedef ::testing::Types<char,
                         unsigned char,
                         uint8_t,
                         signed char> IOStreamBufTestTypes;

TYPED_TEST_CASE(IOStreamBufTest, IOStreamBufTestTypes);

TYPED_TEST(IOStreamBufTest, get_and_getline) {
  std::basic_string<TypeParam> s;
  std::getline(this->in_, s, TestFixture::newline);
  EXPECT_EQ(s, typedString<TypeParam>("hello world"));
  EXPECT_TRUE(this->in_.eof());

  this->in_.seekg(1);

  TypeParam c;
  this->in_.get(c);
  EXPECT_EQ(c, 'e');
  this->in_.get(c);
  EXPECT_EQ(c, 'l');
  ASSERT_EQ(this->in_.tellg(), 3);

  std::getline(this->in_, s, TestFixture::newline);
  EXPECT_EQ(s, typedString<TypeParam>("lo world"));

  this->in_.seekg(-2, std::ios_base::end);
  EXPECT_FALSE(this->in_.eof());

  std::getline(this->in_, s, TestFixture::newline);
  EXPECT_EQ(s, typedString<TypeParam>("ld"));
}

TYPED_TEST(IOStreamBufTest, seek) {
  // from start
  this->in_.seekg( 7, std::ios_base::beg);
  TypeParam raw[] = "\xfb\xfb";
  this->in_.get(raw, sizeof(raw), TestFixture::newline);
  EXPECT_EQ(raw[0], IOStreamBuf<TypeParam>::traits_type::to_char_type('o'));
  EXPECT_EQ(raw[1], IOStreamBuf<TypeParam>::traits_type::to_char_type('r'));
  EXPECT_FALSE(this->in_.eof());
  EXPECT_FALSE(this->in_.fail());

  // from end
  this->in_.seekg(-2, std::ios_base::end);
  EXPECT_EQ(this->in_.tellg(), 9);
  this->in_.seekg(-9, std::ios_base::end);
  EXPECT_EQ(this->in_.tellg(), 2);

  // from cur
  this->in_.seekg( 0, std::ios_base::end);
  this->in_.seekg(-9, std::ios_base::cur);
  this->in_.seekg( 2, std::ios_base::cur);
  ASSERT_EQ(this->in_.tellg(), 4);

  std::basic_string<TypeParam> s;
  std::getline(this->in_, s, TestFixture::newline);
  EXPECT_EQ(s, typedString<TypeParam>("o world"));

  EXPECT_TRUE(this->in_.eof());
  EXPECT_FALSE(this->in_.bad());
}

TYPED_TEST(IOStreamBufTest, xsgetn) {
  // xsgetn is called by basic_istream::read
  this->in_.seekg(0);
  ASSERT_EQ(this->in_.tellg(), 0);

  TypeParam cdata[sizeof("zzhello worldzz") * sizeof(TypeParam)];
  this->in_.read(cdata, sizeof(cdata) / sizeof(TypeParam));
  EXPECT_TRUE(this->in_.eof());
  EXPECT_TRUE(this->in_.fail()); // short read = fail
  EXPECT_FALSE(this->in_.bad());
  this->in_.clear(); // clear failbit
  std::basic_string<TypeParam> check(cdata, cdata + this->in_.gcount());
  EXPECT_EQ(check, typedString<TypeParam>("hello world"));

  this->in_.seekg(1);
  ASSERT_EQ(this->in_.tellg(), 1);
  // memset
  IOStreamBuf<TypeParam>::traits_type::assign(cdata,
          sizeof(cdata) / sizeof(TypeParam), '\xfb');
  this->in_.read(cdata, 6);

  EXPECT_EQ(this->in_.gcount(), 6);
  check = std::basic_string<TypeParam>(cdata, cdata + 6);
  EXPECT_EQ(check, typedString<TypeParam>("ello w"));
}

TYPED_TEST(IOStreamBufTest, putback) {
  this->in_.seekg(7); // start in the 2nd IOBuf, walk back to the 1st.

  this->in_.putback(IOStreamBuf<TypeParam>::traits_type::to_char_type('w'));
  ASSERT_TRUE(this->in_.good());

  // continue into the previous IOBuf
  this->in_.putback(IOStreamBuf<TypeParam>::traits_type::to_char_type(' '));
  ASSERT_TRUE(this->in_.good());

  // non-matching putback
  this->in_.putback(IOStreamBuf<TypeParam>::traits_type::to_char_type('z'));
  EXPECT_FALSE(this->in_.good());
}
