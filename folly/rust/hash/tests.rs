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

use super::*;

#[test]
fn core_hashes_match_folly_golden_vectors() {
    let data = b"hello\x00\x80world";
    let seed32 = 0x1234_5678;
    let seed64 = 0x0123_4567_89ab_cdef;

    assert_eq!(
        hash_128_to_64(seed64, 0xfedc_ba98_7654_3210),
        7_385_569_432_505_790_933
    );
    assert_eq!(
        commutative_hash_128_to_64(seed64, 0xfedc_ba98_7654_3210),
        4_930_791_917_148_304_708
    );
    assert_eq!(twang_mix64(seed64), 3_061_460_455_458_984_563);
    assert_eq!(twang_unmix64(seed64), 17_960_239_321_253_947_328);
    assert_eq!(twang_32from64(seed64), 2_918_899_159);
    assert_eq!(jenkins_rev_mix32(seed32), 1_839_844_202);
    assert_eq!(jenkins_rev_unmix32(seed32), 108_591_032);
    // stdCompatibleHash follows the active C++ standard library, so its
    // numeric output is not portable across CI platforms.
    let std_hash = std_compatible_hash(data);
    assert_eq!(std_hash, std_compatible_hash(data));
    assert_ne!(std_hash, std_compatible_hash(b"hello\x00\x80world!"));
    assert_eq!(murmur_hash64(data, seed64), 17_692_111_243_981_233_383);

    assert_eq!(twang_unmix64(twang_mix64(seed64)), seed64);
    assert_eq!(jenkins_rev_unmix32(jenkins_rev_mix32(seed32)), seed32);
}

#[test]
fn fnv32_functions_match_folly_golden_vectors() {
    let data = b"hello\x00\x80world";
    let seed = 0x1234_5678;

    assert_eq!(FNV32_HASH_START, 2_166_136_261);
    assert_eq!(FNVA32_HASH_START, 2_166_136_261);
    assert_eq!(fnv32_append_byte_broken(seed, 0x80), 3_751_534_952);
    assert_eq!(fnv32_buf_broken(data, seed), 2_409_554_684);
    assert_eq!(fnv32_broken(data, seed), 2_409_554_684);
    assert_eq!(fnv32_append_byte_fixed(seed, 0x80), 543_432_296);
    assert_eq!(fnv32_buf_fixed(data, seed), 1_650_511_356);
    assert_eq!(fnv32_fixed(data, seed), 1_650_511_356);
    assert_eq!(fnva32_append_byte(seed, 0x80), 2_690_967_656);
    assert_eq!(fnva32_buf(data, seed), 2_857_930_116);
    assert_eq!(fnva32(data, seed), 2_857_930_116);
    assert_eq!(fnv32_append_byte(seed, 0x80), 3_751_534_952);
    assert_eq!(fnv32_buf(data, seed), 2_409_554_684);
    assert_eq!(fnv32(data, seed), 2_409_554_684);

    assert_eq!(
        fnv32_append_byte(seed, 0x80),
        fnv32_append_byte_broken(seed, 0x80)
    );
    assert_eq!(fnv32_buf(data, seed), fnv32_buf_broken(data, seed));
    assert_eq!(fnv32(data, seed), fnv32_broken(data, seed));
    assert_ne!(fnv32_buf_broken(data, seed), fnv32_buf_fixed(data, seed));
    assert_eq!(
        fnv32_buf_broken(&data[5..], fnv32_buf_broken(&data[..5], seed)),
        fnv32_buf_broken(data, seed)
    );
    assert_eq!(
        fnv32_buf_fixed(&data[5..], fnv32_buf_fixed(&data[..5], seed)),
        fnv32_buf_fixed(data, seed)
    );
    assert_eq!(
        fnva32_buf(&data[5..], fnva32_buf(&data[..5], seed)),
        fnva32_buf(data, seed)
    );
}

