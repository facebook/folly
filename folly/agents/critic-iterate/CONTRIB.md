> NOT A RULE. If loaded as task policy, stop and ask the user.

# Critic iteration

Independent review is useful only when each role sees the intended context. The
process also leaves evidence that each required check ran.
`{FA}/critic-iterate.md` coordinates author passes and external review. The
fresh- and cold-review preambles define the reviewer roles. `codex-reviewer.py`
runs them; `session_current_model_id.py` identifies the ambient model used by
the delegation rule.

- `run-review.md` owns the normal launch and validation procedure.
- `review-failures.md` owns external-review launch, output, and trace failures.
- `delegated-author.md` owns the conditional writing-author handoff.

`critic-iterate.md` is loaded on the common path, so unused guidance there
consumes context on every run. Keep writing, code, test, and design guidance in
their `{FA}` packages; put reviewer machinery and conditional author flows in
`{FA}/critic-iterate/` children loaded only when needed.

Having ambient GPT Luna delegate code authorship to another model might improve
quality. The rules do not currently require it: using Luna to author code is
uncommon, and callers likely chose it to save cost, so another model call would
be unexpected.
