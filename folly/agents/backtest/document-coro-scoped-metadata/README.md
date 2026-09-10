# Explain scoped coroutine metadata

Coroutine metadata lets stack-reading tools attribute suspended work to an
application operation. It follows awaited coroutine work but stops when work
starts on a new async stack. This scenario asks an agent to explain that
contract to Folly users who know coroutines but not the async-stack
implementation.

The packet resembles a real workstream: discussion changed direction, a review
captured an intermediate design, and the frozen source predates later decisions.
`input/input-map.md` defines which evidence wins. The author must recover the
current contract without turning implementation history into user documentation.

The run ends with the reviewed `output.md` (unless in no-rules mode). Inspect it
for factual errors or omissions that could lead a reader to misuse the API,
misread captured metadata, or integrate it incorrectly. Report the specific
wrong action each problem could cause; omit minor detail that would not change
reader action.

Human comparison is the default. The [optional contract check](eval/SETUP.md)
reports factual conflicts and omissions when the public API and an ordinary read
do not settle accuracy.

Run this scenario with the [parent runner instructions](../README.md), using
this directory in place of `SCENARIO`.
