# Critic-iterate

Source of truth for `critic-iterate`: the author-critic loop for writing, design
exploration, test refactoring, code reviews, and any artifact where an
inadequate first pass is the default.

Resolve package paths relative to this file's directory.

## Loading

Author and orchestrator sessions load these package files:

- `core/breadcrumbs.md`: always. Before drafting a durable explanation, recover
  applicable breadcrumbs as specified there.
- `core/conflicts.md`: before deciding whether a broader rule or skill applies.
- `writing.md`: before drafting or revising prose.
- `writing/concise-rules.md`: before editing a rule document.
- `design-vetting.md`: before choosing a substantive design or correctness fix.

If a required file is unavailable, stop and report the missing dependency.
Runtime reviewer roles instead load only the inputs their task permits; their
preamble defines the role-specific rules.

Resolve these once from `PATH`; use the fallback if absent:

- `codex-reviewer.py`: `critic-iterate/codex-reviewer.py`.
- `session_current_model_id.py`: `critic-iterate/session_current_model_id.py`.
- `reformat-md`: `scripts/reformat-md`.

`.../name` means the resolved absolute path. Stop if unavailable.

## Trigger

Default-on for:

- Durable prose — see "Writing specialization" for full scope, including commit
  messages, docs, guidelines, posts, code comments, edits to the rules package
  containing this file, and other personal or project rule documents.
- Code changes intended to persist. Draft first if useful, but run the critic
  loop, apply fixes, and emit each pass's accountability artifact promptly
  before continuing with material work.
- Investigations and recommendations where a false claim could change the
  answer.
- Substantive design or correctness choices.
- Code reviews.

Do not trigger on disposable working scaffolds: dumps, evidence ledgers, scratch
plans, and similar intermediate notes. If the user is expected to read the
material or it is intended for future reuse, the material is durable and the
loop applies.

Accountability artifacts and delegated-review reports produced by this workflow
do not themselves trigger another critic-iterate cycle.

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

Run until a full pass makes no edit.

A **pass** = critique → fix every in-scope flag → cold re-read. Adversarial
throughout: critic is the skeptic, not the cheerleader.

Before calling a pass clean, record in its accountability artifact the strongest
nearby alternative for the most suspect sentence, line, or decision. If it is
materially clearer or better satisfies the active critic dimension, take it.
"Accurate" or "defensible" is not convergence.

Every pass includes a new-reader dimension. For prose, use the primary reader
established before drafting and verify that choice against the artifact's final
location. For other artifacts, determine who will review or use them there. Read
with only what that reader would already know and no drafting history. Do not
assume repo or subsystem context unless that reader normally has it. Flag any
heading, comment, helper name, section break, or sentence whose purpose or
placement only resolves from prior chat, temporary scaffolding, or author
intent. Rewrite, move, or delete it before checking narrower rules.

Per pass:

1. **Identify the critic dimensions** for the artifact type before inspecting
   the current draft, so dimension selection is not biased toward dimensions the
   draft happens to pass. Examples:
   - Writing: explanation / shape / cut / plain language / cold re-read (see
     "Writing specialization").
   - Evidence: Are claims that could change the answer supported, with inference
     and uncertainty visible?
   - `design-vetting.md`: Full problem covered? Viability checked before
     ranking? Invariants preserved?
   - Test refactoring: apply `code/testing.md`. Distinct material risks survive?
     Near-copy structure is compressed without hiding differences? Failures
     still localize the cause?
   - Code changes: invariants preserved? Reader can still trace the control
     flow? Unnecessary structure collapsed or deliberately kept per `code.md`?
2. **Apply each dimension.** Flag what fails.
3. **Author pass.** Fix every in-scope flag immediately.
4. **Cold re-read.** Read the whole artifact as if written by someone else. Fix
   every in-scope issue it finds; any edit requires another full pass. For
   high-stakes artifacts (see "Dual revision"), this self cold read does not
   replace the required Codex review sequence after self-convergence.

