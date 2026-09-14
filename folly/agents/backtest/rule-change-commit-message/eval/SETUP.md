# Run the evaluator checks

Use the [shared evaluator runner](../../README.md#run-an-evaluator) for both
optional checks, with a separate run for each.

## First read

In one evaluator run, stage only:

- the frozen `output.md` as `candidate.md`; and
- `prompt-first-read.md` at the task root.

Run `prompt-first-read.md` as the prompt to record the causal account a new
maintainer can recover before seeing the diff or source packet.

## Source-aware check

In a separate evaluator run, stage:

- the frozen output as `candidate.md`;
- `prompt-check-rationale.md` at the task root; and
- the scenario's `input/request.md`, `input/requirements.md`,
  `input/conversation.md`, `input/commit-state.md`, and `input/change.patch`
  under `evidence/`.

Run `prompt-check-rationale.md` as the prompt.
