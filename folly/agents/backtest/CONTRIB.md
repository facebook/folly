# Maintaining backtests

A scenario is useful when its result could change a rule or how that rule is
applied. It should ask one concrete question using a task realistic enough to
expose the behavior under test.

## Scenario contents

Each scenario has:

- `README.md`: the behavior being isolated, why it matters in real use, the
  artifact and reader, the deliberate stop point, and concrete observations to
  report;
- `scenario.json`: the author prompt (`prompt`), staged file mappings
  (`inputs`), and exact operational rules (`rules`); copy the shape from a
  nearby scenario;
- `input/`: frozen material visible to the author; and
- optional `eval/`: a narrow check used only after `output.md` is frozen.

Keep references, evaluator instructions, sibling outputs, and comparison labels
out of the author workdir. Store generated runs outside this directory.

Read-only staging is not a filesystem sandbox. Check the trace after each run;
an undeclared read makes that run unusable for comparison.

In `scenario.json`, list rules relative to `folly/agents`. Name every required
rule; the runner does not discover files or load a profile. It stages
critic-iterate's reviewer preambles and authorization procedure when
`critic-iterate.md` is selected. Development documents such as `README.md`,
`CONTRIB.md`, `*.contrib.md`, and `*.entrypoint.md` are rejected as rules. The
runner adds the rule-loading instruction; keep the scenario prompt focused on
the task.

## Keep comparisons honest

Run `--prepare-only` and inspect the staged workdir before spending model time.
Across compared runs, keep the author-visible prompt, inputs, model, reasoning
effort, and executable versions fixed. Run each side from its committed checkout
revision. If any of these inputs changes, treat it as another intervention
instead of attributing the result only to rules.

Human reading is the default evaluation. Add an evaluator only when it can make
a narrow, repeatable observation that ordinary artifact comparison cannot make
reliably. Evaluators report evidence; the user judges the artifact and rule
change.

## Source snapshots

Freeze source needed by the task inside `input/`; never point an author at a
live checkout. Store C++ fixtures as `.h.txt` and `.cpp.txt`. The runner removes
the final `.txt` in its temporary workdir, while the inert suffix keeps fixture
headers out of Folly's install and build discovery.

Before exporting a scenario, internal maintainers must follow
`facebook/CONTRIB.md`.
