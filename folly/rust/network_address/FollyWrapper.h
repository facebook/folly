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
#include <cstdint>
#include <memory>
#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include "rust/cxx.h"

namespace facebook::rust {

::std::unique_ptr<::folly::IPAddressV4> IPAddressV4_create(
    const ::std::array<uint8_t, 4>& addr) noexcept;

::std::unique_ptr<::folly::IPAddressV4> IPAddressV4_create_from_IPAddress(
    const ::folly::IPAddress& addr);

::rust::String IPAddressV4_to_string(const ::folly::IPAddressV4& addr) noexcept;

::std::array<uint8_t, 4> IPAddressV4_to_byte_array(
    const ::folly::IPAddressV4& addr) noexcept;

::std::unique_ptr<::folly::IPAddressV6> IPAddressV6_create(
    const ::std::array<uint8_t, 16>& addr) noexcept;

::std::unique_ptr<::folly::IPAddressV6> IPAddressV6_create_from_IPAddress(
    const ::folly::IPAddress& addr);

::rust::String IPAddressV6_to_string(const ::folly::IPAddressV6& addr) noexcept;

::std::array<uint8_t, 16> IPAddressV6_to_byte_array(
    const ::folly::IPAddressV6& addr) noexcept;

::std::unique_ptr<::folly::IPAddress> IPAddress_create_from_IPAddressV4(
    const ::folly::IPAddressV4& addr) noexcept;

::std::unique_ptr<::folly::IPAddress> IPAddress_create_from_IPAddressV6(
    const ::folly::IPAddressV6& addr) noexcept;

::rust::String IPAddress_to_string(const ::folly::IPAddress& addr) noexcept;

bool IPAddress_isV4(const ::folly::IPAddress& addr) noexcept;

bool IPAddress_isV6(const ::folly::IPAddress& addr) noexcept;

} // namespace facebook::rust
