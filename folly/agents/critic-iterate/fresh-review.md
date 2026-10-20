# Prepare a fresh review

## Classify the targets

Select review rules separately for each target, using what it is for and what is
being reviewed or changed. Distinguish substantive changes from mechanical
changes, and targets from evidence sources.

Follow the user's review requests, applicable rules, loader triggers, and review
specializations. A target can need several kinds of review.

## Compose the shared review

Use one fresh task for all targets. For prose targets, also follow
`{FA}/write/prose/c-i-fresh-review.md`.

**Fresh reviewer inputs.** Build its prompt from only:

- **A short task note:**
  - Identify each target's artifact and change types, goal, intended readers or
    users, and applicable checks.
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
- Each full applicable rule file and its triggered dependencies; omit files that
  are only topically related. Batch reads where practical. When a governing rule
  is also the candidate, read and apply it only after `REVIEW FRAME:`.
- Sources, measurements, or run results needed to verify material claims or
  requirements governing the target, including relevant maintainer notes.
- The frozen candidate as an embargoed path. Any diff that exposes it shares the
  embargo. For rule-file candidates, earlier versions do too.
- For review of a change, read-only access to the diff.

Treat task-note wording as evidence, not approved prose: define, replace, or cut
language the intended reader would not understand.

Mark each source path as required to read or merely permitted.
