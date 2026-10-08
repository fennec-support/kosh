# Builtin guidance

## Use shared builtin helpers

Search `src/builtins`, `Builtin.hpp`, and `Builtin.cpp` before adding behavior.
An existing builtin, flag type, parser option, diagnostic helper, or execution
helper should own shared behavior.

Every builtin declares its flags with `FLAG_LIST_DECL`, `FLAG`, and the shared
flag types. `REGISTER_BUILTIN_FLAGS` publishes that list for execution, help,
and completion. `PARSE_BUILTIN_ARGS` parses ordinary operands.
`PARSE_BUILTIN_ARGS_WITH_LOCATIONS` also retains operand spans.

Do not write a local option scanner for ordinary short flags, long flags,
bundled flags, flag arguments, `--`, or operand locations. Extend the shared
flag parser when a missing grammar applies to more than one command. A builtin
may use a specialized parser only when its documented operand grammar cannot be
represented by the shared parser.

Use `SHOW_BUILTIN_HELP_AND_RETURN` or
`SHOW_BUILTIN_HELP_EXTRA_AND_RETURN` for help. Use
`report_soft_builtin_error` for recoverable builtin diagnostics. Source spans
come from `ExecContext` and the parsed operand locations.

## Register one builtin

Add one `KOSH_BUILTIN_KINDS` row and the string table entry in `Builtin.hpp`.
A row holds the kind, the section of the `builtin --list` report, and whether
`execute_builtin` answers `--help` before the builtin runs. The list generates
the kind enum, the dispatch switch, the class declaration, and the section and
help tables, and the kind count follows the list length. The source file
defines only `execute` and belongs directly below `src/builtins`. The source
Makefile discovers it automatically.

Do not add a builtin to special, declaration, optimizer, or fresh-process
tables unless its semantics require that classification. Runtime state belongs
on `EvalContext` or an existing state owner. Platform operations pass through
the `os` wrappers.

## Preserve one behavior owner

The canonical native test is `test/kosh/<builtin>.kosh` unless the behavior
requires a real executable boundary. Add cases to that owner. Compatibility,
completion, and terminal tests cover only contracts owned by those harnesses.
Literal help output has no golden.

A new builtin updates the root `AGENTS.md`, `docs/kosh.1`, and
`completions/kosh.bash`. Registered flags feed runtime completion. Custom
completion policy belongs in the existing completion tables.
