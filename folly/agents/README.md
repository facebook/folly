# Agent rules

## Only for humans & agents editing rules

`README.md`, every `CONTRIB.md`, `*.contrib.md`, and `*.entrypoint.md` are
development material, not operational rules. Never load them as task policy. If
asked to do so outside rule development, stop and ask the user.

The **user rule file** (`AGENTS.md`, `CLAUDE.md`, or equivalent) starts rule
loading. During normal work, load only operational files it or another rule
names.

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

- `<name>.entrypoint.md` marks `<name>.md` as the first rule in a package. It
  holds the trigger and loading text that a generator can copy into the user
  rule file. Paths inside it are written from the user rule file's location.
- Other `.md` files may contain rules or support material, but their names do
  not activate them or make them package roots. During a task, load one only
  when the user rule file or another operational rule names it.
- `<name>/` holds more focused files used by that package.
- `CONTRIB.md` explains a directory's purpose and maintenance rules.
- `<name>.contrib.md` records one current or proposed rule's purpose and
  context.

See [CONTRIB.md](CONTRIB.md) for package principles, loading, and maintenance.