If significant new input changes an artifact's motivation, constraints, or
decision function, the next critic pass applies the normal critic dimensions to
the whole artifact with that input in scope. Do not limit the pass to the local
edit unless the change is an isolated typo or formatting fix.

When critic findings, reviewer findings, or loaded rules appear to conflict,
apply `core/conflicts.md` before triage.

Before drafting durable prose, apply `writing.md` "Set the reader before the
outline".

When user critique names a concrete issue, do not rely on memory: fix it as the
next material action, or record it in the active TODO tool (`update_plan` for
Codex; `TaskCreate` for Claude) before doing anything else. Tracked items must
be actionable and updated to fixed, rejected with reason, or blocked before
convergence. Resolve concrete critique before fresh review.

## Accountability Artifact

The convergence proof. Required for every pass unless a specialization grants an
explicit exemption.

Outside prose dual review, format author passes inline in the chat, with each
finding marked:

- ✅ APPLIED — quote the affected text/element (with location), state the
  change.
- ❌ REJECTED — quote the affected text/element, state why kept.

For prose dual review, use the `MUST_TAKE` / `MINOR` / `REJECTED` classes under
"Integration and closure" for reviewer and author findings. Quote the affected
text and state the applied change or why it was rejected. For `MINOR`, also say
why the candidate was acceptable without it.

What "text/element (with location)" means by artifact type:

- Writing: the sentence verbatim + line/section ref.
- Design: the alternative / claim / constraint verbatim + section ref.
- Code: the line or hunk + `file:line`.
- Tests: the test name or assertion + file:line.

Quoted findings, not narrative paraphrases, are the anti-Goodhart guard: they
cannot be produced without actually reading the artifact.

For writing passes, the artifact must show cut-test evidence. Quote at least one
sentence considered for cutting or compression, answer what is irreplaceably
lost if it is cut, and state the action taken. On a zero-flag pass, quote the
hardest sentence to justify and why it stays. A pass that only says "cut test
applied" is invalid.

**Prompt emission gate.** Emit the artifact for each pass immediately after that
pass's author / cold-read step. Do this before starting the next substantive
task or hiding the artifact in a final wrap-up. The final response may summarize
already emitted artifacts, but it must not be the first place they appear. Relay
a CLI delegate's `passes.md` entries when you next act on the task or are asked;
never wake just to relay.

For dual revision, include all reviewer output directories in the next
accountability artifact. If no later artifact is due, include them in the final
debrief instead. They already preserve the exact reports and process traces; do
not add a separate relay step. Paste a complete report only on request or when
its validity or content is disputed.

For long-running or multi-edit tasks, give a brief progress update before or
alongside the edits a finding drives. This does not replace the pass
accountability artifact, which follows the author / cold-read step above. If you
are about to type "Done" / "Applied" / etc. without having emitted every
required pass artifact, you are not done.

**Rule-doc structural audit.** For any rule-doc edit, the artifact must show
placement and consolidation work. Include:

- **Rule-application search**: name which existing rules govern the edit's
  topic. Place the edit next to them. Skipping this is how existing rules get
  silently ignored.
- Related rule(s) found, with file / heading or search terms that found none.
- The destination heading and adjacent headings checked for fit.
- Related files read in full.
- The consolidation decision: moved / merged / cross-referenced / deliberately
  left separate, with a reason.

Bare conclusions like "no structural issues" are invalid.

## Dual Revision

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

Before the first prose review, derive a **cold-reader brief** from the request
and artifact destination. In 2–3 short sentences, normally 60 words or fewer,
say who the reader is, what they already know, and what they are trying to
accomplish, without supplying the conclusion or causal story. Include only the
non-goals or unresolved facts needed to bound the review. Omit prior critique
and coverage lists. Reuse the brief verbatim in the fresh task note and every
cold-reader prompt. Treat it as complete: knowing a system does not imply
knowing the terms or details of the work under review.

