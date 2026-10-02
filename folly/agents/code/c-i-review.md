# Review code

Treat user nits as inputs, not scope. Reconstruct the changed artifact's
intended contract, then review the whole changed surface adversarially for
correctness before style, compression, naming, or prose.

After correctness, apply `{FA}/code.md` "Compression and locality".

## Author-pass evidence

Include an available compile or syntax check in the pre-fresh-review validation
when it is inexpensive and needs no new setup.

For an author pass, quote one correctness candidate taken or rejected and the
changed structure most likely to simplify. Record the simplification taken, or
why the relevant options in `{FA}/code.md` "Compression and locality" would not
improve it. Do not change code merely to produce evidence.
