# Request

No, the Task-only composition is not intentional. An easy follow-up should
extend this to `now_task`, `safe_task`, and other Task wrappers. Supporting a
Task that already has an executor is TODO, but not part of the immediate stack.

---

Write a document that cleanly defines the user-facing, in-code contract. It may
eventually live under `folly/tracing/docs/`; for now, write it beside the
review.

It should address the questions already raised, including open ones such as the
structured/escaping boundary. For that boundary, describe what the current
implementation does rather than debating possible abstractions.
