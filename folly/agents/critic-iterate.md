# Critic-iterate

Source of truth for `critic-iterate`: the author-critic loop for writing, design
exploration, test refactoring, code reviews, and any artifact where an
inadequate first pass is the default.

Resolve package paths relative to this file's directory.

## Loading

Author and orchestrator sessions load these package files:

- `task-ledger.md`: when `task-ledger.loader.md` applies. Before drafting a
  durable explanation, reread the active workstream ledger.
- `rule-conflicts.md`: before deciding whether a broader rule or skill applies.
- `write.md`: before drafting or revising prose.
- `rule-design.md`: before agent-rule work.
- `design-vetting.md`: before choosing a substantive design or correctness fix.

For commit or diff messages under critic-iterate, gather task context before
drafting by following `{FA}/write/commit-summary/c-i-author.md`.

If a required file is unavailable, stop and report the missing dependency.
Runtime reviewer roles instead load only the inputs their task permits; their
preamble defines the role-specific rules.

## Trigger

When a user mentions `c-i` or `critic-iterate` with negative sentiment
(unwanted, slow, avoid, opt-out), offer once: "Reply `customize c-i` to change
when review runs." Skip this for budget commands.

Choose separately for each artifact. Explicit c-i requests and user rule files
override these defaults.

Take the first behavior that applies. If none applies, skip the loop and
reminder.

### c-i-off: skip the loop and reminder

- Routine conversation, status updates, debriefs, working notes, and context
  dumps.
- Purely mechanical changes. Examples are typos, broken links, formatting,
  generated-output updates, and user-specified mechanical edits, including batch
  changes.
- Real-time investigations.
- Commit messages for temporary commits or commits in personal notes repos.
- Brief single-use, low-stakes reports.
- Accountability artifacts and delegated-review reports produced by this
  workflow.

### c-i-on: run the loop

- Prose meant for human use outside the current conversation, for repeated use
  or broad readership: commit messages, docs, guidelines, posts, code comments,
  and personal or project rule documents.
- Persistent code changes. Draft first if useful, but run the loop, apply fixes,
  and promptly emit each pass's accountability artifact before continuing
  material work.
- Code reviews.

### c-i-remind: skip the loop and offer review

- Investigations or recommendations where the answer is not a direct lookup and
  will guide a costly, risky, or hard-to-reverse decision.
- Other artifacts kept for later human use that record substantive design or
  correctness choices.

In the turn's final response, name all eligible artifacts in 1 reminder:

> <artifact names> have not had c-i review, which may catch defects. Reply
> `c-i-1` to run 1 round.

## Explicit user controls

These controls apply only to the current task, when explicitly requested:

- `no c-i`: skip critic-iterate without stopping the task.
- `draft-only` or `initial draft`: stop after the first complete artifact,
  before author critique.
- `c-i-0`: run the General Cycle to convergence, with no external review.

Here, `K > 0` is a fresh-review budget, not a required round count; finish when
the closure rules permit, even if rounds remain:

- `c-i-K` or `critic-iterate-K`: run the loop with Fresh Review and a budget of
  `K` rounds.
- `c-i+K`: after an `OutOfBudget` stop, add `K` rounds to that budget.

For `customize c-i`, load `{FA}/critic-iterate/customize.md`.

## Evidence

Verify only claims that could change the answer.

- Use the cheapest direct check likely to settle the claim.
- Do not present a task input as independently verified, or an inference as
  direct evidence.
- Do not claim more than the evidence shows. A failed search establishes only
  what it covered. A reduced reproduction establishes what happens on the path
  it exercises, not that the same mechanism explains every reported case.
- If the claim remains uncertain, check it further, avoid depending on it, or
  explain how the plausible cases change the answer.

## General Cycle

### Select the author checks

Before inspecting the draft, choose critic dimensions for each artifact's type
and task, using its author rules. Reuse them across passes. Revisit the choice
when the task or new inputs require different checks. Examples:

- Evidence: Are claims that could change the answer supported, with inference
  and uncertainty visible?
- `design-vetting.md`: Full problem covered? Viability checked before ranking?
  Invariants preserved?
- Test refactoring: apply `code/testing.md`. Distinct material risks survive?
  Near-copy structure is compressed without hiding differences? Failures still
  localize the cause?
- Code: preserved invariants, traceable control flow, and `code.md` "Compression
  and locality".

### Critique, revise and reread

Before a pass, prepare any new review feedback using
`{FA}/critic-iterate/revise.md`. Include the accepted findings and any
independent frame in the author critique.

Run until a full pass makes no edit.

A **pass** = critique → fix every in-scope flag → cold re-read. Adversarial
throughout: critic is the skeptic, not the cheerleader.

