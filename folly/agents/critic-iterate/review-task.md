# Build a fresh-review task

Before the first prose review, derive a **cold-reader brief** from the request
and artifact destination. In 2–3 short sentences, normally 60 words or fewer,
say who the reader is, what they already know, and what they are trying to
accomplish, without supplying the conclusion or causal story. Include only the
non-goals or unresolved facts needed to bound the review. Omit prior critique
and coverage lists. Reuse the brief verbatim in the fresh task note and every
cold-reader prompt. Treat it as complete: knowing a system does not imply
knowing the terms or details of the work under review.

Keep the brief fixed unless its source inputs change or show it is wrong. If it
changes, immediately show the old brief, new brief, and reason; mention the
change in the final debrief and run a new review pair. If a brief exceeds 100
words, explain why in the next accountability artifact and final debrief.

**Prose review round.** The fresh reviewer owns the round and starts the cold
reader as a nested CLI call. The runtime preambles own reviewer behavior and
execution order.

For a substantial document with a distinct opening and body that explains why
something happens, other than a commit or diff message, cold-check the opening.
Put its exact title and opening, but not the body, in the cold task. Give it
only the cold-reader brief and that opening, with no candidate path. For other
prose, give the cold reader only the whole candidate. The fresh reviewer returns
one coherent set of candidate findings after comparing its independent frame,
the candidate, and the cold account.

For non-prose, the fresh reviewer applies the same evidence and design checks
before opening the artifact, then verifies material claims introduced by it and
returns an alternative or findings.

**Fresh reviewer inputs.** Build its prompt from only:

- **A short task note:**
  - Identify the artifact as prose or non-prose. For prose, include the
    cold-reader brief verbatim, say whether the cold reader sees only the
    opening or the whole candidate, and mark its source inputs as required.
    Otherwise state its goal and intended users.
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
- For prose, the exact nested cold-review command and its prompt path. The path
  is execution-only, not a readable input.
- For review of a change, read-only access to the diff.

Treat task-note wording as evidence, not approved prose: define, replace, or cut
language the intended reader would not understand.

Mark each source path as required to read or merely permitted.

For prose, a requested addition must name the reader task or required
relationship it serves. A requested cut must show that the artifact's purpose
does not need that fact. Truth, relatedness, or hypothetical usefulness is not
enough to keep it; "shorter" alone is not enough to cut it.
