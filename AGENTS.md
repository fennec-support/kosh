# Koshka project rules

## Project

Koshka is a C and C++ shell. Speed is the defining goal. The interactive editor
is vendored under `src/toiletline`.

Never modify README.md without explicit approval.

`docs/kosh.1` owns invocation, options, moods, syntax, runtime behavior,
builtins, diagnostics, environment variables, startup, and runtime files.
`docs/kosh.5` owns startup file identity and format. New or renamed flags,
moods, and builtins update `docs/kosh.1` and `completions/kosh.bash`.
Configuration changes also update `docs/kosh.5`. Architecture and workflow
changes update this file.

## Code

- Use `let` and `let const` for deduced locals. Literal counters keep an integer
  type. Functions use `fn name(...) throws -> ret`.
- Compare pointers with `nullptr`. Do not use pointer truthiness.
- Boolean names start with `is_`, `should_`, `was_`, `did_`, or `has_`. Counts
  end with `_count`. Measurements name their unit. Lambdas start with `do_`.
  Accessors start with `get_` or `set_`.
- Free structs and enums use lower snake case. Classes and nested types use
  camel case. File operations accept `Path`.
- Prefer names to comments. Owned code carries no comment beyond the license
  notice and the file description header. C and C++ comments use `/* ... */`.
  Brace conditions containing `&&` or `||`, including every branch of an if and
  else-if chain. Separate logical blocks, loops, and returns with blank lines.
- Every project-owned C and C++ file starts with the top-level license
  notice and a description of at most three sentences naming its concrete
  responsibilities. The description explains why a non-obvious split file
  exists. Do not add the notice to vendored files.
- Use a static table for three or more name comparisons and a switch for hot
  leading-byte dispatch. Static name tables use `consteval StaticStringMap` or
  `StaticStringSet` with SSK keys and derived byte and length filters.
- Search for an existing owner, helper, parser, container, and dependency first.
  New abstractions, file splits, file merges, and dependency upgrades require
  approval. Per-executor state passes through `EvalContext` and constructors.

## Architecture

- `src/Main.cpp` owns flags, startup, scripts, and the interactive loop.
  `src/Lexer.cpp` creates tokens. `src/Parser.cpp` creates the syntax tree.
  `src/Optimizer.cpp` folds constants and dead branches.
- `kosh_main` builds `command_line`, `invocation_identity`,
  `inherited_shell`, the startup configuration, `input_plan`, and
  `session_config` in that order, seeds the session variables, applies the
  startup configuration, then calls `run_startup` and `finish_startup`. The
  startup configuration reads `/etc/kosh.conf`, the user `kosh.conf`, and an
  inherited `KOSHCONF`, removes `KOSHCONF` from the environment, and settles a
  configured mood before the input plan. An option the command line or
  `KOSH_ANALYSIS` names keeps its value. A restricted invocation ignores
  `KOSHCONF` and keeps only presentation and diagnostic options from the files.
  The kosh mood sources no shell startup file. The chunk loop is
  driven by `script_cursor`, `interactive_session`, and `lint_run`.
  `run_script_contents` computes one `script_run_plan`, then runs its parse,
  analysis, and evaluation phases.
- Evaluation is split across `src/Eval.cpp` and the `Eval` sources. Expression
  families live in the `Expressions` sources. Shared helpers are declared in
  `src/ExpressionsInternal.hpp`.
- Declarations from an `Internal` source use `koshka::internal` or the owning
  namespace followed by `internal`.
- `src/Toiletline.cpp` defines the vendored editor configuration macros itself
  and cannot include `src/Toiletline.hpp`. Declarations needed by that source
  belong in a light header such as `src/ToiletlineHistory.hpp`.
- RPS1 and PS1_TRANSIENT expand once per prompt beside PS1 in
  `src/Toiletline.cpp`. The editor measures the right prompt once, and each
  keystroke only compares the first input row against that width.
- Each shell loads history once at startup and then keeps a private branch with
  session-local event numbers. Normal prompts and accepted-command appends do
  not import peer records. Only explicit history synchronization replaces the
  branch; cross-shell search uses a separate read-only file snapshot.
