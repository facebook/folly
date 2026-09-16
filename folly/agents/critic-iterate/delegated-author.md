Delegating authorship does not satisfy the fresh-review requirement. Before
launching a required external review, the delegated author loads and follows
`{FA}/critic-iterate/run-review.md`. The delegated author normally runs those
checks; a non-author ambient does not add another after they pass. If delegated
authorship fails before the draft converges, stop and report it; never take over
the writing. If the draft converged and only its review failed, the top-level
orchestrator may recover the infrastructure and rerun the required review on
that unchanged draft.

When delegating:

- **Source.** Forward source documents verbatim. Never pre-digest them into a
  summarized or bulleted "must-cover" list — Codex compresses. For conversation
  context, quote key user inputs verbatim with minimal glue and give Codex the
  session JSONL path for lookup. Copy it into Codex's workdir when the reviewer
  cannot read the original path.
- **Write access.** Authorship needs a writable environment. Use the caller's
  existing environment; do not reuse the reviewer wrapper or add an outer
  sandbox.
- **Outputs.** Unlike the reviewer, an author writes files: tell it to put the
  draft in `draft.md` and add each pass artifact to `passes.md` under a unique
  numbered heading before starting the next pass. Pass no `-o`.
