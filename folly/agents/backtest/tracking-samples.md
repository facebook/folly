# Tracking sample outputs

A sample preserves one prior run so a later comparison need not repeat that side
of the experiment. It records what happened once; it is not a gold answer, an
expected output, or a claim about the range of possible outputs.

Track samples only when the scenario already has a `samples/` directory or the
user asks to add one.

## Store a sample

Store each run under `SCENARIO/samples/<number>-at-<revision>/`. Start at 1 for
each generation revision. Use the first 10 characters of the
`generation_revision` recorded in `run.json`.

- `output.md` is the exact user-facing artifact.
- `README.md` is a short metadata header followed by the same debrief shown to
  the user. Do not regenerate or restyle the debrief for storage.

The header records the full generation revision and the source revision of each
evaluator. Evaluators may run from a later revision without changing the
generation revision. Record unknown metadata as unknown rather than
reconstructing it. Keep traces, reviewer reports, and other process files out of
the tree.

An older sample without a generation revision may list known component revisions
and mark the rest unknown. New runs still require one clean generation revision.

The runner does not stage samples. Keep them outside the author and reviewer
context, and read them only after the new output is frozen.

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
