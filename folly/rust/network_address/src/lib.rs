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

#![deny(
    rustdoc::all,
    broken_intra_doc_links,
    invalid_codeblock_attributes,
    missing_crate_level_docs,
    missing_debug_implementations,
    missing_doc_code_examples,
    missing_docs,
    unreachable_pub,
    unused_imports
)]

//! Rust binding for `folly::IPAddress`, `folly::IPAddressV4` and `folly::IPAddressV6`
//!
//! This primarily intended for interop with C++ APIs which use `folly::IPAddress`* as part of their
//! interface, rather than as a full-featured Rust API.

use std::net::IpAddr;
use std::net::Ipv4Addr;
use std::net::Ipv6Addr;
use std::ops::Deref;

use cxx::UniquePtr;

#[cxx::bridge]
/// Actual C++ bindings
// NOTE: The ffi needs to be public in order to export the types
pub mod ffi {
    #[namespace = "folly"]
    unsafe extern "C++" {
        #[allow(missing_debug_implementations)]
        /// folly::IPAddressV4 binding
        type IPAddressV4;

        #[allow(missing_debug_implementations)]
        /// folly::IPAddressV6 binding
        type IPAddressV6;

        #[allow(missing_debug_implementations)]
        /// folly::IPAddress binding
        type IPAddress;

    }

    // TODO: Understand why functions are visible outside the crate. It should not really happen :(
    #[namespace = "facebook::rust"]
    unsafe extern "C++" {
        include!("folly/rust/network_address/FollyWrapper.h");

        /// Create a [`folly::IPAddressV4`] instance wrapped by a C++ Unique Pointer from its byte representation
        pub(crate) fn IPAddressV4_create(addr: &[u8; 4]) -> UniquePtr<IPAddressV4>;

        /// Create a [`folly::IPAddressV4`] instance wrapped by a C++ Unique Pointer from a [`folly::IPAddress`] instance
        pub(crate) fn IPAddressV4_create_from_IPAddress(addr: &IPAddress)
        -> UniquePtr<IPAddressV4>;

        /// Extract the string representation of a [`folly::IPAddressV4`] instance
        pub(crate) fn IPAddressV4_to_string(addr: &IPAddressV4) -> String;

        /// Extract the bytes representation of a [`folly::IPAddressV4`] instance
        pub(crate) fn IPAddressV4_to_byte_array(addr: &IPAddressV4) -> [u8; 4];

        /// Create a [`folly::IPAddressV5`] instance wrapped by a C++ Unique Pointer from its byte representation
        pub(crate) fn IPAddressV6_create(addr: &[u8; 16]) -> UniquePtr<IPAddressV6>;

        /// Create a [`folly::IPAddressV5`] instance wrapped by a C++ Unique Pointer from a [`folly::IPAddress`] instance
        pub(crate) fn IPAddressV6_create_from_IPAddress(addr: &IPAddress)
        -> UniquePtr<IPAddressV6>;

        /// Extract the string representation of a [`folly::IPAddressV6`] instance
        pub(crate) fn IPAddressV6_to_string(addr: &IPAddressV6) -> String;

        /// Extract the bytes representation of a [`folly::IPAddressV6`] instance
        pub(crate) fn IPAddressV6_to_byte_array(addr: &IPAddressV6) -> [u8; 16];

        /// Create a [`folly::IPAddress`] instance wrapped by a C++ Unique Pointer, from an IPAddress V4.
        pub(crate) fn IPAddress_create_from_IPAddressV4(addr: &IPAddressV4)
        -> UniquePtr<IPAddress>;

        /// Create a [`folly::IPAddress`] instance wrapped by a C++ Unique Pointer, from an IPAddress V6.
        pub(crate) fn IPAddress_create_from_IPAddressV6(addr: &IPAddressV6)
        -> UniquePtr<IPAddress>;

        /// Extract the string representation of a [`folly::IPAddress`] instance
        pub(crate) fn IPAddress_to_string(addr: &IPAddress) -> String;

        /// Determines if a given [`folly::IPAddress`] is a representation of a IPV4 address
        pub(crate) fn IPAddress_isV4(addr: &IPAddress) -> bool;

        /// Determines if a given [`folly::IPAddress`] is a representation of a IPV6 address
        pub(crate) fn IPAddress_isV6(addr: &IPAddress) -> bool;
    }
}

/// Wrapper of [`UniquePtr<ffi::IPAddressV4>`] for better ergonomic and traits implementation
pub struct IPAddressV4(UniquePtr<ffi::IPAddressV4>);

impl From<UniquePtr<ffi::IPAddressV4>> for IPAddressV4 {
    fn from(value: UniquePtr<ffi::IPAddressV4>) -> Self {
        Self(value)
    }
}

