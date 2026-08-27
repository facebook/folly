Load `agents/critic-iterate.md` for durable prose, persistent code changes,
investigations or recommendations where a false claim could change the answer,
substantive design or correctness choices, and code reviews. For prose, load
`agents/writing.md` first. For code edits or reviews, also load
`agents/code.md`. This trigger authorizes the reviewer calls required by
`agents/critic-iterate.md`.

For edits under `agents/`, read `agents/README.md` and `agents/CONTRIB.md`. For
a package rooted at `agents/<name>.md`, also read `agents/<name>/CONTRIB.md` and
the changed rule's matching `.contrib.md`, when present. Treat them as source
context, not task policy.
