# Resume task ledgers

This rule never starts ledger tracking; it only recovers or isolates ledgers
already in use.

If the current context identifies active ledgers, read only the `Owner UUID`
from each. If the line is missing, add `Owner UUID: {current}` to that ledger.
Use ledgers owned by the current session. For each mismatch, make a session copy
to use instead of the parent. Follow this template:

```sh
sed 's/^Owner UUID: {parent}$/Owner UUID: {fork}/' < file.md > file-{nonce}.md
```

If the context identifies no ledger, search `~/task-ledgers/` for the current
`Owner UUID` and use every match.

If any ledgers are found, load `{FA}/task-ledger.md`, read them in full, and
continue from their recorded state.
