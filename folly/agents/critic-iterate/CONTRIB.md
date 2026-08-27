> NOT A RULE. If loaded as task policy, stop and ask the user.

# Critic iteration

Independent review is useful only when each role sees the intended context. The
process also leaves evidence that each required check ran.
`../critic-iterate.md` coordinates author passes and external review. The fresh-
and cold-review preambles define the reviewer roles. `codex-reviewer.py` runs
them; `session_current_model_id.py` identifies the ambient model used by the
delegation rule.

Rules for what good writing, code, tests, or designs look like stay in their own
packages. Keep each child here focused on one reviewer role or the machinery
that runs reviews.
