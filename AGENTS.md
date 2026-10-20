# Koshka project rules

Koshka is a fast C and C++ shell. The interactive editor is vendored under
src/toiletline.

README.md is not modified without explicit approval. Invocation, options, moods,
syntax, runtime behavior, builtins, diagnostics, environment variables, startup,
and runtime files belong in docs/kosh.1. Startup file identity and format belong
in docs/kosh.5. New or renamed flags, moods, and builtins update docs/kosh.1 and
completions/kosh.bash. Configuration changes update docs/kosh.5. Architecture
and workflow changes update this file.

## Writing

Write direct, grammatical sentences with explicit subjects and objects. Keep
technical terms, commands, conditions, exceptions, and uncertainty exact.
Remove process commentary, conversation references, change descriptions, filler,
forced humor, and duplicated claims. Use a modal or a stated condition when a
behavior depends on one. Keep present simple for behavior that holds every time.
Comments state facts about the current code and stay brief.

## Code

Use let for deduced locals and let const for immutable deduced locals. Functions
use fn name(...) throws -> ret. Pure virtual functions use abstract. Compare
pointers with nullptr.

Boolean names start with is_, should_, was_, did_, or has_. Counts end with
_count. Byte counts end with _byte_count. Measurement variables include their
units. Lambdas start with do_. Accessors start with get_ or set_.

Free structs and enums use lower snake case. Classes and nested types use camel
case. File operations accept Path. Project-owned C and C++ files carry the
license notice and a description of at most three sentences. Vendored files do
not receive the notice.

.clang-format defines the layout, and make fmt applies it. Lines hold at most
80 columns and indent by two spaces. fn stands on its own line above the
function name. An opening brace takes its own line after a function, class,
struct, union, or enum, and after a control condition that spans several lines.
Consecutive assignments, declarations, macros, and short case labels are
aligned. Wrapped parameters go one per line, and pointer and reference markers
attach to the name. A short if without an else may stay on one line, and a loop
body never does. clang-format adds no braces and collapses consecutive blank
lines into one. The body of an if whose condition contains && or || is braced
by hand. Blank lines between declarations, logical blocks, loops, and returns
are also added by hand.

Search for an existing owner, helper, parser, container, dependency, and every
caller before changing an interface. New abstractions, file splits, file merges,
and dependency upgrades require approval. Per-executor state passes through
EvalContext and constructors.

## Ownership

src/Main.cpp owns flags, startup, scripts, and the interactive loop.
src/Lexer.cpp creates tokens. src/Parser.cpp creates the syntax tree.
src/Optimizer.cpp folds constants and dead branches.

A parser that collects errors recovers inside every command list. It records
the error, skips the failed statement by tokens with a stack of expected
closers, and continues the list. Conditions and bodies inside the skipped
statement still parse, so their errors are reported. A closer that an
enclosing list owns ends the inner list. Dialect errors go through
report_soft_error and do not stop the parse. The execution parser still stops
at the first error.

Evaluation is split across src/Eval.cpp and the Eval sources. Expression families
live in the Expressions sources. Shared helpers live in
src/ExpressionsInternal.hpp. Internal declarations use the owning namespace
followed by internal.

src/Options.cpp owns the option registry. set, shopt, koshconf, the -o test,
SHELLOPTS, BASHOPTS, and $- read and write through it. Koshka settings without a
Bash letter belong to koshconf. Settings are written under their canonical
spelling, and legacy aliases resolve to the same registry entry. The kosh mood
fixes nounset, pipefail, failglob, and extended arithmetic on, and nullglob off.
Explicit option states survive changes between other moods.

src/Koshconf.cpp owns kosh.conf, its presets, and the KOSHCONF encoding. A write
resolves symlinks, preserves the target mode, writes beside the target, renames
the completed file over it, and locks the directory. Restricted shells reject
forms that change settings. Every spawn prepares the child environment and
rewrites an exported KOSHCONF.

EnabledCommandStore holds the builtin words and bundled utilities that enable
has turned on or off. Command lookup, completion, highlighting, hints, and
analysis read it through find_enabled_builtin, is_builtin_name_disabled, and
is_koshkit_utility_reachable on EvalContextState.

