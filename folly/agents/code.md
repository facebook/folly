# Code

Resolve package paths relative to this file's directory.

Language-independent coding rules. Load before durable code edits or reviews,
then the relevant language and project docs. Put cross-language coding rules
here first.

When tests are in scope, load `code/testing.md` plus the relevant language and
project test rules.

## Compression and locality

Minimize reader effort, not edit size. Read as the next person who must answer
questions from the code: what it does, whether it is safe, how to change it, and
which invariants hold. Choose the shape that minimizes that work, even when the
shorter version satisfies every named rule — the rubric is a search tool, not
proof that the code is good enough. Make the larger edit when it leaves clearer
code within the requested scope. Keep longer structure when it names a domain
concept, enforces a boundary, preserves correctness, controls lifetime, side
effects, or cost, or makes tangled logic readable.

Minimize hidden state, not just lexical scope. Closure capture is implicit
state; prefer explicit arguments or inlining when a helper depends on a few
loop-local or mutable values. If explicit args would repeat long argument lists,
package the shared state in a small local object with named fields, or suppress
closure-capture lint like B023 when captured values are low-risk and the closure
remains clearest.

During the compression pass, look for unnecessary structure: one-use names;
staging containers or builders that only feed the next expression, loop, return,
or constructor; copy-then-rebuild steps; and control flow made obsolete by
earlier checks. Inline, delete, or collapse it when the result stays clearer.

Before adding tracking state, check whether an existing accumulator or result
already encodes the fact. Reuse it unless that hides the invariant or changes
cost or lifetime.

Prefer the expression at the call site when a local name, helper, wrapper,
predicate, or alias only renames one operation.

Default to inline code first; extract only after the real call sites or boundary
are visible.

When a helper earns extraction, keep it as local as the implementation still
reads clearly. Prefer a nested/local helper when one function needs it and its
inputs stay explicit. Use a private file- or module-local helper for logic
shared within one file or module, and export only when another file or module
needs a stable interface.

A helper, wrapper, predicate, or alias must earn its abstraction and
non-locality cost. A smoother call site is not enough if the reader must jump
elsewhere to understand simple logic.

Introduce a helper, wrapper, predicate, or alias only when it does at least one
of these:

- Removes real duplication: 2+ guaranteed call sites for complicated logic, 3+
  for trivial one-liners.
- Encapsulates a meaningful boundary: policy, ownership, volatility, lifetime,
  side effects, or cost.
- Names a non-obvious domain concept that remains clearer when read away from
  its implementation.
- Makes otherwise tangled branching or nesting materially clearer despite the
  non-locality.

Do not add helpers, wrappers, predicates, or aliases for anticipated reuse,
symmetry, or tidiness.

Inline single-use locals when the expression is clearer at the use site. Keep a
single-use local only when it names a non-obvious domain concept, preserves
correctness, avoids repeated side effects or expensive recomputation, controls
lifetime, or breaks genuinely unreadable nesting.

Inline single-use helpers, wrappers, predicates, and aliases unless they satisfy
one of the extraction reasons above.

Do not extract a helper merely to name a direct equality, membership, null, or
enum-state check. Inline the check unless the predicate is reused or names
domain policy.

## Error handling

Keep an error boundary only when it meaningfully improves operational
reliability or significantly reduces expected debugging time over the program's
life. When handling or translating a failure, preserve its original cause and
actionable context; otherwise let it propagate.

- Production services may crash only on irrecoverable errors (e.g. common policy
  is not to handle OOM).
- Systems programs must decide deliberately which failures are recoverable and
  which terminate the process.
- Scripts, utilities, and config generators/validators should usually let rare
  unexpected failures produce a native exception and stack trace. Handle
  expected user mistakes, and add context to common failures only when it
  preserves the original diagnosis and repays the extra code. `CHECK` and
  `assert` are fine for internal invariants.

## Naming

For maps and dicts, name the mapping as key-to-value: `keyToValue`,
`KEY_TO_VALUE`, or `key_to_value`. Avoid `by` names such as `valueByKey`;
chained mappings stay clearer as `a_to_b_to_c`.

Avoid slavish repetition in identifiers. Before renaming a nontrivial
identifier, list several reasonable names, compare them, then pick the clearest
one.
