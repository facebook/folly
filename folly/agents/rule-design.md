# Rule design

Use this when adding, changing, moving, removing, or auditing an agent rule, or
reviewing a proposed rule change. Rules exist only to change behavior toward a
purpose. Optimize first for reliable behavior, then for context and execution
cost.

These rules govern current rule-design work. Do not copy a work step into the
rule being designed unless that rule's approved purpose requires later users to
perform it.

## Earn the rule

Add or keep a rule only when it is needed to make required behavior reliable.
Use an existing rule or smaller intervention when it suffices. When changing an
existing rule, understand its purpose, history, and required behavior before
claiming a meaning-preserving edit. Missing history is not evidence that a
guardrail is unnecessary.

Choose the least costly intervention that reliably closes the gap. Preserve
effectiveness first: the right tasks must encounter the rule, the agent must
follow it, and the result must serve its purpose. Then reduce loaded context and
execution work.

## Specify the behavior

Make the rule's decision function easy to find: put its trigger, scope, and
required action or output before rationale or qualifications. A topic label is
not enough.

State what a worker will do, not only the result it should reach. For a
judgment, make clear where to start, what to inspect or compare, and how to
decide. Keep enough detail to apply the rule without encoding common syntax,
framework basics, or standard advice. Keep non-obvious caveats needed for
correct action. Retain a concrete callout or prohibition when a positive rule
alone does not reliably prevent the relevant failure.

Resolve ambiguity by reframing, consolidating, or cutting. If successive fixes
add qualifications for individual cases, check whether the current decision rule
already gives the required answers. If so, consolidate the qualifications.
Otherwise, try a different starting point or comparison and walk it through a
different situation within the rule's purpose before refining wording. Add text
only for a distinct behavior-changing decision with no shorter form.

## Place and compress

Before editing, search for rules that govern the decision being changed and read
them in full. Trace the loading path, then check the destination and adjacent
headings.

Keep one owner for each decision. A parent may retain the decision and a
one-line pointer while a child owns the detail. Put project-specific paths,
names, and procedures in the project rule; keep only the shared lesson in a
general rule.

In operational text, keep background only when it changes how to apply the rule.
An example stays only when it teaches a distinct point needed to apply the rule.

Keep operational instructions direct and token-efficient. Preserve the rule's
durable rationale, evidence, and design choices in its contributor note.

## Validate and review

Use evidence proportional to the change. Check intended, missed, and unintended
triggers; compliance; whether the result serves the purpose; unintended behavior
changes; loaded context; and execution cost. For moved text, compare its loading
path and what it governs before and after. For a claimed deduplication, compare
each removed clause's trigger, action, output, boundaries, and exceptions with
the surviving owners. Changed wording or guardrails may also need behavioral
cases.

Read changed instructions as the worker, using only the context it will receive.
Walk through an intended case and a harmless near-match. Identify what triggers
the rule, what information the worker uses, and what it does. Look for an easy
action that follows the words but misses the purpose. Find wording that leaves
those choices unclear or invites a wrong action. Compare what the worker would
do before and after; reject any unintended change or weakening to the trigger or
required action.

During critic iteration, check:

- purpose and behavior traceability;
- whether its trigger, scope, and required behavior are easy to find;
- whether each qualification changes when or how that behavior applies;
- whether the loading path reaches the intended tasks;
- ownership, conflicts, and duplication;
- whether each line changes agent behavior;
- effectiveness and cost evidence; and
- whether a new or changed rule complies with itself.

In the report for each edit or review pass, name related rules by file and
heading, or search terms that found none, the related files read in full, the
destination and adjacent headings checked, and whether guidance was moved,
merged, linked, or kept separate, with the reason.

After each edit pass, reread the full changed document. Reread directly related
files when the edit changes cross-file ownership, loading, or behavior. In
operational text, ask what agent behavior would change without each edited line;
cut it if the answer is none.