- Owned source normalizes CRLF before lexing, analysis, evaluation, and
  diagnostics. A lone carriage return remains data.
- Analysis streams one top-level and-or chain in two passes. The first gathers
  suppressions, scopes, directive spans, and parse errors. The second uses
  `AnalysisUnitStream` and `analyze_ast`, then releases the arena span.
  `top_level_sibling_carry` keeps cross-unit data. `FUNCTION_ARENA` stays null.
- Substitution bodies parse lazily at run time. A parser in substitution
  validation mode makes the lexer parse each body once in a nested parser over
  the outer source from the body offset, so every location is in outer
  coordinates. The lexer records each body error and keeps lexing, and the
  parser reports all of them. The mode covers the scan pass, the syntax
  preflight, the whole-file parse, the language server, and followed sources.
  The streamed analysis pass and the executing parser leave it off. Analysis
  parses each exact body again in a scratch arena when it reaches the owning
  node, and walks it in a subshell scope unless it is a function substitution.
- Runtime state owns moods, diagnostics, strictness marks, and shell options.
  `src/Options.cpp` owns the option registry: a stable numeric id, a koshconf
  name, the Bash spellings, the letter, the type, the class, and the kosh mood
  value of every option. `set`, `shopt`, `koshconf`, the `-o` test,
  SHELLOPTS, BASHOPTS, and `$-` read and write through it. `set` and `shopt`
  accept only Bash names; Koshka settings without a letter belong to
  `koshconf`. The kosh mood holds nounset, pipefail, failglob, and extended
  arithmetic on and nullglob off, and a write of another value is an error.
  Explicit states survive changes between the other moods. An explicit mood
  change clears the level from `-W`, `-WW`, or `-WWW`.
- `src/Koshconf.cpp` owns the `kosh.conf` reader and writer, the presets, and
  the base64 TLV form of `KOSHCONF`. Only the mood and the interactive options
  reach a file or `KOSHCONF`. A restricted shell refuses every `koshconf` form
  that changes a setting.
- Eval snapshots keep shell and shopt state, directories, the working directory,
  and the file creation mask. Each store snapshots, restores, and writes its own
  section of the bootstrap. A fresh evaluator receives replayable shell source
  and one framed section per store under a shared section header. Windows
  duplicates live process handles into the
  authenticated child. The bootstrap owns received handles until evaluator state
  adopts them. Restricted behavior uses one context state. BASHPID identifies
  forked evaluators. `$$` identifies the original shell, and `PPID` names its parent in every
  forked or fresh evaluator.
- Each launch of a fresh evaluator also sends the origin of its command text:
  the source name, the starting line, and the rest of the first and last
  lines. The child maps its command onto a window padded to that line through
  an embedded source, so diagnostics, traces, and LINENO use the parent's
  coordinates. The child skips analysis, because the parent analyzed the text.
  The origin also carries the rendered call site of every function and source
  frame above the launch site, each on its own padded window, and the lines
  LINENO counts beyond a rendered line, such as those before an eval. The
  bootstrap carries the defining name, lines, and edges of each function, which
  the child applies to the definitions it replays.
- Each store owns its state and operations. The trap store keeps one map of
  trap definitions, the history recorder and source retention are separate from
  the source store, and runtime state keeps its fields private. A scope that
  saves state restores it through a guard such as `RuntimeStateScope`,
  `DefinitionStateScope`, `TrapActionScope`, `UntracedTrapScope`,
  `SubstitutionFrame`, or `os::ScopedEnvironment`.
- An asynchronous pipeline job owns and reaps every stage. POSIX stages share a
  process group. The last stage owns status and job output. Stream writes retry
  partial writes and reject zero-length writes while bytes remain.
- A pipe has its writer and its reader in different processes whenever either
  side can exceed the pipe buffer. A deferred stage report is written only after
  every reading stage runs in a child.
- A forked stage closes every descriptor the parent still owns before it reads.
  The close-on-exec flag releases a stage that execs and keeps every descriptor
  of a forked builtin, group, or subshell.
- A named-pipe server connects before its child evaluates source. Thread launch
  order is not connection readiness.
- A parent closes each unused pipe endpoint after CreateProcess so readers can
  observe EOF when the child exits.
