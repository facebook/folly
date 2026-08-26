# Design vetting

Use this before choosing a substantive design or correctness fix.

## Frame the decision

State only what the decision needs:

- the outcome the design must produce;
- the triggers and failure modes it must handle;
- the constraints and invariants it must preserve; and
- the unknowns that could change which design wins.

A reproduction is one case, not the problem definition. Say when the known cases
may be incomplete.

## Settle important unknowns early

Sketch credible approaches only far enough to expose the assumptions that
separate them. Do not invent a weak option to fill a list. Before developing any
branch, look for cheap work that could eliminate it or change the ranking. Start
with the check most likely to disprove a candidate or resolve an assumption
shared by several candidates. Useful checks include:

- reading the interface that defines the behavior;
- tracing a real path or running a tiny end-to-end case;
- measuring the disputed quantity; and
- building a throwaway prototype.

Stop exploring a branch once a decisive constraint or observation rules it out.

When an important unknown is not cheap to settle, prefer a design that does not
depend on it. Branch on an unresolved unknown only when its possible outcomes
change a candidate's viability or the final choice. Do not explore combinations
that lead to the same decision.

## Screen candidates before elaborating them

Reject a candidate that:

- misses a required trigger or failure mode;
- breaks a constraint or invariant;
- depends on a contradicted claim; or
- can plausibly fail with unacceptable consequences.

An unsupported assumption is an unknown to settle or design around, not a reason
to rank a candidate as viable.

## Compare the survivors

Use only factors that could change this decision. For correctness work, compare
these before convenience:

1. **Coverage and placement.** Does the design cover every required trigger and
   failure mode at a layer that has the needed information and owns the
   invariant?
2. **Assumptions.** Which claims must remain true, and how well are they
   supported?
3. **Failure behavior.** If the design is wrong or a dependency fails, does it
   expose the problem and limit harm, or continue with a plausible wrong result?

An existing production design supports a candidate only if it produces the
relevant behavior for the same reason and under conditions that also hold here.
State the behavior, reason, and matching conditions. Similarity alone neither
proves nor disproves the candidate.

Then compare only the costs that matter here:

- implementation and maintenance complexity;
- performance or lost capability; and
- rollout or reversal risk.

Cost cannot rescue a candidate that fails the viability screen. Among viable
candidates, state the tradeoff that decides the choice.

When a choice is required and reasonable investigation finds no viable design or
no clear winner, stop and state what remains unresolved and what evidence or
decision would settle it. Offer conditional branches only when the requested
artifact can use them; do not invent behavior to complete the design.

Before writing the implementation plan, test the leading candidate against the
hardest known case. For each unresolved unknown, consider only outcomes that
could change viability or the choice. Record what remains uncertain and what
observation would justify revisiting the choice.
