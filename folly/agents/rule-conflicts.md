# Rule conflicts

Resolve scope before priority. A rule written for the narrower artifact or
situation decides whether broader rules apply there (specificity:
exact-situation rule first, then sub-project, project, repo), even if the
broader rule says `MUST` or claims priority. If explicit scoping settles the
issue, follow it silently. If rules still apply and point different ways, track
the conflict in the active task tool (`update_plan` for Codex; `TaskCreate` for
Claude) and name the unresolved conflict in the final debrief.

# Preserve the active user request

Whenever rules are loaded, a hook adds instructions to the context, or an
asynchronous result arrives, reread the latest user message. Re-evaluate which
user tasks remain active, then continue them while incorporating the new input.