- A Windows process substitution child uses a private pipe. A relay thread in
  the shell keeps several instances of the public named pipe listening and
  binds the first client that reads a byte or writes one, so a client that
  only opens or probes the path receives no data. Cleanup ends an unbound
  relay, which closes the private pipe before the child is reaped.
- Every internal Windows named pipe has a random name, rejects remote clients,
  creates its first instance exclusively, and carries a DACL that admits only
  the current user and SYSTEM.
- Windows named-pipe redirections use OPEN_EXISTING for every shell open mode.
- Recheck mutable runtime state after any startup file that can change it.
- `src/koshkit` holds only utility sources. Code shared by utilities lives in
  `src`, such as `src/CliLive` and the file-mode parser in `src/UtilsIO.cpp`.
- `src/CliLive` owns every Evil live view and the retained live rows. It holds
  the alternate screen, raw key input, sample and refresh cadence, the styled
  header, and one write per frame. Redirected frames carry no escape sequences.

## Platform

- `src/Platform.cpp` selects POSIX or Windows code. Platform headers, calls,
  types, and macros stay behind `src/Platform.hpp` and `os` wrappers.
- A platform-boundary move preserves each existing platform value unless the
  value is part of the requested behavior change.
- Linux static PIE uses the system linker without `-Bsymbolic`. Binding libc
  locale symbols in the executable corrupts the locale state used by
  `localeconv()`. MODE=tinyrel links a non-PIE static executable, so the loader
  applies no relocations and the image pages stay clean. The tinyrel image
  therefore loads at a fixed address without address space randomization.
- Descriptor-rebinding wrappers increment the descriptor epoch. Cached color
  decisions refresh against it. Forks, process groups, filesystems, and processor
  counts also use platform wrappers.
- `tail -f` is woken by `os::FileWatcher` through inotify or kqueue, and other
  targets are polled. Every followed file is rescanned after each wait. A
  missed event delays the output by at most one interval.
- A routed platform fragment is included in `src/Platform.cpp` and owns no
  object of its own. Compile it through `Platform.o` for the active target and
  mode. Compiling a fragment directly produces unrelated scope errors.

## Completion and language server

- `src/Completion.cpp` drives the `Completion` scan, highlight, syntax, path,
  manpage, and cache sources.
- Completion, highlighting, diagnostics, and koshkit cat share the tolerant
  scanner and semantic roles. Completion, highlighting, and command lookup
  share directory indexes.
- The interactive loop indexes the working directory before each prompt through
  `utils::warm_directory_index`. The ghost suggestion needs no tab. The ghost
  runs for any non-empty token. A directory is indexed as soon as its slash is
  typed.
- An open completion menu narrows the gathered candidates in the editor by the
  same exact, smart-case, and subsequence tiers as `match_tier`. The callback,
  and with it a `complete -F` function or `-C` command, runs again only for the
  first byte of a word or path component, a blank, quote, equals sign, or
  slash, an erase below the gathered token, or a change of the best tier.
- Command completion reads keywords, builtins, bundled utilities, functions,
  aliases, and PATH. `KEYWORD_ENTRIES` is the sole keyword catalog. A `type`
  operand reads the same catalog. Only the listing mode accepts an empty
  operand.
- The inline hint rows show a syntax problem before the synopsis.
  `describe_syntax_problem` parses the line with the real parser in
  substitution validation mode, in the session mood, inside a completion
  scratch mark, and shows the first error with its detail. Hand-written
  wording would drift from what Enter reports. The parse costs about as much
  as the highlight pass. The error shows wherever the caret is. The editor
  has no continuation prompt, so an open
  construct counts as an error, but a trailing backslash continues the line.
- Static koshkit completion names stay alphabetically sorted.
- The inline hint is `compose_command_hint` in
  `src/CompletionManpage.cpp`. It runs on every keystroke for the command of
  the segment or command substitution under the caret, and reads only
  builtin and koshkit registrations, aliases, function definitions, and the
  manpage and help caches. It never forks, searches PATH, or reads a file.
