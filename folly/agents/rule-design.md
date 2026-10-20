# Rule design

Use this when adding, changing, moving, removing, or auditing an agent rule, or
reviewing a proposed rule change. Rules exist only to change behavior toward a
purpose. Optimize first for reliable behavior, then for context and execution
cost.

These rules govern current rule-design work. Do not copy a work step into the
rule being designed unless that rule's approved purpose requires later users to
perform it.

## Earn the rule

Add or keep a rule only when it is needed to make required behavior reliable, or
when it lowers total cost without weakening that behavior. Use an existing rule
or smaller intervention when it suffices. When changing an existing rule,
understand its purpose, history, and required behavior before claiming a
meaning-preserving edit. Missing history is not evidence that a guardrail is
unnecessary.

Choose the least costly intervention that reliably produces required behavior.
Prefer a direct rule when it suffices. Add a clause or work step only when the
simpler intervention would leave a specific required behavior unreliable or cost
more overall. Count loaded context and execution work as cost.

## Specify the behavior

State when the rule applies and what the worker must do, avoid, or produce. Use
a trigger the worker can recognize from its task or context. Keep a scope
boundary only when removing it would change when or how the rule applies. Put
the instruction before rationale or qualifications. A topic label is not enough.

For a judgment, make clear where to start, what to inspect or compare, and how
to decide. Keep enough detail to apply the rule. Omit basics the worker can
already apply reliably. Keep non-obvious caveats needed for correct action.
Retain a concrete callout or prohibition when a positive rule alone does not
reliably prevent the relevant failure.

Resolve ambiguity by reframing, consolidating, or cutting. If successive fixes
add qualifications for individual cases, check whether the current decision rule
already gives the required answers. If so, consolidate the qualifications.
Otherwise, try a different starting point or comparison and walk it through a
different situation within the rule's purpose before refining wording.

## Place and compress

Before editing, search for rules that govern the decision being changed and read
them in full. Trace the loading path and check that intended tasks receive the
rule. Then check the destination and adjacent headings.

Keep one owner for each decision. A parent may retain the decision and a
one-line pointer while a child owns the detail. Put project-specific paths,
names, and procedures in the project rule; keep only the shared lesson in a
general rule.

In operational text, keep background only when it changes how to apply the rule.
An example stays only when it teaches a distinct point needed to apply the rule.

Keep operational instructions direct and token-efficient. To decide whether a
rule change needs a maintainer-note update, follow
`{FA}/write/contributor-notes.md`.

## Validate and review

Use the cheapest evidence that covers each material effect the change can have,
including loaded context and execution work. Add optional checks only when a
plausible result could change the design decision.

Read changed instructions as the worker, using only the context it will receive.
Walk through an intended case and a harmless near-match. For each, identify the
condition that triggers the rule, the information the worker uses, and its
action or output. Look for an easy action that follows the words but misses the
purpose. Check that the result serves the rule's purpose. Compare what the
worker would do before and after; fix any unclear choice, unintended change, or
weakened trigger or action.

Add targeted checks when the edit requires them:

- For moved text, compare its loading path and what it governs before and after.
- For a claimed deduplication, compare each removed clause's trigger, action,
  output, boundaries, and exceptions with the surviving owners.
- For changed wording or guardrails, use additional cases when the edit can
  materially change behavior that the walkthrough does not cover.

During critic iteration, check the rule against its own instructions only where
they apply to its text or development.

In each edit or review pass report, name:

- related rules by file and heading, or search terms that found none;
- related files read in full;
- destination and adjacent headings checked; and
- whether guidance was moved, merged, linked, or kept separate, and why.

After each edit pass, reread the full changed document. Reread directly related
files when the edit changes cross-file ownership, loading, or behavior. In
operational text, check each edited rule or procedure as a whole, then its
edited clauses and work steps. For each, ask what required behavior becomes less
reliable or what total cost increases without it. Cut it if neither changes.
Prefer a simpler direct rule when it preserves both behavior and efficiency.
