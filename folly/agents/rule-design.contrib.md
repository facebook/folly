> NOT A RULE. If loaded as task policy, stop and ask the user.

# Why the rule-design guide takes this shape

Purpose: make agent rules change behavior reliably without wasting loaded
context or execution work.

## [Earn the rule](rule-design.md#earn-the-rule)

A purpose statement alone does not make behavior reliable. Models may know the
relevant facts yet skip a needed comparison, mis-prioritize it, or repeat a
failure under pressure. A rule earns its cost when it closes that gap, whether
it enforces a policy or prevents a failure.

Completeness and precise wording are useful only when they help the right tasks
encounter the rule and produce the intended result. That is why reliability
comes before brevity. The protection is the behavior, not the original wording;
missing history does not show that the protection is unnecessary.

## [Specify the behavior](rule-design.md#specify-the-behavior)

The level of detail creates a tradeoff:

- Abstract wording can be compact and general, yet leave the action unclear.
- Concrete detail can make a rule easier to follow, but unnecessary detail
  competes with important instructions for attention. Details tied to one
  incident can hide the reasoning needed in other cases.

Useful abstraction preserves a reasoning operation across situations. Replacing
concrete words with broad labels does not do that. A worker can follow an
instruction faithfully while examining the wrong subject or making the wrong
comparison. An explicit starting point and comparison make the reasoning
executable.

For example, in writing guidance:

- Goal: minimize reader effort.
- Why: later points depend on context established earlier.
- Instruction: open with the problem and make headings foreshadow the argument.
- Review question: does the opening explain the problem, and do the sections
  build the needed context in order?

Repeated qualifications can be symptoms of one unclear decision. Consolidation
helps when the underlying decision already gives the required answers. If the
worker examines the wrong subject, more exceptions may leave that mistake
intact. A concrete callout or prohibition still matters when a shorter positive
principle does not prevent the same bad action. Neither abstraction nor
shortness is a reason to discard a working protection.

## [Place and compress](rule-design.md#place-and-compress)

Duplicate owners can make one decision drift or conflict across rules. Similar
wording alone does not establish duplication: clauses may govern different
triggers, actors, or outputs. The relationship between their decisions matters
more than the resemblance between their sentences.

This guide is separate from writing guidance because it covers whether a rule is
needed, what behavior it induces, where it applies, and how to verify it—not
only how it reads.

- [write.md](write.md) owns general prose
- [critic-iterate.md](critic-iterate.md) owns the review loop,

## [Validate and review](rule-design.md#validate-and-review)

### Literal compliance can miss the purpose

> Look for an easy action that follows the words but misses the purpose.

A worker can report one easy comparison while skipping the one the task needs. A
review note can record that shortcut without revealing the missing work.

### Moving words can change what they govern

> For moved text, compare its loading path and what it governs before and after.

Suppose a content-selection step says "keep only useful explanations." Moved
unchanged into a wording-only step, that instruction asks the wording worker to
decide which explanations survive, not just how to shorten them.

### Evidence must match the claim

Recorded interpretation checks covered 4 rule-work triggers, 3 non-rule
controls, and 3 design decisions. The model loaded the guide only for rule work,
rejected a global rule based on one awkward answer, retained a negative
guardrail for a recurring failure, and consolidated duplicate guidance under one
owner.

Those results support the intended interpretation in the tested cases, not
quality gains or success rates in full tasks. This is why the guide permits a
small proof for a move or consolidation that preserves behavior, but may require
behavioral cases for changed instructions. The examples above explain the risks;
they do not measure their frequency or the effectiveness of each check.
