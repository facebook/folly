Check one standalone OSS document for `folly::coro::co_withMetadata()` and
metadata-aware async-stack reading. Its readers use `folly::coro` but know
little about Folly's async-stack implementation. The document should explain how
to use the feature, its behavior and portability, its current limitations, and
where propagation stops between awaited work and work started on a new async
stack.

Read only `candidate.md` and files under `evidence/`. Begin with
`evidence/packet/input-map.md` and apply its authority rules to the other
evidence. Later user decisions supersede earlier discussion, assistant
statements, review conclusions, and frozen source behavior. Headers under
`evidence/api/` establish public API spelling, signatures, and examples only
where the authority-mapped packet does not supersede them.

Report only:

- `FACTUAL ERROR — <location>`: quote a claim or example that conflicts with the
  authority-mapped packet or an applicable public API, state what is false, and
  cite the controlling evidence. Judge examples within their stated purpose and
  nearby preconditions; a focused snippet need not repeat unrelated production
  handling.
- `CRITICAL OMISSION — <location>`: state the missing contract fact, the
  specific contract-conflicting API use, output interpretation, or integration
  step it is likely to cause, and the controlling evidence. A fact is not
  required merely because it appears in the packet or would make the document
  more complete.

If there are no findings, say that no factual errors or critical omissions were
found. Do not assess prose, organization, length, completeness, or similarity to
another document. Do not recommend optional additions, rank candidates, or
declare a result from the number of findings. Do not inspect parent directories,
scenario files, source control, or other outputs. Do not edit files or launch
another reviewer. Put the complete check in the closing response.
