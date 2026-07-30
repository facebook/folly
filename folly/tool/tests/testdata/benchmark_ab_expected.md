**Output:** `<OUT>`

**Target patterns:** //folly/test/... (1 target)

**Before:** `1111111111111111111111111111111111111111`

**After:** `2222222222222222222222222222222222222222`

**Targets:** `fbcode//folly/test:bench`

**Options:** `@mode/dev`; adaptive p33.3 (17s max/benchmark); thresholds for estimated Δ:

- hi-pri `>=1.0ns` and `>=10.0%`
- lo-pri `>=0.5ns` and `>=5.0%`

## Needs attention: Benchmark run did not converge after 1 try

- Round 1, `fbcode//folly/test:bench` (before); [see logs](round_1/before_bench).

## Benchmarks present only in "after" runs

- `fbcode//folly/test:bench` (rounds 2-4): `only_after`

## Benchmarks present only in "before" runs

- `fbcode//folly/test:bench` (rounds 2-4): `only_before`

# Results

Each result line starts with the median "before" timing and a Hodges-Lehmann
estimate of Δ ns.  We get one adaptive p33.3 timing per contributing run; the
estimate is the median of every "after" minus "before" combination (e.g., 25
differences from 5+5 round timings).  It also shows (Δ%) when median "before"
exceeds 2ns.

A benchmark appears in the lo-pri or hi-pri section when estimated Δ meets
both that section's nanosecond and percentage thresholds.

The comma-separated `before±Δ` pairs show whether the change is consistent
across rounds.  They are sorted by `before` timing, not by run order.
Parentheses mark a pair whose Δ missed a section threshold.

Within each priority section, rows are sorted by estimated Δ, smallest first.

## High-priority wins

| estimate | benchmark | target | before ± Δ |
| ---: | --- | --- | ---: |
| 15.0-2.0ns (-13.3%) | same_name (win.cpp) | fbcode//folly/test:bench | 10.0-2.0, 15.0-2.0, *(20.0-0.5)* |

## Low-priority wins

| estimate | benchmark | target | before ± Δ |
| ---: | --- | --- | ---: |
| 15.0-0.8ns (-5.3%) | low_win | fbcode//folly/test:bench | 10.0-0.8, 15.0-0.8, *(20.0-0.8)* |

## Low-priority regressions

| estimate | benchmark | target | before ± Δ |
| ---: | --- | --- | ---: |
| 15.0+0.8ns (+5.3%) | low_loss | fbcode//folly/test:bench | 10.0+0.8, 15.0+0.8, *(20.0+0.8)* |

## High-priority regressions

| estimate | benchmark | target | before ± Δ |
| ---: | --- | --- | ---: |
| 1.5+1.2ns | same_name (loss.cpp) | fbcode//folly/test:bench | 1.0+2.0, 1.5+1.5, *(2.0+0.2)* |
