# Coroutine metadata documentation sample

This sample records one reviewed OSS contract from before the reader-focused
rule revision. It is not a gold answer or a complete account of possible
outputs.

- **Generation:** `ea38f9a592ea4c92fd7c1af9fbb1f2627e18c11c`; `gpt-5.6-sol`,
  high reasoning effort.
- **Review:** one required external round with a nested cold read; reviewer
  model and effort not recorded.
- **Evaluation:** optional contract-correctness check run from the generation
  revision with `gpt-5.6-sol` and high reasoning effort.

## Debrief

### Result

- **Scenario:** write and review a standalone OSS contract for coroutine-scoped
  metadata; reached its stop point.
- **Output:** [contract document](output.md), complete for this scenario.

### Findings

The first draft began with the logical async stack without saying why Folly
needs one. The cold reader and external reviewer also found that a new Folly
user needed to know which operation starts a new stack. The author revised the
opening to explain that suspension hides coroutine ancestry from the native
stack and named `AsyncScope::add()` as a detached launch.

The author also applied the reviewer's requests to separate intended Windows,
macOS, libc++, and libstdc++ support from the lack of cross-platform test
evidence, and to label the 64-bit reader interface as planned. The factual
checker found no factual errors or critical omissions in the final document.

### Cost

| Phase                                | Wall time |         Input | Uncached input |     Output |  Reasoning |
| ------------------------------------ | --------: | ------------: | -------------: | ---------: | ---------: |
| Author, including review             |      6:45 |     2,308,194 |        107,618 |     18,916 |      9,218 |
| Review round 1: fresh, inside author |     ~2:37 |     1,102,531 |         87,747 |     11,153 |      5,647 |
| Review round 1: cold, inside fresh   |     ~0:17 |        10,863 |          3,439 |        881 |        714 |
| Contract-correctness evaluator       |      0:51 |       104,941 |         20,717 |      3,673 |      2,783 |
| **Total**                            |  **7:36** | **3,526,529** |    **219,521** | **34,623** | **18,362** |

Review times are included in the author time; the total counts their tokens but
not their wall time twice. Times are rounded independently.
