# Debrief every backtest run

A debrief lets the user inspect the result, understand what changed and what it
cost, and decide what it shows. It is not a run log or compliance report.

Use [writing.md](../writing.md): "General maxims" for voice, "Document" for
investigation evidence, and "Test plans — what you checked, briefly" for checks.
After every attempted run, use the shape below. Omit optional sections and
inapplicable fields. A failed run still includes any findings and cost it
produced.

For a saved sample, follow [tracking-samples.md](tracking-samples.md). Store the
same debrief after its metadata header without repeating those fields.

## Template

```markdown
## Result

- **Scenario:** <what it exercises, where it stops, and whether it got there>
- **Before:** <link, for a before/after scenario>
- **Output:** <link; complete or partial>
- **Generation:** <revision and model/effort; omit if in sample header>
- **Review:** <type, count, required/optional, and models/effort; omit if in
  header>
- **Evaluation:** <checks, required/optional, source revisions, and
  models/effort; omit if in header>

<Quote each user-facing artifact in full when it is shorter than 150 words.>

## Findings

<Use as few paragraphs or bullets as needed for material review findings and
dispositions, evaluator observations, and remaining defects or limits.>

## Cost

| Production phase     | Wall time | Est. cost |
| -------------------- | --------: | --------: |
| ...                  |       ... |       ... |
| **Production total** |   **...** |   **...** |

| Production phase     | Uncached input | Cached input | Cache write |  Output |
| -------------------- | -------------: | -----------: | ----------: | ------: |
| ...                  |            ... |          ... |         ... |     ... |
| **Production total** |        **...** |      **...** |     **...** | **...** |

<Optional evaluators are measurement overhead. Report their cost separately; do
not include it in the production total.>

## Comparison

<Only when useful: compare artifacts first, then consequential setup differences
or unknowns.>

## Problems

<Only when present: explain the effect and evidence-supported cause of failures,
retries, missing checks, undeclared inputs, relevant mismatches, or missing
data.>
```

## Use the evidence

- Use `run.json` for status and configuration, and each trace's final
  `turn.completed` record for tokens. Label estimates and unavailable data. When
  `checkpoints.json` exists, show every `output-*.md` it names. Report each
  phase's added time and tokens, plus the cumulative totals, instead of
  reconstructing the boundaries by hand. These counts include required reviewer
  runs. Production cost includes the author and reviews required by the selected
  rules. Optional evaluators are measurement overhead: report them separately
  and exclude them from production totals and mode comparisons. Mark nested
  times as included in their parent; total only non-overlapping time. For a
  resumed run, name the starting artifact and separate added from cumulative
  cost.
- For `gpt-5.6-sol` long-context runs, estimate cost per million tokens at USD
  8.00 for uncached input, USD 0.80 for cached input, USD 10.00 for cache
  writes, and USD 30.00 for output. Use the selected model's published rates
  when they differ; otherwise mark cost unavailable.
- For each review round, give the material findings, the author's disposition,
  and what remained. Report concrete evaluator findings. Merge repeated
  observations; evaluators do not declare a pass, failure, or winner.
- Present only the scenario's user-facing artifacts and outputs named by
  `checkpoints.json`. Use run metadata, traces, prompts, and reports as
  evidence, but do not link them. Keep harness checks out of the result.
- Check the stop point, required review, and undeclared task evidence. Report
  only failures or unknowns, under `Problems`.
- Compare only completed runs named by the scenario, current request, or a prior
  debrief in the task. Check prompts, inputs, rules, models, effort, and
  executables. Name the comparison source, but do not inventory matches; mention
  only differences or unknowns that could explain the artifacts. If none of
  those sources has a comparable completed run, say so.
