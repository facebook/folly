# Using `enqueue` and `c-i`

Most `folly/agents` rules run automatically. Use `enqueue` to track follow-up
work and `c-i` to control review effort.

## `Enqueue` into the [task ledger](task-ledger.md)

`Enqueue` records a follow-up without interrupting the current task. Use it when
work is interleaved or spans a long session.

## [Critic-iterate](critic-iterate.md) aka `c-i`

Critic-iterate is a review-and-revise loop for prose, code, and more.

An agent drafts, self-reviews, and revises until convergence. Then it runs the
external review loop:

- A fresh agent critiques.
- The author applies feedback.

The loop ends with:

- Convergence: no significant issues remain.
- `OutOfBudget`: the budget runs out first, so significant issues may remain.

The default budget is 1 round, but critical issues auto-extend it.

Control review effort with:

- `c-i-K`: budget up to `K` external-review rounds; `c-i+K` adds more.
- `c-i-0`: use author review only.
- `draft-only`: pause before self-review to align cheaply on purpose and shape,
  then use `c-i-K` for review.
- `no c-i`: complete a multi-step task without review; normal drafting rules
  still apply.

`c-i-1` typically takes 2–5× as long and uses more tokens than running without
these rules, in exchange for substantially higher quality. You can define
"quality"; by default, `folly/agents` optimizes for reader effort.
