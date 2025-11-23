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

#include <folly/hash/UniqueHashKey.h>

#include <folly/portability/OpenSSL.h>
#include <folly/portability/Unistd.h>

#if defined(__linux__)
#include <sys/auxv.h>
#endif

#if __has_include(<blake3.h>) && !defined(_WIN32)
#include <blake3.h>
#endif

#if __has_include(<xxh3.h>)
#include <xxh3.h>
#endif

namespace folly {

namespace detail {

/// unique_hash_key_init_process_key_sha256
///
/// Cryptographically hashes auxval-random or pid, keyed by caller __func__. For
/// each algorithm, for extra margin of safety, we want a different process-key.
///
/// Accepts fixed-sized 32-byte output only.
///
/// SHA256 is not keyed, but HMAC requires two rounds of hashing. So this uses
/// the Secret Suffix MAC construction, which depends on the underlying hash
/// function's collision resistance for its own collision resistance.
void unique_hash_key_init_process_key_sha256(
    char const* const context, span<uint8_t, SHA256_DIGEST_LENGTH> const out) {
  auto const pid = getpid();
  auto const pidkey = reinterpret_span_cast<uint8_t const>(span{&pid, 1});
#if defined(__linux__)
  constexpr size_t auxlen = 16;
  auto const auxval = getauxval(AT_RANDOM);
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto const auxptr = reinterpret_cast<uint8_t const*>(auxval);
  auto const auxkey = span<uint8_t const>{auxptr, auxlen};
#endif
  SHA256_CTX h{};
  SHA256_Init(&h);
  SHA256_Update(&h, context, std::strlen(context));
  SHA256_Update(&h, pidkey.data(), pidkey.size()); // Secret Suffix MAC
#if defined(__linux__)
  SHA256_Update(&h, auxkey.data(), auxkey.size()); // Secret Suffix MAC
#endif
  SHA256_Final(out.data(), &h);
}

struct unique_hash_key_algo_strong_sha256_init_t { // NOLINT
  uint8_t key[SHA256_DIGEST_LENGTH] = {};
};

static unique_hash_key_algo_strong_sha256_init_t
unique_hash_key_algo_strong_sha256_init_impl() {
  unique_hash_key_algo_strong_sha256_init_t res{};
  unique_hash_key_init_process_key_sha256(__func__, res.key);
  return res;
}

// init blake3 hasher with key unique to this process
static unique_hash_key_algo_strong_sha256_init_t const&
unique_hash_key_algo_strong_sha256_init() {
  static auto const object = unique_hash_key_algo_strong_sha256_init_impl();
  return object;
}

#if __has_include(<blake3.h>) && !defined(_WIN32)

/// unique_hash_key_init_process_key_blake3
///
/// Cryptographically hashes auxval-random or pid, keyed by caller __func__. For
/// each algorithm, for extra margin of safety, we want a different process-key.
///
/// Accepts arbitrarily-sized output, which is required by the xxh3 algo.
void unique_hash_key_init_process_key_blake3(
    char const* const context, span<uint8_t> const out) {
#if defined(__linux__)
  constexpr size_t auxlen = 16;
  auto const auxval = getauxval(AT_RANDOM);
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto const auxptr = reinterpret_cast<uint8_t const*>(auxval);
  auto const key = span<uint8_t const>{auxptr, auxlen};
#else
  auto const pid = getpid();
  auto const key = reinterpret_span_cast<uint8_t const>(span{&pid, 1});
#endif
  blake3_hasher h{};
  blake3_hasher_init_derive_key(&h, context);
  blake3_hasher_update(&h, key.data(), key.size());
  blake3_hasher_finalize(&h, out.data(), out.size());
}

struct unique_hash_key_algo_strong_blake3_init_t { // NOLINT
  uint8_t key[BLAKE3_KEY_LEN] = {};
};

static unique_hash_key_algo_strong_blake3_init_t
unique_hash_key_algo_strong_blake3_init_impl() {
  unique_hash_key_algo_strong_blake3_init_t res{};
  unique_hash_key_init_process_key_blake3(__func__, res.key);
  return res;
}

// init blake3 hasher with key unique to this process
static unique_hash_key_algo_strong_blake3_init_t const&
unique_hash_key_algo_strong_blake3_init() {
  static auto const object = unique_hash_key_algo_strong_blake3_init_impl();
  return object;
}

#if __has_include(<xxh3.h>)

struct unique_hash_key_algo_fast_xxh3_init_t { // NOLINT
  uint8_t secret[XXH3_SECRET_SIZE_MIN] = {};
  uint64_t seed = {};
};

static unique_hash_key_algo_fast_xxh3_init_t
unique_hash_key_algo_fast_xxh3_init_impl() {
  unique_hash_key_algo_fast_xxh3_init_t res{};
  unique_hash_key_init_process_key_blake3(__func__, res.secret);
  res.seed = XXH3_64bits(res.secret, sizeof(res.secret));
  return res;
}

// init xxh3 hasher with secret/seed unique to this process
static unique_hash_key_algo_fast_xxh3_init_t const&
unique_hash_key_algo_fast_xxh3_init() {
  static auto const object = unique_hash_key_algo_fast_xxh3_init_impl();
  return object;
}

template <size_t Size>
struct unique_hash_key_algo_fast_xxh3_ops;
template <>
struct unique_hash_key_algo_fast_xxh3_ops<8> {
  static constexpr auto init = XXH3_64bits_reset_withSecret;
  static constexpr auto update = XXH3_64bits_update;
  static constexpr auto finalize = XXH3_64bits_digest;
};
template <>
struct unique_hash_key_algo_fast_xxh3_ops<16> {
  static constexpr auto init = XXH3_128bits_reset_withSecret;
  static constexpr auto update = XXH3_128bits_update;
  static constexpr auto finalize = XXH3_128bits_digest;
};

#endif // __has_include(<xxh3.h>)

#endif // __has_include(<blake3.h>)

template <typename Hash, typename Update>
FOLLY_ERASE static void unique_hash_key_hash_items(
    Update const update,
    Hash* const h,
    span<detail::unique_hash_key_item const> const in) noexcept {
  for (auto const item : in) {
    auto const [len, include_len] = item.unpack_len();
    if (include_len) {
      auto const buf = reinterpret_cast<uint8_t const*>(&len);
      update(h, buf, sizeof(len));
    }
    update(h, item.buf, len);
  }
}

} // namespace detail

/// SHA256 is not keyed, but HMAC requires two rounds of hashing. So this uses
/// the Secret Suffix MAC construction, which depends on the underlying hash
/// function's collision resistance for its own collision resistance.
template <size_t Size>
std::array<uint8_t, Size>
unique_hash_key_algo_strong_sha256_fn<Size>::operator()(
    span<detail::unique_hash_key_item const> const in) const noexcept {
  auto const& init = detail::unique_hash_key_algo_strong_sha256_init();
  SHA256_CTX h;
  SHA256_Init(&h);
  detail::unique_hash_key_hash_items(SHA256_Update, &h, in);
  SHA256_Update(&h, init.key, sizeof(init.key)); // Secret Suffix MAC
  unsigned char mid[SHA256_DIGEST_LENGTH];
  SHA256_Final(mid, &h);
  std::array<uint8_t, Size> out{};
  std::memcpy(out.data(), mid, out.size());
  return out;
}

template std::array<uint8_t, 8> //
unique_hash_key_algo_strong_sha256_fn<8>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 16> //
unique_hash_key_algo_strong_sha256_fn<16>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 24> //
unique_hash_key_algo_strong_sha256_fn<24>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 32> //
unique_hash_key_algo_strong_sha256_fn<32>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;

#if __has_include(<blake3.h>) && !defined(_WIN32)

template <size_t Size>
std::array<uint8_t, Size>
unique_hash_key_algo_strong_blake3_fn<Size>::operator()(
    span<detail::unique_hash_key_item const> const in) const noexcept {
  auto const& init = detail::unique_hash_key_algo_strong_blake3_init();
  blake3_hasher h;
  blake3_hasher_init_keyed(&h, init.key);
  detail::unique_hash_key_hash_items(blake3_hasher_update, &h, in);
  std::array<uint8_t, Size> out{};
  blake3_hasher_finalize(&h, out.data(), out.size());
  return out;
}

template std::array<uint8_t, 8> //
unique_hash_key_algo_strong_blake3_fn<8>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 16> //
unique_hash_key_algo_strong_blake3_fn<16>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 24> //
unique_hash_key_algo_strong_blake3_fn<24>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 32> //
unique_hash_key_algo_strong_blake3_fn<32>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;

#if __has_include(<xxh3.h>)

template <size_t Size>
std::array<uint8_t, Size> unique_hash_key_algo_fast_xxh3_fn<Size>::operator()(
    span<detail::unique_hash_key_item const> const in) const noexcept {
  using ops = detail::unique_hash_key_algo_fast_xxh3_ops<Size>;
  auto const& init = detail::unique_hash_key_algo_fast_xxh3_init();
  XXH3_state_t h;
  // ideally, XXH3_{N}bits_reset_withSecretAndSeed - but it may not be available
  // so we must mimic it with XXH3_{N}bits_reset_withSecret and then setting the
  // seed directly
  ops::init(&h, init.secret, sizeof(init.secret));
  h.seed = init.seed;
  h.useSeed = true;
  detail::unique_hash_key_hash_items(ops::update, &h, in);
  auto v = ops::finalize(&h);
  static_assert(sizeof(v) == Size);
  std::array<uint8_t, Size> out{};
  std::memcpy(out.data(), &v, sizeof(v));
  return out;
}

template std::array<uint8_t, 8> //
unique_hash_key_algo_fast_xxh3_fn<8>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;
template std::array<uint8_t, 16> //
unique_hash_key_algo_fast_xxh3_fn<16>::operator()(
    span<detail::unique_hash_key_item const> in) const noexcept;

#endif // __has_include(<xxh3.h>)

#endif // __has_include(<blake3.h>)

} // namespace folly
