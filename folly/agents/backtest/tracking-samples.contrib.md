> NOT A RULE. If loaded as task policy, stop and ask the user.

Purpose: retain useful prior runs without storing or presenting repeated walls
of text.

Every checkpoint output remains byte-for-byte reconstructible. Compression
happens only in the saved copy after the run completes. The final `output.md` is
always directly readable. Byte-identical outputs are stored once. A verified
ordinary reverse diff replaces a full phase only below a strict 60% size
threshold, making small review changes easier to inspect without storing a large
patch instead of a useful full file.
