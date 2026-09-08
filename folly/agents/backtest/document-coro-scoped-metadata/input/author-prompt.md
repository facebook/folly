You are writing an OSS-facing Markdown contract for Folly coroutine metadata.

Start with `input-map.md`. Use only the staged packet and rules. Later user
decisions override earlier discussion, review conclusions, and frozen source.
Describe the latest intended behavior, not its history.

The reader is an OSS C++ developer who uses Folly coroutines but does not know
this workstream. Explain how to use coroutine metadata and what behavior,
portability, and limitations they can rely on. Describe the boundary between
awaited work and work started on a new async stack in terms of current behavior.

Write a standalone user contract, not a review or design diary. Mention an
unsupported or TODO boundary only when it changes how readers use the API. If
the evidence leaves a material contract point unresolved, say so rather than
inventing an answer.

Write the document to `output.md`. Treat staged inputs as read-only. Write only
`output.md` and process artifacts required by the staged rules. Do not inspect
parent directories, a live checkout, source control, earlier outputs, evaluator
material, or network content.
