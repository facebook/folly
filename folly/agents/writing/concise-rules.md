# Writing concise rules documents

When writing or shortening guidelines and rules documents for token efficiency:

## What to cut

- **Explanations that restate the rule.** If the rule is "avoid X," don't follow
  with "X is bad because..." unless the "because" changes how the reader applies
  the rule.
- **Knowledge strong models already supply.** Do not encode common syntax,
  framework basics, or standard advice. Keep project-specific decisions,
  high-impact caveats, and actions a strong model would not take by default. An
  example must teach one of those.
- **Project details in shared rules.** Keep only the lesson that applies across
  projects. Leave project-specific paths, names, and procedures in that
  project's rules.
- **Sections owned elsewhere in the rule hierarchy.** Do not repeat a parent
  rule in a child. When a child owns the details, keep only the decision and a
  one-line pointer in the parent.
- **Process history.** Cut notes about how a rule was developed, tested, or
  might later be promoted or split. Put rollout context in plans or run records;
  rule docs should state current operating behavior.
- **Redundant code blocks.** One good example beats two that illustrate the same
  point. Consolidate.

## What to keep

Rules here identify what NOT to cut from otherwise-justified content. Not
license to add new content.

- **Decision functions.** The reader must know _when_ and _how_ to apply the
  rule, not just that it exists. "Use `.release()` when the callee stores or
  forwards the value" is a decision function. "Use `.release()` for migration"
  is not — too vague to act on.
- **Scope boundaries.** Rules that say "only for X, never for Y" need both
  halves. Cutting the negative half makes the rule over-broad.
- **Concrete consequences.** "A diff adding X should fail review" is more
  effective than "avoid X."
- **Non-obvious callouts.** "Yes, even `union_field_ref` is nullable" catches a
  real mistake. Don't cut surprises.
- **Cross-references to detailed docs.** A rule + `See Foo.md` is the right
  level for a parent doc. The detail lives in the child.

## Structural moves

- **Optional background.** Keep context needed to apply a rule with the rule.
  Put longer, optional system background in a suitable linked document when one
  exists.
- **Inline one-liners.** Don't give a one-line fact its own `##` heading. Inline
  it or append to a related section.
- **Promote headings when they're peers.** If "C++ Standards" and "Build System"
  are siblings, don't nest one under the other.

## Process

- After each edit pass, re-read the full doc and ask: "Would an agent behave
  differently without this line?" If yes, keep. If no, cut.
- Preserve the decision function — this is the most common mistake when
  shortening.
