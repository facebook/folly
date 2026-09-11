Make reader effort the test for writing, code, and review rules

The rules often substitute mechanical proxies for the reader's actual task. That
can produce both abstract shorthand that hides necessary relationships and
completionist detail that is true but irrelevant. It can also reject useful code
structure simply because it is an abstraction or require reviewers to
manufacture alternatives.

Make the intended audience's reading effort the shared test. Preserve exactly
the facts and relationships needed for correct understanding, and prefer the
shorter form only when comprehension is equal. This keeps commit messages
focused on the model reviewers need before opening the diff, while allowing
abstraction when a boundary, shared rule, or symmetry saves more reading than
its indirection costs.

Test Plan:

- Docs-only.
