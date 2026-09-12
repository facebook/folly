# Task ledgers

Keep a short Markdown ledger as the source of truth for the workstream's current
intent and unfinished work. The ledger is current state, not a history.

## Maintain the ledger

- Keep 1 short, descriptively named file per workstream under `~/task-ledgers/`
  so it can be found after resume. If it contains multiple goals or substreams,
  organize them under separate `#` headings.
- Keep goals and requirements separate from tasks. Record only the detail and
  rationale needed to resume or verify the remaining work.
- Update the ledger before continuing when the user adds work or changes intent.
  `Enqueue <work>` adds work without preempting the current task.
- If available, keep a standing Codex `update_plan` or Claude `TaskCreate` item
  to reread the ledger after compaction or resume.

## Recover and prune

After compaction or resume, reread the whole ledger before acting. Treat it as
more authoritative than an automatic summary; a later direct user instruction
still supersedes it.

Before marking or removing a task as done, check the current artifact against
the task's current requirements; this also reconciles a stale open task. Never
infer active work from completed history. Delete the task once it no longer
helps remaining work, and keep only requirements or rationale that still
constrain the workstream.

Keep goals, unsuperseded requirements, and needed context for the workstream's
lifetime. Remove that context only at the user's request or after a durable
artifact supersedes it.
