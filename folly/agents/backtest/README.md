# Backtesting agent rules

A regular backtest holds a task and its evidence fixed while using the selected
rules. Controlled comparisons show whether those rules improve the artifact and
what the improvement costs. A no-rules run provides an honest bare-model
baseline. A c-i-K run instead keeps the rules and sets the external-review
budget.

Each run uses the scenario and runner from one committed checkout. Regular
rule-backed runs use their rules and helper scripts from that checkout too. Run
the scenario at another revision to compare rule revisions. The runner does not
know which run is a baseline or choose a winner.

After every attempted run, follow [mandatory-debrief.md](mandatory-debrief.md)
before reporting the result.

## Run a scenario

From `fbcode/`, prepare the workdir first:

```bash
python3 -m folly.agents.backtest.run_scenario \
  folly/agents/backtest/SCENARIO \
  --model gpt-5.6-sol \
  --reasoning-effort high \
  --prepare-only
```

Replace `SCENARIO` with a scenario directory and inspect the printed run
directory. Run the command again without `--prepare-only` to create a fresh
workdir and start the author. The runner refuses to start when a scenario
manifest, prompt, declared input, selected rule, helper, or the runner itself
has an uncommitted change.

Add `--no-rules` to measure the model without the selected Folly agent rules.
The scenario must declare `no_rules_prompt` in `scenario.json`; the runner
refuses otherwise. It stages only the declared inputs and does not add its
private rule-helper directory to `PATH`. Inspect the prepared prompt and workdir
before starting the author.

Rule-backed scenarios that start before the initial draft record these
checkpoints by default:

- the initial draft before the first author critique;
- the draft after author review converges, before external review; and
- the draft after each external review is integrated and author checks finish.

The run directory stores them as `output-initial.md`, `output-author.md`, and
`output-reviewN.md`. `checkpoints.json` records each phase's time, author and
reviewer token use, context window, and outcome. This mode does not apply to
review-only scenarios that stage an existing `output.md`.

Add `--critic-iterate-rounds K` to override the rules' normal external-review
budget. `K=0` removes external review rounds.

If it refuses, show the listed files to the user and stop. Retry after the user
commits or amends them, or explicitly authorizes a commit containing only those
files. Never create that commit silently.

Before starting, explain to the user what behavior the scenario isolates, why
that behavior matters in real use, and where the run deliberately stops. The
scenario README supplies this framing.

The run directory contains the staged workdir, author prompt, run metadata,
trace, stderr, and any `output.md`. A regular rule-backed workdir also contains
its selected rules. Runs live under the system temporary directory by default;
pass `--run-root` when one must survive normal temporary cleanup.

## Compare runs

Compare no-rules and c-i-K variations with a regular run from the same committed
revision. Keep the task, staged inputs, model, reasoning effort, and shared
executable versions fixed. Change only:

- **No-rules:** the injected rules, rule-only helpers, and rule-dependent
  wording isolated in `no_rules_prompt`.
- **c-i-K:** the external review budget.

When only an older stored sample is available, follow
[tracking-samples.md](tracking-samples.md) to identify and report any additional
differences.

Read the artifacts first. Report concrete differences in correctness and reader
effort. Use traces, reviewer reports, time, and token use to explain those
differences. Evaluators may add narrow, repeatable observations; they do not
declare a pass, failure, or winner. Leave that judgment to the user.

Before comparing a run, confirm that it reached the scenario's declared stop
point, produced a nonempty `output.md`, and read no undeclared task evidence. A
completed-artifact scenario must also complete every review required by its
rules; a diagnostic may deliberately stop earlier. Authentication, launcher, or
write failures are inconclusive; preserve their run directories for diagnosis.

See [CONTRIB.md](CONTRIB.md) to add or change a scenario. If the scenario has a
`samples/` directory, or the user asks to preserve a run, follow
[tracking-samples.md](tracking-samples.md).
