# Agent rules

## Only for humans & agents editing rules

`README.md`, every `CONTRIB.md`, and `*.contrib.md` are development material,
not operational rules. Never load them as task policy. If asked to do so outside
rule development, stop and ask the user.

The **user rule file** (`AGENTS.md`, `CLAUDE.md`, or equivalent) starts rule
loading. During normal work, load only operational files it or another rule
names.

## How do I use this directory?

Rule packages are identified by `*.loader.md` files. Each loader lists brief
trigger conditions for its rules. Link `~/folly_agents` to this source tree (at
Meta, see `scripts/facebook/`). Then, add this to your user rule file:

```markdown
`{FA}` means `~/folly_agents`.

Immediately batch-load these from `{FA}/`:

- `core.loader.md`
- `critic-iterate.loader.md`
- `design-vetting.loader.md`
- `writing.loader.md`
- `code.loader.md`

Stop if a loader or referenced rule is unavailable.
```

Update the `folly/agents` checkout to get new rules & trigger conditions.

## Purpose

More Folly work is being done by coding agents, but contributors apply very
different levels of rigor. The extra productivity helps only if the work stays
high-quality and cheap for humans to review, understand, and maintain. These
rules aim to make that repeatable. Most apply beyond Folly.

Agent work spans design, planning, implementation, measurement, and explanation.
Even strong agents are better at executing a stated plan than at recovering
requirements and constraints that were never stated. They also tend to write for
the context they just consumed rather than the context their readers have.

The main rules target those gaps:

- `critic-iterate.md` improves quality by spending more model time and tokens on
  repeated drafting and independent review across design, code, and writing.
- `design-vetting.md` surfaces requirements, constraints, failure modes, and the
  evidence needed to choose between options before a plan hardens.
- `writing.md` pushes explanations toward plain language and the reader's actual
  context.
- `code.md` and the testing rules push implementation toward correctness,
  simplicity, and reviewability.

## Find files

- Other `.md` files may contain rules or support material, but their names do
  not activate them or make them package roots. During a task, load one only
  when the user rule file or another operational rule names it.
- `<name>/` holds more focused files used by that package.
- `backtest/` contains fixed tasks for measuring how rule changes affect an
  artifact. It is development tooling and is never loaded as task policy.
- `CONTRIB.md` explains a directory's purpose and maintenance rules.
- `<name>.contrib.md` records one current or proposed rule's purpose and
  context.

See [CONTRIB.md](CONTRIB.md) for package principles, loading, and maintenance.