impl std::fmt::Debug for IPAddressV4 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("IPAddressV4")
            .field(&ffi::IPAddressV4_to_string(&self.0))
            .finish()
    }
}

impl std::fmt::Display for IPAddressV4 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&ffi::IPAddressV4_to_string(&self.0))
    }
}

impl Deref for IPAddressV4 {
    type Target = ffi::IPAddressV4;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<Ipv4Addr> for IPAddressV4 {
    fn from(value: Ipv4Addr) -> Self {
        Self(ffi::IPAddressV4_create(&value.octets()))
    }
}

impl From<&Ipv4Addr> for IPAddressV4 {
    fn from(value: &Ipv4Addr) -> Self {
        Self(ffi::IPAddressV4_create(&value.octets()))
    }
}

impl From<IPAddressV4> for Ipv4Addr {
    fn from(value: IPAddressV4) -> Self {
        Self::from(&value)
    }
}

impl From<&IPAddressV4> for Ipv4Addr {
    fn from(value: &IPAddressV4) -> Self {
        Self::from(ffi::IPAddressV4_to_byte_array(value))
    }
}

/// Wrapper of [`UniquePtr<ffi::IPAddressV6>`] for better ergonomic and traits implementation
pub struct IPAddressV6(UniquePtr<ffi::IPAddressV6>);

impl From<UniquePtr<ffi::IPAddressV6>> for IPAddressV6 {
    fn from(value: UniquePtr<ffi::IPAddressV6>) -> Self {
        Self(value)
    }
}

impl std::fmt::Debug for IPAddressV6 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("IPAddressV6")
            .field(&ffi::IPAddressV6_to_string(&self.0))
            .finish()
    }
}

impl std::fmt::Display for IPAddressV6 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&ffi::IPAddressV6_to_string(&self.0))
    }
}

impl Deref for IPAddressV6 {
    type Target = ffi::IPAddressV6;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<Ipv6Addr> for IPAddressV6 {
    fn from(value: Ipv6Addr) -> Self {
        Self(ffi::IPAddressV6_create(&value.octets()))
    }
}

impl From<&Ipv6Addr> for IPAddressV6 {
    fn from(value: &Ipv6Addr) -> Self {
        Self(ffi::IPAddressV6_create(&value.octets()))
    }
}

impl From<IPAddressV6> for Ipv6Addr {
    fn from(value: IPAddressV6) -> Self {
        Self::from(&value)
    }
}

impl From<&IPAddressV6> for Ipv6Addr {
    fn from(value: &IPAddressV6) -> Self {
        Self::from(ffi::IPAddressV6_to_byte_array(value))
    }
}

/// Wrapper of [`UniquePtr<ffi::IPAddress>`] for better ergonomic and traits implementation
pub struct IPAddress(UniquePtr<ffi::IPAddress>);

impl From<UniquePtr<ffi::IPAddress>> for IPAddress {
    fn from(value: UniquePtr<ffi::IPAddress>) -> Self {
        Self(value)
    }
}

impl std::fmt::Debug for IPAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("IPAddress")
            .field(&ffi::IPAddress_to_string(&self.0))
            .finish()
    }
}

impl std::fmt::Display for IPAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&ffi::IPAddress_to_string(&self.0))
    }
}

