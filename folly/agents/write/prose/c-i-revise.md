# Revise prose after fresh review

Compare the candidate with the independent frame by reader model and structure,
not sentence by sentence. Neither is preferred; keep the structure that better
serves the reader.

Classify every remaining fresh-reviewer finding before editing; its response
already integrates the cold report:

- `MUST_TAKE`: must be fixed; leaving it would materially harm correctness or
  the reader's task.
- `MINOR`: worth fixing, but the artifact still works without it.
- `REJECTED`: wrong, already addressed, or net-negative.

A material error, missed requirement, wrong action, or reader blocker is
`MUST_TAKE`. Escalate if the allowed evidence cannot repair a material finding.
Before applying a reviewer finding that would change the artifact, verify its
factual claims.

If a `MUST_TAKE` finding changes a proposed fix's behavior, invalidates a
fallback, or exposes a deciding correctness assumption, reapply
`design-vetting.md` before editing. Pure presentation or citation changes do not
trigger this.

Treat findings as diagnoses, not patch instructions. Resolve accepted findings
through a coherent whole-candidate revision, not one-by-one patches.

Record dispositions only in the accountability artifact; summarize
`SCOPE_EXPANSION` items in the final debrief.
