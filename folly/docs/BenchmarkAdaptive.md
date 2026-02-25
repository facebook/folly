# Adaptive benchmark mode

## What is adaptive mode?

Traditional benchmarking runs each benchmark to completion, then reports minimum
observed time. Works on quiet systems. On noisy VMs, it fails — benchmark `A`
runs during a slow period, `B` during a fast one, distorting their relative
performance.

`--bm_mode=adaptive` interleaves short slices of all benchmarks round-robin. All
benchmarks see the same system states. It runs until results are both *stable*
(not oscillating) and *precise* (confidence interval narrow enough).

**What adaptive gives you:**
- **Within-run precision**: Benchmarks in the same run are fairly compared under
  the same system conditions.
- **Measurement stability**: When we detect the system state is oscillating, we
  sample more until the "first half" measurements agree with the "second half".
- **Faster convergence on quiet systems**: Gets precise results quicker than
  best-of mode.

**What adaptive does NOT guarantee:**
- **Absolute accuracy**: Binary layout changes, thermal effects, and long-term
  system state can still shift results.
- **Run-to-run reproducibility**: A converged run today may differ from
  tomorrow's converged run, by more than `--bm_target_precision_pct` might suggest.
  But, in practice, run-to-run results are still very close.

When adaptive mode converges, results closely agree with best-of mode's "good
runs" — the ones where you got lucky. Best-of mode trends slightly lower since
it targets minimum; adaptive targets a percentile (default: 33rd).

**In summary**: adaptive tries to take the luck out of *within-run* comparisons,
at the cost of occasionally longer runtime. For cross-run comparisons (e.g.,
before/after your commit), run both builds under the same conditions and rely on
relative differences, not absolute values.

---

## Quick start

```bash
buck2 run @//mode/opt //your:benchmark -- --bm_mode=adaptive
```

With default options, this converges quickly on quiet systems. On noisy VMs,
increase the per-benchmark timeout:

```bash
buck2 run @//mode/opt //your:benchmark -- --bm_mode=adaptive --bm_max_secs=30
```

Pass `--bm_target_precision_pct` to control the convergence threshold — the
measurement's 95% confidence interval must be narrower than this percentage of
the estimate (the default `0.4` means precise to 0.4ns out of 100ns).  Tighten
it (e.g., `0.1`) if you need finer discrimination between benchmarks, but note
that binary layout effects (see below) often dominate at that level.

*Watch out*: a "converged" measurement can still be wrong — see the next
section.

Add `--bm_verbose` for under-the-hood stats: sampling rounds,
convergence progress.

Use `--bm_min_secs` if you want higher confidence over speed — ensures each
benchmark runs at least that long.

For all flag details, see `folly/Benchmark.cpp`.

*Aside: All comparisons are with "best-of" mode. `--bm_estimate_time` computes
a genuinely different value and is slower. Its utility is unclear to the author
of adaptive mode.*

---

## What adaptive mode cannot detect

### Long-term system state changes

System-state changes lasting longer than a run (tens of seconds) are invisible.
You can have a stable, converged run that doesn't match the next one.

**For cross-run comparisons** (e.g., comparing your commit to baseline):
- Run both builds back-to-back in the same session if possible
- Repeat runs minutes to hours apart to catch long-term drift
- Look at *relative* differences between benchmarks, not absolute values
- Consider running on dedicated benchmark hardware if available

### Binary layout variations

Microbenchmarks are sensitive to instruction cache alignment. Innocuous code
changes can shift timings by several percent — far exceeding requested accuracy.

**Example**: Adding this dead code to `verboseLogFinal()` (which never runs when
`--bm_verbose` is off) consistently shifted timings under
`--bm_mode=adaptive --bm_max_secs=20 --bm_target_precision_pct=0.03`:

```cpp
std::ostringstream rawOss;
rawOss << "\n[bm_raw_samples]";
for (const auto& s : states) {
  rawOss << "\n" << s.name;
  for (const auto& sample : s.samples) {
    rawOss << "\t" << sample.first;
  }
}
LOG(INFO) << rawOss.str();
```

**This is inherent to microbenchmarking.** There is no "true" runtime for a
microbenchmark independent of its binary layout. Adaptive mode improves
*relative* comparisons within a run (different benchmarks see the same system
state), but absolute values can shift between builds.

**Implication**: Don't obsess over 0.1% accuracy targets. Binary layout effects
can easily introduce 5-20% variance. Use adaptive to get stable, precise
*comparisons*, not "perfect" absolute measurements.

---