Keep the brief fixed unless its source inputs change or show it is wrong. If it
changes, immediately show the old brief, new brief, and reason; mention the
change in the final debrief and run a new review pair. If a brief exceeds 100
words, explain why in the next accountability artifact and final debrief.

**Prose review round.** After the author-side cycle converges, format the
candidate, then start the review round. The fresh reviewer owns the round and
starts the cold reader as a nested CLI call. The runtime preambles own reviewer
behavior and execution order.

For a substantial document with a distinct opening and body that explains why
something happens, other than a commit or diff message, cold-check the opening.
Put its exact title and opening, but not the body, in the cold task. Give it
only the cold-reader brief and that opening, with no candidate path. For other
prose, give the cold reader only the whole candidate. The fresh reviewer returns
one coherent set of candidate findings after comparing its independent frame,
the candidate, and the cold account.

For non-prose, the fresh reviewer applies the same evidence and design checks
before opening the artifact, then verifies material claims introduced by it and
returns an alternative or findings.

**Fresh reviewer inputs.** Build its prompt from only:

- **A short task note:**
  - Identify the artifact as prose or non-prose. For prose, include the
    cold-reader brief verbatim, say whether the cold reader sees only the
    opening or the whole candidate, and mark its source inputs as required.
    Otherwise state its goal and intended users.
  - For an investigation, include the question the reviewer must answer unless
    the artifact's goal already states it.
  - Include external facts or requirements needed to verify correctness when
    allowed sources cannot supply them. State them as verification inputs, not
    required artifact wording. Do not supply derivable conclusions or prior
    critique; if critique exposed a fact or requirement, include only that.
  - Derive the reader's starting knowledge from the artifact's final location.
    For stacked commits, review each commit as it will appear after its
    predecessors land. Do not assume the reader has read their messages. Omit
    process history.
- Each full rule file whose declared trigger covers the artifact type or a
  decision under review; omit files that are only topically related. Batch reads
  where practical. When a governing rule is also the candidate, read and apply
  it only after `REVIEW FRAME:`.
- Sources, measurements, or run results needed to verify material claims.
- The frozen candidate as an embargoed path. Any diff that exposes it shares the
  embargo. For rule-file candidates, earlier versions do too.
- For prose, the exact nested cold-review command and its prompt path. The path
  is execution-only, not a readable input.
- For review of a change, read-only access to the diff.

Mark each source path as required to read or merely permitted.

For non-exhaustive prose, a requested addition must name what the primary reader
could not understand or do without it.

**Integration and closure.** The General Cycle's no-edit rule governs
author-side passes. For external prose review, classify every fresh-reviewer
finding before editing; its response already integrates the cold report:

- `MUST_TAKE`: must be fixed; leaving it would materially harm correctness or
  the reader's task.
- `MINOR`: worth fixing, but the artifact still works without it.
- `REJECTED`: wrong, already addressed, outside the reader's task, or
  net-negative.

A material error, missed requirement, wrong action, or reader blocker is
`MUST_TAKE`. Escalate if the allowed evidence cannot repair a material finding.
Treat the independent reviews as evidence, not a vote.

If a `MUST_TAKE` finding changes a proposed fix's behavior, invalidates a
fallback, or exposes a deciding correctness assumption, reapply
`design-vetting.md` before editing. Pure presentation or citation changes do not
trigger this.

A successful fresh review and its cold read count as 1 review round. The review
budget defaults to 1 round. A personal rule can set a different default with
`critic-iterate-N`; a task can set its budget with that form or `c-i-N`. After
the budget is exhausted, each `c-i+N` adds N rounds.

After each review round:

1. Fix every `MUST_TAKE` and `MINOR` finding.
2. Run the General Cycle until a full pass makes no edit.
3. Format, then cold-read the final candidate. Any edit returns to step 2. Any
   material problem fixed in steps 2 or 3, or material change made after the
   review started, becomes `MUST_TAKE`.
