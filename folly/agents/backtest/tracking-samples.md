# Tracking sample outputs

A sample preserves one prior run so a later comparison need not repeat that side
of the experiment. It records what happened once; it is not a gold answer, an
expected output, or a claim about the range of possible outputs.

Track samples only when the scenario already has a `samples/` directory or the
user asks to add one.

## Store a sample

Store a regular rule-backed run under `SCENARIO/samples/<number>-at-<revision>/`
and a `--no-rules` run under
`SCENARIO/samples/<number>-no-rules-at-<revision>/`. Store a run with an
explicit critic-iterate budget under
`SCENARIO/samples/<number>-c-i-<K>-at-<revision>/`. Start at 1 for each
generation revision and run style; each explicit `K` is a separate run style.
Use the first 10 characters of the `generation_revision` recorded in `run.json`.

- `output.md` is the exact user-facing artifact.
- For a checkpointed run, first copy `checkpoints.json` and every output file it
  names. Compress only the saved copy, as described below.
- `README.md` starts as a copy of the completed
  [run debrief](mandatory-debrief.md). Do not write a second account.

For the saved README:

- Replace `Result` with a title, a sentence that links the scenario and states
  the outcome, and a reproducibility header. Link the final output from the
  opening or phase list.
- Keep the applicable `Review`, `Evaluation`, `Cost vs impact`, and `Problems`
  sections unchanged.
- Omit `Comparisons`, live artifact quotes, and critic-iterate's
  `Delegated checks` tail.

The header preserves the `Generation`, `Review`, and `Evaluation` details from
`Result`, using full generation and evaluator source revisions. Also record
shared executable versions when known and any explicit `c-i-K` budget.
Evaluators may run from a later revision without changing the generation
revision. Record unknown metadata as unknown rather than reconstructing it. An
older sample without a generation revision may list known component revisions
and mark the rest unknown; new runs require one clean generation revision.

Keep traces, reviewer reports, and other process files out of the tree.

The runner does not stage samples. Keep them outside the author and reviewer
context, and read them only after the new output is frozen.

## Compress checkpoint outputs

After copying an uncompressed checkpointed run, save the generated README
section outside the sample. From `fbcode/`, run:

```bash
checkpoint_map=$(mktemp)
python3 -m folly.agents.backtest.compress_checkpoint_outputs \
  SAMPLE_DIRECTORY >"$checkpoint_map"
```

The command stores byte-identical outputs once. For each remaining change, it
uses a verified ordinary reverse diff only when that diff is less than 60% of
the full phase. It also removes stale `artifact` fields from `checkpoints.json`.
Insert the contents of `$checkpoint_map` before the debrief in `README.md`, then
remove the temporary file. Each short phase bullet links a full file or says how
far to run a displayed `apply_diffs` command. Shared command prefixes appear
once. The command starts with a later state, applies reverse diffs from left to
right, and writes the reconstructed output to stdout.

## Refresh stale samples

A revision difference alone does not make a comparison invalid. When a sample
may be stale, compare the current tree with its generation revision or listed
component revisions. Explain it concretely: "These samples may now be stale
because the prompt and writing rules changed between `abc123def4` and the
current tree."

Then report how many completed samples from the current setup are already
available, including runs preserved earlier in the task. Ask whether to keep the
existing samples, replace them with those runs, run a stated number of
additional samples, or delete the samples. Do not run or delete anything before
the user chooses.

Append a sample when another run under substantially the same setup usefully
shows run-to-run variation. When an evaluator changes, reevaluate the unchanged
`output.md` and update the evaluation metadata and findings in the saved
debrief.
