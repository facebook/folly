# Writing style

Resolve package paths relative to this file's directory.

## General maxims (apply to all prose)

Commit messages get the specialized loop in "## Iterate — inner loop, until
convergence" below. The maxims here apply to all prose (docs, posts, comments).

- **Set the reader before the outline.** Before drafting durable prose,
  establish:
  - its primary reader;
  - what that reader already knows; and
  - what that reader should know or be able to do afterward.

  Inspectable material is not starting knowledge. State the framing the reader
  needs; rely on that material only for detail and evidence.

  Derive these from where the artifact will live and why it exists. If two
  plausible audiences would require materially different content, ask which one
  is intended before drafting. Name a secondary audience only when it has a
  distinct required task.

- **Optimize for the needed understanding, then length.** Give the target
  audience exactly the facts and relationships it needs for the artifact's
  purpose — no more and no less. First minimize the effort needed to understand
  them correctly. If two versions do that equally well, choose the shorter one.

- **Rework, don't append.** When fixing prose, default to rewriting the line
  rather than adding to it. Append-style patches ("See X for Y", "Note: Z") are
  almost always barnacles.
- **Do not edit for motion.** In review mode, change text only to improve
  clarity or accuracy, or to make it materially shorter without losing needed
  content or increasing reader effort. Replace an abstract process label when
  the target audience would have to unpack it.
