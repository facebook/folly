# Build commit-message context

The author or orchestrator builds a context packet for commit messages.
Structure it into three named sections so the author can scan it predictably.

Before constructing the packet, reread the active workstream ledger when one
exists. Build from current task inputs relevant to Stack context, Reader must
know, or Decision trail, including any ledger goal and unsuperseded requirements
or rationale. Before using the packet, treat every input as a claim or
requirement, not approved wording. Apply "Evidence" when a false claim could
change the message, then check each input against the intended reader's starting
knowledge. Keep code identifiers when they anchor a fact or help find the
relevant code. Explain the concrete actor, condition, action, or outcome hidden
by unfamiliar shorthand, and define unavoidable technical terms on first use.
Raw input may be overcomplete, but not opaque.

- **Stack context** — for diffs in a stack: what predecessors covered and what
  follow-ons will do.
- **Reader must know** — the few facts whose absence would make a reader act
  wrongly or misunderstand the change, plus the artifact goal and intended
  readers. Past three or four facts, reapply that test to each; do not merge
  distinct causal facts into an abstract label. The final message may compress
  detail and wording only while preserving the reader's needed model.
- **Decision trail** — for a design choice not mechanically forced by the spec
  or bug, collect only the choices, constraints, or reversals needed to explain
  the final shape. Typical candidates are a rejected alternative whose trade-off
  is not clear from the diff, a constraint that pinned the choice, or a reversal
  that explains a surprising result. Do not inventory the rest of the
  discussion.

The Decision trail is RAW input — the inner loop selects only the facts needed
for the reader's task, then applies the cut test (typically the load-bearing
constraint or rejected alternative; see `write.md` "## What evergreen context
means"). The packet-vs-final-message split is input-vs-keep, not a different
taxonomy. Omitting a decision the reader needs starves the loop; forcing process
history the reader does not need invents motivation and adds noise.
