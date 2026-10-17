External review runs exactly this:

```bash
[[ -z "${TMPDIR:-}" || "$TMPDIR" == /* ]] || { echo "TMPDIR must be absolute" >&2; exit 2; }
review_tmp=$(mktemp -d)
# Write fresh-prompt.md here; add cold-prompt.md when prose review requires it.
"{FA}/critic-iterate/codex-reviewer.py" \
  --preamble-dir "{FA}/critic-iterate" \
  --preamble fresh-review-preamble \
  "$review_tmp/fresh-prompt.md"
```

For a review with prose targets, include this nested command in
`fresh-prompt.md`, replacing `{FA}` and `$review_tmp` with their absolute paths:

```bash
"{FA}/critic-iterate/codex-reviewer.py" \
  --preamble-dir "{FA}/critic-iterate" \
  --preamble cold-review-preamble \
  "$review_tmp/cold-prompt.md" >"$review_tmp/cold-result.txt"
```

Do not prefix either wrapper call with an environment assignment. Hermetic runs
use the copied rule's sibling preambles.

On success, each review command prints `REVIEW_OUTPUT_DIR=<path>` followed by
its report. The cold command's output goes to `cold-result.txt`.

Each private output directory holds the same review in `review.md`, the model
setting and reasoning effort in `metadata.json`, plus `effective-prompt.md`,
`run.jsonl`, and `err.txt` for audit.

Record the fresh-review marker. For prose review, also record the required
cold-review marker.

Make launch commands self-contained: use absolute paths or define path variables
in the command.

Each prompt must name every input its reviewer may read. Start repo-relative
commands with `cd "<repo>" &&`. Do not include raw chat or the full context
packet. For commit / diff-message review, follow the specialization in
`{FA}/critic-iterate.md`.

The author revises from the fresh review's `review.md`; it already incorporates
the cold report. Every other file in either private directory is process
evidence.

Before accepting a review, check that `REVIEW FRAME:` precedes every embargoed
read. For prose reviews, also check the fresh trace:

- One cold launch precedes any source read or statement about the artifact.
- `ARTIFACT CHECK:` precedes reading the cold result.

For prose reviews, check that the cold reader uses only its assigned inputs and
launches no reviewer. In mixed opening/whole-candidate tasks, its opening
assessments must precede whole-candidate reads and remain unchanged in the final
report.

If the command fails, stdout is missing or unusable, or a trace check fails,
follow `{FA}/critic-iterate/review-failures.md`.