- **Pick the right shape.** Lifecycle/procedure → numbered list. Parallel states
  / parallel facts (N≥2) / inline enumeration (3+ items) → bullets, with lead
  labels where they aid scanning. Reserve semicolon/em-dash glue for tight
  causal pairs ("keep X — stripping breaks Y"). The shape rule fires regardless
  of punctuation. An enumeration or parallel-states comparison hiding in prose
  is a shape miss — rewrite the container, not just the sentences. (The Iterate
  loop's Shape pass applies this for commit messages specifically.)
- **Lead with why.** For prose about code, start with the problem or goal. Add a
  constraint, rejected alternative, or invariant when it explains the choice.
  Include enough of what the code does to make that reason clear, then leave
  routine mechanics to the code. If one sentence is enough, stop.

## Author disposition

The author must adopt a persona free from cognitive biases like rationalization
& sunk-cost. Channel these traits as you revise:

- **Reader-first.** Every unnecessary word taxes every reader.
- **Essentialist.** Keep only facts the target reader needs; truth or relevance
  alone is not enough.
- **You are not your draft.** Existing wording gets no preference. Rewrite or
  delete it when that better serves the reader.
- **Rationalization-hostile.** "Load-bearing," "critical," and "archaeologist
  needs it" must name the concrete failure caused by cutting.
- **Subtractive.** After selecting the necessary facts, remove excess wording.
  Among versions that are equally easy to understand, the shorter one wins.

## Substance

**Select facts, then compress wording.** Use sources to get the facts right, not
to decide that every fact belongs. Keep only the facts the primary reader needs
to do the job the artifact exists to support. Being true, related, or
non-obvious is not enough. For a fact that stays, keep the concrete detail that
makes it useful to that reader. Replacing that detail with a broad label is not
concision.

- Assume the intended reader's normal background, but not this artifact or its
  drafting history. State the question or problem and enough framing and
  reasoning to follow the conclusion; process labels and bare verdicts do not
  suffice.
- Be direct: each word earns its place, but not telegraphic. Prose should read
  naturally.
- Avoid wordiness: cut filler, hedging, and restatement (don't say the same
  thing twice in different words — merge sentences that make the same point).
- State simple points simply. Add how or why only when the reader needs it.
- Prefer common words and concrete verbs when they are equally precise. If a
  sentence says a change "enables," "supports," or "provides" something vague,
  rewrite it around the concrete outcome.
- Prefer a concrete example when it makes a needed point clearer than abstract
  prose.
- Use identifiers to point readers to relevant code, not in place of a plain
  explanation.
- When shortening rules/guidelines, preserve the decision function: enough
  detail to apply the rule, not just know it exists.

## Sentences

- Short sentences. Short paragraphs.
- One claim per sentence by default — the goal is fast comprehension. Glue
  (em-dash, semicolon) when the second clause depends on the first to make sense
  ("keep X — stripping breaks Y"). Split independent claims: "X — and Y" → "X.
  Y." Definitions glue freely ("the Foo — what makes the move safe and
  revertible").
- With a shared subject, put a comma before "and" when a long first predicate
  makes the boundary easy to miss; omit it between a pair of short predicates or
  adjectives, and retain the Oxford comma in lists.
- Labels, headings, and bullet lead-ins are fine when they create useful
  structure. In running prose, do not let a label replace a precise
  plain-language claim with a vague category; state the actual relationship,
  trade, gate, cause, or decision.
- Backticks for code identifiers (`FrontendReadService`, `metadata`).
- Em-dashes ( — ) with spaces.

## Document

- **Make the opening survive the body.** In a substantial explanatory document,
  the opening should establish its problem or purpose and enough scope to
  organize what follows. When the document explains why something happens, state
  the relationships needed to follow the cause and why it matters. Do not assume
  that a reader who knows the surrounding systems also knows this work's terms
  or details. If unfamiliar language carries one of those relationships, say who
  does what to what and what changes before relying on it. Later detail may
  refine that understanding, but should not force the reader to replace it.
  During cold re-read, compare the opening with the full document.
- Headings: short and direct. Cut filler like "Discussion of" or "Notes on" —
  "Discussion of potato pros & cons" → "Potato trade-offs". Noun ("Pastes") or
  verb ("Prefer X"), both fine.
- Dedup cross-references — keep the informative version (a listing entry with
  description beats a bare "see X").
- Wrap text to 80 chars with `.../reformat-md FILE...`; it edits files in place.
- **Proposals.** Apply `design-vetting.md` before a substantive design or fix
  proposal. State where the problem occurs and what outcome the proposal must
  produce.
- **Investigations.** Include:
  - how the evidence was produced;
  - where to inspect it or how to reproduce the work;
  - what was observed; and
  - only conclusions the observations support, with the reasoning that connects
    them.
- Keep godbolt links — critical for human readers even if useless to agents.

## Code comments

Code comments are read beside the code. Explain the outside fact, invariant, or
reason that the code alone does not show; do not narrate what the code already
shows.

- State the condition the reader should rely on, not the mechanism that checks
  or produces it. For a side-effecting call or ignored return, "Check that every
  X resolves" beats "Fetch every X"; name the mechanism only when the mechanism
  is surprising.
- When a comment's claim depends on facts outside the local expression, include
  the shortest checkable proof. Name the outside source of truth and the
  invariant it establishes; don't compress so far that a cold reader must infer
  why the claim follows.
- Apply the same standard to code-adjacent prose, including `static_assert`
  messages: include text only when it adds information the expression does not
  already carry.
- `/*paramName=*/` inline comments: only for same-type parameter disambiguation.
  Remove when parameter types already distinguish the arguments.

## Explain intent of code changes

Code-change prose exists to **reduce the audience's work**. Its primary audience
depends on the genre:

| Genre                  | Audience                                                       |
| ---------------------- | -------------------------------------------------------------- |
| Commit / diff messages | Reviewer first; future maintainer second                       |
| Code comments          | Future code reader (next to touch this code)                   |
| Design proposals       | Design reviewers first; future implementers after the decision |
| Code-review comments   | The author of the diff being reviewed                          |

**Commit messages are read before the code.** The reviewer first forms a mental
model from the message, then reads the code through that framing. The message
must make sense before the diff is opened and include the facts and
relationships needed to review the change. Once that model is clear, leave the
remaining implementation detail to the diff. A fact's presence in the diff
neither earns nor disqualifies it; keep it here only when the reviewer needs it
before reading the code.

**State the goal early.** Commit / diff messages and design docs must say what
the artifact is trying to accomplish for intended readers. Lead with the goal,
or with a concise account of the concrete problem it solves followed immediately
by the goal. A self-explanatory invariant (for example, that a refactor is a
no-op) may come first when the reviewer needs it to interpret the goal. Put
context-dependent invariants after the goal. Omit invariants the reviewer does
not need to understand or verify the change. Stack context may briefly
cross-reference a prior `D<num>` for detail or evidence; include all framing the
current reader needs inline.

## Titles

Commit / diff / doc titles must be clear in a title-only list-view without the
summary. Name the concrete object and the relationship or condition changed; do
not shorten away the object that makes the change intelligible. Use plain
language when implementation detail does not help readers identify the change.

Example: Prefer `check job migration state vs that of its reservation` over
`validate paired migration states`.

**Title-only test.** A title term whose meaning only resolves after reading the
body is suspect — "paired" in the bad title above fails this test (paired how,
with what?). Swap context-dependent jargon for plainer language.

## Two modes — brief or essay

Match the message shape to the change:

In either mode, state framing in the prose; rely on external links only for
detail or evidence.

**Simple explanation → brief.** Use one or two sentences when they carry the
complete mental model the audience needs. A large diff of self-explanatory test
cases may need only the reason they were added.

**Use the shortest form that carries the needed model.** Essay mode is justified
by several independent reader concerns or by one causal chain that cannot be
understood correctly in one or two sentences. Diff size does not decide message
length: a one-line race fix may need how the bug was detected, what caused it,
why the fix works, and how that was checked.

Example (D104870443, ~70 words):

> `Template:Warning` and `Template:Error` are wiki pages whose body bakes in
> light hex backgrounds, illegible in dark mode. The HSL inversion helper that
> already handles this for QUIP applies just as cleanly here.
>
> The "proper framework fix" would be to route through `XDSBanner`, but
> currently MediaWiki `{{Warning}}` / `{{Error}}` go through
> `InternWikiTransclusion::genRenderTransclusion`, not through any React
> component on this path.

**Substantial explanation → structured essay.** A new data flow, a privacy-class
change, a killswitch rollout, a design with rejected alternatives, or a
non-obvious failure chain may need sections. Use `#`/`##` to chunk distinct
concerns; keep each section short. Sections that typically earn their keep:

- Motivation / situation / problem.
- Mechanism — only when the choice is non-obvious.
- Alternatives considered and why they lost.
- Privacy / killswitch / rollback strategy.

**Composite diff (main + tangential cleanup) → main mode + `Drive-by:` line.**
Label the cleanup with `Drive-by:` so the reader knows where the main thread
ends. Describe pain or constraint (the bug, the awkwardness it removes), not the
mechanic the diff already shows. If the cleanup is self-evident, a one-liner
like `Drive-by: drop some dead code.` is enough.

## What evergreen context means

These are common kinds of useful context, not a checklist. Keep one only when
the artifact's audience and purpose require it.

- The situation that made the change necessary, named as reviewer-facing pain
  ("untenable to review", "can't ship without manual diff inspection"), not the
  mechanical symptom ("version bump", "JSON churn").
- The constraint that pinned the design (compat, privacy, perf, deadline).
- The alternative considered and rejected, with a one-line why.
- The invariant preserved.
- The kill-switch / rollback story for risky changes.
- For multi-diff stacks where the endpoint isn't obvious from the diff: a brief
  trajectory line ("Up-stack: every caller migrated, old API deleted"). Skip
  when the next diff is clearly implied.

Exclude is operationalized by the Cut test and Banned patterns in "## Iterate"
below — don't re-enumerate here.

## Iterate — inner loop, until convergence

When required, dual revision follows this iterative inner loop after it
converges (see "Outer evaluator" below).

Per round: explanation → shape → cut (including the test plan) → plain language
→ cold re-read. Converge when a full round makes no edit.

### Shape pass (before sentence cuts)

If a paragraph has a shape problem, fix the container FIRST — sentence cuts on
the wrong container are wasted motion.

1. **Prose hiding an enumeration?** "A, B, and C" → bullets.
2. **Parallel-states comparison?** A vs. B vs. C → bullets, lead labels where
   they aid scanning.
3. **One dense paragraph doing two reader jobs?** Split: claim sentence first,
   consequence sentence next.
4. **Dense paragraph with jargon pile-up?** If a cold-read makes you re-parse a
   noun chain ("the X's Y whose Z affects W") or reach for the dictionary, split
   or rewrite. Jargon stacks fail even when each term is correct.

### Cut test — per sentence

Ask: **"Which required part of this audience's mental model or task becomes
wrong, missing, or materially harder if I cut this sentence?"** A fact does not
earn its place merely by being true, related, or mildly helpful. If no required
part is lost, cut it.

Cut on (locality first, style second):

- **Nothing material lost** — the artifact reads cleanly without it.
- **Restates the title or an earlier sentence.** (Includes wrap-ups, especially
  invariant restatements after a goal-led lead.)
- **Unneeded intermediate detail.** State the result the reader needs. Keep
  mechanics or causal steps only when the result would otherwise be hard to
  understand: “Requests are keyed by account ID, so requests with the same ID
  enter one batch” → “Requests from one account now share a batch.”
- **Opaque-identifier enumeration.** Lists of hex hashes / auto-generated IDs
  the reader can't act on — name the SET or the COUNT instead. "Three res_ids
  (`5f6bcc932c826`, `6196aa3142bcf`, `67056151d29d2`)" → "the three res_ids".
  Keep only when a specific ID is itself the load-bearing reference for a named
  gotcha.
- **Structure is not motivation.** Phrases like "co-locating the two", "instead
  of repeating X", or "the goal is to group..." are bloat when they re-label the
  diff's mechanics as a why. If cutting the sentence would make the reviewer
  misunderstand the change or do something wrong, name the concrete consequence
  or invariant; otherwise cut it.
- **Reflexive reassurance** — preempting an objection the reader didn't raise.
  Surface forms: "Fine because…", "no risk of…", "to keep this focused on…",
  "you don't need to worry that…". Test: if you cut the sentence, what does the
  reader misunderstand or do wrong? If nothing, cut it. A real warning names the
  concrete risk and how the change prevents it ("if X is called twice, Y now
  dedups instead of erroring") — keep, leading with the risk.

When "tighter" is rationalizable, ask whether a shorter version preserves the
needed structure without increasing reader effort. If yes, use it.

### Cut test — per section

Before defending individual sentences, test the section itself: **would deleting
this section remove context the target audience needs?** Sentence-level cuts
protect sections that shouldn't exist — every sentence looks defensible when
read alone.

Commit messages usually don't need these sections:

- **Roadmap / implementation status.** Track future work outside the commit.
- **File-by-file / shape-of-diff.** Let the diff carry file shape.
- **"What survives" / mission-preserved.** Keep the invariant once, not as a
  section.

### Test plan — cut tests

Cut an item if a cheaper check provides the same coverage or it only narrates
the diff without saying what was checked. Match each verb to the rigor used, and
reduce routine checks to `CI`. See "## Test plans" below.

### Cold re-read and loop

Cold re-read the WHOLE message after each round of cuts. If any paragraph scans
as dense, jargon-heavy, or "I had to re-read that" — restart the loop.

**Convergence = a full round that makes no edit.** Before declaring convergence,
confirm every must-know fact named by the context packet is still locatable in
the draft.

### Banned patterns

**Cut on sight.** These patterns are bloat by default. Keep one only when it
passes the sentence Cut test, and lead with the content that earns it.

- Empirical checks in Summary prose: "we verified that …", "tested that …", "no
  production caller actually depends on …". These are Test Plan bullets, not
  Summary prose.
- Shape-of-diff: "Source diff is N lines per file", "this extracts ... into a
  helper", "renamed parameter X to Y", "the 13k-line diff is mechanical".
- Jargon that hides the actor, action, or outcome: "enables analysis of",
  "supports future extensibility", "provides a robust foundation". State the
  concrete outcome instead.
- Sibling-diff summaries: don't restate what a predecessor or follow-on diff
  does, and don't add a `Predecessor` line merely because a diff dependency
  exists. Continuation markers ("Continues from D<num>", "Next batch", "Same
  pattern as D<num>") are bookkeeping unless they carry reader context. Default:
  omit; the ddep edge carries the relationship.
- **Scope-defense prose**: explaining why the diff didn't do more. Surface
  forms: "X stays for now", "deferred to D<num>", "intentionally not bundled",
  "minimal translation here, polish in follow-up". The diff scope is what it is;
  defending it reads as anxiety. Brief stack xrefs that ARE warranted are
  governed by the trajectory bullet (up-stack) and the sibling-diff-summaries
  exception above (down-stack).
- Wrap-ups (closing form): sentences whose content the reader just read.
  Invariant restatements after a goal-led lead — if the lead states "I want
  refactors that don't touch the JSON", don't close with "after this commit,
  refactors produce byte-identical JSON." The reader closes the loop.
- Wrap-ups (opening form): the first sentence after a header, or a bullet's lead
  label, must not restate what the header named. The header carries the
  navigational anchor; the restatement pays nothing.
- Show-then-tell: the prose already showed it; then a tag tells the reader what
  they just extracted. "This is the X failure mode", "the result is Y", "what
  just happened was Z", "in summary, this means W". The tag pays nothing.
  Failure mode of "show, don't tell" — when both fire in sequence, cut the tell.

**Judgment calls** (cut OR keep depending on context):

- **Verb-as-label openings** that restate the title while hiding a needed WHY:
  "Extracted X.", "Consolidates Y.", "Removes Z." For trivial mechanical or
  no-op changes with no hidden rationale, a direct action sentence is fine;
  don't invent context.
- **Author-coined terms** — a term the reader can't decode from what they've
  seen: coined for this artifact with no in-text definition, or a code
  identifier used as prose without the reader having opened the source. Define
  on first use, or rewrite around the concrete operation. Industry-standard
  terms ("idempotent", "race condition") are fine in body prose; for titles,
  apply the title-only test.

## Outer evaluator — anchor-free regeneration + rubric (separate)

When dual revision is required, run it AFTER the inner loop converges. Follow
`critic-iterate.md` "Dual Revision". The source-aware fresh evaluator integrates
the required cold read. For commit and diff messages, it provides two outputs:

1. **Anchor-free regenerated draft.** The reviewer produces its OWN draft from
   the allowed inputs (task note, selected rule files, diff artifact) before
   reading the author draft. This is the primary signal — the side-by-side
   comparison surfaces failures the author can't see because they're locked into
   their draft (mis-led lead, wrong shape choice, buried invariant, missing
   must-know fact). Compare structurally, not sentence-by-sentence.
2. **Rubric findings.** A small rubric (below) the reviewer runs anchor-free
   against its own draft and reports against the author's. Scoped to patterns
   that require fresh eyes — NOT a re-run of the cut test.

Use `critic-iterate.md` "Integration and closure" to triage the result, run the
required author review, and decide whether another external pair follows.

### Rubric (fresh-eyes patterns only)

For each item: read the FULL draft (Summary + Test Plan) with that one item in
mind. Capture ✅ (clean) or ❌ (offending — quote + location).

1. **Opening states the goal early?** Apply "State the goal early." Flag missing
   or buried goals, unnecessary setup before the goal, unjustified
   invariant-first leads, stack references that are dependency bookkeeping
   rather than explanation, and unclear intended readers.
2. **Any sentence stacks 4+ noun phrases or chains possessives** ("the X's Y
   whose Z affects W")? First ask: does this sentence earn its slot? If not,
   cut. If yes, restructure.
3. **Any sentence > 30 words?** Read aloud. If you stumble, first ask: does this
   sentence earn its slot? If not, cut. If yes, restructure.
4. **Any 2+ parallel facts in prose that should be bullets?**
5. **Any 3+ inline items in prose that should be bullets?**
6. **Does the draft give the intended audience exactly the facts and
   relationships needed for its purpose?** Flag true but unnecessary detail,
   abstractions the reader must unpack, and missing relationships the reader
   must guess. When two versions require the same effort and convey the same
   needed structure, prefer the shorter one.

Cut-test patterns (mechanism narration, scope defense, predecessor
re-explanation, verb-as-label, wrap-ups) are NOT in the rubric — the inner loop
owns them. If the regenerated draft is markedly different on any of those,
that's a finding worth reporting; but the rubric itself doesn't pattern-match
for them.

## Test plans — what you checked, briefly

A reviewer learns trust from the rigor of your verification, not from
exit-code 0. The inner loop's Test-plan cut tests (above) call back here.

Failure modes:

- **Test-plan theatre.** A verification that adds no coverage over a cheaper
  check above it (e.g., manual byte-identity check when CI catches it).
- **Over-explaining CI.** Ordinary build/test/lint/format is "CI"; don't expand.
  Name only non-obvious/custom checks: ASan mode, a manual repro,
  generated-output inspection, screenshots.
- **Missing the obvious test.** Before writing the Test Plan, ask: does this
  change obviously merit a unit / integration test / manual repro? If yes and
  you haven't done it, fix the gap BEFORE writing.
- **Rigor mismatch.** "Verified" claimed when you only spot-checked — fix one or
  the other.
- **`sl diff` restatements.** "No other files changed" — the diff carries it.
- **Local-only artifacts.** Reviewers cannot see paths under your home directory
  or scratch output dirs. Include the result inline, link a durable paste, or
  cut the reference.
- **Over-detailed bullets.** A 20-bullet test plan is iteration debt.

Verb choice carries rigor. Match what you actually did: "checked", "verified",
"stepped through" for rigorous; "eyeballed", "spot-checked", "skimmed" for quick
/ representative; "manually ran", "repro'd" for repro. Command-style ("diffed
X", "ran Y") leaves ambiguous whether you read the result. Vary the word — don't
lean on "eyeballed" as a tic.

Bullets scan; prose runs together. Iterate the Test Plan with the same loop
discipline as the Summary.

Good — `CI` / `Docs-only` first for vanilla diffs, the rest for the non-obvious
cases:

- "CI" / "Docs-only."
- "Skimmed materialized JSON — only the 6 expected handles changed."
- "Added a unit test for the new branch; pre-existing tests still pass."
- Before/after screenshots, repro, "A-B-A-B to rule out luck".

Avoid:

- "Compilation succeeded."
- "`buck2 test ... passed`" (unless ASan-only or similar non-obvious mode).

## Worked examples

### Bug fix — short message

**Good (3 sentences):**

> TW job names routinely contain regex metacharacters.
>
> `fullStringRegex("tsp_x/foo.bar")` used to produce `^tsp_x/foo.bar$` — this
> accidentally overmatches, e.g. capturing `tsp_x/foozbar`.
>
> Fix this by escaping the regexes.

**Typical agent draft on the same diff:**

> `fullStringRegex(s)` produced `^s$` — which silently over-matched any spec
> whose handle contained regex metacharacters (e.g. `tsp_x/foo.bar` matched
> `tsp_x/fooXbar`). No production spec deliberately exercised regex semantics;
> this is a latent bug fix. Source diff is two lines per file. The materialized
> JSON delta is exactly the 613 metacharacter-bearing regexes getting their
> meta-characters escaped — reviewable now that the predecessor diff
> determinized the ordering.

**Lesson:** "No production spec deliberately exercised regex semantics" is an
empirical check — belongs in Test Plan, not Summary. "this is a latent bug fix"
labels what the example already shows. Sentence 3 describes diff shape. Sentence
4 previews the predecessor instead of a brief xref. The good version drops all
four; the example carries the bug.

### Refactor — short message with invariant

**Good (3 sentences + Test Plan):**

> This refactor does NOT change the materialized JSON.
>
> The goal here is to make migration specs operate on explicit job handles,
> **not** on regexes. Regexes are confusing and risky (the prior diff shows a
> latent bug).
>
> Test Plan:
>
> - CI (materialized JSON byte-identity is enforced).

### Substantial change — essay-shaped

```
# Motivation
<short — problem statement, situation, or proposal context>

# Mechanism (only when non-obvious)
<short, code-pointer-style>

# Alternatives rejected
- <option A> — <one-line why not>
- <option B> — <one-line why not>

# Killswitch / rollback (for risky changes)
<killswitch name, what flipping it does>

# Notes on design choices (optional)
- <specific decision> — <one-line why>
```

## Note on genre

The examples here are commit-message-shaped. The same principles apply to design
proposals, group posts seeking input, and code-review comments — the audience
structure (current + future) and the goal (evergreen context, few reasonable
words) carry over. Adapt the section structure to the medium.
