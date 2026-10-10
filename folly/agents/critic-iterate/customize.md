# Customize critic-iterate

Use this for `customize c-i`. Change the user's default artifact selection, not
the current task's review budget.

1. Load `{FA}/detail/update-user-rules.md` and follow its Find steps, using the
   agent's configured global rule path if known.
2. Use the conversation to ask a focused preference question; without that
   context, offer the current defaults as a starting point. Do not announce file
   reads, narrate rule requirements or approval steps, or make the user learn
   rule syntax.
3. After the user chooses, draft a short block with only their overrides:

   ```markdown
   # Critic-iterate preferences

   Override the package's default loading and artifact selection. Load
   `{FA}/critic-iterate.md` for c-i-on and c-i-remind matches.

   - c-i-on: <artifacts to review>
   - c-i-remind: <artifacts to offer review for>
   - c-i-off: <artifacts to skip without a reminder>
   ```

   Omit unchanged categories. Make categories unambiguous; explicit requests for
   the current task still override these defaults.

4. If they use both AGENTS.md and CLAUDE.md, ask whether both should change.
   Show the exact edits and placement as `-U3` patches. Wait for approval of
   that patch before following the shared rule's Update steps. If the patch
   changes, show it again for approval. Update existing preferences instead of
   duplicating them.
5. Report the saved defaults and paths. If a file cannot be edited, give the
   approved block for the user to paste.
