> NOT A RULE. If loaded as task policy, stop and ask the user.

A scenario is useful when its result could change a rule or how that rule is
applied. It should ask one concrete question using a task realistic enough to
expose the behavior under test.

## Scenario contents

Each scenario has:

- `README.md`: the behavior being isolated, why it matters in real use, the
  artifact and reader, the deliberate stop point, and concrete observations to
  report;
- `scenario.json`: the normal author prompt (`prompt`), optional bare prompt
  (`no_rules_prompt`), staged file mappings (`inputs`), startup rules (`rules`),
  and available rule packages (`packages_under_test`);
- optional `input/`: frozen material visible to the author;
- optional `eval/`: a narrow check used only after `output.md` is frozen; and
- optional `samples/`: selected prior outputs.

Keep the author's workdir free of:

- evaluation-only references and evaluator instructions;
- sibling outputs; and
- comparison labels.

Store runner-created directories outside the scenario directory.

Related scenarios may reuse the same frozen input with an explicit `../` source
in `scenario.json`. Keep task- or scope-specific instructions in each scenario's
prompt instead of copying and editing the shared evidence.

Read-only staging is not a filesystem sandbox. Check the trace after each run;
reading undeclared task evidence makes that run unusable for comparison.

In `scenario.json`, list startup rules relative to `folly/agents`. Declare the
packages needed by the task's startup and conditional rule loads, including
cross-package dependencies. Do not copy another scenario's package list: source
code used only as evidence does not require code-review rules. For example,
`write` includes `write.md`, `write.loader.md`, and operational Markdown under
`write/`. A package may have just its root file or just its directory. The
runner:

- stages tracked operational Markdown from those packages, rejecting symlinks;
- binds `{FA}` to the common directory containing all staged rules; and
- lists only the selected startup rules in their declared order.

The runner excludes `README.md`, `TESTING.md`, `TUTORIAL.md`, `INSTALL.md`,
`CONTRIB.md`, `*.contrib.md`, `tests/` subtrees, and `backtest/`. Runtime
scripts are staged separately. Keep the scenario prompt focused on the task; the
runner adds the rule-loading instruction for a normal run.

- **Rule-focused tests.** List the exact operational rules in `scenario.json`.
  These tests exercise only rule behavior, and bypass the loader files.
- **Future loader tests.** Check that loader files load the right rules. Start
  from the same batch-load snippet as a user, with `{FA}` bound to staged rules.
  Do not list the rules directly; the test must reach them via the loaders.

No-rules support is secondary and must not change the prompt or input behavior
of normal runs. To support `--no-rules`, set `no_rules_prompt` to a prompt path
relative to the scenario directory. If the normal prompt assumes staged rules,
use a separate bare prompt; otherwise it may name the same file. The two prompts
must describe the same task; the bare copy may remove only rule-dependent
process instructions. Rule-workflow diagnostics that cannot stand alone without
their rules do not have a meaningful no-rules baseline.

Before adding a scenario whose final response may mention `OutOfBudget`, discuss
the collision with the user; accounting may misclassify the run as a budget
stop. The marker's trailing colon is deliberately omitted here.

## Keep comparisons honest

Follow [TESTING.md](../TESTING.md) to prepare, evaluate, and compare runs. Add
an evaluator only for a narrow, repeatable observation that ordinary artifact
comparison cannot make reliably. Keep only its declared inputs and turn sequence
in the evaluator's `SETUP.md`.

## Source snapshots

Freeze task evidence in the prompt or inside `input/`; `inputs` lists the files
to copy and may be empty. Never point an author at a live checkout. Store C++
fixtures as `.h.txt` and `.cpp.txt`. The runner removes the final `.txt` in its
temporary workdir, while the inert suffix keeps fixture headers out of Folly's
install and build discovery.

Before exporting a scenario, internal maintainers must follow
[`backtest/facebook/CONTRIB.md`](../backtest/facebook/CONTRIB.md).
