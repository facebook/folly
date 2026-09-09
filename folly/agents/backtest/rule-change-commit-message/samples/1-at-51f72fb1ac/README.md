# Rule-change commit-message sample

This sample records one reviewed commit message. It is not a gold answer or a
complete account of possible outputs.

- **Generation:** `51f72fb1acff34230e3e0acc32ca551125dc7ea1`; `gpt-5.6-sol`,
  high reasoning effort.
- **Review:** one required external round with a nested cold read; reviewer
  model and effort not recorded.
- **Evaluation:** optional first-read and source-aware checks from
  `51f72fb1acff34230e3e0acc32ca551125dc7ea1`, both with `gpt-5.6-sol` and high
  reasoning effort.

## Debrief

### Result

- **Scenario:** write and review a complete commit message explaining why
  mechanical shortcuts in agent rules are being replaced with a reader-effort
  standard; generation and the required review completed, and evaluation found
  the causal gap below.
- **Output:** [frozen commit message](output.md).

```text
Make reader effort the shared objective for agent rules

The rules could reward both over-compression and completionism: agents hid
necessary relationships behind terse abstractions, yet kept unnecessary facts
simply because they were true or nearby. The code rules had the same problem:
fixed preferences for inlining or extraction could obscure the invariants they
were meant to clarify.

Writing, code, and critic iteration should preserve exactly the facts and
relationships the audience needs, in the form that takes the least effort to
understand correctly. Prefer the shorter form when comprehension is equal.
Abstraction is neither inherently good nor bad; it earns its cost when it
exposes a real boundary, shared rule, or symmetry.

Test Plan:

- Docs-only.
```

### Findings

The required review replaced the draft's abstract “structure needed” with the
concrete facts-and-relationships criterion.

The first reader recovered both failure directions and the new reader-effort
standard. It also found that “Writing, code, and critic iteration” and “a real
boundary, shared rule, or symmetry” name topics instead of connecting them. The
source-aware evaluator exposed the missing causal story: guidance to compress
prose and avoid repeating the diff encouraged vague labels in place of needed
anchors, while mandatory review evidence encouraged manufactured or unnecessary
material. The required review did not catch this gap.

### Cost

| Production phase      | Wall time |  Est. cost |        Cached |    Uncached | Cache write |     Output |  Reasoning |
| --------------------- | --------: | ---------: | ------------: | ----------: | ----------: | ---------: | ---------: |
| Author                |     ~3:16 |     $2.240 |     1,603,072 |      75,073 |           0 |     11,910 |      6,190 |
| Review round 1: fresh |     ~1:36 |     $0.920 |       418,816 |      46,278 |           0 |      7,141 |      4,639 |
| Review round 1: cold  |     ~0:10 |     $0.044 |         7,424 |       3,441 |           0 |        341 |        197 |
| **Production total**  |  **5:02** | **$3.204** | **2,029,312** | **124,792** |       **0** | **19,392** | **11,026** |

The estimate uses published `gpt-5.6-sol` long-context rates of `$8.00/M` for
uncached input, `$0.80/M` for cached input, `$10.00/M` for cache writes, and
`$30.00/M` for output. Reasoning is included in output.

Optional evaluation overhead, excluded from the production total:

| Evaluator               | Wall time |  Est. cost |      Cached |   Uncached | Cache write |    Output | Reasoning |
| ----------------------- | --------: | ---------: | ----------: | ---------: | ----------: | --------: | --------: |
| First-read evaluator    |      0:17 |     $0.067 |      17,408 |      3,812 |           0 |       766 |       516 |
| Source-aware evaluator  |      0:57 |     $0.395 |      94,720 |     27,275 |           0 |     3,363 |     2,743 |
| **Evaluation overhead** |  **0:57** | **$0.462** | **112,128** | **31,087** |       **0** | **4,129** | **3,259** |

The evaluators ran concurrently, so their overhead total uses the longer wall
time.
