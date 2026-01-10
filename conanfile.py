import os
import shutil
import subprocess

from conan import ConanFile
from conan.tools.system.package_manager import Apt, Brew, Chocolatey

BLAKE3_GIT_URL = "https://github.com/BLAKE3-team/BLAKE3.git"


class FollyConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "PkgConfigDeps", "CMakeDeps", "CMakeToolchain"
    requires = [
        "bzip2/[*]",
        "fast_float/[*]",
        "fmt/[*]",
        "gflags/[*]",
        "glog/[*]",
        "libdwarf/[<100]",
    ]

    def configure(self):
        if self.settings.os != "Windows":
            self.requires("libiberty/[*]")
        if self.settings.os in ("Linux", "FreeBSD"):
            self.requires("libunwind/[*]")
        if self.settings.os == "Linux":
            self.requires("liburing/[*]")

    def _run(self, args, **kwargs):
        """Run a command, raising on failure."""
        subprocess.run(args, check=True, **kwargs)

    def _install_blake3(self):
        """Build and install BLAKE3 C library from source."""
        blake3_dir = os.path.join(os.getcwd(), "conan-build", "tmp", "BLAKE3")
        build_dir = os.path.join(blake3_dir, "c", "build")

        # Clean any previous build
        if os.path.exists(blake3_dir):
            shutil.rmtree(blake3_dir)

        # Platform-specific install prefix
        if self.settings.os == "Windows":
            install_prefix = "C:/Program Files/BLAKE3"
        else:
            install_prefix = "/usr/local"

        # Clone and build
        self._run(["git", "clone", "--depth", "1", BLAKE3_GIT_URL, blake3_dir])
        self._run(["cmake", "-B", build_dir, f"-DCMAKE_INSTALL_PREFIX={install_prefix}"],
                  cwd=os.path.join(blake3_dir, "c"))

        # Install (sudo required on Unix, not on Windows)
        # Windows MSBuild needs --config Release for both build and install
        if self.settings.os == "Windows":
            self._run(["cmake", "--build", build_dir, "--config", "Release"])
            self._run(["cmake", "--install", build_dir, "--config", "Release"])
        else:
            self._run(["cmake", "--build", build_dir])
            self._run(["sudo", "cmake", "--install", build_dir])

    def system_requirements(self):
        # Linux (Debian/Ubuntu)
        Apt(self).install([
            "libboost-all-dev",
            "libc++-dev",
            "libc++abi-dev",
            "libdouble-conversion-dev",
            "libevent-dev",
            "libgtest-dev",
            "libjemalloc-dev",
            "liblz4-dev",
            "libsnappy-dev",
            "libsodium-dev",
            "libssl-dev",
            "libxxhash-dev",
        ], update=True, check=True)

        # macOS
        Brew(self).install([
            "boost",
            "double-conversion",
            "googletest",
            "jemalloc",
            "libevent",
            "libsodium",
            "lz4",
            "openssl",
            "snappy",
            "xxhash",
        ], update=True, check=True)

        # Windows
        Chocolatey(self).install([
            "boost-msvc-14.3",
            "openssl",
        ], update=True, check=True)

        self._install_blake3()
