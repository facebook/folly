# Coroutine metadata no-rules sample

This sample records one OSS contract produced from the scenario prompt and
inputs without the selected agent rules, for comparison with a regular
rule-backed sample's artifact quality and cost. It is not a gold answer or a
complete account of possible outputs.

- **Generation:** `716c4a5bf9b27f8c6c94398601badb5a7eb5b114`; `gpt-5.6-sol`,
  high reasoning effort; no rules.
- **Review:** none; no review was required in no-rules mode.
- **Evaluation:** optional contract-correctness check run from the generation
  revision with `gpt-5.6-sol` and high reasoning effort.
- **Executables:** Codex path `/usr/local/bin/codex_cli/codex`; version unknown.

## Debrief

### Result

- **Scenario:** write a standalone OSS contract for coroutine-scoped metadata;
  reached the no-rules stop point.
- **Output:** [contract document](output.md), complete for this scenario.

### Findings

The factual checker found no factual errors or critical omissions. The document
correctly separates absent metadata from a present zero value, explains that a
new detached async stack does not inherit the caller's metadata, and preserves
the current portability and profiler limitations.

### Cost

| Production phase     | Wall time |  Est. cost |      Cached |   Uncached | Cache write |    Output | Reasoning |
| -------------------- | --------: | ---------: | ----------: | ---------: | ----------: | --------: | --------: |
| Author               |      2:12 |     $0.949 |     351,488 |     51,105 |           0 |     8,617 |     5,137 |
| **Production total** |  **2:12** | **$0.949** | **351,488** | **51,105** |       **0** | **8,617** | **5,137** |

The estimate uses published `gpt-5.6-sol` long-context rates of `$8.00/M` for
uncached input, `$0.80/M` for cached input, `$10.00/M` for cache writes, and
`$30.00/M` for output. Reasoning is included in output.

Optional evaluation overhead, excluded from the production total:

| Evaluator                    | Wall time | Est. cost |  Cached | Uncached | Cache write | Output | Reasoning |
| ---------------------------- | --------: | --------: | ------: | -------: | ----------: | -----: | --------: |
| Contract-correctness checker |      1:02 |    $0.424 | 115,968 |   24,487 |           0 |  4,515 |     3,782 |
