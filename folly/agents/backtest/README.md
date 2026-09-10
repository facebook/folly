# Backtesting agent rules

A regular backtest holds a task and its evidence fixed while using the selected
rules. Controlled comparisons show whether those rules improve the artifact and
what the improvement costs. A no-rules run provides an honest bare-model
baseline. A c-i-K run instead keeps the rules and sets the maximum number of
external review rounds.

Each run uses the scenario and runner from one committed checkout. Regular
rule-backed runs use their rules and helper scripts from that checkout too. Run
the scenario at another revision to compare rule revisions. The runner does not
know which run is a baseline or choose a winner.

After every attempted run, follow [mandatory-debrief.md](mandatory-debrief.md)
before reporting the result.

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

Add `--no-rules` to measure the model without the selected Folly agent rules. In
this mode, the runner uses `no_rules_prompt` from `scenario.json` when present;
otherwise it sends the normal scenario prompt unchanged. It stages only the
declared inputs and does not add its private rule-helper directory to `PATH`.
Inspect the prepared prompt and workdir before starting the author.

For the secondary c-i-K mode, add `--critic-iterate-rounds K`, where `K` is the
maximum number of external review rounds. In particular, `K=0` removes external
review rounds.

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

Compare each secondary mode with a regular run from the same committed revision.
Keep the task, staged inputs, model, reasoning effort, and shared executable
versions fixed. Change only:

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