4. Take the first action that applies:
   - If neither the last fresh review nor later checks found a `MUST_TAKE`
     issue, finish.
   - If every `MUST_TAKE` fix since the last fresh review was mechanical, verify
     each one directly and finish. Rewriting prose is not mechanical.
   - If review budget remains, start another review round and return to step 1.
   - Otherwise, reread the finished draft.

     Start another review round only when:
     - later edits could make the reader misunderstand something important or
       take the wrong action; and
     - no fresh reviewer checked or proposed the resulting meaning.

     Tell the user before re-reviewing. When the round finishes, return to step
     1. Mention any extra rounds in the final debrief.

     Otherwise, finish with this notice:

     > This output may have easy-to-spot gaps because I ran out of review
     > budget. Reply `c-i+K` to allow up to K more review rounds; later rounds
     > usually yield smaller gains. The default is 1 round; personal rules may
     > override it with `critic-iterate-N`.

Record the dispositions only in the accountability artifact. For other
artifacts, take the better version, merge, or apply its findings.

Do not edit the candidate while either reviewer runs. If it changes after a
round starts, that round no longer covers the revision. After the reviewers
finish, resume above at step 2.

**Codex reviewer mechanism.** Every required reviewer call uses this fixed
command surface and private output path.

```bash
package_dir="$(dirname "$(readlink -f "/path/to/critic-iterate.md")")"
review_tmp=$(mktemp -d)
# Write cold-prompt.md and fresh-prompt.md under "$review_tmp" before this call.
.../codex-reviewer.py \
  --preamble-dir="$package_dir/critic-iterate" \
  --preamble=fresh-review-preamble \
  --workdir="$(mktemp -d)" \
  "$review_tmp/fresh-prompt.md"
```

When writing `fresh-prompt.md`, replace each variable below with its current
absolute value; the child shell will not inherit them:

```bash
.../codex-reviewer.py \
  --preamble-dir="$package_dir/critic-iterate" \
  --preamble=cold-review-preamble \
  --workdir="$(mktemp -d)" \
  "$review_tmp/cold-prompt.md" >"$review_tmp/cold-result.txt"
```

Do not prefix either wrapper call with an environment assignment. Hermetic runs
use the copied rule's sibling preambles.

`cold-result.txt` contains `REVIEW_OUTPUT_DIR=<path>` followed by the report.
The top-level author or orchestrator may poll the outer fresh-review command
normally. If polling loses later stdout from that command after recording its
`REVIEW_OUTPUT_DIR`, use that directory's `review.md` only if it is nonempty,
`run.jsonl` reaches `turn.completed`, and the trace checks below pass.
Otherwise, treat the round as failed; never scan temporary directories or infer
a result from partial output.

On success it prints `REVIEW_OUTPUT_DIR=<path>` followed by the review. The
private directory holds the same review in `review.md`, plus
`effective-prompt.md`, `run.jsonl`, and `err.txt` for audit.

The outer marker names the fresh-review directory. The fresh response includes
the child marker; the author records both.

Each prompt must name every input its reviewer may read. Do not include raw chat
or the full context packet. For commit / diff-message review, follow the
specialization below. The cold reader uses a fresh temporary directory. For the
fresh reviewer, pass an absolute repository or relative-path base only when it
needs caller-relative sources or a repository diff. Otherwise use a fresh
temporary directory as shown above.

`run.jsonl` and `err.txt` are process evidence, not material to use when
revising the artifact. On failure, inspect the smallest relevant log excerpt.

Before accepting a prose review, confirm that its only cold-review launch
precedes any source read or statement about the artifact, that `REVIEW FRAME:`
appears before any embargoed input is read, and that `ARTIFACT CHECK:` appears
before the cold-result file is read. Confirm that the cold trace reads no
undeclared source and launches no reviewer. If either check fails, discard the
round, fix its prompt if needed, and start a new outer fresh-review command
before editing.

