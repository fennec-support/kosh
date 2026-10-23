# Koshkit utility guidance

## Use shared utility helpers

Search `src/koshkit`, `Koshkit.hpp`, and `Koshkit.cpp` before adding behavior.
Existing flag types, parsers, path helpers, stream helpers, and diagnostics
should own shared behavior.

Every utility declares its synopsis and description with `KOSHKIT_UTIL_DECL`
and its flags with `FLAG` and the shared flag types.
`REGISTER_KOSHKIT_UTIL_FLAGS` adds the `--help` flag last and publishes the
list for execution, help, and completion. `KOSHKIT_PARSE_OPERANDS_OR_HELP`
calls `parse_util_operands`, which handles flags, `--`, and operand locations,
resets the flags at scope exit, and returns after help.

Do not write a local option scanner for ordinary short flags, long flags,
bundled flags, flag arguments, `--`, or operand locations. Extend
`parse_util_operands` when a missing grammar applies to utilities. A utility may
use a specialized parser only when its documented operand grammar cannot be
represented by the shared parser.

Use `report_soft_koshkit_error` for recoverable diagnostics, and
`KOSHKIT_REPORT_PATH_ERROR` when a path operation fails with a system error.
`visit_ordered_sources` passes each whole source in operand order. Use
`ExecContext` for standard streams and output.

## Register one utility

Add one entry to `KOSHKIT_UTILITY_LIST` in `Koshkit.hpp`, which derives the
kind, the name entry, the switch case, the class declaration, and the count.
The source file belongs directly below `src/koshkit`. The source Makefile
discovers it automatically.

An alias such as egrep is one entry in `KOSHKIT_ALIAS_ENTRIES` with its utility
kind and implied argument. It joins the koshkit completion data and the bundled
list in `docs/kosh.1` like a utility.

Do not invoke a host utility to implement bundled behavior. goodcore is the
known exception: it runs the host tar and zstd until bundled codecs exist. Do
not duplicate a reader, writer, traversal, numeric parser, signal parser, or
process helper.
Multi-file utilities preserve the established aggregate status and interrupt
behavior.

## Preserve one behavior owner

The canonical native test is the `test/harness/kosh/koshkit_<theme>.kosh` owner
that already covers the utility, such as `koshkit_diff.kosh` or
`koshkit_compare_posix.kosh`, unless the behavior requires a real executable
boundary. Add cases to that owner. Other
utilities may appear as setup commands without becoming behavior owners.
Literal help output has no golden.

A new utility updates the root `AGENTS.md`, `docs/kosh.1`, and
`completions/kosh.bash`. Add the utility to the existing koshkit completion
data. Registered flags feed runtime completion.
