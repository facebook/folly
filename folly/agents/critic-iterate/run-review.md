External review runs exactly this:

```bash
review_tmp=$(mktemp -d)
# Write cold-prompt.md and fresh-prompt.md under "$review_tmp" before this call.
"{FA}/critic-iterate/codex-reviewer.py" \
  --preamble-dir="{FA}/critic-iterate" \
  --preamble=fresh-review-preamble \
  "$review_tmp/fresh-prompt.md"
```

When writing `fresh-prompt.md`, replace `{FA}` and `$review_tmp` below with
their current absolute values:

```bash
"{FA}/critic-iterate/codex-reviewer.py" \
  --preamble-dir="{FA}/critic-iterate" \
  --preamble=cold-review-preamble \
  "$review_tmp/cold-prompt.md" >"$review_tmp/cold-result.txt"
```

Do not prefix either wrapper call with an environment assignment. Hermetic runs
use the copied rule's sibling preambles.

`cold-result.txt` contains `REVIEW_OUTPUT_DIR=<path>` followed by the report.
The top-level author or orchestrator may poll the outer fresh-review command
normally. If polling loses later stdout from that command after recording its
`REVIEW_OUTPUT_DIR`, use that directory's `review.md` only if it is nonempty,
`run.jsonl` reaches `turn.completed`, and the trace checks below pass.
Otherwise, treat the round as failed; never scan temporary directories or infer
a result from partial output.

On success it prints `REVIEW_OUTPUT_DIR=<path>` followed by the review. The
private directory holds the same review in `review.md`, the model setting and
reasoning effort in `metadata.json`, plus `effective-prompt.md`, `run.jsonl`,
and `err.txt` for audit.

The outer marker names the fresh-review directory. The fresh response includes
the child marker; the author records both.

Each prompt must name every input its reviewer may read. Start repo-relative
commands with `cd <repo> &&`. Do not include raw chat or the full context
packet. For commit / diff-message review, follow the specialization in
`{FA}/critic-iterate.md`.

The author revises from the outer fresh review's `review.md`; it already
incorporates the cold report. Every other file in either private directory is
process evidence. On failure, inspect the smallest relevant log excerpt.

Before accepting a prose review, confirm that its only cold-review launch
precedes any source read or statement about the artifact, that `REVIEW FRAME:`
appears before any embargoed input is read, and that `ARTIFACT CHECK:` appears
before the cold-result file is read. Confirm that the cold trace reads no
undeclared source and launches no reviewer. If a check fails, discard the round,
fix its prompt if needed, and start a new outer fresh-review command before
editing.

## Failures

Missing `codex`, auth / sandbox failure, non-zero exit, or timeout means the CLI
path cannot run; stop and report the infra/setup failure. For a Guardian
possible-exfiltration rejection of the top-level wrapper call, read
`{FA}/critic-iterate/auth-prompt.md` only for that rejection, never on the happy
path or for an unrelated policy failure. Those recovery steps do not reach a
nested reviewer running with a private `CODEX_HOME`. The fresh reviewer reports
a nested rejection or run failure and stops. Do not rerun until the blocking
policy or configuration changes; then the top-level author or orchestrator
starts a new prose review round. Empty, off-topic, or malformed reviewer output
is a bad run; the top-level author or orchestrator discards it, tightens the
prompt, and starts a new outer fresh-review command. Each required reviewer
check is incomplete until it produces usable output. Do not proceed self-only or
switch reviewer paths.
