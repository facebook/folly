# Testing agent rules

Use unit tests to check the shared test tools, and fixed agent tasks to check
what a rule changes in practice. A fixed task holds the request and evidence
constant while an agent produces an artifact. Read that artifact to judge the
rule; successful execution and accounting do not establish its quality.

Keep fixed tasks under a `tests/` directory beside the rules they test, such as
`write/tests/reader-case/`, or under `backtest/`. The runner excludes these
directories from rule packages, keeping prompts, evaluators, and prior outputs
out of the author's rules. Each task has `scenario.json` and a local README
explaining its question, inputs, output, and stop point.
[Scenario maintenance](tests/CONTRIB.md) explains how to configure one.

A regular run uses the selected rules. Controlled comparisons show whether those
rules improve the artifact and what the improvement costs. A no-rules run
provides a bare-model baseline. A c-i-K run instead keeps the rules and sets the
external-review budget.

Each run uses the scenario and runner from one committed checkout. Regular
rule-backed runs use their rules and helper scripts from that checkout too. Run
the scenario at another revision to compare rule revisions. The runner does not
know which run is a baseline or choose a winner.

Inside a rule-backed run, `{FA}` points to that run's staged rule package.
No-rules runs have neither the binding nor rule helpers.

After every attempted run, follow
[mandatory-debrief.md](tests/mandatory-debrief.md) before reporting the result.

## Run a scenario

When planning a test that requires committed sources, check commit
authorization. Ask immediately if it is missing.

From `fbcode/`, prepare the workdir first:

```bash
python3 -m folly.agents.tests.run_scenario \
  SCENARIO_DIRECTORY \
  --model gpt-5.6-sol \
  --reasoning-effort high \
  --prepare-only
```

Replace `SCENARIO_DIRECTORY` with the fixed task's directory and inspect the
printed run directory. Run the command again without `--prepare-only` to create
a fresh workdir and start the author. The runner refuses to start when a
scenario manifest, prompt, declared input, available package rule, helper, or
the runner itself has an uncommitted change.

Add `--no-rules` to measure the model without the selected Folly agent rules.
The scenario must declare `no_rules_prompt` in `scenario.json`; the runner
refuses otherwise. It stages only the declared inputs and does not add its
private rule-helper directory to `PATH`. Inspect the prepared prompt and workdir
before starting the author.

Scenarios listing `critic-iterate.md` in startup `rules` record these
checkpoints when the author writes a new draft:

- the initial draft before the first author critique;
- the draft after author review converges, before external review; and
- the draft after each external review is integrated and author checks finish.

The run directory stores them as `output-initial.md`, `output-author.md`, and
`output-reviewN.md`. `checkpoints.json` records each phase's time, author and
reviewer token use, context window, and outcome. This mode does not apply to
review-only scenarios that stage an existing `output.md`.

For scenarios listing `critic-iterate.md` in startup `rules`, add
`--critic-iterate-rounds K` to override the normal external-review budget. `K=0`
removes external review rounds.

If it refuses, inspect the listed source changes. Commit them only within the
authorized work, or ask the user to commit them or authorize a commit containing
those files. Retry after the sources are clean.

Before starting, explain to the user what behavior the scenario isolates, why
that behavior matters in real use, and where the run deliberately stops. The
scenario README supplies this framing.

The run directory contains the staged workdir, author prompt, run metadata,
trace, stderr, and any `output.md`. A regular rule-backed workdir also contains
its selected rules. Runs live under the system temporary directory by default;
pass `--run-root` when one must survive normal temporary cleanup.

## Repeat a rule test

1. State what observable behavior should improve and which fixed task could
   expose a regression. Use its README to understand the intended reader and
   stop point.
2. Run the task before changing the rule. Keep its prompts and evidence fixed.
3. Change the rule, commit the authorized source changes, and run the same task
   with the same model, effort, and executable versions.
4. Check the traces and required outputs, then compare the artifacts. Describe
   the change in behavior and any remaining defect. Repeat when variation
   between runs could change the conclusion.

For a new regression case, preserve the task and evidence that exposed the
problem. Put it under the relevant rules' `tests/` directory, with a README
describing what would show a regression.

## Run unit tests

Use Python instead of Buck for fast behavior checks while editing, and in OSS
checkouts. Run from the directory containing `folly/agents/`:

```bash
python3 -m unittest discover -s folly/agents/tests
```

## Run an evaluator

From `fbcode/`, create an isolated evaluator run:

```bash
evaluator=folly/agents/scripts/isolated_codex.py
run=$(mktemp -d)
"$evaluator" prepare "$run"
```

Copy only the evaluator's declared inputs into `$run/workspace/task`, then run
its prompt:

```bash
"$evaluator" run "$run" \
  --prompt PATH \
  --name NAME \
  --model MODEL \
  --effort EFFORT
```

`PATH` is the evaluator prompt. `NAME` is a unique label for that turn.

For a later turn in the same session, add only its declared inputs and use a new
turn name:

```bash
"$evaluator" resume "$run" --prompt PATH --name NAME
```

The runner prepends the `$W` task-root instruction. Each attempt directory
contains the effective prompt, response, event trace, and diagnostics. The run
metadata fixes the engine, Codex executable, model, effort, and session ID
across turns.

Use separate runs for independent checks. For a stored run, stage evaluator
inputs from the `generation_revision` in `run.json`. Do not evaluate uncommitted
prompts or inputs. Keep the evaluator model, effort, and Codex executable fixed
across comparisons, and do not expose evaluator prompts or results to the
scenario author. Record the evaluator source revision and token use with the run
artifacts. A failed evaluator run says nothing about the candidate.

## Compare runs

Compare no-rules and c-i-K variations with a regular run from the same committed
revision. Keep the task, staged inputs, model, reasoning effort, and shared
executable versions fixed. Change only:

- **No-rules:** the injected rules, rule-only helpers, and rule-dependent
  wording isolated in `no_rules_prompt`.
- **c-i-K:** the external review budget.

When only an older stored sample is available, follow
[tracking-samples.md](tests/tracking-samples.md) to identify and report any
additional differences.

Read the artifacts first. Report concrete differences in correctness and reader
effort. Use traces, reviewer reports, time, and token use to explain those
differences. Evaluators may add narrow, repeatable observations; they do not
declare a pass, failure, or winner. Leave that judgment to the user.

Before comparing a run, confirm that it reached the scenario's declared stop
point, produced a nonempty `output.md`, and read no undeclared task evidence. A
completed-artifact scenario must also complete every review required by its
rules; a diagnostic may deliberately stop earlier. Authentication, launcher, or
write failures are inconclusive; preserve their run directories for diagnosis.

If the scenario has a `samples/` directory, or the user asks to preserve a run,
follow [tracking-samples.md](tests/tracking-samples.md).
