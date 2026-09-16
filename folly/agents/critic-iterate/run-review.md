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
The top-level author or orchestrator may poll the fresh-review command normally.

On success it prints `REVIEW_OUTPUT_DIR=<path>` followed by the review. The
private directory holds the same review in `review.md`, the model setting and
reasoning effort in `metadata.json`, plus `effective-prompt.md`, `run.jsonl`,
and `err.txt` for audit.

The fresh-review marker names its output directory. The fresh response includes
the cold-review marker; the author records both.

Each prompt must name every input its reviewer may read. Start repo-relative
commands with `cd <repo> &&`. Do not include raw chat or the full context
packet. For commit / diff-message review, follow the specialization in
`{FA}/critic-iterate.md`.

The author revises from the fresh review's `review.md`; it already incorporates
the cold report. Every other file in either private directory is process
evidence.

Before accepting a prose review, confirm in the fresh-review trace that its only
cold-review launch precedes any source read or statement about the artifact,
that `REVIEW FRAME:` appears before any embargoed input is read, and that
`ARTIFACT CHECK:` appears before the cold-result file is read. Confirm in the
cold trace that it reads no undeclared source and launches no reviewer.

If the command fails, stdout is missing or unusable, or a trace check fails,
follow `{FA}/critic-iterate/review-failures.md`.
