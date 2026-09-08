# Run the contract check

Use this optional check after `output.md` is frozen. Give every candidate the
same evaluator model and effort. A failed evaluator run says nothing about the
candidate.

In a fresh workdir, stage:

- the frozen output as `candidate.md`;
- `prompt.md` at the workdir root;
- `input-map.md`, `conversation.md`, `working-review.md`, `request.md`, and
  `later-design-clarifications.md` under `evidence/packet/`;
- `input/source/folly/coro/{AsyncScope,WithMetadata}.h.txt` as
  `evidence/api/{AsyncScope,WithMetadata}.h`; and
- `input/source/folly/debugging/symbolizer/StackTrace.h.txt` as
  `evidence/api/StackTrace.h`.

Run the evaluator with `prompt.md` as its prompt. Record the invocation and any
evaluator failure with the run artifacts.
