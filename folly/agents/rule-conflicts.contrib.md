> NOT A RULE. If loaded as task policy, stop and ask the user.

Purpose: decide what to do when loaded rules disagree without treating read
order as priority.

- Loaded whenever new instructions are added through `rule-conflicts.loader.md`.
- Owns scope and specificity rules for conflicts.

# Why is "Preserve the active user request" in this package?

It is here only for trigger convenience. Agents tend to get confused in these
kinds of scenarios:

```
> user prompt
* load rules
* hook injection
* prior subagent completion
* Agent now forgot about user prompt, does something else.
```

Since this loader triggers on most of those, it may be a good place to remind
the agent to actually do what the user asked for.

Codex's default `model_messages.instructions_template` requires, “The final
answer must always be fully self-contained.” This rule narrows that default when
user changes question. Repeating completed work wastes tokens and can replace
requested follow-up.