Each store owns its state and operations. Scope guards restore saved state.
Snapshots carry shell options, directories, the working directory, and the
creation mask. Fresh evaluators receive replayable source and framed store
sections, prepared environment state, and no startup files.

Owned source normalizes CRLF before lexing, analysis, evaluation, and diagnostics.
Substitution bodies use validation parsing where diagnostics need outer source
coordinates. Analysis reparses each body in its owning node and walks it in the
correct subshell scope.

Runtime diagnostics retain source frames, function calls, embedded sources, and
trap definitions. Source locations use 32-bit positions and interned source
indices. Syntax nodes store end positions separately. Diagnostics and LINENO
share a line index.

src/Platform.cpp selects POSIX or Windows code. Platform calls and types stay
behind src/Platform.hpp and os wrappers. Routed platform fragments compile
through Platform.o. Windows path conversion and named-pipe behavior stay behind
the platform wrappers. Recursive chmod, chown, and chgrp walk through
os::walk_tree, which addresses each entry through its parent directory
descriptor.

src/Completion.cpp owns completion scanning and the completion sources.
Completion, highlighting, diagnostics, and koshkit cat share the tolerant
scanner and semantic roles. Readers of shell source use the shared heredoc
delimiter and terminating-line helpers.

The interactive loop warms the working-directory index before each prompt.
Prompt validation starts a directory epoch. Completion and highlighting reuse
that epoch and defer uncached documentation loads without blocking the editor.
The language server follows relative sources from the document directory.
Rename edits use semantic spans and preserve variable sigils and braces.

src/CLILive owns live views, the screensaver, retained rows, terminal state, and
one synchronized frame write. Redirected frames contain no escape sequences.

## Startup

kosh_main builds command_line, invocation_identity, inherited_shell, startup
configuration, input_plan, and session_config in that order. It seeds session
variables, applies startup configuration, then runs startup and finishes startup.

The startup configuration loads the system file, the user kosh.conf, and inherited
KOSHCONF, then removes KOSHCONF from the environment. It settles a configured
mood before the input plan and applies configured init_moods for an interactive
or login shell after it. A release build has no system-file override. Restricted
or privileged invocations ignore KOSHCONF. An interactive non-login kosh
mood reads ~/.koshrc and no system rc file.

A koshkit utility runs through run_as_multicall before shell startup. The chunk
loop uses script_cursor, interactive_session, and lint_run. run_script_contents
parses, analyzes, and evaluates one script run plan.

## Completion and editor

Command completion uses keywords, builtins, bundled utilities, functions, aliases,
and PATH. KEYWORD_ENTRIES is the sole keyword catalog. Aliases expand up to
eight levels before builtin and koshkit flag lookup.

The completion menu narrows command, path, and koshconf option lists by exact,
smart-case, and subsequence tiers. Other lists retain their candidates. A
callback runs again after a word boundary, a quote, an equals sign, an at sign,
a slash, an erase below the gathered token, or a changed match tier. Deferred
gathers use the pending protocol and the 24 millisecond menu debounce. TAB,
navigation keys, and row acceptance gather immediately. An argument with an at
sign is highlighted as a URL when the command takes ssh or teleport targets.

Inline hints use builtin and koshkit registrations, aliases, functions, manpage
caches, and help caches. They do not search PATH or open files. The editor owns
hint layout and keeps rows within the terminal space available below the input.

Raw mode handles kitty disambiguation, xterm modifyOtherKeys, signal keys,
terminal restoration, and legacy key conversion. The interactive loop enters
raw mode only after job reports, PROMPT_COMMAND, prompt expansion, and the
prompt itself have run.

## Data and process behavior

A forked child marks inherited jobs so wait cannot reach them. An asynchronous
pipeline owns and reaps every stage. POSIX stages share a process group. A
background child, coprocess, or asynchronous stage does not own the terminal.

A pipe puts its writer and reader in different processes when either side can
exceed the pipe buffer. Forked stages close descriptors owned by the parent
before taking input. Parent processes close unused pipe endpoints after
CreateProcess.