- Every hint is a header naming its kind, a line break, and a body. The
  headers are `builtin synopsis`, `utility synopsis`, `command synopsis`,
  `subcommand synopsis`, `alias synopsis`, `function synopsis`, `flag`,
  `syntax error`, and the severity word of an analysis finding.
  The editor owns the layout because it knows the width: a two-column
  indent on every row, the header cut to one row, and the body wrapped at
  spaces onto at most three rows with an ellipsis on the last. The rows
  under the input are never more than the terminal rows the block leaves
  free, so a short terminal drops body rows, then the header, then the
  hint. The rows are drawn under the last input row without counting them
  in the block rows, every frame erases rows a shorter hint left behind,
  and the editor holds them away while a menu or search is open. While a
  prefix key such as Ctrl-X, a vi operator, or a vi find key waits for its
  next key, the editor writes its own pressed keys and waiting text in the
  rows without calling the callback, so the row options also govern it.
- `src/Toiletline.cpp` defines `TL_NO_SUSPEND` and `TL_CTRL_Z_UNDO`, so
  Ctrl-Z undoes while a line is read. A foreground program runs with the
  terminal in its usual mode and receives Ctrl-Z as a stop signal.
- Raw mode requests the kitty disambiguate flag and xterm modifyOtherKeys
  level 1 when the `extended-keys` option is on. Leaving raw mode and turning
  signal keys on withdraw both. The editor byte reader turns each key reported
  in either form into its legacy bytes before any key loop reads it, and leaves
  a key without a legacy form, such as Ctrl-Shift-Z, to the parser.
- The editor calls an idle hook after 250 ms without a key. The hook fills
  the hint caches through `step_idle_documentation`, which starts one
  `os::ProgramCapture` child under the man and help trust rules and reads it
  without blocking on each repeat, so a key is served while the child runs.
  Every load, miss, and timeout lands in the same caches explicit flag
  completion uses, and a submitted line kills a running load. The hook also
  keeps `describe_analysis_finding` for the paused line, which analyzes with
  unresolved commands silenced and no followed sources, so it reads no file.
- The highlight callback receives the caret and follows it. It reuses the
  spans of an unchanged line within one prompt, and on a caret move it only
  overlays the pair from `find_matching_bracket`, which reads the highlight
  spans to skip quoted, commented, and here-document brackets.
- The language server wraps completion in `begin_explicit_completion` and loads
  command documentation lazily. Mood selection checks the shebang, language
  identifier, then extension. `shellscript` selects bash.
- `analyze_ast` optionally collects `analysis_symbol_records` for hover,
  outline, definition, and rename. Ordinary analysis passes null. Assignment
  records cover ordinary, builtin, loop, arithmetic, read, mapfile, getopts,
  printf, and declaration binders. Function records keep body spans.
- Rename uses semantic spans from the open document. Variable edits preserve
  sigils and braces. Command edits require a local function or alias definition.
  Variable names use `word_is_plain_identifier`. Command names use
  `word_is_function_name`.
- `src/EvalVariables.hpp` owns dynamic variables. Completion and hover respect
  the mood. Bare koshkit completion works in the default mood and with the
  koshkit option.

## Editor integrations

- The editor clients live in their own repositories:
  `fennec-support/kosh.nvim`, `fennec-support/kosh-vscode`, and
  `fennec-support/kosh-zed`. Homebrew installs from
  `fennec-support/homebrew-kosh`, whose formula reads the newest release tag
  and its `SHA256SUMS` each time Homebrew loads it.
- Formatting reaches every client through `textDocument/formatting`. The
  `kosh --format` command is the documented fallback for a setup without the
  server.
- A client document selector names only the language identifiers the format
  detector compares against. These are `markdown`, `yaml`, `dockercompose`,
  `dockerfile`, `makefile`, `json`, and `jsonc`, plus the shell identifiers. Any other
  non-empty identifier makes the whole document parse as shell. A host format
  the detector finds by name is matched by file name in the client.
- Zed receives no identifier for Shell Script. The extension of the file
  selects the mood.
- A client specifies no transport kind. The stdio kind appends a `--stdio` flag
  that the shell rejects. An executable server with no transport uses the
  child's standard streams. Read the client library's argument construction
  before selecting a transport.
