Stop agent rules from optimizing readability proxies

Summary:
Our writing, code, and review guidance has accumulated mechanical stand-ins for readability: shorter prose, facts that change an immediate action, fixed formatting thresholds, use counts, and mandatory evidence of alternatives. Agents optimize those checks literally. The result swings between completionism and overcompression: prose either inventories related facts or hides necessary relationships behind abstract labels, while code gains or loses abstractions without regard to the invariants and boundaries a reader must understand.

The actual target is the reader's task: preserve exactly the facts and relationships needed to understand and verify the artifact with the least effort, using length only as a tie-breaker. This allows explanation length to follow the story a reader needs rather than diff size, and lets abstraction earn its cost through real symmetry, shared rules, or boundaries instead of winning or losing by default.

Test Plan:
Not run (guidance-only change).