Missing `codex`, auth / sandbox failure, non-zero exit, or timeout means the CLI
path cannot run; stop and report the infra/setup failure. For a Guardian
possible-exfiltration rejection of the top-level wrapper call, read
`critic-iterate/auth-prompt.md` only for that rejection, never on the happy path
or for an unrelated policy failure. Those recovery steps do not reach a nested
reviewer running with a private `CODEX_HOME`. The fresh reviewer reports a
nested rejection or run failure and stops. Do not rerun until the blocking
policy or configuration changes; then the top-level author or orchestrator
starts a new prose review round. Empty, off-topic, or malformed reviewer output
is a bad run; the top-level author or orchestrator discards it, tightens the
prompt, and starts a new outer fresh-review command. Each required reviewer
check is incomplete until it produces usable output. Do not proceed self-only or
switch reviewer paths.

**Commit / diff messages — separate inner loop from outer evaluator.** The
author runs the `writing.md` inner loop to convergence before the outer
evaluator.

Before opening the author draft or cold report, the evaluator drafts from only
the task note, the rule files selected under "Fresh reviewer inputs," and the
diff. Treat task-note wording as untrusted: define, replace, or cut anything the
intended reader would not understand. Emit the finalized draft as required
above, then follow the general prose order: compare the author draft before
classifying the cold report.

The evaluator returns the regenerated draft verbatim, followed by quoted rubric
flags against the author draft. Apply the plain-language check to the author
draft too, even when its wording came from the task note; include cold-reader
artifact failures among the flags.

The author uses the general `MUST_TAKE` / `MINOR` / `REJECTED` triage and
closure rules.

**Structural best-of-both.** Treat the regenerated draft as a diagnostic and
idea source, not a second draft to blend. Borrow changes that reduce reader
cost: shorter structure, clearer ordering, or plainer language. Reject changes
that mainly add coverage, copy the reviewer wholesale, or make the message feel
more complete.

**Context packet discipline (commit messages).** The author or orchestrator
still builds a context packet for commit messages. For fresh review, pass only
the short task note described above, not the whole packet. Structure the
author-side packet into three named sections so the author can scan it
predictably:

Before constructing the author packet or reviewer task note, recover applicable
breadcrumbs as specified in `core/breadcrumbs.md`. Build the author packet from
current task inputs relevant to Stack context, Reader must know, or Decision
trail, including the recovered goal and unsuperseded requirements or decisions.
Before the author uses or dispatches the packet, treat every input as a claim or
requirement, not approved wording. Apply "Evidence" when a false claim could
change the message, then check each input against the intended reader's starting
knowledge. Keep only code identifiers that help verify a fact or find the
relevant code. Explain the concrete actor, condition, action, or outcome hidden
by unfamiliar shorthand, and define unavoidable technical terms on first use.
Raw input may be overcomplete, but not opaque.

Never pass breadcrumb paths or raw history to the fresh-review task note.

- **Stack context** — for diffs in a stack: what predecessors covered and what
  follow-ons will do. Include review-affecting predecessor framing or follow-on
  plans in the task note; omit mechanics visible in the current code.
- **Reader must know** — the few facts whose absence would make a reader act
  wrongly, plus the artifact goal and intended readers. Past three or four
  facts, consolidate — keep each one only if its absence predicts a distinct
  wrong action. The final message may compress its detail and wording unless
  that changes reader action. Put a fact in the fresh-review task note only when
  the reviewer needs it to verify correctness and cannot derive it from the
  sources it may read.
