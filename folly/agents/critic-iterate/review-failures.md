- **Failed review command.** For missing `codex`, a non-Guardian auth / sandbox
  failure, nonzero exit, or timeout, inspect the command error, stop, and report
  the infrastructure failure.
- **Guardian rejection.** For a possible-exfiltration rejection of the
  fresh-review wrapper, follow `{FA}/critic-iterate/auth-prompt.md`.
- **Missing fresh-review output.** If stdout disappears after the fresh review's
  `REVIEW_OUTPUT_DIR` is recorded, use that directory's `review.md` only if it
  is nonempty, `run.jsonl` reaches `turn.completed`, and the trace checks in
  `{FA}/critic-iterate/run-review.md` pass. Otherwise, discard the round and
  start a new fresh review. Never scan temporary directories or infer a result
  from partial output.
- **Unusable fresh review.** If the command succeeds with no marker or with
  empty, off-topic, or malformed output, discard the round, tighten the prompt,
  and start a new fresh review.
- **Failed fresh-review trace.** Discard the round, fix its prompt if needed,
  and start a new fresh review before editing the candidate.
- **Nested reviewer failure.** The fresh reviewer reports a nested rejection or
  run failure and stops. Rerun only after the blocking policy or configuration
  changes; the top-level author or orchestrator starts the new round.

Each required reviewer check is incomplete until it produces usable output. Do
not proceed self-only or switch reviewer paths.
