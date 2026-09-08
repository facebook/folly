## USER (2026-08-06T23:55:29.959Z)

Use `uintptr_t` for the metadata value.

## USER (2026-08-07T05:26:08.141Z)

> unsupported architectures

Wait did this end up being overcomplicated? The goal is to have as little
conditional compilation as possible, and not to commit to a 32-bit contract
until we have use-cases evidence in the future.

## USER (2026-08-07T05:27:22.827Z)

Also I did NOT mean to propose #erroring on 32-bit. The error was for "neither
32 nor 64". On 32, we just omit the getter. Did you misunderstand?

## USER (2026-08-07T06:34:44.707Z; excerpt)

> +Zero means no metadata.

We discussed something else! Recall I asked for Metadata with optional
semantics. What happened?
