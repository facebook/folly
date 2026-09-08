# Backtesting agent rules

A backtest holds a task and its evidence fixed while changing the rules an agent
receives. It helps answer whether a rule change improves the artifact and what
the improvement costs.

Each run uses the scenario, rules, runner, and helper scripts from one committed
checkout. Run the scenario at another revision, then compare the artifacts. The
runner does not know which run is a baseline or choose a winner.

## Run a scenario

From `fbcode/`, prepare the workdir first:

```bash
python3 folly/agents/backtest/run_scenario.py \
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

If it refuses, show the listed files to the user and stop. Retry after the user
commits or amends them, or explicitly authorizes a commit containing only those
files. Never create that commit silently.

Before starting, explain to the user what behavior the scenario isolates, why
that behavior matters in real use, and where the run deliberately stops. The
scenario README supplies this framing.

The run directory contains the staged workdir, selected rules, author prompt,
run metadata, trace, stderr, and any `output.md`. Runs live under the system
temporary directory by default; pass `--run-root` when one must survive normal
temporary cleanup.

## Compare runs

Read the artifacts first. Report concrete differences in correctness and reader
effort. Use traces, reviewer reports, time, and token use to explain those
differences. Evaluators may add narrow, repeatable observations; they do not
declare a pass, failure, or winner. Leave that judgment to the user.

Before comparing a run, confirm that it reached the scenario's declared stop
point, produced a nonempty `output.md`, and read no undeclared task evidence. A
completed-artifact scenario must also complete every review required by its
rules; a diagnostic may deliberately stop earlier. Authentication, launcher, or
write failures are inconclusive; preserve their run directories for diagnosis.

See [CONTRIB.md](CONTRIB.md) to add or change a scenario.
