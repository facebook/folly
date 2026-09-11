# Debrief every backtest run

A debrief lets the user inspect the result, understand what changed and what it
cost, and decide what it shows. It is not a run log or compliance report.

Use [writing.md](../writing.md): "General maxims" for voice, "Document" for
investigation evidence, and "Test plans — what you checked, briefly" for checks.
After every attempted run, use the shape below. Omit optional sections and
inapplicable fields. A failed run still includes any findings and cost it
produced.

## Template

```markdown
## Result

- **Scenario:** <what it exercises, where it stops, and whether it got there>
- **Output:** <link to output.md, or say none; complete or partial>
- **Generation:** <revision and model/effort>
- **Review:** <type, count, required/optional, and models/effort>
- **Evaluation:** <checks, required/optional, source revisions, and
  models/effort>

## Review

<For each author or external review that changed the output, state the concrete
change and anything still wrong. If a review made no change, say so in one short
sentence. Use one bullet per phase. Omit unused suggestions unless they explain
a remaining limit.>

## Evaluation

<State each material finding and its consequence in plain language. Include how
the evaluator worked only when that limits the finding. Use one bullet per
finding.>

## Cost vs impact

<Generated production cost-and-impact and token tables. Without checkpoints,
report one production row with its main effect, wall time, estimated cost, and
token counts.>

| Evaluator    | Wall time | Est. cost | Uncached input | Cached input | Cache write |  Output |
| ------------ | --------: | --------: | -------------: | -----------: | ----------: | ------: |
| ...          |       ... |       ... |            ... |          ... |         ... |     ... |
| **Overhead** |   **...** |   **...** |        **...** |      **...** |     **...** | **...** |

## Problems

<Only when present: explain the effect and evidence-supported cause of failures,
retries, missing checks, undeclared inputs, relevant mismatches, or missing
data.>

## Comparisons

<Live debrief only, when useful: link the prior output, compare artifacts first,
then consequential setup differences or unknowns. This section is always last.>
```

After the run-specific debrief is complete, the live response may insert a
user-facing artifact after `Result` when it is shorter than 150 words and the
quote helps the immediate discussion. Keep it before `Comparisons`.

## Use the evidence

- Use `run.json` for status and configuration, and each trace's final
  `turn.completed` record for tokens. Label estimates and unavailable data. Link
  only `output.md` under `Result`; say when it is partial or missing. Checkpoint
  artifacts remain in the output directory for closer inspection.
- When `checkpoints.json` exists, run
  `python3 -m folly.agents.backtest.debrief_tables RUN_DIRECTORY` and use its
  production tables. Replace each `TODO` with a short, evidence-based account of
  that phase's main effect; use `No artifact change` when applicable. Changed
  words show scale, not quality. For a resumed run, name the starting artifact
  and separate added from cumulative cost.
- Production cost includes the author and reviews required by the selected
  rules. Evaluators are measurement overhead: report them separately and exclude
  them from production totals and mode comparisons. Mark nested times as
  included in their parent; total only non-overlapping time.
- Under `Review`, report what changed in the output after author review and each
  external round. Omit reviewer requests and rejections that did not change the
  output unless they explain a remaining limit.
- Before reporting, apply [critic-iterate.md](../critic-iterate.md)'s
  author-side General Cycle to all authored debrief prose, using its sources.
  This does not add an external review round. Make each material finding,
  disposition, and limit easy to locate; split dense prose into labeled bullets
  when that scans faster. Write for a reader who has not seen the prompts,
  source packet, or review reports. Give each substantive factual or correctness
  defect its own bullet beginning `**Correctness:**`; do not bury it among style
  observations.
- Put evaluator results under `Evaluation`. Lead with the concrete finding and
  what it means for the artifact. Merge repeated observations, and omit
  evaluator process unless it limits confidence. Evaluators do not declare a
  pass, failure, or winner.
- Use run metadata, traces, prompts, and reports as evidence, but do not link
  them. Keep harness checks out of the result.
- Check the stop point, required review, and evidence read even though the task
  did not declare it. Report failures or unknowns that limit interpretation
  under `Problems`. Metadata already marked unknown in the header need not be
  repeated.
- Put comparisons only in the final `Comparisons` section. Compare only
  applicable tracked samples and completed runs named by the scenario, current
  request, or a prior debrief in the task. Check prompts, inputs, rules, models,
  effort, and executables. Name and link the comparison source, but do not
  inventory matches; mention only differences or unknowns that could explain the
  artifacts. If a comparison was requested or expected but none is available,
  say so.