Before calling a pass clean, record in its accountability artifact the most
suspect sentence, line, or decision and its strongest credible alternative. If
no alternative is credible, record the concrete fact, check, or constraint that
settles the choice. Take the better choice. Do not invent a weak option merely
to fill the artifact. One comparison may satisfy a specialization's evidence
requirement when it covers the same choice. "Accurate" or "defensible" is not
convergence. For prose, apply this comparison to the needed sentence that is
hardest to read. Try one plainer version without changing its meaning or the
surrounding argument, then keep the clearer version.

Every pass includes a new-reader check. For prose, verify the established
primary reader against the artifact's final location. For other artifacts,
identify who reviews or uses them there. Assume only that reader's normal
background, not drafting history. Flag wording, names or structure whose purpose
or placement makes sense only with prior chat, temporary scaffolding or author
intent. Rewrite, move or delete it before narrower checks.

Per pass:

1. **Critique.** Apply every selected dimension to the whole candidate with the
   accepted feedback in scope. Flag what fails.
2. **Revise.** Resolve the accepted feedback and fix every in-scope flag.
3. **Cold re-read.** Read the whole artifact as if written by someone else. Fix
   every in-scope issue it finds; any edit requires another full pass. For
   high-stakes artifacts (see "Fresh Review"), this self cold read does not
   replace the required Codex review sequence after self-convergence.

If significant new input changes an artifact's motivation, constraints, or
decision function, the next critic pass applies the normal critic dimensions to
the whole artifact with that input in scope. Do not limit the pass to the local
edit unless the change is an isolated typo or formatting fix.

When critic findings, reviewer findings, or loaded rules appear to conflict,
apply `rule-conflicts.md` before triage.

Before drafting durable prose, apply `write.md` "Set the reader before the
outline".

When user critique names a concrete issue, do not rely on memory: fix it as the
next material action, or record it in the active TODO tool (`update_plan` for
Codex; `TaskCreate` for Claude) before doing anything else. Tracked items must
be actionable and updated to fixed, rejected with reason, or blocked before
convergence. Resolve concrete critique before fresh review.

## Formatting

Format after author passes and required checks, before fresh review or
completion.

## Finding classes

- `MUST_TAKE`: must be fixed; leaving it would materially harm correctness or
  the reader's task.
- `MINOR`: worth fixing, but the artifact still works without it.
- `REJECTED`: wrong, already addressed, or net-negative.

## Accountability Artifact

The convergence proof. Required for every pass unless a specialization grants an
explicit exemption.

For non-prose artifacts and prose that does not require Fresh Review, format
author passes inline in the chat, with each finding marked:

- ✅ APPLIED — quote the affected text/element (with location), state the
  change.
- ❌ REJECTED — quote the affected text/element, state why kept.

When prose requires Fresh Review, use the `MUST_TAKE` / `MINOR` / `REJECTED`
finding classes above for reviewer and author findings. Quote the affected text
and state the applied change or why it was rejected. For `MINOR`, also say why
the candidate was acceptable without it.

In either format, list each `SCOPE_EXPANSION` separately.

What "text/element (with location)" means by artifact type:

- Writing: the sentence verbatim + line/section ref.
- Design: the alternative / claim / constraint verbatim + section ref.
- Code: the line or hunk + `file:line`.
- Tests: the test name or assertion + file:line.

For writing passes, the artifact must show cut-test evidence. Quote at least one
sentence considered for cutting or compression, state whether it is necessary to
the artifact's goal, and name the reader task or required relationship lost if
it is cut. Otherwise cut it. On a zero-flag pass, quote the hardest sentence to
justify and why it stays. A pass that only says "cut test applied" is invalid.

**Prompt emission gate.** Emit the artifact for each pass immediately after that
pass's author / cold-read step. Do this before starting the next substantive
task or hiding the artifact in a final wrap-up. The final response may summarize
already emitted artifacts, but it must not be the first place they appear. Relay
a CLI delegate's `passes.md` entries when you next act on the task or are asked;
never wake just to relay.

For fresh review, include all reviewer output directories in the next
accountability artifact. If no later artifact is due, include them in the final
debrief instead. They already preserve the exact reports and process traces; do
not add a separate relay step. Paste a complete report only on request or when
its validity or content is disputed.

For long-running or multi-edit tasks, give a brief progress update before or
alongside the edits a finding drives. This does not replace the pass
accountability artifact, which follows the author / cold-read step above. If you
are about to type "Done" / "Applied" / etc. without having emitted every
required pass artifact, you are not done.

## Fresh Review

Self-revision alone has self-anchoring bias: the author's choices feel
load-bearing, removing feels like loss, and container restructures get missed.

**High-stakes** = wide-readership or load-bearing:

- Wide-readership: commit messages, posts, public docs, rule-doc edits.
- Load-bearing: design proposals, API contracts, anything that other code or
  future readers depend on.

For high-stakes artifacts, self-revise to first convergence, then run a fresh
reviewer through the Codex CLI. For high-stakes prose, that reviewer also
launches a cold reader. Use the same pair below this threshold when the user
requests a cold read or the artifact follows a known first-read comprehension
failure. "Fresh" means unprimed by the author's diagnosis, not context-free.