## When your system is having a bad day

Sometimes you'll see benchmarks stuck oscillating. Even if they eventually
converge, the numbers are suspect — thermal throttling, noisy neighbors,
background tasks.

**If you see persistent `[unstable]` warnings:**
- Try again later (or on a quieter machine)
- Consider if the instability is real (does your code have variance?)
- If most benchmarks converge but one doesn't, the benchmark itself may be
  inherently noisy (data-dependent branches, allocations, etc.)
- Use `--bm_min_secs` to force longer runs that average out short-term noise
- Or just accept the timeout — at least you know your numbers are questionable

---

## Why use the 33rd percentile?

The default `--bm_target_percentile=33.3` is a heuristic, not science.

**Rationale:**
- **p0 (minimum)** gives an over-optimistic estimate. For example, it can hide
  code misbehavior caused by low-grade system contention.
- **p50 (median)** is more robust but is biased by transient system slowdowns.
- **p33** tries to be the "median of good runs" — filtering out top-end noise
  while staying realistic.

**Risk of low percentiles:** If your code has a bimodal distribution (fast path
vs. slow path), low percentiles might hide the slow path. If you care about tail
latencies, try `--bm_target_percentile=90` etc.

**Comparison to best-of mode:** Best-of mode reports p0 (minimum across all
iterations). Adaptive's p33 will trend slightly higher but is more repeatable
on noisy systems. If comparing adaptive to best-of, expect a small difference on
the same binary under quiet conditions.

---

## Understanding stability

A benchmark is **stable** when first-half and second-half estimates agree.

Split samples chronologically. Compute percentile estimate and confidence
interval for each half. Stable means:
- First-half estimate is within second-half's CI
- Second-half estimate is within first-half's CI

E.g., after 200 samples: first-half p33 = 4.2ns ± 0.1ns, second-half
p33 = 4.3ns ± 0.15ns — stable, because each estimate falls within the other's
interval.

Never drops samples. As count grows, CIs shrink, halves must eventually agree
(or timeout shows your system is too noisy).

**Intuition**: If system state is stationary, both halves converge to the same
true value. When they disagree, something changed — keep collecting until they
agree.

**Stability ≠ accuracy.** You can have a stable, precise measurement of the
"wrong" number (due to binary layout, thermal throttling that persists all run,
etc.). But an *unstable* measurement is definitely unreliable.

---

## Design choices

### Statistical robustness

We target a percentile, not a mean.  While costlier to compute than mean/stdev,
order statistics are robust to outliers, and don't assume a distribution.
**Aside:** we use neither `StreamingStats.h` (online mean/stdev) nor `TDigest.h`
(approx quantiles) because we don't have to -- see below.

Our percentile CI estimator approximates the binomial distribution, but in
practice we always have enough samples to make that a good choice.

### Low overhead

Most CPU usage should go to the benchmarks, not the harness. Choices to achieve
this:

**Rare convergence checks**: Every 150ms, not every sample.  The O(n log n)
sorting for percentile CIs is amortized, making complex machinery like
`TDigest.h` unnecessary for our use-case.

**Downsampled baselines**: Baseline (empty benchmark loop) runs every 8 rounds
(errors matter little since the baseline is cheap, ~0.3ns/iter). The suspender
baseline (cost of `BenchmarkSuspender` usage) runs every 2 rounds (expensive,
~35ns/iter).

**Measurement slices**: Each benchmark runs for ~`bm_slice_usec` per sample
(default 1ms). Long enough to amortize harness overhead and avoid timing noise
from function call boundaries.

---

## Algorithm overview

**Self-calibrating iteration counts**

Each benchmark starts at `iterCount=1` and self-adjusts after every sample so
that each measurement slice takes ~`bm_slice_usec`.  This avoids a separate
calibration phase, which was susceptible to cold-start effects (page faults,
TLB misses) that could lock `iterCount` at 1 for the entire run.

> *Note:* `bm_slice_usec` should be high enough to avoid interference from the
> harness code.  The default of 1ms is the reasonable minimum -- 100μs already
> shows serious interference.

**Interleaved sampling**

```
while not converged:
  measure baseline (every 8 rounds)
  measure suspender baseline (every 2 rounds)
  for each benchmark:
    measure calibrated iterations, record sample, recalibrate
  check convergence every 150ms
```

**Are we done?**

For each benchmark:
1. Check stability (split-half agreement)
2. Check accuracy (CI width < target %)
3. Done when both pass, or timeout

**Phase 4: Report**

Subtract baselines, pass to existing display logic.
