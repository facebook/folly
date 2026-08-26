# Requirement breadcrumbs

Breadcrumbs keep a workstream's goal, requirements, and decisions independently
findable across turns, plan rewrites, interruptions, and interleaved work. They
do not mirror the current request, execution steps, or progress.

## When to post

Post a brief, user-visible goal when the workstream's goal, a requirement, or a
decision must remain independently findable across turns, plan rewrites, or
interleaved work. If Q&A grows into such a workstream, post the goal and durable
requirements or decisions already established. Multiple commands or plan steps
alone do not qualify.

Add another breadcrumb only for a requirement or decision likely to matter when
the work is reviewed, explained, or resumed. A `TODO` or `enqueue` request
belongs only in `update_plan` / `TaskCreate`. If it also establishes a goal,
requirement, or decision that must outlive the item's completion, breadcrumb
that durable content separately.

## Format

```text
REQ[<path>] GOAL: <current goal>
REQ[<path>]: <requirement or decision>
```

Use any useful slash-separated path. Give related work a shared prefix,
interleaved work distinct subpaths, and keep the path stable while it identifies
the same workstream.

## Update and recover

When the desired outcome becomes clearer, post another `GOAL` at the same path.
The newest wording is current, but does not discard earlier requirements. State
what a changed requirement replaces. Give a materially different outcome a new
path.

Recover the resumed path and its ancestors. If the resumed path is a parent,
also recover its descendants. For each recovered path, use the newest `GOAL` and
read its history back to the first `GOAL` so refinements do not hide
unsuperseded requirements or decisions. Keep execution steps and progress in
`update_plan` / `TaskCreate`.
