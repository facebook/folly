# Recover a rule change's purpose

This scenario preserves a commit-message failure. The conversation and diff
contained a clear reason for changing the shared rules, but the message reduced
each edited area to a separate clause and left that reason behind. A reader who
had not seen the drafting history got a list of changes instead of the story
that connected them.

The author writes a complete replacement commit message from the original task,
conversation, and diff. A useful result explains what problem the rule change
solves and connects the edits to that purpose. It does not inventory every rule
hunk or assume the reader has the source packet.

The run ends after the author follows the staged rules, writes the complete
replacement commit message to `output.md`, and completes any review those rules
require (unless in no-rules mode).

After the run, confirm that `output.md` is nonempty and that the trace contains
no undeclared reads.

The [optional evaluator checks](eval/SETUP.md) record what a fresh reader can
recover from the message, then compare its rationale with the source packet.
Never expose them to the author.

Run this scenario with the [parent runner instructions](../README.md), using
this directory in place of `SCENARIO`.
