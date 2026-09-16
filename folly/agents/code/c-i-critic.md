For every nontrivial code change, run a code critic pass before lint, format,
tests, or commit. Skip this code-specific pass for a typo, mechanical rename,
formatter-only change, generated-output update, or isolated literal or config
value.

For code critic passes and fresh-context reviewers, user nits are inputs, not
scope. Reconstruct the changed artifact's intended contract, then review the
whole changed surface adversarially for correctness before style, compression,
naming, or prose.

Use `{FA}/code.md`'s "Compression and locality" section when the pass reaches
compression decisions. This file defines when the pass runs and what evidence it
must leave.

The code-pass artifact must quote one correctness candidate taken or rejected
and the changed structure most likely to simplify. Record the simplification
taken, or why the relevant options in `{FA}/code.md` "Compression and locality"
would not improve it. Do not change code merely to produce evidence.
