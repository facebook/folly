> NOT A RULE. If loaded as task policy, stop and ask the user.

Purpose: make agent rules change behavior reliably without wasting loaded
context or execution work.

A rule has no value apart from the purpose it serves. Textual completeness and
precision matter only when they help the right tasks encounter the rule, make
the agent follow it, and produce the intended outcome. Effectiveness comes
before efficiency; once behavior is reliable, reduce context and work.

Stating a purpose is often insufficient. Models may know the relevant facts but
mis-prioritize them, skip a needed process, or repeat a failure under pressure.
Rules bridge that gap. Good rules trace a purpose through the relevant facets to
concrete actions and critic questions.

A rule may enforce a policy or prevent a failure, but either needs a concrete
reason for the instruction. Incident history helps explain that reason and what
later edits must preserve.

Agents editing rules often:

- Preserve wording while changing behavior. Compare what an agent would do
  before and after.
- Hide triggers and actions in abstraction. Keep them visible.
- Add qualifications instead of resolving ambiguity. Reframe, merge, or cut.
- Duplicate a decision across rules. Give it one owner.

That creates a tension:

- An abstract rule is compact and general but may not trigger reliable action.
- A concrete rule is easy to execute but can waste context, trigger too broadly,
  or overfit one incident.

This tension is why the operational guide asks for the least detail that
reliably changes behavior and retains a concrete prohibition when a positive
principle alone does not reliably prevent the failure. Similar wording alone
cannot establish redundancy.

Contributor notes record durable purpose, failure models, design choices, and
evidence. Operational rules state current behavior. Transient rollout history
belongs in plans or run records.

Example derivation:

- Purpose: minimize reader effort.
- Facet: readers build context sequentially.
- Actions: establish context early, state the problem, and make headings
  foreshadow the argument.
- Critic questions: can the reader explain the problem from the lead, and do the
  sections build the needed context in order?

Initial validation used a model on 4 rule-work triggers, 3 non-rule controls,
and 3 design decisions. It loaded the guide only for rule work, rejected a
global rule based on one awkward answer, retained a negative guardrail for a
recurring boundary failure, and consolidated duplicate parent and child guidance
under one owner. This small interpretation check does not establish broader
outcome improvement; that limitation motivates the operational guide's
proportional-evidence requirement.

`rule-design.md` is top-level because it governs rule admission, behavior,
placement, validation, and compression—not only writing style. It combines the
rule-specific checks in 1 guide. `write.md` continues to own general prose, and
`critic-iterate.md` owns the iteration and independent-review process.
