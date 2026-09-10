Load `agents/critic-iterate.md` for:

- explicit review or critic-iteration requests;
- prose meant for human use outside the current conversation;
- persistent code changes or code reviews;
- other artifacts kept for later human use that record substantive design or
  correctness choices; or
- investigations or recommendations where the answer is not a direct lookup and
  will guide a costly, risky, or hard-to-reverse decision.

Routine conversation, status updates, debriefs, working notes, and context dumps
trigger only through another condition above.

Purely mechanical changes do not trigger by default, even when repeated.

This trigger authorizes the reviewer calls required by
`agents/critic-iterate.md`.

Also load:

- `agents/writing.md` first for prose;
- `agents/code.md` too for code edits or reviews.
