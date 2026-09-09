# Run the evaluator checks

Run both optional checks in fresh sessions with no personal, project, or
scenario rules. Give compared outputs the same evaluator model and effort. Do
not expose evaluator prompts or outputs to the scenario author. For each check,
record the evaluator source revision, model, effort, and token use with the run
artifacts.

## First read

In one clean workdir, stage only:

- the frozen `output.md` as `candidate.md`; and
- `prompt-first-read.md` at the workdir root.

Run `prompt-first-read.md` as the prompt to record the causal account a new
maintainer can recover before seeing the diff or source packet.

## Source-aware check

In a separate workdir, stage:

- the frozen output as `candidate.md`;
- `prompt-check-rationale.md` at the workdir root; and
- the scenario's `input/request.md`, `input/requirements.md`,
  `input/conversation.md`, `input/commit-state.md`, and `input/change.patch`
  under `evidence/`.

Run `prompt-check-rationale.md` as the prompt.
