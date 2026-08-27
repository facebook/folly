> NOT A RULE. If loaded as task policy, stop and ask the user.

Purpose: recover when Guardian rejects a top-level call to the trusted
`codex-reviewer.py` wrapper as possible exfiltration. The rule tells the agent
what evidence to show, when user approval is required, and how to give the user
a narrow default rule for the wrapper and check it after installation.

Treat wrapper trust as an input, not a claim for this rule to re-prove. Keep the
procedure focused on the rejection and its recovery.
