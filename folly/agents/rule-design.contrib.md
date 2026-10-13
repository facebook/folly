> NOT A RULE. If loaded as task policy, stop and ask the user.

# Why the rule-design guide takes this shape

Purpose: make agent rules change behavior reliably without wasting loaded
context or execution work.

## [Earn the rule](rule-design.md#earn-the-rule)

A purpose statement alone does not make behavior reliable. Models may know the
relevant facts yet skip a needed comparison, mis-prioritize it, or repeat a
failure under pressure. A rule earns its cost when it closes that gap, whether
it enforces a policy or prevents a failure.

A rule can also earn its cost by making already reliable behavior cheaper
without weakening it.

Reliability comes before brevity, but complexity is not evidence of reliability.
Extra mechanism consumes attention and execution work even when it closes no
gap. Steps can appear necessary because they support each other, even when the
whole procedure serves no purpose. The protection is the behavior, not the
original wording; missing history does not show that protection is unnecessary.

## [Specify the behavior](rule-design.md#specify-the-behavior)

The level of detail creates a tradeoff:

- Abstract wording can be compact and general, yet leave the action unclear.
- Concrete detail can make a rule easier to follow, but unnecessary detail
  competes with important instructions for attention. Details tied to one
  incident can hide the reasoning needed in other cases.

A topic label can make workers infer different triggers or actions. Useful
abstraction preserves a reasoning operation across situations. Replacing
concrete words with broad labels does not do that. Even precise wording can
direct a worker to the wrong subject or comparison. An explicit starting point
and comparison make a judgment executable.

For example, in writing guidance:

- Purpose: minimize reader effort.
- Why: later points depend on context established earlier.
- Action: open with the problem and make headings foreshadow the argument.
- Review: does the opening explain the problem, and do the sections build the
  needed context in order?

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

Checks earn their cost by covering a gap or uncertainty, not by repeating an
inventory. Loading paths and removed clauses can contain differences the
walkthrough's cases do not exercise.

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

The examples above explain the risks. They do not measure how often the failures
occur or how well each check prevents them.
