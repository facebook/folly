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

use cxx::ExternType;
use cxx::type_id;
pub use root::folly;
pub use root::folly::TDigest;

unsafe impl ExternType for TDigest {
    type Id = type_id!("folly::TDigest");
    type Kind = cxx::kind::Opaque;
}

#[cxx::bridge]
pub mod bridge {
    #[namespace = "folly"]
    unsafe extern "C++" {
        include!("folly/stats/TDigest.h");

        type TDigest = crate::TDigest;

        fn maxSize(self: &TDigest) -> usize;
        fn mean(self: &TDigest) -> f64;
        fn sum(self: &TDigest) -> f64;
        fn count(self: &TDigest) -> f64;
        fn min(self: &TDigest) -> f64;
        fn max(self: &TDigest) -> f64;
        fn empty(self: &TDigest) -> bool;
        fn estimateQuantile(self: &TDigest, q: f64) -> f64;
        // getCentroids() implemented as extract_centroid_means() and extract_centroid_weights()
    }

    #[namespace = "facebook::folly_rust::tdigest"]
    unsafe extern "C++" {
        include!("folly/rust/tdigest/tdigest.h");

        // static functions
        fn new_tdigest(max_size: usize) -> UniquePtr<TDigest>;
        fn new_tdigest_vec() -> UniquePtr<CxxVector<TDigest>>;
        fn add_tdigest(vector: Pin<&mut CxxVector<TDigest>>, tdigest: &TDigest) -> bool;
        fn new_tdigest_with_unsorted_values(
            max_size: usize,
            unsorted_values: &Vec<f64>,
        ) -> UniquePtr<TDigest>;
        fn new_tdigest_with_centroids(
            centroid_means: &Vec<f64>,
            centroid_weights: &Vec<f64>,
            sum: f64,
            count: f64,
            max_val: f64,
            min_val: f64,
            max_size: usize,
        ) -> UniquePtr<TDigest>;
        fn merge(tdigest_vec: &CxxVector<TDigest>) -> UniquePtr<TDigest>;
        fn extract_centroid_means(tdigest: &TDigest) -> Vec<f64>;
        fn extract_centroid_weights(tdigest: &TDigest) -> Vec<f64>;
    }

    impl UniquePtr<TDigest> {}
    impl CxxVector<TDigest> {}
}

#[cfg(test)]
mod tests {
    use rand::seq::SliceRandom;
    use rand::thread_rng;

    use super::bridge::*;

    #[test]
    fn test_new_tdigest() {
        let tdigest = new_tdigest(22);
        assert!(tdigest.maxSize() == 22);
    }

    #[test]
    fn test_empty() {
        let tdigest = new_tdigest(22);
        assert!(tdigest.empty() == true);
    }

