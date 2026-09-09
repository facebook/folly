Make reader effort the objective across Folly agent rules

Some rules still used brevity and call counts as shortcuts for readability. They
could therefore favor a smaller artifact while forcing readers to reconstruct
missing relationships.

Set one standard: give the intended audience exactly the facts and relationships
needed for the artifact's purpose with the least reading effort. Brevity wins
only when comprehension is equal. Abstraction has no default direction; weigh
the clarity it adds against the indirection it creates.

Test Plan:

- Docs-only.
