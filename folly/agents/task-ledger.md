# Task ledgers

Keep a short Markdown ledger as the source of truth for the workstream's current
intent and unfinished work. The ledger is current state, not a history.

## Maintain the ledger

- Keep 1 short, descriptively named file under `~/task-ledgers/` for each
  workstream this session owns, so it can be found after resume. Start it with
  `Owner UUID: <current-session UUID>`. If it contains multiple goals or
  substreams, organize them under separate `#` headings.
- Keep goals separate from tasks. Keep each task's current requirements with it.
  Make the current activity and every outcome awaiting delivery explicit. Record
  only the detail and rationale needed to resume or verify the remaining work.
- Update the ledger before continuing when the user changes intent or a tracked
  task changes state. `Enqueue <work>` adds work without preempting the current
  task.
- Edit ledgers only to change tracked state, using narrow patches. Never format
  them.

## Recover and debrief

After compaction or resume, reconcile the current activity and undelivered
outcomes from the whole ledger. Use an automatic summary only to locate
evidence. If the task set is incomplete or unclear, repair it from the relevant
direct messages and artifacts; ask the user if ambiguity remains.

Immediately before the final response, prune each in-scope task that is done.
Then send the response before doing anything else. Do not recap other work
unless the user asks.

Keep goals and any requirements or context that still constrain the workstream.
Remove them at the user's request, when a durable artifact supersedes them, or
when the workstream closes.