Windows process substitution uses a private pipe and a relay thread for the
public FIFO. The relay serves one reader at a time, retries busy opens, repeats
bytes not reported as consumed, and closes every listening instance before
cleanup completes. Internal named pipes use random names, reject remote clients,
create their first instance exclusively, and allow only the current user and
SYSTEM.

## Build and tests

Use root make targets. make MODE=rel builds ./kosh. make MODE=dbg builds the
sanitized debug binary. make MODE=cov builds coverage. make MODE=tinyrel builds
the size-focused binary. make test runs Kosh, CLI, completion, highlighting,
POSIX, and Bash suites. make toiletline_test runs the editor suite.

NO_TOILETLINE=1 make and NO_KOSHKIT=1 make use separate object directories.
Rebuild the ordinary configuration before the next test run. Completion tests
require debug. Bound interactive and long-running commands.

Focused tests use direct Make targets with matching MODE, BIN, and TARGET. The
owning runner controls setup, comparison, cleanup, and result paths. Refill
records output but does not validate behavior. Validate every changed golden
against output, status, diagnostics, side effects, and platform branches. A
direct test target can return success when the test failed, so inspect the
runner's final status line.

Kosh has no native unit tests. Behavior is covered by shell and utility cases.
Test runners use the host shell. The shell under test is passed through BIN.
Keep test owners that share result paths sequential.

## Workflow

Read this file and applicable scoped guidance before planning, reviewing, editing,
writing tests, or committing. Preserve unrelated working-tree changes.

Resolve optional files with rg --files. Verify unfamiliar commands, paths,
interpreters, targets, and environment variables in their execution context.
Run independent reads separately. Keep redirected logs tied to the runner's
working directory.

Use apply_patch for edits. Inspect the exact target immediately before a broad
patch. Print the before-and-after table after each edit batch. Inspect the full
diff, git diff --check, untracked files, and changed goldens before committing.
Verify the active git identity, stage only intended paths, inspect staged
name-status, and keep commit body lines within 72 columns. Never push or create
external artifacts.

## Failure prevention

Recorded mistakes are in [MISTAKES.md](MISTAKES.md). Review the matching entry
before repeating a similar action.

Resolve file paths and executables before invoking them. Read scoped guidance
before changing tests. Search callers and failure contracts before changing a
shared interface. Wait for a build to finish before testing its binary. Use rmdir
for empty directories. Resolve log parents before redirecting output.

Use the owning Make target or export its required runner variables. Keep the host
shell for runners and BIN for the shell under test. Run Windows test targets in
separate steps. Inspect utility flag lists before replacing host commands.
Inspect non-command variable uses before bulk replacement.

Use full paths when assertions require them. Capture one untimed output for
comparisons and discard repeated timing output. Resolve timing tools and serialize
measured workloads. Verify input files before measuring. Bound large reads by
bytes and lines.

Use a throwaway clone or archive for read-only review agents. Do not detach or
change the main checkout from a review agent. Give parallel worktrees private
scratch directories. Check disk space before parallel builds and remove merged
worktrees.

Run PTY tests through their shell wrappers. Judge direct test targets by
their final status line. Keep process cleanup tied to an exit marker. Preserve
platform-specific file attribute semantics when simplifying filesystem code.

A failed review tool call must be checked against the available tool scope before
another parallel call is composed.

The host ps command can report macOS session identifiers directly. Do not scan
each process separately. A shell filter must read its input to the end when the
producer can report EPIPE. Use a fresh tmux socket after stopping a server and
poll for text the pane is known to show.

Create symlinks one at a time when the destination directory does not yet
exist. A full path assertion needs long directory output. A link attribute check
must preserve Windows behavior and must not be replaced blindly with a stat that
follows links.

Cross builds use make -C src TARGET=Windows_NT MODE=rel. Wine probes use the
host shell and WINEDEBUG=-all. A Windows runner receives its test binary through
BIN, not as the runner interpreter. PowerShell environment assignments use its
own syntax.

Read test wrappers before invoking Make. A .py PTY probe runs through its
.sh wrapper. Judge direct test status from the final status line. An
editor test waits for an exit marker instead of a fixed sleep.
