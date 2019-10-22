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

#include <array>

#include <folly/experimental/crypto/Blake2xb.h>
#include <folly/lang/Bits.h>

namespace folly {
namespace crypto {

// static
constexpr size_t Blake2xb::kMinOutputLength;
// static
constexpr size_t Blake2xb::kMaxOutputLength;
// static
constexpr size_t Blake2xb::kUnknownOutputLength;

namespace {

// In libsodium 1.0.17, the crypto_generichash_blake2b_state struct was made
// opaque. We have to copy the internal definition of the real struct here
// so we can properly initialize it.
#if SODIUM_LIBRARY_VERSION_MAJOR > 10 || \
    (SODIUM_LIBRARY_VERSION_MAJOR == 10 && SODIUM_LIBRARY_VERSION_MINOR >= 2)
struct _blake2b_state {
  uint64_t h[8];
  uint64_t t[2];
  uint64_t f[2];
  uint8_t buf[256];
  size_t buflen;
  uint8_t last_node;
};
#define __LIBSODIUM_BLAKE2B_OPAQUE__ 1
#endif

constexpr std::array<uint64_t, 8> kBlake2bIV = {{
    0x6a09e667f3bcc908ULL,
    0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL,
    0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL,
    0x5be0cd19137e2179ULL,
}};

void initStateFromParams(
    crypto_generichash_blake2b_state* _state,
    const detail::Blake2xbParam& param,
    ByteRange key) {
#ifdef __LIBSODIUM_BLAKE2B_OPAQUE__
  auto state = reinterpret_cast<_blake2b_state*>(_state);
#else
  crypto_generichash_blake2b_state* state = _state;
#endif
  auto p = reinterpret_cast<const uint64_t*>(&param);
  for (int i = 0; i < 8; ++i) {
    state->h[i] = kBlake2bIV.data()[i] ^ Endian::little(p[i]);
  }
  std::memset(
      reinterpret_cast<uint8_t*>(state) + sizeof(state->h),
      0,
      sizeof(*state) - sizeof(state->h));
  if (!key.empty()) {
    if (key.size() < crypto_generichash_blake2b_KEYBYTES_MIN ||
        key.size() > crypto_generichash_blake2b_KEYBYTES_MAX) {
      throw std::runtime_error("invalid key size");
    }
    std::array<uint8_t, 128> block;
    memcpy(block.data(), key.data(), key.size());
    memset(block.data() + key.size(), 0, block.size() - key.size());
    crypto_generichash_blake2b_update(
#ifdef __LIBSODIUM_BLAKE2B_OPAQUE__
        reinterpret_cast<decltype(_state)>(state),
#else
        state,
#endif
        block.data(),
        block.size());
    sodium_memzero(block.data(), block.size()); // erase key from stack
  }
}
} // namespace

Blake2xb::Blake2xb()
    : param_{},
      state_{},
      outputLengthKnown_{false},
      initialized_{false},
      finished_{false} {
  static const int sodiumInitResult = sodium_init();
  if (sodiumInitResult == -1) {
    throw std::runtime_error("sodium_init() failed");
  }
}

Blake2xb::~Blake2xb() = default;

void Blake2xb::init(
    size_t outputLength,
    ByteRange key /* = {} */,
    ByteRange salt /* = {} */,
    ByteRange personalization /* = {}*/) {
  if (outputLength == kUnknownOutputLength) {
    outputLengthKnown_ = false;
    outputLength = kUnknownOutputLengthMagic;
  } else if (outputLength > kMaxOutputLength) {
    throw std::runtime_error("Output length too large");
  } else {
    outputLengthKnown_ = true;
  }
  std::memset(&param_, 0, sizeof(param_));
  param_.digestLength = crypto_generichash_blake2b_BYTES_MAX;
  param_.keyLength = static_cast<uint8_t>(key.size());
  param_.fanout = 1;
  param_.depth = 1;
  param_.xofLength = Endian::little(static_cast<uint32_t>(outputLength));
  if (!salt.empty()) {
    if (salt.size() != crypto_generichash_blake2b_SALTBYTES) {
      throw std::runtime_error("Invalid salt length, must be 16 bytes");
    }
    std::memcpy(param_.salt, salt.data(), sizeof(param_.salt));
  }
  if (!personalization.empty()) {
    if (personalization.size() != crypto_generichash_blake2b_PERSONALBYTES) {
      throw std::runtime_error(
          "Invalid personalization length, must be 16 bytes");
    }
    std::memcpy(
        param_.personal, personalization.data(), sizeof(param_.personal));
  }
  initStateFromParams(&state_, param_, key);
  initialized_ = true;
  finished_ = false;
}

void Blake2xb::update(ByteRange data) {
  if (!initialized_) {
    throw std::runtime_error("Must call init() before calling update()");
  } else if (finished_) {
    throw std::runtime_error("Can't call update() after finish()");
  }
  int res =
      crypto_generichash_blake2b_update(&state_, data.data(), data.size());
  if (res != 0) {
    throw std::runtime_error("crypto_generichash_blake2b_update() failed");
  }
}

void Blake2xb::finish(MutableByteRange out) {
  if (!initialized_) {
    throw std::runtime_error("Must call init() before calling finish()");
  } else if (finished_) {
    throw std::runtime_error("finish() already called");
  }

  if (outputLengthKnown_) {
    auto outLength = static_cast<uint32_t>(out.size());
    if (outLength != Endian::little(param_.xofLength)) {
      throw std::runtime_error("out.size() must equal output length");
    }
  }

  std::array<uint8_t, crypto_generichash_blake2b_BYTES_MAX> h0;
  int res = crypto_generichash_blake2b_final(&state_, h0.data(), h0.size());
  if (res != 0) {
    throw std::runtime_error("crypto_generichash_blake2b_final() failed");
  }

  param_.keyLength = 0;
  param_.fanout = 0;
  param_.depth = 0;
  param_.leafLength = Endian::little(
      static_cast<uint32_t>(crypto_generichash_blake2b_BYTES_MAX));
  param_.innerLength = crypto_generichash_blake2b_BYTES_MAX;
  size_t pos = 0;
  size_t remaining = out.size();
  while (remaining > 0) {
    param_.nodeOffset = Endian::little(
        static_cast<uint32_t>(pos / crypto_generichash_blake2b_BYTES_MAX));
    size_t len = std::min(
        static_cast<size_t>(crypto_generichash_blake2b_BYTES_MAX), remaining);
    param_.digestLength = static_cast<uint8_t>(len);
    initStateFromParams(&state_, param_, {} /* key */);
    res = crypto_generichash_blake2b_update(&state_, h0.data(), h0.size());
    if (res != 0) {
      throw std::runtime_error("crypto_generichash_blake2b_update() failed");
    }
    res = crypto_generichash_blake2b_final(&state_, out.data() + pos, len);
    if (res != 0) {
      throw std::runtime_error("crypto_generichash_blake2b_final() failed");
    }
    pos += len;
    remaining -= len;
  }
  finished_ = true;
}

} // namespace crypto
} // namespace folly