- The client log of a real editor session confirms an integration change. The
  VS Code family writes one file for each extension output channel under its
  own log directory.
- A client resolves the shell from its configured path, then PATH, then its own
  storage. A missing shell is downloaded from the newest release of
  `fennec-support/kosh`. Drafts and prereleases are skipped. An asset is named
  `kosh-<platform>-<processor>-<tag>`. The platform is `darwin`, `linux`, or
  `win32`. The processor is `aarch64` for arm64 and `amd64` for x86-64. A
  Windows asset ends in `.exe`. Each release also carries `kosh.1.zst`,
  `kosh.5.zst`, `kosh.bash`, and a `SHA256SUMS` file. The install scripts
  are served from the master branch. `scripts/install.sh` and
  `scripts/install.ps1` resolve the latest release,
  verify every file against `SHA256SUMS`, and install with the `make install`
  layout. They exit without installing when the kosh at the target or on PATH
  is at least that release, or exactly a pinned `KOSH_INSTALL_VERSION`, unless
  forced.
- `docs/RELEASING.md` is the release checklist for this repository and every
  client and packaging repository. `scripts/update-package-recipes.sh` pins the
  Arch and Alpine recipes in `deploy/` to a release; recipes stay static
  because package builds may not query the network.

## Diagnostics and storage

- `print_source_backtrace` merges source frames and function calls into one
  trace. An error renders where its stack is intact, and a rendered error is
  never printed twice. `embedded_sources` maps substitution text onto the
  file, and `trap_definition` carries the `trap` command of an action.
  `to_string` resolves function windows and embedded text itself.
- `DiagnosticsCatalog.cpp` owns analysis diagnostics.
  `DiagnosticsDispatch.cpp` owns command dispatch. Other `Diagnostics` sources
  own grouped checks. `SimpleCommand::analyze` builds one `command_lint_input`.
  Whole-script checks run once after `analyze_ast`.
- A leading ShellCheck disable applies to the file. Other disables apply to the
  next complete and-or command. A numeric code suppresses all variants. An exact
  slug suppresses one. Parser and runtime errors remain enabled.
- `SourceLocation` stores 32-bit position, length, and interned source index.
  Syntax nodes store end positions separately. Diagnostics and LINENO share a
  line index. Each interned source name records whether it names a file or the
  command string, so a file called `-c` stays a file. Only a sourced file or
  mimicked script frame owns a BASH_SOURCE row. An eval has none, and a line
  number inside an eval counts from the line of the eval command.
- Small types stay in light headers. Shared behavior stays on the value type.
  `ArrayList::find` returns `Maybe<usize>`. Membership uses
  `find().has_value()`.
- `Allocator` is one tagged word for pooled heap, bump arena, or fake storage.
  Project code allocates through this API. Only heap storage is freed. Ownership
  queries identify a specific arena. Raw storage is guarded until throwing
  construction succeeds.
- `ArrayList` allocates on first growth and stores 32-bit length and capacity.
  `String` has inline storage and an exact first heap allocation. It caches
  whether its bytes are ASCII, and every mutator that can add a high byte
  resets that cache. Use `SparseList` for almost-empty member lists.
- Bump arenas register destructors for nontrivial objects. Use
  `is_arena_destructor_noop` only when reachable resources have arena lifetime
  or need no cleanup. Destructor chunks hold 32 records first, double, and stay
  at 64 KiB. Arena blocks after the first grow fourfold up to 64 KiB.
- `WordSegment` is 32 bytes on 64-bit targets. Two 32-bit units hold its span,
  kind, and flags. Parsed text, cache data, flattened values, and segment lists
  move into the token arena. Runtime copies own heap storage. Parsed word tokens
  need no destructor record.
- Segment caches hold substitution, arithmetic, folded results, and lifetime
  identities. Parsed caches use syntax or function arenas. Runtime copies use
  the heap. Lifetime checks reject data after arena reuse.

## Build and tests

