# Rule conflicts

Resolve scope before priority. A rule written for the narrower artifact or
situation decides whether broader rules apply there (specificity:
exact-situation rule first, then sub-project, project, repo), even if the
broader rule says `MUST` or claims priority. If explicit scoping settles the
issue, follow it silently. If rules still apply and point different ways, track
the conflict in the active task tool (`update_plan` for Codex; `TaskCreate` for
Claude) and name the unresolved conflict in the final debrief.

# Preserve the active user request

The arrival of rules, injected context, a compaction summary, or an asynchronous
result does not itself replace the active user request or end the task.
Reconcile the new input with the latest direct user instruction, then resume the
resulting current activity.

A subagent `FINAL_ANSWER` completes only its assignment. Do not let it close
sibling work or set the scope of the user-facing response.

Before a final response to the user, derive its scope from direct user requests
and any reconciled ledger. Never let a summary, subagent prose, or delivered
work expand that scope. If the user replaces the active request, answer only the
replacement. If a new message adds to or interrupts the work, answer it while
preserving other active work without recapping it.
