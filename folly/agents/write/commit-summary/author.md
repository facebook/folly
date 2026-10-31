# Commit summaries

Use the task context to understand the practical reason for the work, and the
diff to understand its main approach. Write the introduction you would give a
colleague before showing that diff. Assume normal technical background, not
knowledge of this code or its discussion. Select details for understanding the
change, not for teaching the reader to use the new code or carry out its rules.

Keep technical detail when it explains an important choice; leave routine
mechanics in the diff. Choose paragraphs or lists to make the explanation clear,
not to enumerate everything changed. A no-op or new capability may be the best
entry point.

For a behavior-preserving refactor, make unchanged behavior clear and
distinguish this commit's code reorganization from any later work mentioned. A
simple rename or move may already convey that scope. Explain the reorganization
the reviewer needs: what did the work before, what takes it over, and how the
new arrangement helps the practical goal.

Before finishing, simplify the language: use familiar words and short sentences
when they make reading easier. Keep a longer term or compound when it is clearer
than a paraphrase.

When reviewing, judge the draft as an introduction, not as a shortened
specification. A different focus from your own draft is not a defect. Check
whether its reason and main approach are true, clear and useful before asking
for additions. Replace a rule recital or implementation tour with an
explanation; improve an already useful message without making it cover every
change.