- **Decision trail** — required when the change embodies any design choice not
  mechanically forced by the spec or bug (typo, version bump, mechanical rename,
  and pure-config-value tweaks are exempt regardless of line count). The raw
  development-process surface the chat went through:
  - Alternatives considered and why rejected (named, not "we discussed
    options").
  - Decisions that changed mid-design and the trigger for the change.
  - Constraints that pinned the final shape (compat, privacy, perf, deadline,
    invariant being preserved).
  - Recursive realizations — moments where the problem reframed itself.
  - For commits that are themselves checkpoints in a named iterative design
    (meta-project, RFC series, sequenced refactor): the iteration trail (what
    iter-N exposed, what iter-N+1 added, why).

The Decision trail is RAW input — the inner loop compresses aggressively from
it, keeping whichever items survive the cut test (typically the load-bearing
constraint or rejected alternative; see `writing.md` "## What evergreen context
means"). The packet-vs-final-message split is input-vs-keep, not a different
taxonomy. Omitting Decision trail on a non-trivial change starves the loop;
forcing it on a trivial change manufactures motivation — per `writing.md` "##
General maxims", the same bloat reversed.

**Debrief tail.** End multi-step debriefs with
`Delegated checks: T required, A attempts, F failed`; count each required
reviewer call.

Any `critic-iterate` trigger authorizes the Codex reviewer calls required by
this process. Treat this paragraph as explicit delegation authorization. Do not
count an omitted required reviewer check as a pass.

## Delegation

Any subagent driving a critic-iterate loop must be at least as capable as the
parent: same model, same or larger context window, or stronger. Weaker driver →
weaker convergence gate → weaker output. When in doubt: newer generation >
older; within a generation, Opus > Sonnet > Haiku.

Default the Agent-tool `model` parameter to inherit for any subagent whose
output feeds convergence. Downgrade only for pure-mechanical work (file moves,
grep-and-report, ID renames).

Delegating authorship does not satisfy the fresh-review requirement. Give
authorship subagents the Codex reviewer mechanism above. Whoever authors
normally runs those checks; a non-author ambient does not add another after they
pass. If delegated authorship fails before the draft converges, stop and report
it; never take over the writing. If the draft converged and only its review
failed, the top-level orchestrator may recover the infrastructure and rerun the
required review on that unchanged draft.

Never substitute self-assessment for a required delegated check (Codex reviewer
calls per Dual Revision, or any subagent call this file mandates).

**Delegate authorship of writing artifacts to the Codex CLI unless the ambient
model is Opus 5+ or GPT-5.5+ (resolve it with
`.../session_current_model_id.py <session UUID>`).** When delegating:

- **Source.** Forward source documents verbatim. Never pre-digest them into a
  summarized or bulleted "must-cover" list — Codex compresses. For conversation
  context, quote key user inputs verbatim with minimal glue and give Codex the
  session JSONL path for lookup. Copy it into Codex's workdir when the reviewer
  cannot read the original path.
- **Write access.** Authorship needs a writable environment. Use the caller's
  existing environment; do not reuse the reviewer wrapper or add an outer
  sandbox.
- **Outputs.** Unlike the reviewer, an author writes files: tell it to put the
  draft in `draft.md` and add each pass artifact to `passes.md` under a unique
  numbered heading before starting the next pass. Pass no `-o`.
- **Guardian rejection.** Handle as above.

## Resist These Shortcuts

- Do NOT skim on later passes. Each pass must be as careful as the first.
- Avoid confirmation bias — critic is adversary, not cheerleader.
- Do NOT defer an in-scope issue as "pre-existing."
- Do NOT narrate completion before pasting the accountability artifact.
- Do NOT pick critic dimensions after inspecting the draft.
- Do NOT use reviewer failure as an escape hatch. Codex CLI infra failure
  blocks; bad reviewer output must be retried, not accepted.
- Do NOT skip required dual revision because a commit message "looks tight" or
  the outer evaluator seems unnecessary. Only the "Dual-Revision Thresholds"
  exemptions apply.
- User critique is not dual revision; run the applicable Codex review, including
  anchor-free regeneration where required.
- A reviewer result belongs to the exact candidate it inspected, not to a
  completed workflow stage. After any later text edit, the round no longer
  covers the final revision; follow "Integration and closure" to choose another
  pair or close after author review.
- Critic-iterate runs the full process on every trigger. Do not label material
  as scratch when the user is expected to read it or it is intended for future
  reuse. Beyond the explicit exemptions in this file, the sole process exemption
  is an explicit user ask for "one inner loop".
- Do NOT invoke "context bottleneck" to skip the Codex CLI reviewer. Real
  exhaustion means token count within the window limit's warning band or tools
  returning truncation errors — otherwise, run it.

## Code Specialization

For every nontrivial code change, run a code critic pass before lint, format,
tests, or commit. Nontrivial means more than an isolated typo, rename, literal
or config value, formatter-only change, or generated-output update.

For code critic passes and fresh-context reviewers, user nits are inputs, not
scope. Reconstruct the changed artifact's intended contract, then review the
whole changed surface adversarially for correctness before style, compression,
naming, or prose.

Use `code.md`'s "Compression and locality" section when the pass reaches
compression decisions. This `Code Specialization` section defines when the pass
runs and what evidence it must leave.

The code-pass artifact must include correctness and compression evidence: quote
at least one correctness candidate taken or rejected; quote at least one
simplification taken, or quote a concrete candidate rejected with the reason.
"No correctness or compression opportunities" without quoted candidates is
invalid.

## Writing Specialization

The general process governs. This section names the writing-specific dimensions,
thresholds, and exemptions.

### Cycle

Identify these critic dimensions before inspecting the current draft:

- **Explanation critic** — For durable explanatory prose, apply `writing.md`
  "Substance". For a durable document, also apply "Document". Flag a missing
  question or problem, the facts and reasoning needed to follow the conclusion,
  or any applicable proposal or investigation requirement before narrower style
  issues.
- **Shape critic** — For prose with multiple sections or that answers more than
  one independent question, set the draft's structure aside and sketch the
  simplest outline that serves its primary reader and purpose. Compare it with
  the draft before line edits. Combine parts that do the same job and cut text
  that serves no additional reader need. Then apply `writing.md` "Pick the right
  shape" to each remaining container.
- **Sentence critic** — Apply `writing.md` "## Iterate" Cut test; it is
  canonical.
- **Plain-language critic** — per `writing.md` "## Substance". A necessary
  sentence can still be jargon-heavy. Replace noun chains and abstract process
  labels with concrete actors, actions, conditions, or outcomes. Remove
  qualifiers that do not change the instruction.

Then cold re-read per the general cycle.

### Scope and Exemptions

The accountability artifact is required for:

- Prose blocks ≥3 sentences.
- Any edit to the rules package containing this file or to another personal or
  project rule document, including additions made during the turn that encodes a
  new rule. The writing exemptions below do not apply.

Writing exemptions:

- Typo fixes: single-character corrections, no semantic shift.
- Single-line edits with no semantic shift: variable rename, comment rephrase.
- Disposable working scaffolds as defined under "Trigger".
- If unsure: artifact required.

### Dual-Revision Thresholds

The general "high-stakes" definition applies to all writing. Concrete thresholds
for cases that need them:

- **Commit messages:** dual revision required when the change affects ≥1
  sentence of substantive content. Typo, broken-link, and format fixes are
  exempt. So are mechanically forced version bumps, renames, and pure
  config-value changes when one sentence says everything the reader needs, the
  context packet adds no other `Reader must know` fact, and the author checks
  the message against the diff. A design choice or known comprehension failure
  restores dual revision.
- **Rule-doc edits** in the rules package containing this file or another
  personal or project rule document: dual revision required for every semantic
  or readability change. Pure typo, broken-link, and format fixes are exempt;
  there is no size threshold otherwise.
- **Posts seeking input:** dual revision required by default, with no threshold.

### Self-Dog-Fooding Gate

When the edit adds or modifies a rule, the edit's own prose must comply with
that rule. Apply during the cycle, not after.
