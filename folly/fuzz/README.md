# Folly Fuzz Targets

Fuzz targets for [Folly](https://github.com/facebook/folly), integrated with
[OSS-Fuzz](https://github.com/google/oss-fuzz/tree/master/projects/folly).

## What is fuzzed

| Target | API |
|--------|-----|
| `json_fuzzer` | `folly::parseJson()`, `folly::toJson()` — parse + round-trip invariant |
| `uri_fuzzer` | `folly::Uri::tryFromString()` — all URI field accessors |
| `ipaddress_fuzzer` | `folly::IPAddress::tryFromString()`, `tryCreateNetwork()` — all predicates |
| `conv_fuzzer` | `folly::tryTo<T>()` — all signed/unsigned integer, float, and bool conversions |

## Building

Requires Clang with AddressSanitizer and libFuzzer support (Clang ≥ 12).

```sh
cmake -S . -B _build \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_FUZZ_TARGETS=ON \
    -DLIB_FUZZING_ENGINE="-fsanitize=fuzzer" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

cmake --build _build --target json_fuzzer uri_fuzzer ipaddress_fuzzer conv_fuzzer
```

## Running

```sh
./_build/folly/fuzz/json_fuzzer       folly/fuzz/corpus/json_fuzzer/
./_build/folly/fuzz/uri_fuzzer        folly/fuzz/corpus/uri_fuzzer/
./_build/folly/fuzz/ipaddress_fuzzer  folly/fuzz/corpus/ipaddress_fuzzer/
./_build/folly/fuzz/conv_fuzzer       folly/fuzz/corpus/conv_fuzzer/
```

Pass `-max_total_time=600` to limit each run to 10 minutes.

## OSS-Fuzz

These targets are built and exercised continuously by OSS-Fuzz. Crashes are
triaged automatically and filed as security bugs against the Folly project.
