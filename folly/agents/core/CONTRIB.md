> NOT A RULE. If loaded as task policy, stop and ask the user.

# Core rules

Every task loads this package, so keep it small. Add a rule here only when every
task needs it. Loading merely useful guidance everywhere wastes context and can
distract the agent.

The package starts at `../core.md`; `../core.entrypoint.md` supplies its loading
instruction.

Both current rules work across domains: `breadcrumbs.md` preserves task intent
over time, while `conflicts.md` resolves applicable instructions across scopes.
