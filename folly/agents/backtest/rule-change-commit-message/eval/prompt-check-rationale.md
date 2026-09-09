Check the title, Summary, and Test Plan of one commit message against its frozen
rationale and patch. Its primary reader is a reviewer who knows the purpose of
the Folly agent rules but has not followed the drafting conversation. The
message is read before the diff and should supply the causal model needed to
understand why the change exists and evaluate it.

Read only `candidate.md`, `evidence/request.md`, `evidence/requirements.md`,
`evidence/conversation.md`, `evidence/commit-state.md`, and
`evidence/change.patch`. Treat `evidence/request.md` as the user's task. Use the
requirements and conversation as evidence of the problem and decisions. The
commit state and patch establish the original state and implemented scope; they
are not an outline the message must reproduce.

Report only concrete observations in these categories:

- **TASK MISS** — the output omits the requested title, Summary, or Test Plan,
  or substitutes an inventory of edits for the problem that should frame the
  diff.
- **FACTUAL DISTORTION** — the message contradicts the evidence or turns a
  limited failure into a categorical claim.
- **CAUSAL GAP** — a reader cannot follow what went wrong, why the existing
  guidance allowed it, or why the resulting decision follows.
- **SAFEGUARD LOSS** — the message makes a prior safeguard sound pointless or
  removed when the change must still prevent the real failure that motivated it.
- **SEMANTIC INVENTORY** — a sentence or list exists mainly to mention each
  affected concept or rule family, without advancing the causal account.
- **PRIVATE PROCESS HISTORY** — drafting chronology appears even though removing
  it would leave the rationale intact. The failed commit message preserved by
  this scenario motivates the test; it does not belong in the candidate.
- **READER-EFFORT FAILURE** — wording removes a relationship the reviewer needs,
  replaces it with a label the reviewer must unpack, or retains detail that does
  not help the reviewer understand or verify the change.

For each observation, quote the candidate and state the concrete effect on the
reader. Do not request a fact merely because it appears in the patch or source
packet. Do not use word count as a proxy, rank candidates, or declare the
message a pass or failure. If no category has an observation, say so.

Do not inspect parent directories, source control, other outputs, or network
content. Do not edit files or launch another reviewer. Put the complete
observations in the closing response.
