# folly/tracing: Utility for User-level Statically Defined Tracing
----------------------------------------------------------

## StaticTracepoint

The `StaticTracepoint.h` header file defines the Macro
```
FOLLY_SDT(provider, name, arg1, arg2, ...)
```
Invoking the Macro will add a Static Tracepoint at the calling location. Using a
tracing toolkit ([BCC](https://github.com/iovisor/bcc) is an excellent example),
a probe can be attached to the Tracepoint, consume the provided arguments and
perform other tracing / profiling works.

The Tracepoint defined using `StaticTracepoint.h` is also compatible with any
toolkit designed for consuming [SystemTap](https://sourceware.org/systemtap/)
Tracepoints.

Internally, the Macro emits a `nop` operation at the calling location, along
with an Assembler Instruction with empty template and empty output Operands,
and the provided arguments and their sizes as input Operands.

The Macro then append to the ELF `.note` section, with information including
the provider and name of the Tracepoint, address of the `nop` operation, and
size and location (register name or memory location) of the provided arguments.
This way, the tracing toolkits would be able to parse the information, attach
the probes to the correct address, and consume arguments.

The default constraint for the arguments in the Assembler Instruction as
operands is `"nor"`. It means the argument could be an immediate integer
operand, a register operand or an offsettable memory operand. This is a good
default since tracing arguments tend to be integral, and the number of arguments
is likely to be less than the number of registers.

Otherwise, you may see compiler report errors like
```
'asm' requires impossible reload
```
You may want to simplify the Tracepoint (fewer and simpler arguments) in
such case. You may also choose to override the constraint
```
#define FOLLY_SDT_ARG_CONSTRAINT "g"
```
which means the arguments can be any memory or register operands.