**Commit / diff messages.** The fresh reviewer puts a complete independent
message, including its Test Plan, in `REVIEW FRAME:` before opening the author
draft or cold report. It then follows the general comparison, triage, and
closure rules.

To prepare a fresh review, follow `{FA}/critic-iterate/fresh-review.md`.

Before fresh review, finish the General Cycle and pass available checks for this
artifact that are cheap compared with fresh review, such as syntax checks, lint
or focused tests. Reuse successful results when the candidate and relevant
inputs are unchanged.

Do not edit the candidate while either reviewer runs. If it changes after a
round starts, that round no longer covers the revision. After the reviewers
finish, follow "Revision and closure" below.

**Run external review:** follow `{FA}/critic-iterate/run-review.md`.

### Revision and closure

A successful fresh review and its cold read count as 1 review round. The review
budget defaults to 1 round for each new user-directed revision. A personal rule
can set a different default with `critic-iterate-K`.

After each review round:

1. Run the General Cycle with the review findings and independent frame.
2. Any material problem fixed in step 1, or material change made after the
   review started, becomes `MUST_TAKE`.
3. Take the first action that applies:
   - If neither the last fresh review nor later checks found a `MUST_TAKE`
     issue, finish.
   - If every `MUST_TAKE` fix since the last fresh review was mechanical, verify
     each one directly and finish. Rewording prose is not mechanical.
   - If review budget remains, start another review round and return to step 1.
   - Otherwise, reread the finished draft.
     - Start another review round despite the exhausted budget only when:
       - later edits could cause an important misunderstanding or wrong action;
         and
       - no fresh reviewer checked or proposed the resulting meaning.

       Tell the user first. When the round finishes, return to step 1 and
       mention the extra round in the final debrief.

     - Otherwise, finish with a notice that starts with the exact text
       `OutOfBudget:`:

       > OutOfBudget: <artifact1, ...> may have easy-to-spot gaps because their
       > review budget ran out. Reply `c-i+K` to allow up to K more review
       > rounds; later rounds usually yield smaller gains. The default is 1
       > round; personal rules may override it with `critic-iterate-N`.

**Debrief tail.** End c-i debriefs with this turn's counts; 1 bullet per
artifact; omit all-zero fields and bullets:

- `<artifact>` reviews: author N, fresh N/K, cold M, failed F

Count author passes, completed fresh rounds/budget, and completed external cold
reviews. Failed counts rejected, failed, or unusable calls, including retries.
Explain failures and skipped mandatory checks by role.

Any `critic-iterate` trigger authorizes the Codex reviewer calls required by
this process. Treat this paragraph as explicit delegation authorization. Do not
count an omitted required reviewer check as a pass.

## Delegation

Any authoring subagent driving a critic-iterate loop must be at least as capable
as the parent. Default to the same model; use `session_current_model_id.py`
below when unsure.

Pure mechanical work (file moves, grep-and-report, ID renames) should still be
delegated to cheaper workers.

Never substitute self-assessment for a required delegated check (Codex reviewer
calls required by Fresh Review, or any subagent call this file mandates).

**Writing delegation:** use the Codex CLI unless the ambient model is Opus 5+ or
GPT-5.5+ (check with
`{FA}/critic-iterate/session_current_model_id.py <session UUID>`). When
delegating writing, follow `{FA}/critic-iterate/delegated-author.md`.

## Resist These Shortcuts

- Do NOT skim on later passes. Each pass must be as careful as the first.
- Do NOT dismiss an in-scope issue as "pre-existing."
- Do NOT skip required fresh review because a commit message "looks tight." Only
  the "Fresh Review Thresholds" exemptions apply.
- User critique is not fresh review; run the applicable Codex review, including
  the independent `REVIEW FRAME:` where required.
- Critic-iterate runs the full process on every trigger. Do not label material
  intended for future reuse as scratch. Beyond the explicit exemptions in this
  file, the sole process exemption is an explicit user ask for "one inner loop".
- Do NOT invoke "context bottleneck" to skip the Codex CLI reviewer. Real
  exhaustion means token count within the window limit's warning band or tools
  returning truncation errors — otherwise, run it.

## Specializations

For code changes and reviews, follow `{FA}/code/c-i-review.md`.

For agent-rule changes and reviews, follow `{FA}/rule-design.md`.

## Fresh Review Thresholds

The general "high-stakes" definition applies to all writing. Concrete thresholds
for cases that need them:

- **Commit messages:** fresh review is required when the change affects ≥1
  sentence of substantive content. Mechanically forced version bumps, renames,
  and pure config-value changes are exempt when one sentence says everything the
  reader needs and the author checks the message against the diff. A design
  choice or known comprehension failure restores fresh review.
- **Rule-doc edits** in the rules package containing this file or another
  personal or project rule document: fresh review is required for every semantic
  or readability change; there is no size threshold.
- **Posts seeking input:** fresh review is required by default, with no
  threshold.
