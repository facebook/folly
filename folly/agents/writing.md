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

- **Re-read cold before shipping prose.** After substantive edits, re-read the
  changed section and fix any friction. Before closing the pass, read the whole
  artifact; `critic-iterate.md` governs this default-on prose cycle.
- **Rework, don't append.** When fixing prose, default to rewriting the line
  rather than adding to it. Append-style patches ("See X for Y", "Note: Z") are
  almost always barnacles.
- **Do not edit for motion.** In review mode, leave text alone unless the edit
  makes it clearer, more accurate, or materially shorter. Plain language beats
  abstract process labels.
- **Pick the right shape.** Lifecycle/procedure → numbered list. Parallel states
  / parallel facts (N≥2) / inline enumeration (3+ items) → bullets, with lead
  labels where they aid scanning. Reserve semicolon/em-dash glue for tight
  causal pairs ("keep X — stripping breaks Y"). The shape rule fires regardless
  of punctuation. An enumeration or parallel-states comparison hiding in prose
  is a shape miss — rewrite the container, not just the sentences. (The Iterate
  loop's Shape pass applies this for commit messages specifically.)
- **Lead with the why; the artifact carries the what.** Prose about code (commit
  message, docblock, inline comment, design doc) earns its slot by giving its
  reader needed framing: situation, constraint, rejected alternative, preserved
  invariant. Lead with whichever most moves the reader, within the genre's
  opening rules. Narrating mechanism the code already shows is bloat; padding
  thin code with manufactured motivation is the same bloat reversed. Trivial
  cases take brief mode (one sentence or nothing) and stop.

## Author disposition

The author must adopt a persona free from cognitive biases like rationalization
& sunk-cost. Channel these traits as you revise:

- **Reader-first.** Every extra word taxes every reader.
- **Essentialist.** Hates stamp-collecting completionism. Keep only facts whose
  absence changes reader action.
- **No ego, no attachment to prior words.**
- **Rationalization-hostile.** "Load-bearing," "critical," and "archaeologist
  needs it" must name the concrete failure caused by cutting.
- **Subtractive.** A shorter shape is the default winner; add back only what
  changes reader action.

## Substance

**Select facts, then compress wording.** Use sources to get the facts right, not
to decide that every fact belongs. Being non-obvious is not enough. Keep a fact
only when leaving it out would prevent the primary reader from understanding the
point or completing the task the writing is meant to support. For a fact that
stays, keep the concrete detail that makes it useful to that reader. Replacing
that detail with a broad label is not concision.

- Assume the intended reader's normal background, but not this artifact or its
  drafting history. State the question or problem and enough framing and
  reasoning to follow the conclusion; process labels and bare verdicts do not
  suffice.
- Be direct: each word earns its place, but not telegraphic. Prose should read
  naturally.
- Avoid wordiness: cut filler, hedging, and restatement (don't say the same
  thing twice in different words — merge sentences that make the same point).
- State simple points simply — don't explain the how/why when the what suffices.
- Prefer common words and concrete verbs when they are equally precise. If a
  sentence says a change "enables," "supports," or "provides" something vague,
  rewrite it around the concrete outcome.
- Prefer concrete examples over abstract explanation.
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

The general maxim "Lead with the why; the artifact carries the what." applies:
give future readers needed framing without narrating the code.

- Edit comments for clear, efficient communication — but never discard
  meaningful content.
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
| Commit / diff messages | Reviewer first; future archaeologist only after cruft is cut   |
| Code comments          | Future code reader (next to touch this code)                   |
| Design proposals       | Design reviewers first; future implementers after the decision |
| Code-review comments   | The author of the diff being reviewed                          |

Every rule below reduces that audience's work. The general maxim "Lead with the
why; the artifact carries the what." applies here: the future reader sees only
the message and the code. As few words as reasonable for the change; the author
iterates.

**State the goal early.** Commit / diff messages and design docs must say what
the artifact is trying to accomplish for intended readers. Lead with the goal,
or with a concise account of the concrete problem it solves followed immediately
by the goal. A self-explanatory invariant that changes review behavior may
instead precede the goal. Put context-dependent invariants after the goal. Omit
invariants that do not change review behavior. Stack context may briefly
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

## Craft principles (Hemingway-flavored)

- **Iceberg.** "If a writer of prose knows enough of what he is writing about he
  may omit things that he knows and the reader, if the writer is writing truly
  enough, will have a feeling of those things..." (Death in the Afternoon) →
  keep the framing and evergreen context the intended reader needs (see "## What
  evergreen context means"); omit detail or evidence already in the diff, code,
  or comments.
- **One true sentence.** "Write the truest sentence that you know." → The
  general maxim "Lead with the why; the artifact carries the what." is the
  Hemingway distillation: pick the load-bearing fact and let the rest fall away.
  Concrete contrast: "I want refactors that don't touch the JSON" before "this
  commit reorders the JSON."
- **First draft is shit.** → Read it critically before trusting it.
- **Built-in shock-proof shit detector.** (Paris Review) → develop the critic
  reflex below. Read your draft cold. Cut what doesn't belong.
- **Prose is architecture, not interior decoration.** → For substantial
  messages, structure with `#`/`##` sections beats long paragraphs.
- **Short first paragraphs. Vigorous English. Positive not negative.** (Star
  copy style) → operational; use verbs, not throat-clearing.

## Two modes — brief or essay

Match the message shape to the change:

In either mode, state framing in the prose; rely on external links only for
detail or evidence.

**Trivial change → brief.** A typo, version bump, small bug fix, config tweak.
Default to one sentence — the fix or invariant. Add a second sentence only when
a non-obvious WHY or rejected alternative would change reviewer action.

**When in doubt, prefer brief.** Essay mode is justified only when the change
touches multiple independent reader concerns (motivation + privacy + rollback,
or parser-spec + alternatives + killswitch strategy) — each one a different
question the reader would otherwise have to ask. Complexity alone is not enough.
A single design choice plus an implementation note is brief, with the note
appended.

Example (D104870443, ~70 words):

> `Template:Warning` and `Template:Error` are wiki pages whose body bakes in
> light hex backgrounds, illegible in dark mode. The HSL inversion helper that
> already handles this for QUIP applies just as cleanly here.
>
> The "proper framework fix" would be to route through `XDSBanner`, but
> currently MediaWiki `{{Warning}}` / `{{Error}}` go through
> `InternWikiTransclusion::genRenderTransclusion`, not through any React
> component on this path.

**Substantial change → structured essay.** A new data flow, a privacy-class
change, a killswitch rollout, a design with rejected alternatives, a performance
change. Use `#`/`##` to chunk distinct concerns; each section short. Sections
that typically earn their keep:

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

Each include below is a default-keep candidate, but each is subject to the Cut
test in "## Iterate" — defaults don't override the local test.

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

### Cut test — per sentence (Iceberg in operation)

Ask: **"what is IRREPLACEABLY lost if I cut this sentence?"** Not "what was my
reason." Not "what does the rubric allow." What does the future reader lose.

Cut on (locality first, style second):

- **Nothing material lost** — the message reads cleanly without it.
- **Detail or evidence lives elsewhere.** Do not repeat it, but keep the framing
  needed here. A passing mention that just gestures at a construct defined
  elsewhere is still bloat — cut entirely or move the load-bearing fact in, no
  half-include.
- **Restates the title or an earlier sentence.** (Includes wrap-ups, especially
  invariant restatements after a goal-led lead.)
- **Names the mechanism when stating the outcome would be tighter.** E.g., "X is
  keyed by res_id, so any shared res_id must merge into one unit" → "Every X now
  lives inside a single Y."
- **Causal chain spelled out where one claim would carry it.** "X has Y, so Z"
  with each step elaborated → state Z.
- **Opaque-identifier enumeration.** Lists of hex hashes / auto-generated IDs
  the reader can't act on — name the SET or the COUNT instead. "Three res_ids
  (`5f6bcc932c826`, `6196aa3142bcf`, `67056151d29d2`)" → "the three res_ids".
  Keep only when a specific ID is itself the load-bearing reference for a named
  gotcha.
- **Structure is not motivation.** Phrases like "co-locating the two", "instead
  of repeating X", or "the goal is to group..." are bloat when they re-label the
  diff's mechanics as a why. If the structure changes reviewer action, name the
  consequence or invariant; otherwise cut the sentence.
- **Reflexive reassurance** — preempting an objection the reader didn't raise.
  Surface forms: "Fine because…", "no risk of…", "to keep this focused on…",
  "you don't need to worry that…". Test: if you cut the sentence, what action
  does the reader take wrong? If nothing — cut. A real warning names the wrong
  action and how the change prevents it ("if X is called twice, Y now dedups
  instead of erroring") — keep, leading with the action.

When "tighter" is rationalizable, ask: can you rewrite shorter without losing
reader-actionable content? If yes, it wasn't earning its length.

### Cut test — per section

Before defending individual sentences, test the section itself: **would deleting
this section remove reader-actionable context?** Sentence-level cuts protect
sections that shouldn't exist — every sentence looks defensible when read alone.

Commit messages usually don't need these sections:

- **Roadmap / implementation status.** Track future work outside the commit.
- **File-by-file / shape-of-diff.** Let the diff carry file shape.
- **"What survives" / mission-preserved.** Keep the invariant once, not as a
  section.

### Test plan — cut tests

Per item: coverage over a cheaper check above? Rigor matches the verb? Readable
from `sl status` / `sl diff`? Over-explaining CI? Cut on any yes — see "## Test
plans" below for the full rules. Test-plan theatre is bloat.

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
- Labels the example already shows: "this is a latent bug fix", "this is a
  refactor", "this is a no-op".
- Shape-of-diff: "Source diff is N lines per file", "this extracts ... into a
  helper", "renamed parameter X to Y", "the 13k-line diff is mechanical".
- Code-structure / source-doc restatements: don't narrate implementation
  mechanics the diff already shows, or fields, constants, file names, and API
  detail the design docs already carry. State the reader-facing decision or
  invariant instead. The commit message orients the reviewer; the diff and docs
  carry the mechanics.
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
6. **Can a cold member of the intended audience understand the framing without
   reconstructing it from supporting material?** Flag private jargon or missing
   bridges; do not require repeated mechanics or evidence.

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
