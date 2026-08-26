# Testing

Resolve package paths relative to this file's directory.

Optimize for material regressions caught per maintenance and runtime cost. A
changed unit does not automatically need a new test.

Project rules refine this file. For gtest, also load `gtest.md`.

## Add or extend coverage

Before adding a test, answer:

- **Behavior:** What observable contract or failure mode does it protect?
- **Risk:** What plausible change would break it, and why would that matter?
- **Signal:** What outcome would reveal the regression?
- **Increment:** Why would existing coverage miss it?
- **Cost:** Is this the cheapest sufficient test, and will its protection repay
  its maintenance and runtime?

If an answer is missing, do not add the test. Extend existing coverage when it
can catch the risk with an equally useful failure. Several changed units may
need only one test; one observable behavior may need several cases when they
protect distinct material risks.

Test at the cheapest boundary that establishes the real contract. Prefer
observable outcomes over internal call sequences. Do not broaden a production
API solely for tests.

## Design before mocking

Before adding a mock or test-only hook:

- Separate pure policy from effects when that creates a real boundary.
- Make nondeterminism and external results explicit at their actual boundary.
- Depend on a narrow capability for a genuine external boundary, not an
  interface created only to mock one implementation.
- Use a real local dependency or behaviorally faithful fake when affordable.

Use a mock only when every condition holds:

- The dependency is external or effectful and impractical to exercise on every
  run.
- No higher-fidelity substitute can test the behavior at reasonable cost.
- The boundary is narrow and stable.
- Assertions target observable behavior. Assert interactions only when they are
  the contract.
- The mock models relevant success and failure behavior from an authoritative
  schema or observed behavior.

State the remaining integration risk.

## Refactor repeated tests

Near-copy tests do not imply redundant coverage. Classify the protected risks
before changing their structure:

- Table-drive cases only when the data variants exercise the same contract and
  protect no distinct material risks. Each row must identify the broken case.
- Keep separate test names for distinct material risks or materially better
  diagnosis. Extract repeated flow only when the helper reduces net reader
  effort, and keep meaningful differences explicit at each call site.
- Compose small local state objects for shared setup. Split unrelated state
  instead of accumulating hidden or inherited setup.
- Keep preconditions, action, and outcome traceable. A helper or default that
  hides a material difference has compressed the wrong thing.

After refactoring, verify that every distinct risk remains covered and each
failure still identifies the broken case.

## Prune

Before materially editing or deleting an existing test, ask: **If this test were
absent, which material regression could escape?**

- Delete or merge a test when another catches the same material regression with
  an equally useful failure. Keep overlap only for a distinct material risk or
  materially better diagnosis.
- Delete assertions that only repeat construction or implementation shape.

When uncertainty remains and the check is cheap, temporarily introduce the
smallest plausible bug. Keep the candidate test only if it fails for the stated
reason and existing tests either miss the bug or diagnose it materially worse.
