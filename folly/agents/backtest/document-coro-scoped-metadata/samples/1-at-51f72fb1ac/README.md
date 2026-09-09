# Coroutine metadata documentation sample

This sample records one reviewed OSS contract. It is not a gold answer or a
complete account of possible outputs.

- **Generation:** `51f72fb1acff34230e3e0acc32ca551125dc7ea1`; `gpt-5.6-sol`,
  high reasoning effort.
- **Review:** one required external round with a nested cold read; reviewer
  model and effort not recorded.
- **Evaluation:** optional contract-correctness check from
  `51f72fb1acff34230e3e0acc32ca551125dc7ea1`, with `gpt-5.6-sol` and high
  reasoning effort.

## Debrief

### Result

- **Scenario:** write and review a standalone OSS contract for attaching a
  diagnostic identity to a logical coroutine stack and defining where it
  propagates; generation and the required review completed, and evaluation found
  the contract error below.
- **Output:** [frozen contract document](output.md).

### Findings

The required review corrected two claims: address-only readers do not
universally hide metadata markers, and the raw marker is not yet a portable
external-reader interface. The final contract checker found one material error
that review missed. The document presents `now_task`, `safe_task`, and other
task wrappers as excluded from the contract, while the request says their
current absence is unintentional and an easy follow-up should support them.

Executor-bound tasks are a separate, later TODO.

### Cost

| Production phase                     | Wall time |  Est. cost |        Cached |    Uncached | Cache write |     Output |  Reasoning |
| ------------------------------------ | --------: | ---------: | ------------: | ----------: | ----------: | ---------: | ---------: |
| Author, including review time        |      7:37 |     $3.154 |     2,198,272 |     100,921 |           0 |     19,599 |      8,755 |
| Review round 1: fresh, inside author |     ~3:05 |     $1.958 |     1,162,496 |      85,014 |           0 |     11,588 |      6,809 |
| Review round 1: cold, inside fresh   |     ~0:13 |     $0.054 |         7,424 |       3,389 |           0 |        699 |        543 |
| **Production total**                 |  **7:37** | **$5.166** | **3,368,192** | **189,324** |       **0** | **31,886** | **16,107** |

The estimate uses published `gpt-5.6-sol` long-context rates of `$8.00/M` for
uncached input, `$0.80/M` for cached input, `$10.00/M` for cache writes, and
`$30.00/M` for output. Reasoning is included in output. Review time is included
in the author time, so the production total does not add it again.

Optional evaluation overhead, excluded from the production total:

| Evaluator                    | Wall time | Est. cost |  Cached | Uncached | Cache write | Output | Reasoning |
| ---------------------------- | --------: | --------: | ------: | -------: | ----------: | -----: | --------: |
| Contract-correctness checker |      1:14 |    $0.551 | 163,072 |   35,839 |           0 |  4,460 |     3,550 |
