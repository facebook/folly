# Prepare a fresh review

For non-prose, require the fresh reviewer to apply the same evidence and design
checks before opening the artifact, then verify material claims introduced by it
and return an alternative or findings.

**Fresh reviewer inputs.** Build its prompt from only:

- **A short task note:**
  - For non-prose, identify it as non-prose and state its goal and intended
    users.
  - For an investigation, include the question the reviewer must answer unless
    the artifact's goal already states it.
  - Include external facts or requirements needed to verify correctness when
    allowed sources cannot supply them. State them as verification inputs, not
    required artifact wording. Do not supply derivable conclusions or prior
    critique; if critique exposed a fact or requirement, include only that.
  - Derive the reader's starting knowledge from the artifact's final location.
    For stacked commits, review each commit as it will appear after its
    predecessors land. Do not assume the reader has read their messages. Omit
    process history.
- Each full rule file whose declared trigger covers the artifact type or a
  decision under review; omit files that are only topically related. Batch reads
  where practical. When a governing rule is also the candidate, read and apply
  it only after `REVIEW FRAME:`.
- Sources, measurements, or run results needed to verify material claims.
- The frozen candidate as an embargoed path. Any diff that exposes it shares the
  embargo. For rule-file candidates, earlier versions do too.
- For review of a change, read-only access to the diff.

Treat task-note wording as evidence, not approved prose: define, replace, or cut
language the intended reader would not understand.

Mark each source path as required to read or merely permitted.
