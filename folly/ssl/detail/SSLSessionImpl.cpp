/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/ssl/detail/SSLSessionImpl.h>
#include <folly/ssl/detail/OpenSSLVersionFinder.h>

namespace folly {
namespace ssl {
namespace detail {

//
// Wrapper OpenSSL 1.0.2 (and possibly 1.0.1)
//

SSLSessionImpl::SSLSessionImpl(SSL_SESSION* session, bool takeOwnership)
    : session_(session) {
  if (session_ == nullptr) {
    throw std::runtime_error("SSL_SESSION is null");
  }
  // If we're not given ownership, we need to up the refcount so the SSL_SESSION
  // object won't be freed while SSLSessionImpl is alive
  if (!takeOwnership) {
    upRef();
  }
}

SSLSessionImpl::SSLSessionImpl(const std::string& serializedSession) {
  auto sessionData =
      reinterpret_cast<const unsigned char*>(serializedSession.data());
  if ((session_ = d2i_SSL_SESSION(
           nullptr, &sessionData, serializedSession.length())) == nullptr) {
    throw std::runtime_error("Cannot deserialize SSLSession string");
  }
}

SSLSessionImpl::~SSLSessionImpl() {
  downRef();
}

std::string SSLSessionImpl::serialize(void) const {
  std::string ret;

  // Get the length first, then we know how much space to allocate.
  auto len = i2d_SSL_SESSION(session_, nullptr);

  if (len > 0) {
    std::unique_ptr<unsigned char[]> uptr(new unsigned char[len]);
    auto p = uptr.get();
    auto written = i2d_SSL_SESSION(session_, &p);
    if (written <= 0) {
      VLOG(2) << "Could not serialize SSL_SESSION!";
    } else {
      ret.assign(uptr.get(), uptr.get() + written);
    }
  }
  return ret;
}

std::string SSLSessionImpl::getSessionID() const {
  std::string ret;
  if (session_) {
    const unsigned char* ptr = nullptr;
    unsigned int len = 0;
#if defined(OPENSSL_IS_102) || defined(OPENSSL_IS_101)
    len = session_->session_id_length;
    ptr = session_->session_id;
#elif defined(OPENSSL_IS_110) || defined(OPENSSL_IS_BORINGSSL)
    ptr = SSL_SESSION_get_id(session_, &len);
#endif
    ret.assign(ptr, ptr + len);
  }
  return ret;
}

const SSL_SESSION* SSLSessionImpl::getRawSSLSession() const {
  return const_cast<SSL_SESSION*>(session_);
}

SSL_SESSION* SSLSessionImpl::getRawSSLSessionDangerous() {
  upRef();
  return session_;
}

void SSLSessionImpl::upRef() {
  if (session_) {
#if defined(OPENSSL_IS_102) || defined(OPENSSL_IS_101)
    CRYPTO_add(&session_->references, 1, CRYPTO_LOCK_SSL_SESSION);
#elif defined(OPENSSL_IS_BORINGSSL) || defined(OPENSSL_IS_110)
    SSL_SESSION_up_ref(session_);
#endif
  }
}

void SSLSessionImpl::downRef() {
  if (session_) {
    SSL_SESSION_free(session_);
  }
}

} // namespace detail
} // namespace ssl
} // namespace folly
