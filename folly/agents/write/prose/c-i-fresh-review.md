# Prepare a prose fresh review

Before the first prose review, derive a **cold-reader brief** for each target
from the request and its destination. Reuse a brief when reader knowledge and
task are the same. In 2–3 short sentences, normally 60 words or fewer, say who
the reader is, what they already know, and what they are trying to accomplish,
without supplying the conclusion or causal story. Include only the non-goals or
unresolved facts needed to bound the review. Omit prior critique and coverage
lists. Reuse the brief verbatim in the fresh task note and every cold-reader
prompt. Treat it as complete: knowing a system does not imply knowing the terms
or details of the work under review.

Keep the brief fixed unless its source inputs change or show it is wrong. If it
changes, immediately show the old brief, new brief, and reason; mention the
change in the final debrief and run a new review pair. If a brief exceeds 100
words, explain why in the next accountability artifact and final debrief.

For each prose target, choose whether to give the cold reader only the opening
or the whole text. For a substantial document with a distinct opening and body
that explains why something happens, other than a commit or diff message,
cold-check the opening. Embed its exact title and opening, with its brief but no
body or candidate path. For other prose, give the cold reader the whole
candidate by path, with its brief.

Combine these checks in one cold-reader prompt. For each target, include its
reader brief and state what text it may read. Put opening-only checks before
whole-candidate checks so the cold reader records its first reading before
seeing other bodies. The fresh reviewer integrates all of the cold assessments
into its candidate findings.

In the short task note, identify the targets receiving prose review. Include
their cold-reader brief verbatim, say whether the cold reader sees openings or
whole candidates, and mark their source inputs as required.

Include the exact nested cold-review command and its prompt path. The path is
execution-only, not a readable input.

Fresh reviewer must get `{FA}/write/prose/c-i-review.md` as a separate required
input.
