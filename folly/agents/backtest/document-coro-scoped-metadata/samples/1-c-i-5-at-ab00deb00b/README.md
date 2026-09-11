# Coroutine metadata documentation c-i-5 sample

This completed run of [Explain scoped coroutine metadata](../../README.md)
produced a reviewed OSS contract after author review and five external rounds.
It is one observed output, not a gold answer.

- **Generation:** `ab00deb00bbf0609f714348408f3ef278270f082`; `gpt-5.6-sol`,
  high reasoning effort.
- **Review:** explicit `c-i-5` budget; five external rounds, each with a nested
  cold read, using `gpt-5.6-sol` at high reasoning effort.
- **Accounting:** phase artifacts and costs generated with
  `490d70dd7f815a31e473d874ac62d07a5154d8c6`.
- **Evaluation:** optional contract checker from
  `ab00deb00bbf0609f714348408f3ef278270f082`, using `gpt-5.6-sol` at high
  reasoning effort.

## Checkpoints

Below, `apply_diffs` is short for `../../../../scripts/apply_diffs`.

- **Initial draft:**
  `apply_diffs output-author.md output-author-to-initial.diff`
- **Author review:** [output-author.md](output-author.md)
- **Review 1:**
  `apply_diffs output.md output-review5-to-review4.diff output-review4-to-review3.diff output-review3-to-review2.diff output-review2-to-review1.diff`
- **Review 2:** truncate the Review 1 command after
  `output-review3-to-review2.diff`
- **Review 3:** truncate the Review 1 command after
  `output-review4-to-review3.diff`
- **Review 4:** truncate the Review 1 command after
  `output-review5-to-review4.diff`
- **Review 5:** [output.md](output.md)

## Review

- **Author review:** Corrected how long metadata survives cancellation.
  Distinguished 32-bit attachment from unsupported 32-bit reads, and showed why
  a present zero overrides an outer value.
- **Review 1:** Stated that OSS has no supported C++ reader yet. Narrowed claims
  about propagation, marker filtering, 32-bit support, and portability, and
  removed async-stack jargon from the opening.
- **Review 2:** Made attachment and propagation the current contract; the reader
  API remains future work. Clarified that new async stacks do not inherit
  metadata and that 32-bit attachment differs from reading.
- **Review 3:** Limited propagation claims to awaited tasks on the same
  async-stack chain. Removed unsupported reader internals and the cost section.
- **Review 4:** Restored the safety boundary: read on the owning thread or while
  the target is stopped, never concurrently from another running thread.
  Separated portable attachment from platform-dependent reader availability.
- **Review 5:** Limited attachment to builds where Folly enables coroutines.

## Evaluation

- The contract checker found no claim that conflicts with the staged sources.
  The document treats the public reader as unfinished instead of presenting
  older internal APIs as usable.

## Cost vs impact

| Phase                | Main effect                                               | Changed words vs prior |   Wall time |    Est. cost |
| -------------------- | --------------------------------------------------------- | ---------------------: | ----------: | -----------: |
| Initial draft        | Initial artifact                                          |                      - |      2:18.5 |      $1.6533 |
| Author review        | Corrected lifetime and 32-bit claims                      |                   9.3% |      0:44.4 |      $0.4934 |
| Review 1 fixes       | Stated no supported public reader exists; narrowed claims |                  17.5% |      4:28.0 |      $3.7442 |
| Review 2 fixes       | Scoped the contract to attachment and propagation         |                  10.4% |      3:15.8 |      $2.9181 |
| Review 3 fixes       | Removed unsupported reader details; narrowed propagation  |                  19.3% |      3:24.0 |      $3.2442 |
| Review 4 fixes       | Restored the reader-safety rule                           |                   5.6% |      3:30.0 |      $3.8441 |
| Review 5 fixes       | Limited attachment to coroutine-enabled builds            |                   3.3% |      3:56.3 |      $4.0503 |
| **Production total** |                                                           |                      - | **21:37.0** | **$19.9476** |

| Phase                | Uncached input |   Cached input | Cache write |     Output |
| -------------------- | -------------: | -------------: | ----------: | ---------: |
| Initial draft        |         78,857 |        931,840 |           0 |      9,234 |
| Author review        |          7,317 |        405,760 |           0 |      3,676 |
| Review 1 fixes       |        114,439 |      2,757,376 |           0 |     20,761 |
| Review 2 fixes       |         97,991 |      2,069,504 |           0 |     15,952 |
| Review 3 fixes       |        110,173 |      2,285,312 |           0 |     17,818 |
| Review 4 fixes       |        106,522 |      3,143,424 |           0 |     15,905 |
| Review 5 fixes       |        107,945 |      3,436,544 |           0 |     14,582 |
| **Production total** |    **623,244** | **15,029,760** |       **0** | **97,928** |

| Optional evaluator |  Wall time |   Est. cost | Uncached input | Cached input | Cache write |    Output |
| ------------------ | ---------: | ----------: | -------------: | -----------: | ----------: | --------: |
| Contract checker   |     1:09.2 |     $0.4344 |         25,630 |      134,656 |           0 |     4,056 |
| **Overhead**       | **1:09.2** | **$0.4344** |     **25,630** |  **134,656** |       **0** | **4,056** |

Estimated cost uses published `gpt-5.6-sol` rates for all production tokens.