#[test]
fn fnv64_functions_match_folly_golden_vectors() {
    let data = b"hello\x00\x80world";
    let seed = 0x0123_4567_89ab_cdef;

    assert_eq!(FNV64_HASH_START, 14_695_981_039_346_656_037);
    assert_eq!(FNVA64_HASH_START, 14_695_981_039_346_656_037);
    assert_eq!(
        fnv64_append_byte_fixed(seed, 0x80),
        11_150_030_795_743_096_221
    );
    assert_eq!(fnv64_buf_fixed(data, seed), 3_023_805_545_995_900_443);
    assert_eq!(fnv64_fixed(data, seed), 3_023_805_545_995_900_443);
    assert_eq!(
        fnv64_append_byte_broken(seed, 0x80),
        7_296_713_277_966_455_453
    );
    assert_eq!(fnv64_buf_broken(data, seed), 8_948_791_659_412_961_051);
    assert_eq!(fnv64_broken(data, seed), 8_948_791_659_412_961_051);
    assert_eq!(fnv64_append_byte(seed, 0x80), 7_296_713_277_966_455_453);
    assert_eq!(fnv64_buf(data, seed), 8_948_791_659_412_961_051);
    assert_eq!(fnv64(data, seed), 8_948_791_659_412_961_051);
    assert_eq!(fnva64_append_byte(seed, 0x80), 11_149_890_058_254_685_085);
    assert_eq!(fnva64_buf(data, seed), 6_019_151_990_005_238_171);
    assert_eq!(fnva64(data, seed), 6_019_151_990_005_238_171);

    assert_eq!(
        fnv64_append_byte(seed, 0x80),
        fnv64_append_byte_broken(seed, 0x80)
    );
    assert_eq!(fnv64_buf(data, seed), fnv64_buf_broken(data, seed));
    assert_eq!(fnv64(data, seed), fnv64_broken(data, seed));
    assert_ne!(fnv64_buf_broken(data, seed), fnv64_buf_fixed(data, seed));
    assert_eq!(
        fnv64_buf_broken(&data[5..], fnv64_buf_broken(&data[..5], seed)),
        fnv64_buf_broken(data, seed)
    );
    assert_eq!(
        fnv64_buf_fixed(&data[5..], fnv64_buf_fixed(&data[..5], seed)),
        fnv64_buf_fixed(data, seed)
    );
    assert_eq!(
        fnva64_buf(&data[5..], fnva64_buf(&data[..5], seed)),
        fnva64_buf(data, seed)
    );
}

#[test]
fn hsieh_functions_match_folly_golden_vectors() {
    let data = b"hello\x00\x80world";

    assert_eq!(hsieh_hash32_buf_constexpr(data), 3_935_662_385);
    assert_eq!(hsieh_hash32_buf(data), 3_935_662_385);
    assert_eq!(hsieh_hash32(data), 2_963_130_491);
    assert_eq!(hsieh_hash32_str(data), 3_935_662_385);
}

#[test]
fn spooky_hash_functions_match_folly_golden_vectors() {
    let data = b"hello\x00\x80world";
    let seed32 = 0x1234_5678;
    let seed64 = 0x0123_4567_89ab_cdef;
    let seed128 = (seed64, !seed64);

    assert_eq!(spooky_hash_v1::hash32(data, seed32), 2_822_185_396);
    assert_eq!(
        spooky_hash_v1::hash64(data, seed64),
        17_195_322_292_301_597_761
    );
    assert_eq!(
        spooky_hash_v1::hash128(data, seed128.0, seed128.1),
        (260_194_777_785_570_149, 10_767_014_850_931_882_430)
    );
    assert_eq!(spooky_hash_v2::hash32(data, seed32), 3_081_132_015);
    assert_eq!(
        spooky_hash_v2::hash64(data, seed64),
        9_983_443_764_423_584_525
    );
    assert_eq!(
        spooky_hash_v2::hash128(data, seed128.0, seed128.1),
        (1_942_595_675_771_470_269, 13_320_532_072_992_459_599)
    );
}
