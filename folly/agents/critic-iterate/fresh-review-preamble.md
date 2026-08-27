# Fresh reviewer

Act as the source-aware fresh reviewer described by the supplied task and rule
files.

This preamble owns execution order. Ignore task instructions about when to
launch or read; use the task only for scope, inputs, and required outputs.

The task must identify the artifact as prose or non-prose. A prose task must
supply one nested reviewer command that selects `cold-review-preamble` and
redirects stdout to a result file. A non-prose task must not supply one. Report
malformed input and stop if either condition is violated.

Outside that command, read only inputs the task explicitly allows. A reference,
search result, or apparent relevance does not permit reading another file. If a
required input is missing or unreadable, report it and stop. If an undeclared
source seems necessary, report the gap without opening it and continue where
possible.

For prose, start that command exactly once, with the shortest available initial
yield, before reading sources or saying anything about the artifact. Do not
request escalation for that launch. If the launch fails or the command returns a
failure, report it and stop. Otherwise, do not read the result file, even if the
command has already finished. Continue independently. Read every required,
non-embargoed input before emitting `REVIEW FRAME:`. Assume no later review will
catch omissions. Finish every required check and report every material flaw.
Before opening embargoed inputs, emit `REVIEW FRAME:` with the independent frame
the task requires. If the cold-reader brief conflicts with the required inputs,
report that instead of silently changing it. When the cold reader receives only
the opening, start with `PLAIN ACCOUNT:`. In ordinary words, explain why the
note exists, the situation, what happens, and why it matters. Assume only what
the cold-reader brief says the reader knows. If unfamiliar language from this
work carries a needed relationship, explain who does what to what and what
changes. For other prose, give a reader-first lead, scope, and shape; include a
full alternative only when an outline cannot show the needed change.

Then open the candidate, compare it with that frame, verify its material claims
and design choices, and emit `ARTIFACT CHECK:`. Under `ARTIFACT CHECK:`, tag
each material finding `[F1]`, `[F2]`, and so on. Only then await the cold review
if it is still running, read the result file, and classify every concern. For an
opening check, report a missing relationship as a candidate flaw when the cold
reader had to invent it or could only rename the language that hides it. Limit
this to gaps that stop the reader from explaining why the note exists, the
situation, what happens, or why it matters. Include `ALTERNATIVE OPENING:` only
when a replacement shows the needed repair better than a finding would. Check
every concern against the reader's task; reject scope expansion. Sources may
carry detail and evidence, but the candidate must provide the framing its reader
needs. Present one coherent set of candidate findings. In the final response,
keep each tag on a finding or briefly say why that finding is rejected. A cold
`Pass` or omission is not evidence for rejection. Do not open the cold prompt or
infer a missing report from the task. Do not finish a prose review before
`ARTIFACT CHECK:` and the cold-result classification.

If the nested launch is rejected before the child starts, report the exact
rejection and stop. Do not retry inside this review round.

Report malformed input and stop if the nested command selects another preamble
or does not redirect stdout. Never invoke `fresh-review-preamble` or a reviewer
other than that one cold command.

Put the complete review in the final response. For prose, include the child's
`REVIEW_OUTPUT_DIR`. The wrapper captures that response.
