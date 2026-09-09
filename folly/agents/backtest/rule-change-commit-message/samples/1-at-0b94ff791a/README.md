# Rule-change commit-message sample

This sample records one reviewed commit message from before the reader-focused
rule revision, using the revised scenario evidence. It is not a gold answer or a
complete account of possible outputs.

- **Generation:** `0b94ff791a07d7694ce3e6316a92b4a5d4576e13`; `gpt-5.6-sol`,
  high reasoning effort.
- **Review:** one required external round with a nested cold read; reviewer
  model and effort not recorded.
- **Evaluation:** optional first-read comprehension and source-aware rationale
  checks, both run from the generation revision with `gpt-5.6-sol` and high
  reasoning effort.

## Debrief

### Result

- **Scenario:** write and review a complete commit message explaining why
  mechanical shortcuts in agent rules are being replaced with a reader-effort
  standard; the workflow completed its required review.
- **Output:** [commit message](output.md), preserved exactly; known evaluator
  gaps remain below.

```text
Make reader effort the objective across Folly agent rules

Some rules still used brevity and call counts as shortcuts for readability. They
could therefore favor a smaller artifact while forcing readers to reconstruct
missing relationships.

Set one standard: give the intended audience exactly the facts and relationships
needed for the artifact's purpose with the least reading effort. Brevity wins
only when comprehension is equal. Abstraction has no default direction; weigh
the clarity it adds against the indirection it creates.

Test Plan:

- Docs-only.
```

### Findings

The message explains how brevity and call-count shortcuts can make an artifact
smaller while increasing reader work. During the required review, the author
fixed an unsupported claim that agents generally followed those shortcuts. The
nested cold reader found no additional problem in that pre-review draft.

The first-read evaluator recovered the problem and new principle, but had to
infer why the old safeguards were useful and how abstraction relates to the
motivating failure. The source-aware evaluator reported two remaining gaps:

- The message omits the opposite failure from unnecessary detail and review
  rules that reward invented alternatives or exhaustive history.
- “Brevity wins only when comprehension is equal” can still favor wording that
  takes more effort to understand.

### Cost

| Phase                                | Wall time |         Input | Uncached input |     Output |  Reasoning |
| ------------------------------------ | --------: | ------------: | -------------: | ---------: | ---------: |
| Author, including review             |      5:35 |     2,195,777 |        161,089 |     13,401 |      7,115 |
| Review round 1: fresh, inside author |     ~1:39 |       324,415 |         84,799 |      6,260 |      4,271 |
| Review round 1: cold, inside fresh   |     ~0:17 |        21,835 |          6,987 |        566 |        296 |
| First-read evaluator                 |      0:17 |        20,386 |          5,538 |        541 |        288 |
| Source-aware evaluator               |      0:56 |        62,816 |         14,688 |      3,300 |      2,811 |
| **Total**                            |  **6:31** | **2,625,229** |    **273,101** | **24,068** | **14,781** |

The two evaluators ran concurrently. Review times are included in the author
time; the total counts their tokens but not their wall time twice. Author and
evaluator times were measured directly; nested-review times are approximate.
