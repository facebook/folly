Center writing and code rules on reader effort

The rules use local proxies such as brevity, actionability, and low abstraction
instead of asking what the intended reader needs to understand. Applied
mechanically, they can hide necessary relationships behind abstract shorthand,
retain unrelated facts for hypothetical usefulness, or reject a useful code
boundary merely because it adds indirection.

Make the reader's task the common test: preserve exactly the facts and structure
needed for correct understanding, then prefer the shorter form when clarity is
equal. This gives commit reviewers the model they need before opening the diff
and lets code abstractions earn their cost when they expose a real boundary,
shared rule, or useful symmetry.

Test Plan:
- Docs-only.
