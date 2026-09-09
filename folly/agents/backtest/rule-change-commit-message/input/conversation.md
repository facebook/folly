# Conversation evidence

This packet preserves the alternating feedback and proposals that led to the
rule change. It is evaluation input, not proposed rule wording.

## Assistant diagnosis 1

> The repeated failure is that I kept translating concrete code into abstract
> design language:
>
> - `copyAttemptId` became “per-attempt identifier.”
> - `destinationObjectId` became “numeric destination-object ID.”
> - Three fields became a “recovery unit.”
> - An optional struct became an “explicit absent state.”
>
> That increases reader effort. None of the rules require this. I misapplied
> “don’t duplicate the diff” as “avoid identifiers.” The correct rule is: use
> identifiers as anchors, but explain why they exist instead of narrating their
> types.

The response then proposed identifier guidance, vocabulary bans, explaining
optionality, and an actor/action requirement for every sentence. Its example
summary was declarative: it listed what the history must store, then described
the optional wrapper.

## User feedback 1

> Abstract-legalese, as I mock it, is a very common agent failure mode.
>
> Please propose rule changes that would prevent it. I NEVER want agentic
> writing or coding to do premature abstraction. Every abstraction has a high
> long-term cost, but agents don't see that because their large context makes
> abstraction locally (temporarily) cheap.
>
> I was attempting to reference the existing “don't duplicate the diff” rule.
> Would the existing rule benefit from an update?
>
> The loop rewarded compression. What was the rationale given? Compression is
> good, illegibility is bad; are there tweakables?
>
> I don't want to legislate identifier usage in detail in the core rules. It
> might be OK to add a clarification that we don't hate anchoring via
> identifiers, but we do hate identifiers replacing framing / narrative /
> explanation.
>
> Vocabulary bans are problematic. The thing we should instead encourage is
> deliberately choosing words that minimize reader effort.
>
> I have no idea what the proposed optionality rule was talking about.
>
> An “every sentence” actor/action rule is likely to end badly.
>
> The proposed summary was still declarative. Instead, describe what recovery
> has to do; the things that must be stored should fall out of that story. Plain
> language is key.
>
> “One optional recovery unit ...” was entirely unclear. All that was needed was
> “If a `resumeState` field is absent ...” (or the best identifier).

## Assistant proposal 2

The response proposed a broad `AGENTS.md` rule to prefer concrete work over
abstraction, plus edits to `writing.md`, `code.md`, and `critic-iterate.md`. It
said abstractions add long-term cost, suggested replacing mechanism and causal
chains with concrete flow, and proposed a reader-effort check.

## User feedback 2

> The `AGENTS.md` rule is in the wrong place and likely would not affect
> wording. It also biases away from useful abstraction: symmetry can justify
> abstraction and compression.
>
> Did you think critically about any of these proposals, or were they ad-libbed?

## Assistant diagnosis 2

The response acknowledged that the broad rule was not tested against the actual
failure. It narrowed the likely changes to the writing rule about duplicating
the diff, a compression-equivalence test, and a reviewer check. It proposed that
a shorter draft was acceptable only if a reader could still answer the same
questions without opening the diff.

## User feedback 3

> “Without opening the diff” is not a general compression rule.
>
> The reviewer test sounded like a license never to cut wordiness.
>
> Facts must be necessary to the artifact's goal. Agents are routinely
> completionist: they restate diff contents in commit messages and adjacent API
> behavior in docs without an essential connection.
>
> “Understandable” is too weak. The target audience should understand the right
> amount of structure — neither too much nor too little — correctly, with the
> least effort.

## Assistant proposal 3

The response proposed a lexicographic objective: minimize reader effort first,
then length. For commit messages it described a message-first reader model: the
message supplies the mental model used to read the code. It proposed adjusting
the duplicate-diff rule, compression test, reviewer rule, and code abstraction
rule.

## User feedback 4

> Retelling the whole code adds understanding, but is redundant with later
> reading the diff. Consider the limit points:
>
> - A diff with many self-explanatory test scenarios needs only one or two
>   sentences explaining why it exists.
> - A one-line fix to a complex system that mitigates a heisenbug may need the
>   complete story from detection through diagnosis, mechanism, remediation,
>   and validation.
>
> Calling this field a “Summary” is misleading, but Phabricator fixes that name.
>
> The commit-message and source-restatement rules overlap. Do not carry that
> duplication into the revised rules.
>
> “Do not prefer a shorter draft when it requires more inference” still risks
> licensing the retention of everything.
>
> The rules need to ask whether each fact is necessary to the artifact's goal.
> “Understandable” is not enough: the audience should understand the right
> amount of structure, correctly, with the least effort.
>
> Use this repeated A-B-A-B feedback as the evaluation framework. Do not
> over-condense it. Ground a c-i-5 revision of the canonical `~/folly_agents`
> rules in it.

## Assistant proposal 4

The response proposed an objective that minimized reader effort before length,
made shorter wording a tie-breaker, and treated change cost as another design
factor. It also proposed a commit-message rule against repeating code details
that added no further understanding.

## User feedback 5

> Retelling code can add understanding even when it is redundant with the diff.
> The decision is not whether repetition adds anything in isolation, but whether
> the message needs that fact before the reader opens the code.
>
> A commit message is read first. The code is then read through the model the
> message established. The message should therefore be intelligible without the
> code, but should not repeat details that are unnecessary to establish that
> model.
>
> Reading cost matters much more than change cost because code is read far more
> often than it is changed. If reading effort is equal, shorter still wins.
>
> The highest-level target is not merely “understandable.” The intended audience
> should understand the right amount of structure, correctly, with the least
> effort. Wordy prose, abstract legalese, and vague overcompression all fail that
> target in different ways.

## User feedback 6

> “Do not repeat code details that add no further understanding” is still too
> simple. A large diff full of self-explanatory test cases may need only one or
> two framing sentences. A one-line heisenbug fix may need the whole story:
> detection, diagnosis, mechanism, remediation, and validation.
>
> The “commit message” and “source-doc restatement” rules overlap; do not preserve
> both as separate rules.
>
> A rule against shortening whenever it requires more inference can license
> keeping everything. Facts must earn their place by serving the artifact's
> goal. Agents otherwise become completionist: they restate diff contents and
> adjacent API behavior merely because those facts are true and related.
>
> Preserve this reasoning in the rules package's contributor guidance as well as
> the operational rules, so future edits do not drift back toward word-count,
> completionism, or blanket anti-abstraction.