- Prefer make targets. The top Makefile delegates to `src/Makefile` and supplies
  the processor count. `make MODE=rel` builds `./kosh`. `make MODE=dbg` builds
  `./kosh-dbg` with AddressSanitizer and UndefinedBehaviorSanitizer.
  `make MODE=cov` builds `./kosh-cov`. `make MODE=tinyrel` builds
  `./kosh-tinyrel` with size flags and section garbage collection; only that
  mode takes extreme size options, and rel stays speed oriented. Debug is the
  default. `make clean` owns
  removal. Never remove `./kosh` directly.
- `NO_TOILETLINE=1 make` builds the no editor configuration in a separate
  object directory and links the same `./kosh-dbg` path. Rebuild the ordinary
  configuration before the next fixture run.
- `NO_KOSHKIT=1 make` leaves out `src/koshkit`, `src/CliLive.cpp`, and
  `src/UtilsOwnership.cpp`, defines `KOSH_NO_KOSHKIT`, and links the same
  path from its own object directory. `Koshkit.cpp` keeps signal and size
  helpers and stubs the lookup, so no utility name resolves and the `koshkit`
  builtin reports an error. The test Makefile skips fixtures that need the
  utilities, and the harness helpers need `KOSHKIT_BIN` set to an ordinary
  build.
- `make test` runs Kosh, CLI, completion, highlighting, POSIX, and Bash
  suites. `make bench` runs benchmarks.
  `make toiletline_test` runs the standalone editor unit suite.
- Kosh has no native unit tests. Never write them. Behavior is covered by
  fixtures that drive the shell or a utility.
  Completion tests require debug. Bound interactive and long-running commands.
  Test runners apply a deadline to each case.
- An editor fixture types the next line only after the prompt hook reports
  the next prompt. Bytes that arrive while a command runs meet the cooked
  terminal, which echoes them into the command output. An explicit TAB
  completion turns Ctrl-C into a signal while its program runs.
- `make -C test refill` regenerates goldens for Kosh, CLI, completion, and
  highlight fixtures. `REFILL` selects source stems. POSIX and Bash fixtures
  compare against their reference shells and do not use repository goldens.
  Goldens live directly under `test/expected` and have unique names. Read every
  changed line.
- Refill records process output. It does not validate behavior. Every changed
  golden requires absolute validation against its fixture, including output,
  status, diagnostics, side effects, and active platform branches.
- A native fixture that reaches the timeout status cannot use `refill`. The
  runner treats that status as a driver failure. Verify its output and patch
  its golden.
- Make discovers inputs, platform skips, and direct fixture targets. Runners own
  setup, output, comparison, refill, and cleanup. Results are under
  `.test-work`. Auxiliary test shell scripts use two-space indentation.
- The test Makefile owns fixture discovery, pattern targets, platform skips, and
  parallel scheduling. Each target delegates one fixture to a small runner.
  `test/bin/run-test` prints the running and final status lines. Harness
  runners execute one fixture, compare its result, write failures to stderr,
  and return the comparison status.
- The shared CLI, completion, and highlight behavior is implemented by
  `test/bin/run-harness-script`. The other process models use one runner each.
  Helpers live in `test/bin` because `bin` is a valid directory name on every
  supported checkout platform.
- The harness was simplified by deleting the worker layer and directory
  specific wrapper copies. Make expands each harness wildcard into direct
  pattern targets. Each recipe passes its fixture path to `run-test`. The
  shared runner prints status and records diagnostics. A process model runner
  only launches the required commands, compares output, and returns a status.
  Shared setup is exported by Make or kept in the runner that uses it once.
- Run a focused fixture through its direct path target, for example
  `make -C test harness/kosh/name.kosh` or
  `make -C test harness/cli/name.sh`. Pass matching `MODE`, `BIN`, and `TARGET`
  values when a direct invocation needs a different root build. Native Kosh
  fixtures run with `-WWW` and keep all diagnostics in their output. The test
  Makefile points XDG_CONFIG_HOME at a missing directory and unexports
  KOSHCONF, so no user `kosh.conf` reaches a fixture; a fixture that needs one
  sets XDG_CONFIG_HOME or HOME itself. A host `/etc/kosh.conf` is still read.
- Koshkit rm tests use `--dry-run`. Cleanup uses koshkit rm after a nonempty
  path check. Bashdiff and mimicrydiff need Bash 5.3 or newer.
  `scripts/find-modern-bash.sh` selects one from PATH, and `BASHP` overrides that
  choice. An unsuitable `BASHP` fails the suite while a suitable Bash is
  installed.
