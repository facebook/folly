- **Guardian rejection.** For a possible-exfiltration rejection of the
  fresh-review wrapper, follow `{FA}/critic-iterate/auth-prompt.md`.
- **Review launch failure.** Stop and report setup failures, failures lasting 10
  seconds or more, permanent infrastructure failures, and failed retries. For
  any other non-Guardian failure under 10 seconds, diagnose it. The top-level
  author may retry once against the unchanged candidate only if it is
  mechanically correctable or likely transient; otherwise stop and report it.
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
- **Nested reviewer failure.** The fresh reviewer reports it and stops; the
  top-level author applies “Review launch failure.”

Each required reviewer check is incomplete until it produces usable output. Do
not proceed self-only or switch reviewer paths.
