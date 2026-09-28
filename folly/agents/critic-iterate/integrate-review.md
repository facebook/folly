# Integrate a fresh review

The General Cycle's no-edit rule governs author-side passes. Before acting on
review, mark any useful proposal outside the user's agreed task as
`SCOPE_EXPANSION`. Without user approval, do not apply it or let it block
completion.

For prose, compare the candidate with the independent frame by reader model and
structure, not sentence by sentence. Neither is preferred; keep the structure
that better serves the reader.

For external prose review, classify every remaining fresh-reviewer finding
before editing; its response already integrates the cold report:

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

Record dispositions only in the accountability artifact; summarize
`SCOPE_EXPANSION` items in the final debrief. For other artifacts, take the
better version, merge, or apply its findings.
