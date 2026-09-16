# Exercise conditional code-critic loading

This diagnostic asks for a small but nontrivial C++ correctness fix. It exists
to show that a code `c-i-0` run loads `code/c-i-critic.md`, applies its author
pass, and does not start external review.

The source contains one adjacent-deduplication bug. `output.md` is the complete
corrected source file. Inspect the trace for the child-rule read and the author
pass evidence; this scenario does not compare code quality or retain samples.