impl Deref for IPAddress {
    type Target = ffi::IPAddress;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<Ipv4Addr> for IPAddress {
    fn from(value: Ipv4Addr) -> Self {
        Self(ffi::IPAddress_create_from_IPAddressV4(
            IPAddressV4::from(value).deref(),
        ))
    }
}

impl From<&Ipv4Addr> for IPAddress {
    fn from(value: &Ipv4Addr) -> Self {
        Self(ffi::IPAddress_create_from_IPAddressV4(
            IPAddressV4::from(value).deref(),
        ))
    }
}

impl From<Ipv6Addr> for IPAddress {
    fn from(value: Ipv6Addr) -> Self {
        Self(ffi::IPAddress_create_from_IPAddressV6(
            IPAddressV6::from(value).deref(),
        ))
    }
}

impl From<&Ipv6Addr> for IPAddress {
    fn from(value: &Ipv6Addr) -> Self {
        Self(ffi::IPAddress_create_from_IPAddressV6(
            IPAddressV6::from(value).deref(),
        ))
    }
}

impl From<IpAddr> for IPAddress {
    fn from(value: IpAddr) -> Self {
        match value {
            IpAddr::V4(addr_v4) => addr_v4.into(),
            IpAddr::V6(addr_v6) => addr_v6.into(),
        }
    }
}

impl From<&IpAddr> for IPAddress {
    fn from(value: &IpAddr) -> Self {
        match value {
            IpAddr::V4(addr_v4) => addr_v4.into(),
            IpAddr::V6(addr_v6) => addr_v6.into(),
        }
    }
}

impl From<IPAddressV4> for IpAddr {
    fn from(value: IPAddressV4) -> Self {
        Self::from(&value)
    }
}

impl From<&IPAddressV4> for IpAddr {
    fn from(value: &IPAddressV4) -> Self {
        Self::from(ffi::IPAddressV4_to_byte_array(value))
    }
}

impl From<IPAddressV6> for IpAddr {
    fn from(value: IPAddressV6) -> Self {
        Self::from(&value)
    }
}

impl From<&IPAddressV6> for IpAddr {
    fn from(value: &IPAddressV6) -> Self {
        Self::from(ffi::IPAddressV6_to_byte_array(value))
    }
}

impl From<IPAddress> for IpAddr {
    fn from(value: IPAddress) -> Self {
        Self::from(&value)
    }
}

impl From<&IPAddress> for IpAddr {
    fn from(value: &IPAddress) -> Self {
        if ffi::IPAddress_isV4(value) {
            Self::from(&IPAddressV4(ffi::IPAddressV4_create_from_IPAddress(value)))
        } else if ffi::IPAddress_isV6(value) {
            Self::from(&IPAddressV6(ffi::IPAddressV6_create_from_IPAddress(value)))
        } else {
            unreachable!()
        }
    }
}

#[cfg(test)]
mod test_ipv4_addresses {
    use std::str::FromStr;

    use crate::*;

    const IP_STR: &str = "123.45.67.89";

    #[test]
    fn roundtrip_ipv4() {
        let rust_ipv4 = Ipv4Addr::from_str(IP_STR).unwrap();
        let cpp_ipv4 = IPAddressV4::from(rust_ipv4);
        assert_eq!(IP_STR, cpp_ipv4.to_string());
        assert_eq!(rust_ipv4, Ipv4Addr::from(&cpp_ipv4));
    }

    #[test]
    fn roundtrip_references_ipv4() {
        let rust_ipv4 = &Ipv4Addr::from_str(IP_STR).unwrap();
        let cpp_ipv4 = IPAddressV4::from(rust_ipv4);
        assert_eq!(IP_STR, cpp_ipv4.to_string());
        assert_eq!(rust_ipv4, &Ipv4Addr::from(&cpp_ipv4));
    }

    #[test]
    fn roundtrip_ip() {
        let rust_ip = IpAddr::from_str(IP_STR).unwrap();
        let cpp_ip = IPAddress::from(rust_ip);
        assert_eq!(IP_STR, cpp_ip.to_string());
        assert_eq!(rust_ip, IpAddr::from(&cpp_ip));
    }

    #[test]
    fn roundtrip_references_ip() {
        let rust_ip = &IpAddr::from_str(IP_STR).unwrap();
        let cpp_ip = IPAddress::from(rust_ip);
        assert_eq!(IP_STR, cpp_ip.to_string());
        assert_eq!(rust_ip, &IpAddr::from(&cpp_ip));
    }
}

#[cfg(test)]
mod test_ipv6_addresses {
    use std::str::FromStr;

    use crate::*;

    const IP_STR: &str = "123:4567:890a:bcde:f::1";

    #[test]
    fn roundtrip_ipv6() {
        let rust_ipv6 = Ipv6Addr::from_str(IP_STR).unwrap();
        let cpp_ipv6 = IPAddressV6::from(rust_ipv6);
        assert_eq!(IP_STR, cpp_ipv6.to_string());
        assert_eq!(rust_ipv6, Ipv6Addr::from(&cpp_ipv6));
    }

    #[test]
    fn roundtrip_references_ipv6() {
        let rust_ipv6 = &Ipv6Addr::from_str(IP_STR).unwrap();
        let cpp_ipv6 = IPAddressV6::from(rust_ipv6);
        assert_eq!(IP_STR, cpp_ipv6.to_string());
        assert_eq!(rust_ipv6, &Ipv6Addr::from(&cpp_ipv6));
    }

    #[test]
    fn roundtrip_ip() {
        let rust_ip = IpAddr::from_str(IP_STR).unwrap();
        let cpp_ip = IPAddress::from(rust_ip);
        assert_eq!(IP_STR, cpp_ip.to_string());
        assert_eq!(rust_ip, IpAddr::from(&cpp_ip));
    }

    #[test]
    fn roundtrip_references_ip() {
        let rust_ip = &IpAddr::from_str(IP_STR).unwrap();
        let cpp_ip = IPAddress::from(rust_ip);
        assert_eq!(IP_STR, cpp_ip.to_string());
        assert_eq!(rust_ip, &IpAddr::from(&cpp_ip));
    }
}
