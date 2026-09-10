> NOT A RULE. If loaded as task policy, stop and ask the user.

# Maintaining the rules

Read [README's Purpose](README.md#purpose) first. It explains why this package
exists; the rest of this file assumes that context.

One sentence here can affect many tasks. A bad edit can waste tokens everywhere
or push an agent toward the wrong work.

## Principles

- Solve general problems. Add a rule only for a real, recurring failure, not a
  one-off incident.
- Say the rule in the fewest plain words that still tell an agent what to do.
- "Every rule is written in blood." Learn why a rule exists before changing it.
  Record its goal, important requirements, and background in the nearest
  `.contrib.md`.
- When a task needs several kinds of guidance, combine small, focused rules
  instead of growing one file to cover everything.
- Load only rules that help with the current task. Use
  `writing/concise-rules.md` and careful editing to keep every loaded word
  useful.
- Keep general rules free of assumptions about one user, company, repository,
  agent tool, or filesystem. Put Meta-only rules under `facebook/`; Folly's
  open-source export omits that directory.

## Reader-effort objective

Changes to the writing, code, or critic-iteration rule families must preserve
one shared objective: give the target audience exactly the facts and
relationships needed for the artifact's purpose, correctly and with the least
reading effort. If two forms do that equally well, the shorter one wins.

Do not turn the objective into a word-count target or a blanket preference for
or against abstraction.

## Before you edit

Read the nearest `CONTRIB.md` first. When editing a rule package identified by
`<name>.entrypoint.md`, also read `<name>/CONTRIB.md` and `<name>.contrib.md`
when present. These files explain why the rule exists and what a change must
preserve.

To see what triggers a top-level rule, read its `<name>.entrypoint.md`. Then
follow explicit filenames in operational rules to see what else loads. A nearby
file or a similar name does not make one rule load another.

## User rule-file templates

We plan to generate user rule files from templates. Starting now, every
top-level rule in this package that can be loaded on its own has a neighboring
`<name>.entrypoint.md`. Put its short trigger and loading instruction there, and
update it when either changes. Do not repeat its text in maintainer notes.
