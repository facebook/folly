# Requirements derived from the conversation

## Objective

For the target audience and purpose, communicate exactly the facts and
relationships they need, correctly, with the least reading effort. When two
versions require the same effort and convey the same necessary structure,
prefer the shorter one.

## Required behavior

1. Select content by necessity to the artifact's goal, not by whether it is
   available, interesting, non-obvious, or present in nearby code. A kept fact
   must serve a concrete reader task or prevent a concrete misunderstanding;
   hypothetical usefulness is not enough.
2. Preserve enough concrete sequence and relationships for the target audience
   to build the intended mental model.
3. Cut details that do not improve that model, even when they add true
   information.
4. Reject wording that replaces concrete relationships with abstractions the
   reader must unpack.
5. Reject vague compression that omits relationships the reader must guess.
6. Prefer shorter wording when comprehension and required structure are equal.
   Reading cost dominates change cost; do not make common reads harder to
   simplify rare edits.
7. For commit messages, model the actual reading order: message first, code
   second through the message's framing.
8. In a commit message, repeat code facts only when they are needed to establish
   that framing before the code is read. A fact's availability in the diff
   neither requires nor forbids repeating it.
9. Identifiers may anchor the framing, but must not replace it.
10. For code, permit useful abstraction when symmetry, a shared rule, ownership,
    or another real boundary reduces reading effort overall. Charge abstraction
    for indirection and concepts introduced; do not ban it.
11. Review rules must test both additions and cuts against the artifact's goal.
    They must not reward completeness, word count, manufactured alternatives,
    or refusal to cut any true fact.
12. Maintainer guidance must preserve this objective across future edits to the
    writing, code, and critic rules without duplicating their operational text.

## Anti-requirements

1. No vocabulary blacklist.
2. No detailed identifier-formatting policy in core rules.
3. No actor/action mandate for every sentence.
4. No rule that preserves all information merely because removing it adds some
   inference.
5. No rule that treats word count as the primary objective.
6. No blanket bias against abstraction.
7. No commit-message rule that assumes the code was already read.
8. No duplicated commit-message/source-restatement rules.
9. No general rule derived from the local Thrift optional-field discussion.

## Evaluation cases

### Commit message: self-explanatory test expansion

A large diff adds many readable test scenarios. The message should give the
reason those scenarios were needed in one or two sentences. Repeating each case
fails because the code supplies the detail after the reader has the reason.

### Commit message: one-line heisenbug fix

A one-line change mitigates a complex race. The message may need the full path
from detection through diagnosis, mechanism, remediation, and validation. The
small diff does not make that story redundant.

### Wordy prose

True facts that do not change the reader's model or help complete the artifact's
goal must be cut.

### Abstract-legalese

A short phrase such as “recovery unit” fails when the audience must reconstruct
the underlying actors, stored values, or sequence before understanding it.

### Vague compression

A short conclusion fails when it removes a relationship the audience needs to
understand the design or evaluate correctness.

### Useful identifier

An identifier is kept when it gives the reader a stable anchor into the code
after the prose has explained its role. It is removed when it merely repeats a
field list or substitutes for explanation.

### Useful code abstraction

An abstraction is kept when it exposes real symmetry or one rule that several
call sites must share and lowers overall reading effort. It is rejected when it
exists for tidiness, hypothetical reuse, or local brevity while adding a concept
and navigation.
