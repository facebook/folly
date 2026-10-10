# Update user rule files

Use this when adding or changing personal user rules. The caller supplies the
content, confirmation flow, and backup suffix, if any. Never use repository rule
files for personal defaults.

## Find

1. Use personal paths specified by the caller. Otherwise select each file whose
   agent directory exists:
   - Claude Code: `~/.claude/CLAUDE.md`
   - Codex: `~/.codex/AGENTS.md`

   If none of those directories exists, ask the user where to keep their rules.

2. Resolve symlinks only to avoid editing the same file twice.
3. Read each distinct existing file before planning changes.

## Update

1. Follow the caller's confirmation flow. If a backup suffix was supplied, copy
   each existing destination that will change to `<file><suffix>` before
   editing.
2. Update or insert the approved content, creating a selected rule file when
   needed. Preserve all other content; never replace the whole file.
3. Read each full changed file.