- Golden comparisons use the host `diff` command with platform-specific flags.
- A compatibility fixture compares standard output and status exactly. It
  compares only the presence of error output because Kosh formats and locates
  diagnostics independently. A fixture whose error output must agree byte for
  byte carries `# compat-stderr: exact` on a line of its own.

## Workflow

- Read the applicable repository guidance before planning, editing, reviewing,
  writing prose, tests, or commits. Check [MISTAKES.md](MISTAKES.md) before
  repeating a failed action. Preserve unrelated working-tree changes.
- Resolve files with fd or rg --files before naming optional paths. Verify
  unfamiliar tool options, make recipes, interpreters, executables, and
  environment paths in the context where they run. Do not infer a filename
  from a type or a previous run.
- Put every rg option before its pattern and resolved paths. Use one fd
  pattern per invocation; select glob mode for wildcard patterns. Keep
  searches bounded. Run independent reads as separate commands or parallel
  tool calls, never joined with shell separators. A no-match result is
  evidence only after filters and command status are checked.
- Resolve redirected logs from the command runner's working directory.
  A nested make -C changes Make's directory, not the parent shell's
  redirection path. Keep result and artifact paths tied to their runner.
- Use the verified host shell for host probes, timing, signals, and compound
  commands. Pass source through -c, use verified absolute executable paths
  when command search can differ, and inspect nested quoting before running.
  Bound live commands and publish a process identity before signaling it.
- Edit with apply_patch. Use narrow changes and inspect the diff after
  formatting. Do not use Python, here-documents, sed -i, or awk rewrites.
  Print the required before-and-after table immediately after each edit batch,
  before any other tool call.
- Search owners and all callers before changing an interface or deleting a
  shared symbol. Inspect exact types, aggregate layouts, ownership, platform
  boundaries, and every active build configuration. Review both ends of a
  framed format together.
- Treat a workaround for one specific case as a design warning. Ask whether
  the whole construction can be simplified and whether the assumption that
  requires the workaround is wrong. Verify those answers before keeping it.
- For shell compatibility, measure the exact construct in both shells first.
  Verify mood, options, input channel, output, and status. Keep fixture
  operands stable across both commands.

## Build and validation

- Use root make for builds and root make test for the full suite. Inspect a
  focused target's recipe, inputs, runner, golden, and cleanup owner before
  invoking it. Pass matching MODE, BIN, and TARGET to direct test targets.
- Run test owners that share result or artifact paths sequentially, including
  debug and release modes. Keep the CLI harness separate from other full
  suites. Do not load the machine while timed interactive fixtures run.
- Redirect a build or suite to a resolved log, capture its final status, then
  read the log. A full suite passes only after all shards finish and no
  failure artifact remains. Check that capability-gated tests took the active
  branch.
- Use focused regression coverage for behavior changes. Inspect changed
  golden lines and exact streams and statuses. Native fixtures with status
  126 or 127 need a manually verified golden instead of refill.
- Check the fixture runner's interpreter before shell-specific syntax.
  Host-dependent counts belong in shape assertions, not goldens. Run
  containers without a terminal and with the workspace user and group when
  they write through a bind mount. Verify packages and binary dependencies
  inside the target container.
- Use MODE=rel for performance comparisons. Verify the timing tool and
  workload inside the exact environment, use isolated bounded inputs, and
  check status before comparing numbers. Start GDB with -nx if startup
  configuration can stop at main; confirm the actual stop signal.

## Finish

- Keep MISTAKES.md concise: record a distinct cause and reusable fix once;
  fold recurrences into that entry. Add a general rule here only when the
  current rules do not cover the cause.
- Inspect the full diff, git diff --check, untracked files, and any changed
  goldens. Confirm README.md is untouched unless its edit was approved.
- Format and validate meaningful changes, then commit locally. Verify the
  active git identity before posting through a CLI or committing. Stage only
  intended paths, inspect staged name-status including rename deletions, and
  pass commit options before pathspecs. Keep commit body lines within 72
  columns. Never push or create external artifacts without a request.
