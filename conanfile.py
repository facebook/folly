import os
import shutil
import subprocess

from conan import ConanFile
from conan.tools.system.package_manager import Apt

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
        "libdwarf/[*]",
        "libiberty/[*]",
        "libunwind/[*]",
        "liburing/[*]",
    ]

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

        # Clone and build
        self._run(["git", "clone", "--depth", "1", BLAKE3_GIT_URL, blake3_dir])
        self._run(["cmake", "-B", build_dir, "-DCMAKE_INSTALL_PREFIX=/usr/local"],
                  cwd=os.path.join(blake3_dir, "c"))
        self._run(["cmake", "--build", build_dir])
        self._run(["sudo", "cmake", "--install", build_dir])

    def system_requirements(self):
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
        self._install_blake3()
