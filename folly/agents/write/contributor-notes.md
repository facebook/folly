# Maintainer notes

Use this when writing or reviewing `CONTRIB.md` or `*.contrib.md`, or deciding
whether a rule change needs a note update. `CONTRIB.md` covers a directory and
its subtree; a rule's `.contrib.md` covers that rule.

Write for a future maintainer with normal technical background who has not read
the maintained material or its design discussion. Read that material, any
existing note and supplied context for facts, not a list of contents to copy.

Start with a plain purpose. Keep an existing one when it is clear and accurate.
For a needed point in an existing explanation, compose the plain version you
would give that maintainer before deciding the old wording is effective. Compare
what each version actually tells the reader. Keep the clearer account, including
the old text when it already explains the point well. This comparison does not
require rewriting every passage.

Use your ability to explain things clearly. Write a connected account of what
the design is for and why its main choices matter. Say what each needed point
means; give context first when the reader needs it. Explain only the key reasons
needed to preserve the design's intention. For a directory note, explain the
shared idea and relevant differences. Leave operating instructions in the
maintained material.

When keeping a caution, state what future edits should preserve and why.

Usually explain the idea in your own words. When meaning depends on tricky rule
wording, quote just enough to show it. Explain what it means and why it matters;
mark omissions with `...`. History and evidence are optional. Omit them when the
explanation works without them. Check claims against the sources; do not invent
intent or past events. Link a source when its location would be unclear.

Read the whole note as the maintainer. Keep useful reasons and connections; cut
facts that add no needed understanding. Keep the clearest explanation when
removing repetition. Use familiar words and short, connected sentences. Compare
a revision with the complete original note. Default to no growth; extra text
earns its place only by making a key purpose or needed connection clearer. Apply
the same standard when reviewing.

Update a note when its design explanation changes or becomes stale. A routine
edit does not need a new paragraph.

## Template and format

Do not repeat which rule or subtree a note covers. Its filename and location
already identify that.

Every contributor note begins with this fixed heading:

```text
> NOT A RULE. If loaded as task policy, stop and ask the user.
```

After the heading, the first paragraph states the purpose of the rule or
subdirectory.
