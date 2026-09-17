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
- Load only rules that help with the current task. Use `write/concise-rules.md`
  and careful editing to keep every loaded word useful.
- Keep general rules free of assumptions about one user, company, repository,
  agent tool, or filesystem. Put Meta-only rules under `facebook/`; Folly's
  open-source export omits that directory.
- Keep maintainer notes concrete and plain. Use simple words. Keep an example
  when it explains the problem better than a summary.

## Make the reader's job easy

Agent rules should lead to work that is correct and easy to understand. Readers
should get exactly the facts they need and see how those facts fit together. If
two versions are equally correct and easy to understand, use the shorter one.

Do not count words or treat abstraction as always good or always bad. Choose the
form that is easiest to understand.

## Before you edit

Read the nearest `CONTRIB.md` first. When editing a rule package identified by
`<name>.loader.md`, also read `<name>/CONTRIB.md` and `<name>.contrib.md` when
present. These files explain why the rule exists and what a change must
preserve.

To see what triggers a top-level rule, read its `<name>.loader.md`. Then follow
explicit filenames in operational rules to see what else loads. A nearby file or
a similar name does not make one rule load another.

## User rule-file loaders

The user rule file defines `{FA}` as `~/folly_agents` and loads every top-level
`<name>.loader.md`. It stops if a loader or referenced rule is unavailable.

Each loader says when to load its package's rules. Keep loaders small because
agents read all of them up front. Use `{FA}` for package paths, and update a
loader when its trigger or loading behavior changes. Explain the reason in
maintainer notes; do not copy the loader text there.

README.md shows how users can install the loaders into their rule file.
