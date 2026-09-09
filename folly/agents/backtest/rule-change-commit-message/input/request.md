metaedit the current commit df0d0d47ba with a proper summary/test plan message.

IIRC you have a pretty reasonable "rationale doc" from the hydration, use the appropriate part of that, don't make the commit story worse.

To reiterate the requirement: the message is the problem being solved, NOT a restatement of the code. It optimizes for understanding the map, and making it easier to consume the diff. You do not repeat the change itself.

As a secondary, lower-fi source of motivation, here's an external inference of what the changes are meant to do, but trust the "input" doc more.

---

From the diff alone, the change appears intended to make reader effort — not
word count, diff size, or abstraction count — the shared optimization target for
writing, code, and critic iteration. Agents should preserve exactly the facts
and relationships their audience needs, then choose the shortest equally clear
form.

It appears aimed at these recurring failures:

- Prose is shortened until causal detail, useful identifiers, or evidence needed
  for understanding disappears.
- Formatting and explanation length follow fixed thresholds instead of the
  reader's task.
- Code is inlined or extracted mechanically based on use counts, anticipated
  reuse, or visual symmetry rather than readability, invariants, and real
  boundaries.
- Critic artifacts manufacture alternatives, simplifications, or exhaustive
  decision histories to prove that work happened.
- Details are kept or removed based only on whether they change an immediate
  action, overlooking information needed to understand or verify the result.