    // Clone of https://fburl.com/code/ycyckj20
    #[test]
    fn test_basic() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        for i in 1..=100 {
            unsorted_values.push(i as f64);
        }
        let tdigest = new_tdigest_with_unsorted_values(100, &unsorted_values);
        assert_eq!(100_f64, tdigest.count());
        assert_eq!(5050_f64, tdigest.sum());
        assert_eq!(50.5_f64, tdigest.mean());
        assert_eq!(1_f64, tdigest.min());
        assert_eq!(100_f64, tdigest.max());
        assert_eq!(1_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!((2.0 - 0.5) as f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(50.375_f64, tdigest.estimateQuantile(0.5_f64));
        assert_eq!((100.0 - 0.5) as f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(100_f64, tdigest.estimateQuantile(0.999_f64));
    }

    // Based on https://fburl.com/code/3c52e6gz
    #[test]
    fn test_merge() {
        let mut tdigest_vec = new_tdigest_vec();

        let mut uv1: Vec<f64> = Vec::new();
        for i in 1..=100 {
            uv1.push(i as f64);
        }
        add_tdigest(
            tdigest_vec.pin_mut(),
            &new_tdigest_with_unsorted_values(100, &uv1),
        );

        let mut uv2: Vec<f64> = Vec::new();
        for i in 101..=200 {
            uv2.push(i as f64);
        }
        add_tdigest(
            tdigest_vec.pin_mut(),
            &new_tdigest_with_unsorted_values(100, &uv2),
        );

        let tdigest = merge(&tdigest_vec);
        assert_eq!(200_f64, tdigest.count());
        assert_eq!(20100_f64, tdigest.sum());
        assert_eq!(100.5_f64, tdigest.mean());
        assert_eq!(1_f64, tdigest.min());
        assert_eq!(200_f64, tdigest.max());
        assert_eq!(1_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!((4.0 - 1.5) as f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(100.25_f64, tdigest.estimateQuantile(0.5_f64));
        assert_eq!((200.0 - 1.5) as f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(200_f64, tdigest.estimateQuantile(0.999_f64));
    }

    // Based on https://fburl.com/code/e230z6fm
    #[test]
    fn test_merge_small() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        unsorted_values.push(1_f64);
        let tdigest = new_tdigest_with_unsorted_values(100, &unsorted_values);
        assert_eq!(1_f64, tdigest.count());
        assert_eq!(1_f64, tdigest.sum());
        assert_eq!(1_f64, tdigest.mean());
        assert_eq!(1_f64, tdigest.min());
        assert_eq!(1_f64, tdigest.max());
        assert_eq!(1_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!(1_f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(1_f64, tdigest.estimateQuantile(0.5_f64));
        assert_eq!(1_f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(1_f64, tdigest.estimateQuantile(0.999_f64));
    }

    // Based on https://fburl.com/code/k6xfn3nv
    #[test]
    fn test_merge_large() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        for i in 1..=1000 {
            unsorted_values.push(i as f64);
        }
        let tdigest = new_tdigest_with_unsorted_values(100, &unsorted_values);
        assert_eq!(1000_f64, tdigest.count());
        assert_eq!(500500_f64, tdigest.sum());
        assert_eq!(500.5_f64, tdigest.mean());
        assert_eq!(1_f64, tdigest.min());
        assert_eq!(1000_f64, tdigest.max());
        assert_eq!(1.5_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!(10.5_f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(500.25_f64, tdigest.estimateQuantile(0.5_f64));
        assert_eq!(990.25_f64 as f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(999.5_f64, tdigest.estimateQuantile(0.999_f64));
    }

    // Based on https://fburl.com/code/qevprhj8
    #[test]
    fn test_merge_large_as_digests() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        for i in 1..=1000 {
            unsorted_values.push(i as f64);
        }

        let mut tdigest_vec = new_tdigest_vec();
        let mut rng = thread_rng();
        unsorted_values.shuffle(&mut rng);

        for i in 0..10 {
            let start_index = i * 100;
            let end_index = (i + 1) * 100;
            let v = unsorted_values[start_index..end_index].to_vec();
            add_tdigest(
                tdigest_vec.pin_mut(),
                &new_tdigest_with_unsorted_values(100, &v),
            );
        }

        let tdigest = merge(&tdigest_vec);
        assert_eq!(1000_f64, tdigest.count());
        assert_eq!(500500_f64, tdigest.sum());
        assert_eq!(500.5_f64, tdigest.mean());
        assert_eq!(1_f64, tdigest.min());
        assert_eq!(1000_f64, tdigest.max());
        assert_eq!(1.5_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!(10.5_f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(990.25_f64 as f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(999.5_f64, tdigest.estimateQuantile(0.999_f64));
    }

    // Based on https://fburl.com/code/3cbv9vba
    #[test]
    fn test_negative_values() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        for i in 1..=100 {
            unsorted_values.push(i as f64);
            unsorted_values.push((i * -1) as f64);
        }
        let tdigest = new_tdigest_with_unsorted_values(100, &unsorted_values);
        assert_eq!(200_f64, tdigest.count());
        assert_eq!(0_f64, tdigest.sum());
        assert_eq!(0_f64, tdigest.mean());
        assert_eq!(-100_f64, tdigest.min());
        assert_eq!(100_f64, tdigest.max());
        assert_eq!(-100_f64, tdigest.estimateQuantile(0_f64));
        assert_eq!(-100_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!(-98.5_f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(98.5_f64 as f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(100_f64, tdigest.estimateQuantile(0.999_f64));
        assert_eq!(100_f64, tdigest.estimateQuantile(1_f64));
    }

    // Based on https://fburl.com/code/l9rziydy
    #[test]
    fn test_negative_values_merge_digests() {
        let mut tdigest_vec = new_tdigest_vec();

        let mut uv1: Vec<f64> = Vec::new();
        let mut uv2: Vec<f64> = Vec::new();
        for i in 1..=100 {
            uv1.push(i as f64);
            uv2.push((i * -1) as f64);
        }
        add_tdigest(
            tdigest_vec.pin_mut(),
            &new_tdigest_with_unsorted_values(100, &uv1),
        );
        add_tdigest(
            tdigest_vec.pin_mut(),
            &new_tdigest_with_unsorted_values(100, &uv2),
        );

        let tdigest = merge(&tdigest_vec);
        assert_eq!(200_f64, tdigest.count());
        assert_eq!(0_f64, tdigest.sum());
        assert_eq!(0_f64, tdigest.mean());
        assert_eq!(-100_f64, tdigest.min());
        assert_eq!(100_f64, tdigest.max());
        assert_eq!(-100_f64, tdigest.estimateQuantile(0_f64));
        assert_eq!(-100_f64, tdigest.estimateQuantile(0.001_f64));
        assert_eq!(-98.5_f64, tdigest.estimateQuantile(0.01_f64));
        assert_eq!(98.5_f64, tdigest.estimateQuantile(0.99_f64));
        assert_eq!(100_f64, tdigest.estimateQuantile(0.999_f64));
        assert_eq!(100_f64, tdigest.estimateQuantile(1_f64));
    }

    // Based on https://fburl.com/code/css7wgm2
    #[test]
    fn test_construct_freom_centroids() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        for i in 1..=100 {
            unsorted_values.push(i as f64);
        }
        let t1 = new_tdigest_with_unsorted_values(100, &unsorted_values);
        let t2 = new_tdigest_with_centroids(
            &extract_centroid_means(&t1),
            &extract_centroid_weights(&t1),
            t1.sum(),
            t1.count(),
            t1.max(),
            t1.min(),
            100,
        );

        assert_eq!(t1.sum(), t2.sum());
        assert_eq!(t1.count(), t2.count());
        assert_eq!(t1.min(), t2.min());
        assert_eq!(t1.max(), t2.max());
        assert_eq!(
            extract_centroid_means(&t1).len(),
            extract_centroid_means(&t2).len()
        );
        assert_eq!(
            extract_centroid_weights(&t1).len(),
            extract_centroid_weights(&t2).len()
        );

        let t3 = new_tdigest_with_centroids(
            &extract_centroid_means(&t1),
            &extract_centroid_weights(&t1),
            t1.sum(),
            t1.count(),
            t1.max(),
            t1.min(),
            extract_centroid_means(&t1).len() - 1,
        );

        assert_eq!(t1.sum(), t3.sum());
        assert_eq!(t1.count(), t3.count());
        assert_eq!(t1.min(), t3.min());
        assert_eq!(t1.max(), t3.max());
        assert_ne!(
            extract_centroid_means(&t1).len(),
            extract_centroid_means(&t3).len()
        );
        assert_ne!(
            extract_centroid_weights(&t1).len(),
            extract_centroid_weights(&t3).len()
        );
    }

    // Based on https://fburl.com/code/89diizxv
    #[test]
    fn test_large_outlier() {
        let mut unsorted_values: Vec<f64> = Vec::new();
        for i in 0..19 {
            unsorted_values.push(i as f64);
        }
        unsorted_values.push(1000000_f64);

        let tdigest = new_tdigest_with_unsorted_values(100, &unsorted_values);
        assert!(tdigest.estimateQuantile(0.5_f64) < tdigest.estimateQuantile(0.9_f64));
    }
}
