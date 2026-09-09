Make reader effort the shared objective for agent rules

The rules could reward both over-compression and completionism: agents hid
necessary relationships behind terse abstractions, yet kept unnecessary facts
simply because they were true or nearby. The code rules had the same problem:
fixed preferences for inlining or extraction could obscure the invariants they
were meant to clarify.

Writing, code, and critic iteration should preserve exactly the facts and
relationships the audience needs, in the form that takes the least effort to
understand correctly. Prefer the shorter form when comprehension is equal.
Abstraction is neither inherently good nor bad; it earns its cost when it
exposes a real boundary, shared rule, or symmetry.

Test Plan:

- Docs-only.
