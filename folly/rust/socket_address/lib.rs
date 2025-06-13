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

use std::fmt;
use std::fmt::Display;
#[cfg(unix)]
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

use anyhow::Result;
#[cfg(windows)]
use anyhow::anyhow;
use cxx::UniquePtr;
use cxx::let_cxx_string;

pub use self::ffi::SocketAddress;

#[cxx::bridge]
mod ffi {
    #[namespace = "folly"]
    extern "C++" {
        type SocketAddress;
    }

    #[namespace = "folly"]
    unsafe extern "C++" {
        type IPAddress = network_address::ffi::IPAddress;
    }

    #[namespace = "facebook::rust"]
    unsafe extern "C++" {
        include!("folly/rust/socket_address/RustSocketAddress.h");

        fn socketaddress_create(
            host: &CxxString,
            port: u16,
            allowNameLookup: bool,
        ) -> Result<UniquePtr<SocketAddress>>;

        fn socketaddress_create_from_ipaddress_and_port(
            ipAddress: &IPAddress,
            port: u16,
        ) -> UniquePtr<SocketAddress>;

        fn socketaddress_from_path(path: &CxxString) -> Result<UniquePtr<SocketAddress>>;

        fn socketaddress_describe(addr: &SocketAddress) -> UniquePtr<CxxString>;

        fn socketaddress_get_ip_address(addr: &SocketAddress) -> Result<UniquePtr<IPAddress>>;

        fn socketaddress_get_port(addr: &SocketAddress) -> Result<u16>;

        fn socketaddress_is_initialized(addr: &SocketAddress) -> bool;
    }
}

impl SocketAddress {
    pub fn new(host: &str, port: u16, lookup: bool) -> Result<UniquePtr<Self>> {
        let_cxx_string!(host = host);
        let addr = ffi::socketaddress_create(&host, port, lookup)?;
        Ok(addr)
    }

    pub fn make_from_path<P: AsRef<Path>>(path: P) -> Result<UniquePtr<Self>> {
        let path = path.as_ref();

        #[cfg(unix)]
        let path = path.as_os_str().as_bytes();

        #[cfg(windows)]
        let path = path
            .to_str()
            .ok_or_else(|| anyhow!("Path is not valid UTF-8: '{}'", path.display()))?
            .as_bytes();

        let_cxx_string!(path = path);

        Ok(ffi::socketaddress_from_path(&path)?)
    }

    /// Convenient method for converting from [`std::net::SocketAddr`]. This
    /// unfortunately cannot be a `std::convert::TryFrom` implementation due to
    /// the need of returning `UniquePtr`.
    pub fn from_addr(addr: std::net::SocketAddr) -> UniquePtr<Self> {
        ffi::socketaddress_create_from_ipaddress_and_port(
            &network_address::IPAddress::from(addr.ip()),
            addr.port(),
        )
    }

    /// Returns Err for non-IP addresses
    pub fn get_ip_address(&self) -> Result<std::net::IpAddr> {
        Ok(network_address::IPAddress::from(ffi::socketaddress_get_ip_address(self)?).into())
    }

    /// Returns Err for non-IP addresses
    pub fn get_port(&self) -> Result<u16> {
        Ok(ffi::socketaddress_get_port(self)?)
    }

    pub fn is_initialized(&self) -> bool {
        ffi::socketaddress_is_initialized(self)
    }
}

impl Display for SocketAddress {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        let description = ffi::socketaddress_describe(self);
        Display::fmt(&description, formatter)
    }
}
