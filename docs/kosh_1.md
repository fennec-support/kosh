# Koshka shell reference

> Declarative and operational specification for the Koshka command shell, static analyzer, formatter, language server, interactive line editor, and bundled Koshkit utilities across Linux, macOS, and Windows.

2026-10-26

Prerequisites - Familiarity with POSIX shell fundamentals and [kosh(5) configuration settings](kosh_5.md).

`kosh` is the Koshka command shell. The binary integrates a POSIX and Bash-compatible command interpreter, a pre-execution static analyzer, a source code formatter, a Language Server Protocol (LSP) daemon, and the `koshkit` suite of bundled POSIX and system utilities. The same grammar is available on Linux, macOS, and Windows, and platform differences are listed under [Windows platform semantics](#windows).

## Core system primitives

| Primitive | Functional role |
| --- | --- |
| `kosh` | Primary shell interpreter, static analyzer (`--lint`), formatter (`--format`), and language server (`--as-language-server`). |
| `mood` | Execution profile (`kosh`, `bash`, `bash-posix`, or `sh`) governing parser dialect, static analysis enforcement, option locks, and error termination. |
| `koshconf` | Builtin configuration manager that reads, mutates, and persists shell settings and `KOSHCONF` payloads. |
| `koshkit` | Builtin and standalone dispatcher for 108 bundled POSIX, system inspection (`evil*`), and diagnostic (`good*`) utilities. |
| `slurp` | Builtin file editor that runs the line editor with the Emacs, Vi insert, or Vi normal key set. |

## Command execution lifecycle

1. **Invocation and input selection** - The `kosh` binary resolves `KOSH_FLAGS` and command-line flags, sets the initial `mood` from `--mood` or the invocation basename (`sh`, `dash`, `bash`, `rbash`), and selects the input source in strict priority order (`-s`, `-c`, script file operand, `-i` or terminal stdin, or non-interactive stdin).
2. **Configuration and startup loading** - Unless `-Q`, rescue mode, `--lint`, `--format`, `--as-language-server`, a secure (setuid or setgid) execution, or a `koshkit` or utility invocation skips them, the shell loads `/etc/kosh.conf` (or `%ProgramData%\kosh\kosh.conf`), `$XDG_CONFIG_HOME/kosh/kosh.conf` (or `~/.config/kosh/kosh.conf`), and `KOSHCONF`, then sources the startup files for the active `init_moods` or session mood. `--no-config` and privileged mode (`-p`) skip only parts of this sequence, as listed under [Startup bypass](#skipping-startup-files).
3. **Lexing, parsing, and static analysis** - The parser builds an abstract syntax tree (AST) and reports all syntax or dialect errors in a single pass. In the default `kosh` mood, the static analyzer inspects the AST before execution and rejects scripts with `strict` or `lenient` errors (exit status `1`). In the compatibility moods, analysis runs only when `-W` or `set -W` enables it, and its findings are warnings that do not stop the script.
4. **Word expansion** - For each simple command, words expand in fixed order: brace expansion, tilde expansion, parameter/arithmetic/command/process expansion (left to right), field splitting (`IFS`), pathname globbing, and quote removal.
5. **Command resolution and dispatch** - Commands containing a slash execute directly. Words without a slash resolve in strict order: shell functions, enabled builtins, hashed `PATH` entries, `PATH` search, and bundled `koshkit` utilities when the mood, `--enable-koshkit`, or `enable` makes them reachable. Interactive shells with `autocd` enabled fall back to changing directories if the word matches a directory path.

## Install and verify Koshka releases <a id="downloads"></a>

Download the release binary matching the target operating system and CPU architecture, replacing `X.Y.Z` with the release version string.

| Target platform | Release binary artifact |
| --- | --- |
| Linux on x86-64 | `kosh-linux-amd64-X.Y.Z` |
| Linux on ARM64 | `kosh-linux-aarch64-X.Y.Z` |
| macOS on Apple silicon | `kosh-darwin-aarch64-X.Y.Z` |
| Windows on x86-64 | `kosh-win32-amd64-X.Y.Z.exe` |

* **Release bundle contents** - Releases include `SHA256SUMS`, compressed manual pages (`kosh.1.zst` and `kosh.5.zst`), and the Bash completion script (`kosh.bash`). Verify the binary checksum against `SHA256SUMS` and run `kosh --version` to confirm the installed build.
* **Package manager and editor integrations** - On macOS, upgrade existing Homebrew installations via `brew update && brew upgrade kosh`. The VS Code and Zed extensions automatically download `kosh` when absent from `PATH`. The Neovim plugin is distributed as `kosh.nvim`.

## Execute common workflows <a id="getting-started"></a>

Launch an interactive session, execute an inline command string, or run a script file with positional arguments.

```sh
kosh
kosh -c 'printf "%s\n" hello'
kosh script.kosh argument
```

Run static analysis without execution, or format a script to standard output. Append `--apply` to write safe lint fixes or formatted code directly to the file in place.

```sh
kosh --lint script.kosh
kosh --format script.kosh
kosh --lint --format --apply script.kosh
```

## Command-line synopsis <a id="synopsis"></a>

```text
kosh [options] [script [argument ...]]
kosh -c command [argument ...]
kosh -s [argument ...]
kosh --lint [-c command ...] [file ...]
kosh --format [file]
kosh --lint --format [file]
kosh [--lint] [--format] --apply file ...
kosh --as-language-server
```

## Koshka architecture and core features <a id="koshka-features"></a>

### Execution moods and option invariants <a id="moods"></a>

Koshka partitions shell behavior into four moods. In this manual, `bash` and `bash-posix` are referred to as the **Bash moods**, `bash-posix` and `sh` (where `posix` is on) are the **POSIX moods**, and `bash`, `bash-posix`, and `sh` are the **compatibility moods**.

| Mood | Dialect and baseline behavior | Option mutability and transitions |
| --- | --- | --- |
| `kosh` (default) | Enables pre-run static analysis, resolves bundled `koshkit` utilities after `PATH`, and locks `nounset`, `pipefail`, `failglob`, and `extended arithmetic` to `on` and `nullglob` to `off`. | `set`, `shopt`, and `koshconf` reject mutations to locked options with a hint to run `koshconf set mood bash`, but accept redundant no-op assignments so `set -euo pipefail` succeeds. All other options are toggleable. |
| `bash` | Enables Bash extensions with static analysis disabled by default. | All Bash options are toggleable. Explicit changes to `set -u`, `set -o pipefail`, `shopt -s failglob`, or `interpreter.arithmetic_uses_big_numbers` persist across mood switches between compatibility moods. Running `set -o posix` switches to `bash-posix`. |
| `bash-posix` | Enables Bash syntax with the `posix` option enabled and sources Bash startup files. | Running `set +o posix` switches to `bash`. |
| `sh` | Emulates `dash` grammar and semantics with `posix` enabled. | Running `set +o posix` switches to `bash`. |

#### Default option states across moods

Every option not listed in the table below shares an identical default across all four moods. Mood differences in syntax, expansion, builtins, and error handling are specified under [Shell grammar](#shell-grammar), [Expansions](#expansions), [Builtins](#builtins), and [Shell errors](#shell-errors).

| Option | `kosh` | `bash` | `bash-posix`, `sh` |
| --- | --- | --- | --- |
| `expand_aliases` | `on` | `off` in scripts, `on` in interactive shells | `on` |
| `extglob` | `on` | `off` | `off` |
| `failglob` | `on` (locked) | `off` | `off` |
| `nounset` | `on` (locked) | `off` | `off` |
| `pipefail` | `on` (locked) | `off` | `off` |
| `extended arithmetic` | `on` (locked) | `off` | `off` |
| `posix` | `off` | `off` | `on` |

### Options and settings reference <a id="options"></a> <a id="shell-settings"></a>

Command-line flags override both settings files and `KOSHCONF`. Runtime settings are read and mutated through `set`, `shopt`, and `koshconf`. See the [kosh(5) configuration manual](kosh_5.md#command-line-options) for the exhaustive list of command-line flags, option IDs, namespaces, aliases, and mutability restrictions.

### Static analysis and runtime diagnostics <a id="diagnostics"></a>

All diagnostics write to standard error in an identical format across every mood (with colored labels and codes on color-capable terminals).

* **Diagnostic layout** - Each entry prints `file:line:column: severity: message`, with the check code in parentheses on an analysis finding (`(SC2086)`), followed by the source line, a caret (`^`) under the offending column, and optional `note:` remediation lines. Dynamic runtime sources prefix locations as `eval:line:col:`, `trap:line:col:`, or `-c:line:col:`, while stdin and interactive lines omit the source label.
* **Stack backtraces** - Each active function call, sourced file, `eval`, trap action, subshell, and command/function/process substitution between the top level and the failing command appends a `trace:` frame. Consecutive identical frames collapse into a single frame with a repeat count. When multiple `-c` strings or files are processed, the top-level command-line frame prints once after all diagnostics for that input.
* **Multi-error syntax reporting** - Linting, the language server, and script execution report all syntax errors in a single pass. Dialect violations (such as `[[` in the `sh` mood) are reported while still parsing the inner contents for additional syntax errors.
* **Pre-run static analysis tiers** - Analysis categorizes checks into `strict`, `lenient`, and `annoying` tiers (configured via `-W` and listed via `--list-diagnostics`).
  * In the `kosh` mood, analysis runs automatically before execution; `strict` and `lenient` findings abort the script with exit status `1` before any command executes.
  * In compatibility moods, analysis runs only when enabled by `-W` or `set -W`, and all findings emit as non-fatal warnings. When the warning level is nonzero, script operands lacking a shebang are flagged.
  * Interactive shells analyze each submitted line prior to execution.
* **Core static checks** - Analysis detects variables expanded but never assigned, functions referencing positional arguments that are invoked without arguments, functions invoked prior to their definition, and non-`local` variable assignments inside functions (in the `sh` mood, which lacks `local`, only variables first assigned inside a function and referenced outside it are flagged).
* **Scope and traversal** - The analyzer follows static `source` operands into readable files, evaluates command/backtick/process substitutions as subshells, and evaluates `${ ...; }` function substitutions in the current shell environment. When multiple findings occur, a summary line (`Encountered M warning and N errors.`) appends to the output.
* **Inline suppression directives** - Comments matching `# shellcheck disable=SC2086,unquoted-command-substitution` suppress static analysis findings. Numeric codes (`SC2086`) suppress all variants under that ID, slug names suppress only that exact variant, `all` suppresses all checks, and ranges (`SC2000-SC2100`) suppress numeric spans. A directive placed before the first command applies to the entire file; placed later, it applies to the immediately following and-or list. Directives never suppress syntax or runtime errors.
* **Path and internal fault reporting** - Missing commands or file paths report the first non-existent path component when identifiable. Internal interpreter faults emit an `error:` line and `note:` lines with process and build metadata before aborting.

### Offline linting and host extraction (`--lint`) <a id="linting"></a>

`kosh --lint` performs static analysis without executing code, enabling all three diagnostic tiers (`strict`, `lenient`, `annoying`) at the severity rules of the active mood regardless of `-W`, `--no-diagnostics`, or `--no-annoying-diagnostics`.

* **Input order** - Analyzes `-c` command strings in order, then file operands, or standard input if neither is provided. Startup and settings files are skipped. Diagnostics write to stderr and stdout remains empty.
* **Polyglot host file extraction** - Extracts and analyzes embedded `sh`, `bash`, and `kosh` scripts from CI workflows, Ansible playbooks, Dockerfiles, Docker Compose files, Markdown documents, Makefiles, justfiles, and Kubernetes manifests. Files are identified by extension or shebang; unrecognized file formats fail with status `1` and print the supported format list.
  * Host interpreter declarations (`shell:` step keys, `SHELL` instructions, shebangs, or executable flags) select the analysis mood; fragments targeting non-shell interpreters are skipped.
  * `exec` arrays extract only the script operand of a shell `-c` invocation.
  * `justfile` recipe lines (joined across `\` continuations) are analyzed as individual commands, while shebang or `[script]` recipes are analyzed as single scripts.
  * Jinja shell templates (`*.sh.j2`) and host template interpolations/editor variables are masked as plain words. Unresolved command checks are suppressed in a fragment that contains a host interpolation. Host-injected variables (CI variables, exported recipe arguments, Dockerfile `ARG`/`ENV`) are pre-marked as assigned. All diagnostic line and column numbers map back to the outer host file.
* **Automated remediation (`--apply`)** - Writes safe structured AST fixes to file operands, prints a fix summary, and outputs only remaining unresolvable diagnostics. If a transformation would produce invalid syntax, the file is left unmodified. Exits with status `0` when only warnings remain, and status `1` on remaining errors or I/O failures.

### Source code formatting (`--format`) <a id="formatting"></a>

`kosh --format` normalizes shell source code without executing it.

* **Formatting invariants** - Places structural keywords and compound braces on dedicated lines, indents nested blocks by `2` spaces, rewrites unshadowed `test` invocations into `[ ... ]` bracket commands, wraps lines at `80` columns at safe token boundaries, and normalizes line endings to `LF`.
* **Input and host behavior** - With no file operand or `-`, reads stdin and writes to stdout (with syntax coloring when stdout is a terminal). In polyglot host files, formats only mutable shell regions (such as Markdown fenced code blocks). Syntax errors emit diagnostics and exit with status `1`. When combined with `--lint`, safe lint fixes apply before formatting.
* **Atomic in-place updates (`--apply`)** - Writes formatted output to a temporary file alongside each target and atomically renames it over the original, preserving file permissions and symbolic links. Rejects non-regular files and files modified externally during processing.

### Language Server Protocol daemon (`--as-language-server`) <a id="language-server"></a>

`kosh --as-language-server` serves the LSP specification over standard input and output.

* **Capabilities** - Publishes live diagnostics on document change, tab completion (triggered by space or `-`), quick fixes, a `fix-all` code action for non-conflicting safe fixes, document formatting, go-to-definition, hover documentation, semantic token highlighting, document symbols/outline, and rename within the open document.
* **Polyglot and cross-file resolution** - Operates inside embedded shell regions of all host formats supported by `--lint` and follows relative `source` paths from the document directory. Rename updates variables, functions, and aliases across the open document while preserving `$name` vs `${name}` bracing; commands can be renamed only when defined as a function or alias within the open document.
* **Document mood resolution** - Resolves mood in priority order: document shebang, LSP language ID (`kosh`, `bash`, `sh`, `bash-posix`, where `rbash` and `shellscript` map to `bash`), file extension (`.kosh`, `.bash`, `.sh`, `.dash`), or fallback `kosh`. Skips startup/settings files and exits with status `0` on clean LSP shutdown or `1` otherwise.

### Startup and configuration file loading <a id="startup-files"></a>

Every shell instance loads declarative settings before sourcing startup scripts:
1. System settings file: `/etc/kosh.conf` (POSIX) or `%ProgramData%\kosh\kosh.conf` (Windows).
2. User settings file: `$XDG_CONFIG_HOME/kosh/kosh.conf` or `~/.config/kosh/kosh.conf` (skipped when the POSIX effective user is `root`).
3. Environment payload: `KOSHCONF` (unset from the environment immediately after ingestion).

In the `kosh` mood, login and interactive shells source `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh` (or `~/.config/kosh/scripts/rc.kosh`); no `/etc/koshrc` or `~/.koshrc` exists, and the POSIX `root` user skips user `rc.kosh`. In interactive non-login `kosh` shells, `--norc`, `--rcfile`, and `-p` (`--privileged`) govern `rc.kosh` identically to `~/.bashrc`. Any Bash startup file sourced from `rc.kosh` automatically executes under the `bash` mood and returns to `kosh` upon completion (see [`source`](#builtin-source)). Passing `-L` or setting `init_moods` replaces the list of moods whose startup files run (for example, `-L bash` makes a `kosh` session source only the Bash startup files in the `bash` mood and then return to `kosh`, while `-L bash,kosh` also sources `rc.kosh` afterwards). A mood set in a settings file selects the startup files only when no `--mood`, `--dumb`, `--posix`, invocation basename, or script shebang selects one (see [kosh(5)](kosh_5.md#compatibility-option-details) for the precedence), and command-line options keep their values over settings files. Compatibility moods use the files described under [Bash and POSIX startup files](#bash-and-posix-startup-files).

#### Startup bypass and post-startup enforcement (`skipping-startup-files`) <a id="skipping-startup-files"></a>

| Execution context | Files skipped and enforcement rules |
| --- | --- |
| `-Q` (`--no-init-files`), rescue mode, `--lint`, `--format`, `--as-language-server`, setuid/setgid (`EUID != UID` or `EGID != GID`), standalone `koshkit` / utility symlink | Skips all settings files, `KOSHCONF`, and all startup scripts. |
| `--no-config` | Skips settings files and `KOSHCONF`; startup scripts still run. |
| `-p` (`--privileged`) | Skips settings files, `KOSHCONF`, `ENV`, `BASH_ENV`, and interactive non-login `rc.kosh`. |
| Subshells | Inherit settings directly from the parent shell without re-reading files. |
| Startup execution window | Restricted mode (`-r`), `nounset`, `pipefail`, and pre-run static analysis activate only *after* startup files finish, though `set`, `shopt`, and `koshconf` inside startup files immediately reject option values locked by the target mood. Missing default startup files are silently skipped; unreadable custom `--rcfile` or `BASH_ENV` targets emit an error. |

### Interactive line editing <a id="line-editing"></a>

The interactive line editor defaults to `emacs` mode (`set -o vi` and `set -o emacs` switch modes).

* **Viewport and rendering** - Multi-line buffers taller than the terminal viewport scroll around the cursor row, reserving `2` bottom rows for hints and hiding ghost text and the right prompt. Control bytes render in caret notation (`^[` for `ESC`), filenames containing control bytes complete in `$'...'` syntax, and bracketed paste inserts text literally without executing newlines. When the cursor rests on or immediately after an unquoted, unescaped bracket `( ) [ ] { }` outside comments and here-documents, its matching partner highlights.
* **Kill ring, undo, and external editing** - Kill operations push erased text onto a 16-entry session kill ring, merging consecutive kills into a single entry. `Ctrl-K` on an empty line joins the next line, or deletes an empty final line. Undo reverts a yank or `Alt-.` insertion and all its cycled replacements in one step. `Ctrl-X Ctrl-E` (or `:e` in Vi mode) opens the current buffer in `$VISUAL`, `$EDITOR`, or `vi`; exiting the external editor with status `0` replaces the buffer without executing it, while nonzero exits preserve the original buffer and print a message.
* **Extended key reporting and undo/redo bindings** - While collecting input, `Ctrl-Z` undoes edits (during command execution, `Ctrl-Z` suspends the foreground job). `Ctrl-Shift-Z` redoes edits when reported distinctly via the kitty keyboard protocol or xterm `modifyOtherKeys` (forwarded by `tmux` when `extended-keys` is `on`). Disable protocol requests via `koshconf set editor.request_extended_key_reports off`. On the Windows console, both `Ctrl-Z` and `Ctrl-Shift-Z` undo, and `Ctrl-^` redoes.
* **Prefix history recall and auto-pairing** - With `editor.history.arrow_keys_search_by_typed_prefix=on` (default), pressing `Up` on a non-empty line recalls only history entries matching the prefix preceding the first `Up` press, while an empty line steps through all history. Setting `editor.auto_close_brackets_and_quotes=on` automatically inserts matching closers for `(`, `[`, `{`, `'`, and `"`, steps over typed closers, and deletes empty pairs on `Backspace`.

#### Live synopsis hints and pre-run diagnostics <a id="hints"></a>

When the cursor advances past the command word, up to `16` hint rows appear below the input line (hidden while completion menus or searches are active). A keystroke never runs the command itself, and a Tab that needs an uncached documentation load shows `loading...` until the load finishes.
* **Command synopsis (`editor.hints.show_command_synopsis=on`)** - Displays command, subcommand, and flag documentation under `synopsis`, marking the active argument with `>`. Sources data from builtin/`koshkit` help, alias definitions, function source locations, section 1 man pages (`man` / `manpath`), subcommand man pages (`git-commit(1)`), or `--help` output for allowlisted binaries. External `man` and allowlisted binaries execute only from absolute directories owned by the user or `root` and not writable by group or others (never from empty or relative `PATH` entries). Pending multi-key prefixes (`Ctrl-X` or Vi operators) display valid completion keys. Disabling the option also stops its documentation loads.
* **Live diagnostics (`editor.hints.show_live_diagnostics=on`)** - Displays the first static analysis finding (`error`, `warning`, `note`) or parse error (`syntax error`) after a brief pause without opening files, following `source` targets, or looking up commands. Because pressing `Enter` submits the entire buffer without a `PS2` continuation prompt, incomplete constructs report immediately as syntax errors. Disabling both hint options also hides pending-key hints.

#### Tab completion and candidate selectors <a id="completion"></a>

* **Ghost text** - Suggests inline prefix matches from completion and history; `Right` or `End` at the end of the buffer accepts the full suggestion, and `Ctrl-Right` or `Alt-F` accepts the next word.
* **Context-sensitive candidate generation** - `Tab` computes exact-prefix, smart-case, and subsequence matches:
  * Empty command word: shell keywords, builtins, functions, aliases, bundled utilities, and `PATH` executables.
  * Arguments: file paths (directories append `/`), `$var` / `${var}` variables, `~user` accounts, and `-flag` options.
  * Specialized commands: programmable `complete` specs take top priority; `cd` and `pushd` complete directories across `CDPATH` (unless starting with `/`, `.`, or `~`); `make` extracts targets statically from `Makefile`; `cmake --build <dir> --target` extracts targets from `<dir>/CMakeFiles/TargetDirectories.txt` without executing `make`, `cmake`, or `$(shell ...)`.
* **Insertion and selector modes (`--tab-selector` / `completion.menu_style`)** - Pressing `Tab` inserts the longest common prefix. A single match replaces the token and appends a space according to `completion.add_space_after_completed_word` (`off` default, `on`, or `on-excluding-trailing-slash`). Multiple candidates open the configured selector:

| Selector mode | Completion and `Ctrl-R` history search behavior |
| --- | --- |
| `interactive` (default) | Renders an in-editor menu under `selecting completions` or `incremental history search`. Typing filters candidates (commands, paths, and `koshconf` options retain the highest non-empty tier among exact-prefix, smart-case-prefix, and subsequence matches; other lists use case-insensitive prefix matching; history matches prefix, substring, or ordered subsequence bytes). `Backspace` widens results; typing space, `/`, or deleting past token start refreshes candidates; accepting a directory opens its contents. |
| `external` | Pipes multiple candidates into `$KOSH_FZF_COMPLETION_COMMAND` (`fzf` default) and replaces the token with stdout. A single candidate or a session without a controlling terminal uses the `plain` listing. An empty or missing selector command, or an exit status other than `0`, `1`, or `130`, uses the `plain` listing after a message. Status `1` or `130` dismisses the selector and leaves the line unchanged. |
| `plain` | Prints candidates in a multi-column list (always used on terminals lacking decoration support) and keeps `Ctrl-R` history search inline. |

* **History search (`Ctrl-R`)** - Searches a snapshot of the shared history file newest-first through the selected selector with the current line as the initial query, without mutating the in-memory private history list. An accepted entry replaces the line.

#### Terminal shell integration marks <a id="terminal-integration"></a>

Interactive sessions attached to a terminal emit `OSC 133` (`A`, `B`, `C`, `D`) prompt/command/status marks, `OSC 7` working directory URIs, and `OSC 633` marks when `TERM_PROGRAM=vscode`. Emission is disabled when `TERM=dumb` or `KOSH_SHELL_INTEGRATION=0`.

#### Editor keybinding reference <a id="emacs-mode-keys"></a> <a id="vi-insert-mode-keys"></a> <a id="vi-command-mode-keys"></a> <a id="vi-visual-and-rectangle-keys"></a> <a id="vi-colon-commands"></a> <a id="emacs-m-x-commands"></a> <a id="multi-cursor-keys"></a> <a id="completion-menu-keys"></a> <a id="history-search-keys"></a>

##### Emacs mode and Vi insert mode keys

Vi insert mode supports all Emacs bindings below except `Ctrl-V`, `Alt-X`, and `Ctrl-X` chords; arrow keys in Vi insert mode recall history, and `Escape` transitions to Vi command mode (moving 1 character left). Unlisted `Shift` or `Alt` combinations with `Up`, `Down`, `Left`, `Right`, `Home`, or `End` behave as the unmodified key, and `Ctrl-Backspace` deletes 1 character left.

| Keybinding | Action |
| --- | --- |
| `Ctrl-A`, `Home` | Move to start of current line. |
| `Ctrl-E`, `End` | Move to end of current line, or accept full ghost suggestion at end of buffer. |
| `Ctrl-Home`, `Ctrl-Up`, `Ctrl-PageUp` | Move to start of buffer. |
| `Ctrl-End`, `Ctrl-Down` | Move to end of buffer. |
| `PageUp`, `PageDown` | Move cursor up or down by screen height. |
| `Ctrl-B`, `Left` | Move 1 character left. |
| `Right` | Move 1 character right, or accept full ghost suggestion at end of buffer. |
| `Alt-B`, `Ctrl-Left`, `Alt-Left` | Move to start of previous word. |
| `Alt-F`, `Ctrl-Right`, `Alt-Right` | Move past next word, or accept next word of ghost suggestion. |
| `Ctrl-P`, `Up` | Move up 1 row, or recall older history entry from top row (a typed or edited multi-line buffer with no backslash-joined line needs a second press). |
| `Ctrl-N`, `Down` | Move down 1 row, or recall newer history entry from bottom row. |
| `Alt-<`, `Alt-,`, `Alt->` | Recall oldest or newest history entry (or move to start/end of file in `slurp`). |
| `Enter`, `Ctrl-J`, `Ctrl-M`, `Shift-Enter` | Execute buffer, continue after trailing `\`, or execute on the second press for a typed or edited multi-line buffer with no backslash-joined line. |
| `Alt-Enter` | Insert literal line break. |
| `Tab`, `Shift-Tab`, `Ctrl-Space` (POSIX) | Trigger token completion. |
| `Backspace`, `Ctrl-H`, `Delete` | Delete character before (`Backspace`/`Ctrl-H`) or under (`Delete`) cursor. |
| `Ctrl-D` | Delete character under cursor, or exit shell on an empty buffer. |
| `Ctrl-W`, `Alt-Backspace`, `Alt-H` | Kill previous word to kill ring. |
| `Alt-D`, `Ctrl-Delete` | Kill from cursor to the end of the run of word characters, delimiters, or blanks under it. |
| `Ctrl-K`, `Ctrl-U` | Kill to end (`Ctrl-K`) or start (`Ctrl-U`) of line. |
| `Ctrl-Y`, `Alt-Y` | Yank newest kill (`Ctrl-Y`), or cycle to next older kill (`Alt-Y`). |
| `Ctrl-T`, `Alt-T` | Transpose characters (`Ctrl-T`) or words (`Alt-T`) around cursor. |
| `Alt-U`, `Alt-L`, `Alt-C` | Uppercase, lowercase, or capitalize from cursor to end of word. |
| `Alt-\` | Delete all spaces and tabs around cursor. |
| `Alt-.`, `Alt-_` | Insert last argument of older history entries on each press. |
| `Ctrl-_`, `Ctrl-Z`, `Ctrl-X Ctrl-U` | Undo last edit. |
| `Ctrl-^`, `Ctrl-Shift-Z` | Redo last undone edit. |
| `Ctrl-X Ctrl-E` | Edit buffer in `$VISUAL`, `$EDITOR`, or `vi`. |
| `Alt-X`, `Ctrl-X z` | Open `M-x` command minibuffer (`Alt-X`), or repeat last `M-x` command (`Ctrl-X z`). |
| `Ctrl-X Ctrl-C` | Exit shell (or `slurp`) with last command status. |
| `Ctrl-X s`, `Ctrl-X Ctrl-S` | Save file in `slurp`. |
| `Ctrl-S` | Incremental forward buffer search (smart-case, wraps around; `Ctrl-S`/`Ctrl-R` step to next/previous match, or reuse the last search on empty text, `Enter` commits cursor, `Escape` restores cursor). |
| `Ctrl-R`, `Ctrl-F` | Search command history (in `slurp`, `Ctrl-R` searches buffer backward). |
| `Ctrl-L` | Clear and redraw terminal screen. |
| `Ctrl-C` | Discard current input buffer. |
| `Ctrl-V` | Enter multi-cursor editing mode. |

##### Vi command, visual, and rectangle mode keys

In Vi command mode, arrow keys move across buffer rows without recalling history.

| Keybinding | Action |
| --- | --- |
| `1-9` then `0-9`, `"x` | Set numeric repeat count, or select alpha register `x` (`a-z`, `A-Z`) for delete/change/yank/paste. |
| `h`, `l`, `Left`, `Right`, `Backspace`, `Space`, `Ctrl-B`, `Ctrl-F` | Move cursor left or right. |
| `w`, `W`, `b`, `B`, `e`, `E` | Move to word or `blank`-delimited WORD start/end. |
| `0`, `^`, `$`, `Home`, `End`, `Ctrl-A`, `Ctrl-E` | Move to line start (`0`), first non-blank (`^`), or line end (`$`). |
| `fx`, `Fx`, `tx`, `Tx`, `;`, `,` | Find character `x` forward/backward on line, stop before `x`, or repeat find. |
| `k`, `j`, `Up`, `Down`, `Ctrl-P`, `Ctrl-N` | Move between rows (`k` and `j` recall history from top/bottom row). |
| `gg`, `G`, `{`, `}` | Move to first row (`gg`), last row or row `N` (`G`), or previous/next empty line (`{` / `}`). |
| `d`, `c`, `y` | Delete, change, or yank over motion (doubled `dd`/`cc`/`yy` or paired with vertical motions acts on full lines). |
| `gq`, `gqq` | Reflow lines over motion (`gq`) or current line (`gqq`) to `editor.wrap_width` (`80`), preserving first-line paragraph indentation. |
| `x`, `X`, `D`, `C`, `s`, `S`, `rx`, `~` | Delete char under/before cursor, delete/change to line end, change char/line, replace char with `x`, or toggle case and move right (`~`). |
| `p`, `P`, `o`, `O`, `J` | Paste after/before cursor, open line below/above, or join next line with 1 space. |
| `i`, `a`, `I`, `A`, `R`, `.` | Enter insert mode at cursor, after cursor, at first non-blank, at line end, in non-overwriting replace mode (`R`), or repeat the last change other than a yank (`.`). |
| `u`, `Ctrl-_`, `Ctrl-Z` / `Ctrl-R`, `Ctrl-^`, `Ctrl-Shift-Z` | Undo / Redo. |
| `/`, `?`, `n`, `N`, `:` | Search history (or buffer text in `slurp` with `n`/`N` repeats), or open `:` command prompt. |
| `v`, `V`, `Ctrl-V` | Enter character visual (`v`), line visual (`V`), or rectangular block (`Ctrl-V`) selection. Motions move the selection end (rectangle corner) and take no count. Visual modes accept `d`/`x` (delete), `c` (change), `y` (yank), `gq` (wrap, repeatable with `.`), and `:` (colon commands on the selected lines, shown as `:'<,'>`); rectangle mode accepts `d`/`x` and `I`/`c`/`C` (insert at the left column of every line when insert mode ends). `Escape`, `v` (visual modes), `Ctrl-V` (rectangle mode), and unrecognized keys leave the selection; `Enter`, `Ctrl-D`, and `Ctrl-C` leave it and behave as in Emacs mode. |
| `Enter`, `Ctrl-D`, `Ctrl-C`, `Ctrl-L` | Behave as in Emacs mode. |

##### Vi colon (`:`) commands, Emacs `M-x` commands, and modal sub-menus

| Mode / Menu | Supported commands and bindings |
| --- | --- |
| Vi `:` commands | `Enter` runs the command, and `Escape`, `Ctrl-C`, `Ctrl-D`, or `Backspace` on an empty line closes the prompt; unknown commands do nothing. `:q`, `:q!`, `:quit`, `:wq`, `:wq!`, `:wqa`, `:wqa!`, `:x` (exit with the status of the last command; in `slurp`, the `w` forms save first); `:w`, `:write` (save in `slurp`); `:N`, `:$` (jump to row `N` or end); `:s/old/new/[g]` and `:%s/old/new/[g]` (replaces the first match of basic regular expression `old` on the caret row, or every match with `g`; `:%s` acts on every row; any punctuation can delimit; `&` in `new` is the match and `\1`-`\9` its groups; `:&`, `:&&` (keeps `g`), and `:s` repeat the last substitution, and `:%&` and `:%&&` repeat it on every row); `:set option[=value\|?]`, `:set nooption` (mutate/query `koshconf` with `Tab` completion); `:save [option]` (writes the option, or every option, to `kosh.conf`); `:format`, `:%format` (format the caret row or the buffer as `--format` does; source that does not parse stays unchanged and its error is shown); `:e`, `:edit` (edit in an external editor); `:u`, `:undo`, `:red`, `:redo`; `:h`, `:help` (list the colon commands until the next key). |
| Emacs `M-x` commands | `Tab` completes a command, `Enter` runs it, and `Escape`, `Ctrl-C`, `Ctrl-D`, or `Backspace` on an empty minibuffer closes it. Hint rows list the matching commands, with a description for each when six or fewer match. Commands: `help` (list the commands), `replace-string`, `replace-regexp` (replace every match in the buffer; supports `\&` and `\1`-`\9`), `repeat` (as `Ctrl-X z`), `set-option`, `get-option`, `save-option` (as `:set` and `:save`), `format`, `format-buffer` (as `:format` and `:%format`), `fill-paragraph` (as `gq` on the paragraph), `isearch-forward`, `isearch-backward`, `kill-kosh` (as `Ctrl-X Ctrl-C`), `undo`, `undo-redo`, `beginning-of-line`, `end-of-line`, `forward-word`, `backward-word`, `scroll-down-command`, `scroll-up-command`, `beginning-of-history`, `end-of-history`, `delete-char`, `backward-delete-char`, `kill-word`, `backward-kill-word`, `kill-line`, `unix-line-discard`, `yank`, `yank-last-arg`, `transpose-chars`, `transpose-words`, `upcase-word`, `downcase-word`, `capitalize-word`, `delete-horizontal-space`, `clear-screen`. |
| Multi-cursor (`Ctrl-V`) | `Up`/`Down`/`Ctrl-P`/`Ctrl-N` add cursors vertically; `Left`/`Right`/`Home`/`End`/`Ctrl-B`/`Ctrl-F`/`Ctrl-A`/`Ctrl-E` move all cursors; typing and `Backspace` edit at all cursors; `Ctrl-G`, `Escape`, and unrecognized keys leave multi-cursor mode; `Enter`, `Ctrl-D`, and `Ctrl-C` leave it and behave as in Emacs mode. |
| Completion menu | `Down`/`Ctrl-N`/`Up`/`Ctrl-P`/`Shift-Tab` cycle rows; `Tab` accepts (opening directories); `Right` accepts and chains completion; `Enter` closes menu and runs command; typing or `Backspace` narrows or widens the rows; `Escape` closes the menu and keeps the edited line; `Ctrl-G` and unrecognized keys close the menu and restore the original line; any other key closes the menu, then acts on the line. |
| History search | In `interactive` mode: `Enter`/`Tab` accept without running; `Escape`, `Ctrl-G`, and unrecognized keys cancel and restore the line; other keys behave as in the completion menu. In `plain` mode: `Down`/`Ctrl-N`/`Ctrl-R` step to an older match; `Up`/`Ctrl-P`/`Ctrl-F`/`Shift-Tab` step to a newer match; typing or `Backspace` extends or shortens the query; `Enter`/`Tab` accept the match without running it; `Escape`, `Ctrl-G`, and unrecognized keys cancel and restore the line; `Ctrl-C`/`Ctrl-D` cancel, restore the line, and act on it; any other key accepts the match, then acts on the line. |

### Prompt rendering and escape sequences <a id="prompting"></a>

* **Prompt lifecycle** - Before reading interactive input, the shell executes `PROMPT_COMMAND` (a single command or an indexed array of commands executed sequentially, preserving the prior command's `$?` and `$_`) and prints `PS1` and the right prompt (`RPS1`, or `RPROMPT` if `RPS1` is unset/empty, defaulting to dim `\L \M` in an interactive shell started with neither set). The right prompt expands like `PS1`, drops blanks at either end, and is drawn on the first input row unless no room remains or it holds a control character other than a color sequence. The default `PS1` shows the Git branch with ahead/behind counts, user, host, shortened directory (`\P`), and status bullet (`\.`). After reading input and before execution, `PS0` prints. `PS2` defaults to `> ` (unused by the interactive editor, which submits full multi-line buffers on `Enter`), `PS3` prompts for `select` (`#? ` default), and `PS4` prefixes `xtrace` lines (`+ ` default, repeating its first byte per nesting level of substitution, `eval`, `source`, or trap).
* **Expansion and transient prompts** - `PS0`, `PS1`, `PS2`, and `RPS1`/`RPROMPT` process backslash escapes first, then undergo parameter, command, and arithmetic expansion (`PS4` prints literally). Setting non-empty `KOSH_TRANSIENT_PS1` or `KOSH_TRANSIENT_RPS1` replaces `PS1` and `RPS1` in scrollback upon `Enter` or `Ctrl-C` (an interactive shell started without `KOSH_TRANSIENT_RPS1` sets it to `\[\]`, which erases the right prompt after submission). For example, `KOSH_TRANSIENT_PS1='\$ '` leaves `$ ` before each command in the scrollback, and `KOSH_TRANSIENT_RPS1='\L'` keeps each duration. The terminal window title displays `user @ directory` while idle and the active command while executing, and output of `PROMPT_COMMAND` or `PS1` can replace the idle title.

| Escape | Read-only variable equivalent | Rendered output |
| --- | --- | --- |
| `\u`, `\h`, `\H` | `KOSH_PS1_USER`, `KOSH_PS1_HOST`, `KOSH_PS1_HOST_FULL` | Current username, short hostname (to first `.`), and full hostname. |
| `\k`, `\K` |  | `\h` and `\H`, colored on a color terminal. |
| `\w`, `\W`, `\P` | `KOSH_PS1_CWD`, `KOSH_PS1_CWD_BASE`, `KOSH_PS1_CWD_SHORT` | Working directory (`~` for `$HOME`), basename, and `\w` truncated to `...` plus final 21 bytes when exceeding 24 bytes. |
| `\g` | `KOSH_PS1_GIT_BRANCH` | Git branch or short detached `HEAD` hash (matches `KOSH_GIT_BRANCH`). |
| `\$`, `\?`, `\j` | `KOSH_PS1_PROMPT_SIGN`, `KOSH_PS1_STATUS`, `KOSH_PS1_JOBS` | `#` for user `root` or `$` otherwise, last exit status (colored on a color terminal), and job count. |
| `\L`, `\M` | `KOSH_PS1_DURATION`, `KOSH_PS1_MEMORY` | Last command duration (empty under `5 ms`) and peak resident memory of its process tree, or the growth of the shell during the command if larger (empty if the shell only reused memory). |
| `\.` | `KOSH_PS1_DOT` | Status bullet: white after `0`, bold red after `1`, shifting toward magenta across 4 steps for higher exit codes on truecolor terminals. |
| `\t`, `\T`, `\@`, `\A`, `\d`, `\D{fmt}` | `KOSH_PS1_TIME`, `KOSH_PS1_TIME_12`, `KOSH_PS1_TIME_AMPM`, `KOSH_PS1_TIME_SHORT`, `KOSH_PS1_DATE` | 24h `HH:MM:SS`, 12h `HH:MM:SS`, 12h `HH:MM AM/PM`, 24h `HH:MM`, `Weekday Month Day`, and custom `strftime` format (`HH:MM:SS` if `fmt` is empty). |
| `\s`, `\v`, `\V`, `\l` | `KOSH_PS1_SHELL`, `KOSH_PS1_VERSION`, `KOSH_PS1_VERSION_FULL`, `KOSH_PS1_TTY` | Invocation basename (stripped of leading `-` and `.exe`), `major.minor` version, full version, and last component of the terminal of standard input (`tty` if none). |
| `\!`, `\#` | `KOSH_PS1_HISTORY_NUMBER`, `KOSH_PS1_COMMAND_NUMBER` | History number of the next command (`1` in a non-interactive shell) and command number, counting from `1` for each top-level command read from a terminal, script, or standard input (`-c` strings are not counted). |
| `\n`, `\r`, `\e`, `\a`, `\\`, `\nnn`, `\[`, `\]` |  | Newline, CR, ESC, bell, literal `\`, octal byte `nnn` (one to three digits), and zero-width non-printing delimiters (raw `0x01`/`0x02` bytes act as `\[`/`\]` in `PS0`, `PS1`, `RPS1`, `KOSH_TRANSIENT_PS1`, and `KOSH_TRANSIENT_RPS1` only). |

* **Unknown escapes** - Any other backslash sequence prints as written.
* **Inserted text** - Text inserted by an escape is never expanded. The user, host, directory, branch, shell, version, and terminal escapes print a control byte in caret notation (`^X`) and a C1 control or invalid UTF-8 byte as `\xHH`.
* **`KOSH_PS1_*` variables** - Assignments are ignored.

### Command history persistence <a id="history"></a>

Interactive shells with `history` enabled (default) record each submitted buffer as a single line-escaped event (`\n` for newlines, `\\` for backslashes) in `$KOSH_HISTORY_FILE` (`~/.kosh_history` default, mode `0600`, directory-locked during writes).
* **Filtering and synchronization** - A non-interactive shell records no events and writes the history file only through `history -w` or `history -a`. Consecutive duplicate events and history expansion errors are not recorded, and a record over `65535` bytes is skipped when the file is read. Interactive shells load the history file at startup and append each submitted event immediately, without auto-importing events appended by concurrent shells (`Ctrl-R` and `history -S` read the shared file snapshot).
* **Size bounds** - `$KOSH_HISTORY_SIZE` (`4096` default; `0` disables history) caps both the in-memory list and the on-disk file, which is truncated to the newest `$KOSH_HISTORY_SIZE` events upon shell exit. Environment values of `KOSH_HISTORY_FILE` and `KOSH_HISTORY_SIZE` override `editor.history.file_path` and `editor.history.max_entries` from `kosh.conf`; neither variable is exported unless inherited exported or exported explicitly. Rewrites go through temporary files beside the history file. Legacy Bash history variables (`HISTFILE`, `HISTSIZE`, etc.) are ignored.

### Koshka environment and shell variables <a id="koshka-variables"></a>

#### Shell-maintained Koshka variables <a id="shell-maintained-variables"></a>

| Variable | Mutability and export | Value and behavior |
| --- | --- | --- |
| `KOSH` | Unexported | Invocation path of the binary. Every mood sets it, so a line guarded by `test -n "$KOSH"` runs only in Koshka. |
| `KOSH_VERSION`, `KOSH_COMMIT`, `KOSH_BUILD_MODE`, `KOSH_OS` | Unexported | Release version, Git commit hash, build profile, and build host description. |
| `KOSH_IDENTITY` | Read-only, exported | Lowercase hexadecimal CRC-32 checksum of the running binary. |
| `KOSH_ANSI_*` | Overwritable, unexported | Terminal escape sequences (`BLACK`, `RED`, `GREEN`, `YELLOW`, `BLUE`, `MAGENTA`, `CYAN`, `WHITE`, `BRIGHT_*`, `BOLD`, `DIM`, `RESET`); empty when color is disabled. |
| `KOSH_ANALYSIS` | Exported | Space-separated analysis state (`mimicry` for `-I`, `warnings=N` for `-W`, `no-annoying`, `no-diagnostics`; `no-annoying` is `optimizer.show_annoying_tier` off and `no-diagnostics` is `optimizer.analyze_before_running` off). Each `set` rewrites it, and the variable is removed when empty. A launched shell applies the inherited value, and an explicit `-W` replaces the inherited level. Command-line options do not set it. |
| `KOSH_GIT_BRANCH`, `KOSH_GIT_AHEAD`, `KOSH_GIT_BEHIND` | Overwritable | Active Git branch (or short detached commit hash) and upstream ahead/behind counts (empty when `0` or unavailable; the search stops at `GIT_CEILING_DIRECTORIES`). An assignment replaces the value. |
| `KOSH_PS1_*` | Read-only | Current text of each prompt escape (see [Prompt rendering](#prompting)). Assignments are ignored. |
| `KOSHCONF` | Read-only, unexported | Dynamic Base64-encoded binary snapshot of active mood and interactive settings (format in [Binary wire format](kosh_5.md#encoding-format)). Assignments are discarded, and the shell never exports it by itself. |

#### User-configurable Koshka variables <a id="configuration-variables"></a>

| Variable | Default value | Effect |
| --- | --- | --- |
| `KOSH_HISTORY_FILE`, `KOSH_HISTORY_SIZE` | `~/.kosh_history`, `4096` | Command history file path and maximum event count. |
| `KOSH_DIRECTORY_HISTORY` | `~/.kosh_directory_history` | Visited directory database ranked by the `z` builtin. |
| `KOSH_CALC_HISTORY` | `~/.kosh_calc_history` | Interactive expression history file for the `calc` utility. |
| `KOSH_FLAGS` | Unset | Default command-line flags parsed before argv (ignored in secure setuid/setgid execution; ignores `-c`, `-s`, `--format`, `--apply`, `--as-language-server`, `--rcfile`, `--init-file`, `--`, and operands). |
| `KOSH_FZF_COMPLETION_COMMAND`, `KOSH_FZF_COMPLETION_OPTS` | `fzf`, `--multi --layout=reverse --height=~40% --min-height=3` | External completion selector program and its options, split on blanks without expansion. |
| `KOSH_SHELL_INTEGRATION` | Enabled | Set to `0` to suppress `OSC 133`, `OSC 7`, and `OSC 633` terminal marks. Marks are written only to a terminal, `OSC 633` only when `TERM_PROGRAM=vscode`, and `TERM=dumb` disables all marks. |
| `KOSH_WELCOME`, `KOSH_FAREWELL` | Mood-dependent | Messages printed with the shell prefix at interactive startup and exit (`Welcome :3` in `kosh`, `Bash me harder!` in Bash moods, `POSIX me harder!` in `sh`, `Goodbye :c` on exit, followed by ` (Code N)` for a nonzero exit status). An interactive shell sets them only when the environment does not hold them. An exported empty value or a value of zero display width (`printf -v KOSH_WELCOME "\r"`) disables a message, and a startup file can set one (`KOSH_WELCOME=$(date)`). |

### Native Koshka builtins <a id="koshka-builtins"></a>

The six native Koshka builtins (`assimilate`, `bench`, `koshconf`, `koshkit`, `slurp`, `z`) are available in every mood.

| Builtin and synopsis | Operational specification and exit codes |
| --- | --- |
| `assimilate [options] [TARGET]` <a id="builtin-assimilate"></a> | Deploys the running `kosh` binary locally (when `TARGET` is omitted) or to a remote host over `scp` as `kosh` (`kosh.exe` for a local Windows install) in the first writable `PATH` directory, preferring `/usr/local/bin`, then `$HOME/.local/bin`, when on `PATH`, and excluding `sbin` paths unless `--install-directory=DIR` is passed. Locally, links each bundled utility and utility alias whose name is free or already links to `kosh` (relative symlink on POSIX, `.exe` hard link on Windows) and each `--link-mood=MOOD` name (`bash`, `dash`, `sh`, `kosh`; repeatable, comma-separated values). Utility names held by another entry in the directory or provided by a program elsewhere on `PATH` stay unlinked, with one warning per group; a conflicting `--link-mood` name rejects the directory (POSIX) or returns `1` (Windows). A later Windows run with a changed binary links again. Options: `--ssh-command=CMD` and `--scp-command=CMD` (replace `ssh` and `scp`; require `TARGET`, else status `2`), `-x` (`--trace` prints progress), and `-y` (`--yes` skips the confirmation, described under [Bundled Koshkit utilities](#bundled-utilities), that names the target and the full path of the new binary; a declined question warns and returns `1`). Validates the copied binary before atomic replacement and restores the previous file or link on failure. |
| `bench [options] command ...` <a id="builtin-bench"></a> | Benchmarks each `command` via `kosh -c` and reports mean, standard deviation, and range for wall time, peak RSS, and platform performance counters, and notes outlier samples after the report. Stops sampling on nonzero command exit unless `--ignore-exit-code` is set, and returns that exit status (`130` on interrupt). A missing command, an invalid or out-of-range count, or `-p` with `-n` returns `1`; an unknown flag returns `2`. Options: `--show-output`, `-N` (`--no-shell` direct exec), `-n` (`--runs` `1`..`100000`), `-t` (`--duration` ms, default `3000`), `-w` (`--warmup` `1`..`100000`; without argument runs 1-3 warmups until two agree within 10%; default 1 warmup), `-p` (`--profile`, alias for `--runs 1 --extended`), and `-E` (`--extended` CPU/wait time, context switches, faults, I/O, waiting time, and block and read/write call counts, plus Linux scheduler and hardware counters and traced syscalls/threads/execs/task counts from one extra untimed run, macOS processor counters, run-queue wait, wakeups, energy, and storage figures, and Windows Job Object process-tree counters). Traced rows are marked `(traced)`, a row marked `*` counts only the measured process, and a row the platform cannot report is omitted. |
| `koshconf <subcommand> ...` <a id="builtin-koshconf"></a> | Manages configuration settings:<br>- `create PRESET [--force]`: Writes the default file of the `kosh`, `bash`, or `sh` preset to user `kosh.conf` with every setting except the mood commented out (and default `rc.kosh` if missing); refuses to replace an existing file without `--force`.<br>- `set OPTION VALUE [--persist]`: Mutates session option, and with `--persist` also rewrites its line in user `kosh.conf` (saving the previous file as `kosh.conf.bak`).<br>- `get OPTION` / `list [-a\|--all]`: Prints single value or alphabetical `name=value` list (`-a` adds a line for each alias and for the `set` options `hashall` and `nolog`, which only `set` accepts).<br>- `load ENCODED` / `save [--persist]`: Applies or outputs the `KOSHCONF` Base64 payload, so a shell started with `KOSHCONF=$(koshconf save)` adopts the settings (`save --persist` writes each setting that differs from the system file, or from the mood default where the system file does not set it, to `kosh.conf`).<br>Booleans are `on` or `off`, also accepted as `true`, `false`, `1`, or `0`. Returns `0` on success, `1` on an unknown option or value or an unreadable or unwritable file, and `2` on invalid command syntax. An interactive shell reports the written file on standard error. Restricted shells reject `create`, `set`, `load`, and `save --persist`. |
| `koshkit [--color when] [utility [arg ...]]` <a id="builtin-koshkit"></a> | Invokes a bundled utility (`--color` takes `always`, `never`, or `auto`), or prints the categorized utility catalog when invoked without arguments (`--list` prints one utility per line; `--list-flags` prints utilities and flags). |
| `slurp [-e\|-v\|-V] [+LINE] FILE ...` <a id="builtin-slurp"></a> | Opens each `FILE` in turn in the full-screen alternate-buffer editor (`-e` Emacs mode, `-v` Vi insert mode, `-V` Vi normal mode on the first line; `+LINE` opens the next `FILE` at that line). Highlights shell scripts (recognized by shebang or extension) and writes files atomically, preserving final newlines, permissions, and symlinks. A missing `FILE` opens empty and is created on save. An empty operand, directory, special or unreadable file, file containing `NUL` or invalid UTF-8, or file over `16 MiB` is rejected, and the next `FILE` still opens. A restricted shell or non-terminal standard streams fail the run. The status is `1` on a rejection or failed run, `2` on usage errors (a missing `FILE`, a `+LINE` without a `FILE`, or more than one mode option), and `130` on `Ctrl-C`, which ends the run. The bundled `vi` and `emacs` run `slurp -V` and `slurp -e`. See the `slurp` bullets below. |
| `z [query ...]` <a id="builtin-z"></a> | Jumps to a directory recorded in `$KOSH_DIRECTORY_HISTORY`. A single operand that is an existing directory is used as it is; otherwise the operands form one case-insensitive substring query ranked by visit count and recency; no operand jumps to `$HOME`. Returns `1` when no match exists. |

* **`slurp` screen** - The top row repeats the prompt and the command, the file fills the rows below, and the last two rows show the file kind, line endings, size, and caret position, then the key set and the last save result or the keys of the key menu. A hint, message, or command line that needs more rows takes up to eight while it shows. Tabs and trailing blanks are drawn shaded. A shell script keeps syntax highlighting, and the format commands change only such a file.
* **`slurp` input** - `Enter` starts a new line, `Up` and `Down` do not recall history, and `Tab` inserts a tab at the start of a line or in a file that is not a shell script, and completes otherwise. The mouse moves the caret, selects by dragging, and scrolls with the wheel; with the Emacs key set, `Shift` with a movement key also selects. `:N` and `:$` move to line `N` and the last line, `Ctrl-S` and `Ctrl-R` (or `/` and `?`) find text as it is typed, and `help` in `M-x` and `:help` list the commands.
* **`slurp` saving and leaving** - `Ctrl-X s`, `Ctrl-X Ctrl-S`, `:w`, and `:write` save and keep editing; `:wq`, `:wq!`, `:wqa`, `:wqa!`, and `:x` save and close the file when the save succeeds; `:q`, `Ctrl-X Ctrl-C`, `M-x kill-kosh`, and `Ctrl-D` on an empty buffer close it without saving, and the next `FILE` opens.
  * While the buffer has unsaved changes, `:q` refuses and `:q!` discards; `Ctrl-C`, `Ctrl-X Ctrl-C`, `M-x kill-kosh`, and `Ctrl-D` need a second press.
  * A save refuses when another file has replaced the loaded one, and a failed save is reported while the edits stay open.
* **`slurp` write method** - Saves through a sibling temporary file renamed over the original. A file the user does not own, a file with several links, and a file in a directory that is owned by neither the user nor root or is group- or world-writable without the sticky bit are rewritten in place instead, keeping owner, group, and links, after a private backup in `$HOME` or `$TMPDIR`. The backup is removed after a successful write and reported after a failed one, and without a backup nothing is written.
* **`slurp` line endings** - A file keeps CRLF endings only when every line ends in CRLF; a file that mixes endings keeps each carriage return as text.

### Bundled Koshkit utilities <a id="bundled-utilities"></a> <a id="standard-utilities"></a>

Koshka bundles 108 utilities plus 4 aliases (`egrep` and `fgrep` for `grep`; `vi` and `emacs` for `slurp -V` and `slurp -e`).

* **Resolution and invocation** - In the `kosh` mood (or when `--enable-koshkit` / `interpreter.resolve_koshkit_applets_as_commands=on` is set), bundled utilities resolve directly as commands *after* `PATH` lookup. Invoking the binary via a symlink/hardlink named after a utility (case-sensitive, stripping `.exe`) or as `koshkit <utility>` executes the utility directly, bypassing shell startup files, settings files, and `KOSH_FLAGS`. The `koshkit` link that `make install` places beside `kosh` runs its first operand as a utility (`koshkit ls -l`) and otherwise acts as the `koshkit` builtin.
* **Option parsing and exit status invariants** - Every utility accepts `--help` (and `--version` when invoked under its own binary name). Flags may appear before or after operands (`--` ends option parsing), except for `calc`, `env`, `expr`, `flock`, `nice`, `nohup`, `retry`, `timeout`, `watch`, and `xargs`, which stop parsing options at the first positional operand. Utilities return `0` on success, `1` on operand/processing failure (continuing to remaining file operands), and `2` on usage errors (which terminates scripts in the `kosh` mood). An unknown utility returns `1`. Diagnostics are prefixed ``Koshkit `seq`:`` under the builtin and `seq:` under the utility's own name.
* **Confirmation prompts** - A confirmation question (`rm -i`, `cp -i`, `mv -i`, `assimilate`) goes to standard error after the shell prefix, followed by `[y/N]` or `[Y/n]` with the default capitalized. The answer is read from standard input up to a newline: an empty line takes the default, an answer beginning with `y` or `Y` confirms, and any other answer or end of input declines.
* **Catalog and color** - `koshkit` with no utility groups the catalog under POSIX and Koshka headings. Styled reports (`cal`, `cat`, `du`, `ls`, `watch`, `meow`, and the `evil` and `good` utilities) use color only when output is a terminal, `NO_COLOR` is empty or unset, and `TERM` is not `dumb`. `koshkit --color when` selects `always`, `never`, or `auto` for the utility that follows, and `diff` and `grep` also take `--color`. `tabs` and `tput` write escape sequences wherever their output goes.

#### Koshka-specific and system inspection utilities

| Utility | Operational specification and flags |
| --- | --- |
| `calc` <a id="utility-calc"></a> | Evaluates arbitrary-precision integer and finite decimal expressions from operands, `-i` (`--interactive` with `Ctrl-R` search of the calc history), or `-p` (`--pipe`, one expression per standard input line, no prompt). Builtin functions: `abs`, `atan`, `ceil`, `cmp`, `cos`, `exp`, `fact`, `fib`, `floor`, `frac`, `gcd`, `int`, `iseven`, `isint`, `isodd`, `length`, `lcm`, `ln`, `max`, `min`, `scale`, `sgn`, `sin`, `sqrt`. |
| `evil` <a id="utility-evil"></a> | Prints system identity, uptime, CPU, memory, swap, filesystem, process, and network summary (`-a` detailed, `-s` select identity fields, `-u` local users). Shared flags of `evilio`, `evilnet`, `evilps`, and `evilss`: `-l`/`--live[=N]` (`evilss` takes only `--live`) refreshes every `N` seconds (default `0.5`) on the alternate screen until `q` (status `0`) or an interrupt (status `130`), and a tall frame scrolls with `j`, `k`, the arrow and page keys, `g`, `G`, and the mouse wheel. `evilio`, `evilnet`, and `evilps` also take `-C`/`--cumulative[=M]` (change over `M` seconds, default `1`; a rolling `M`-second window with `--live`) and `--sort` (a metric or its shortest unambiguous prefix). |
| `evildisk` | Prints disk capacity, filesystem identity, and error/retry totals (`-a` adds labels, UUIDs, error counters, and SMART telemetry; accepts optional device/file filters). |
| `evilfiles` | Lists open file descriptors filtered by `-p` (PID), `-u` (owner), `-c` (command prefix), `-i` (sockets), `-t` (terse PIDs), `-w`/`--wide` (untruncated), and optional path operands. |
| `evilfs` | Prints mounted filesystems. |
| `evilio` | Prints memory, swap, and cumulative disk I/O counters (`-a` samples 1s CPU/paging/scheduler/PSI/disk/swap rates; `-h`/`--human-readable` binary units; per-process counters with `--ps` for every process, `-n COUNT`/`-NUMBER` for that many rows, `-p PID` for one process). Supports `-l`/`--live[=N]`, `-C`/`--cumulative[=M]`, and `--sort` (`pid`, `read`, `write`, `read-ops`, `write-ops`, `busy`, `read-latency`, `write-latency`, `average-queue`, `queue`, `errors`, `retries`). |
| `eviliso` | Inspects isolation primitives: namespaces (`-n`), cgroups (`-c`), login sessions (`-s`), remote sockets (`-r`), container runtimes (`-k`, `--container`), and Kubernetes pods (`--kubernetes`, reading `KUBERNETES_SERVICE_HOST`, service-account namespace, and `kubepods` cgroups), with `-a` for detailed rows. Without a selector it prints every section. |
| `evillogs` | Prints core dump settings, dump reports, and log directories, or only one part with `--cores` or `--logs`. |
| `evilnet` | Reports IPv4/IPv6 interface addresses (`-a` adds traffic, errors, drops, and TCP counters; `-f` TCP failure/loss only; `-t`/`--traffic` traffic table only; supports `-l`/`--live[=N]`, `-C`/`--cumulative[=M]`, and `--sort` by `name`, `rx`, `tx`, `rx-packets`, `tx-packets`, `rx-errors`, `tx-errors`, `rx-drops`, `tx-drops`). |
| `evilps` | Renders process hierarchy tree, limited to the PID operands when given (`-p` PID column, `-n` numeric sort, `-a` all fields, `-A` command lines, `-U` owners, `-c` CPU, `-M` memory, `-w`/`--wide` untruncated, `-NUMBER` row limit, `-l`/`--live[=N]` with `s` sort cycle and `/` search, `-C`/`--cumulative[=M]`, and `--sort` by `name`, `pid`, `cpu`, `memory`). |
| `evilss` | Lists sockets filtered by `-l` (listening), `-a` (all), `-t` (TCP), `-u` (UDP), `-x` (Unix), `-4`, `-6`, `-p` (owning process), `-n` (numeric addresses), `-H` (no header), and `--live`. |
| `goodcore` <a id="utility-goodcore"></a> | Captures live process core (`-p PID`) or packages an existing core and binary (`-b`), bundling `dump/core`, the executable and its shared libraries under `root/`, and `debug-gdb.sh` / `debug-lldb.sh` wrappers into `goodcore-<core>.tar.zst` (`.tar.gz` when zstd is missing, a plain tar with `--no-compress`, `-o PATH` output, `-q` quiet). |
| `goodfsw` <a id="utility-goodfsw"></a> | Reports path changes by scanning metadata at most once per latency period; on Linux a scan follows an `inotify` event and a quiet tree is rescanned every 30 periods, and elsewhere, or when a watch fails, a scan runs every period. A record holds the Unix scan time, numeric event mask, and path (`-m`, the default). Supports `-h` (human timestamps and event labels), `--timezone` (`local` or `utc`) and `--precision` (`0`..`9` fraction digits) for human timestamps, `-r` (recursive), `--event-flags` (append labels), `-x` (one filesystem), `-1` (single batch), `-l` (latency period, default `1` second), and `-e` (exclude substring). |
| `goodnode` <a id="utility-goodnode"></a> | Reports inode identity, allocation blocks, filesystem capabilities, verification state, and a CRC32C checksum for each path (`-i` locates an inode under `-r PATH`; `--verify` runs the native filesystem verification where supported). |
| `goodstat` <a id="utility-goodstat"></a> | Prints labeled file metadata (`-L` dereference symlinks, `-c` include CRC32C checksum, `-f` filesystem capacity report). |
| `meow` <a id="utility-meow"></a> | Renders an animated pixel-art cat screensaver (`-f`/`--fps` `1`..`240`, default `60`; `-s`/`--speed` `0.1`..`10`; `-n`/`--cats` `1`..`8`; `Space` pauses, `+`/`-` adjusts speed, and any other key exits). |
| `retry` <a id="utility-retry"></a> | Repeats a command until it succeeds or the attempts run out, using `-n` (max attempts), `-d` (initial delay), `-b` (backoff multiplier), `-m` (max delay), and `-q` (quiet). |
| `slurp`, `vi`, `emacs` <a id="utility-slurp-vi-emacs"></a> | Edits text files as the [`slurp`](#builtin-slurp) builtin does, with the same options and `+LINE` operand (`vi` implies `-V`; `emacs` implies `-e`). |

#### Standard POSIX and Unix utilities

| Utility | Supported flags and behavioral specification |
| --- | --- |
| `basename` <a id="utility-basename"></a> | Prints the final path component, minus an optional suffix (`-a`/`--multiple` treat every operand as a path, `-s`/`--suffix` remove a suffix from each and imply `-a`, `-z`/`--zero` end entries with NUL). |
| `bc` <a id="utility-bc"></a> | Evaluates expressions, assignments, functions, arrays, control flow, input and output bases, and scale, from the file operands and then standard input (`-l` adds the functions `s` sine, `c` cosine, `a` arctangent, `l` natural logarithm, `e` exponential and sets scale to `20`; `-q` suppress the banner). Division by zero stops the input with status `1`. |
| `cal` <a id="utility-cal"></a> | Prints a Gregorian month or year with weeks starting on Sunday (`-a`/`--today` start weeks on Monday and print the date in words below). |
| `cat` <a id="utility-cat"></a> | Concatenates files or standard input (`-n` number lines across files, `-b` number nonempty lines, `-s` squeeze empty runs, `-v` show control bytes in `^` notation and high bytes with `M-`, `-E` mark line ends with `$` and a CR before a newline as `^M`, `-T` show tabs as `^I`, `-e`/`-t`/`-A` act as `-vE`/`-vT`/`-vET`, `-u` accepted, `--syntax-highlighting` colors shell source on a terminal). |
| `chgrp`, `chown` <a id="utility-chgrp-and-chown"></a> | Changes the group (`chgrp`), or the owner and optional group (`chown`), of each file (`-R` recurse, `-H`/`-L`/`-P` select which links are followed, `-h` change a link itself, `-x`/`--one-file-system` keep recursion on one file system). |
| `chmod` <a id="utility-chmod"></a> | Applies symbolic or octal modes. `-R`/`--recursive` recurses without following links, and `--one-file-system` skips other file systems. |
| `cksum` <a id="utility-cksum"></a> | Prints POSIX CRC checksum, byte count, and filename. |
| `cmp` <a id="utility-cmp"></a> | Compares two files byte by byte (`0` equal, `1` different with the first differing byte and line printed, `2` unreadable file; `-l`/`--verbose` list every differing byte with both values in octal, `-s`/`--silent` print nothing). A file of `-` is standard input. |
| `comm` <a id="utility-comm"></a> | Merges two sorted files into columns of lines only in the first, only in the second, and in both (`-1`/`-2`/`-3` suppress them, `--check-order` stop at the first line out of order, `--nocheck-order` skip the check, the later one wins, `--output-delimiter` column separator with empty for NUL, `--total` append column counts, `-z` NUL line ends). A line out of order after an unpaired line is reported once per file with status `1`. |
| `cp` <a id="utility-cp"></a> | Copies files and trees (`-r`/`-R` recurse, `-f`, `-i`, `-p`, `-v`, `-L` follow every link, `-H` follow links on the command line only, `-P` follow none, `-x`/`--one-file-system` copy a directory on another file system as an empty directory). A recursive copy keeps links as links. A link leading back into a directory being copied is reported as cyclic and skipped with status `1`. |
| `csplit` <a id="utility-csplit"></a> | Splits a file into pieces `xx00`, `xx01`, and so on at each pattern, which is a line number or a `/regex/`, and prints the byte count of each piece (`-f`/`--prefix` replace `xx`, `-n`/`--digits` number of digits, `-s`/`--silent` suppress the counts, `-k`/`--keep-files` keep the pieces after an error). Returns `1` for a line number past the input or a regex that does not match. |
| `cut` <a id="utility-cut"></a> | Selects bytes (`-b`), characters (`-c`), or fields (`-f`) (`-d` delimiter, `-s` skip lines without one, `--complement` invert the selection, `--output-delimiter=STR` join selections with `STR`, `-z`/`--zero-terminated` NUL line ends, `-n` with `-b` leave out a multibyte character that the byte range splits). Returns `1` for an invalid list or a delimiter longer than one byte. |
| `date` <a id="utility-date"></a> | Prints the date and time, or formats them with a `+format` operand of strftime conversions (`-u`/`--utc` use UTC). Refuses an operand that would set the clock. |
| `df` <a id="utility-df"></a> | Reports mounted file systems, or those of the operands, in a right-aligned table (`-k` `1024`-byte blocks, `-P` POSIX header, `-h`/`--human-readable` powers of `1024`, `-H`/`--si` powers of `1000`). Unreadable file systems are reported before the table. |
| `diff` <a id="utility-diff"></a> | Compares two files line by line (`-u` unified output, `-w` ignore whitespace, `-a` force text, up to two `-L` set the unified labels, `--color[=WHEN]` with `auto` default, `always`, `never`; `0` equal, `1` different, `2` error). |
| `dirname` <a id="utility-dirname"></a> | Prints the directory part of each path. `-z`/`--zero` ends entries with NUL. |
| `du` <a id="utility-du"></a> | Prints every file and directory below each path, or the current directory, in descending size order without following links (`-s` totals only, `-h` human-readable sizes, `-x`/`--one-file-system` skip other file systems, `--tree` print a tree, `-T`/`--top-largest` print a tree of the largest entries and their ancestors). Permission errors are warnings and other read failures are errors; either makes the status `1`. |
| `env` <a id="utility-env"></a> | Runs a command with temporary assignments, or prints the environment (`-i` start empty, `-u VAR` remove a variable, repeatable). Finds the command through its `PATH`, or `/usr/bin:/bin` without one. |
| `expand`, `unexpand` <a id="utility-expand-and-unexpand"></a> | Converts tabs to spaces (`expand`) or blanks to tabs (`unexpand`) (`-t` ascending tab stops separated by commas or blanks, or one interval; a last value `/N` adds a stop at each multiple of `N`, and `+N` one every `N` columns, after the last explicit stop; default every eight columns; `unexpand -a` also converts blanks after the first nonblank, and `-t` implies it). |
| `expr` <a id="utility-expr"></a> | Evaluates an expression whose operators and operands are separate arguments and prints the result (`\|`, `&`, `=`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`, and `:`, which matches a basic regular expression anchored at the start and prints the first group or the match length). Returns `0` for a result that is neither `0` nor empty and `1` otherwise. An invalid expression, integer overflow, and division by zero are errors that return `2` in `sh` and `1` in Bash moods, and are fatal in `kosh`. |
| `file` <a id="utility-file"></a> | Classifies file kinds and contents (`-h`/`--no-dereference` classify a link, `-L`/`--dereference` its target, `-i`/`--regular-only` skip content tests, `-m`/`--magic-file` add a magic database, `-M`/`--magic-only` use only the given ones, `-d`/`--default-tests` select the default tests). The kinds include text, data, directory, the special files, ELF executable, common image formats, script text executable, and symbolic link to a target. Returns `1` for a file that cannot be opened. |
| `find` <a id="utility-find"></a> | Walks sorted trees (`-name`, `-iname`, `-type`, `-maxdepth`, `-mindepth`, `-print`, `-print0`, `-xdev`/`-mount` stay on one file system, `-exec COMMAND ... ;` run once per path with `{}` replaced, `-exec COMMAND {} +` run on batches of paths). |
| `flock` <a id="utility-flock"></a> | Runs a command while holding a lock on a file or directory, which the command inherits as a descriptor (`-n`/`--nonblock` fail with status `1` instead of waiting, `--transaction-held-lock` keep the lock after the launching process exits). |
| `fold` <a id="utility-fold"></a> | Wraps the lines of each file or standard input at `80` columns (`-w`/`--width` set the width, `-s`/`--spaces` break after the last blank when one fits, `-b`/`--bytes` count bytes). A tab advances to the next multiple of eight. |
| `fuser` <a id="utility-fuser"></a> | Prints the PIDs that use each file, with `-c`, `-f`, and `-u`. |
| `getconf` <a id="utility-getconf"></a> | Prints a system or string variable, or a path variable for a path (`-a`/`--all` print every variable, `-v`/`--specification` select a POSIX environment). Prints `undefined` for an unsupported variable. |
| `grep`, `egrep`, `fgrep` <a id="utility-grep-egrep-and-fgrep"></a> | Prints lines matching basic regular expressions, extended ones with `-E` (`egrep`), or fixed strings with `-F` (`fgrep`); `-E` and `-F` together return `2`. Patterns come from the first operand, `-e` (add a pattern), or `-f` (lines of a file); with `-e` or `-f` every operand is a file, and a newline separates patterns. Options: `-i` ignore case, `-v` invert, `-x` match whole lines, `-n` number lines, `-h` omit file prefixes, `-r` recurse (`--one-file-system` stays on one file system), `-c` count, `-l` list matching files (overrides `-c`), `-q` exit `0` at the first match, `-s` hide unreadable-file messages, `-a`/`--text` treat a file with a NUL byte as text, `--color[=WHEN]` (`auto` default, `always`, `never`). Returns `0` when a line is selected, `1` when none is, and `2` on error. In a file with a NUL byte and without `-a`, matches before the line of the first NUL print, the next match shows a single warning, and later matches print nothing. In a UTF-8 locale a dot and a bracket expression match one character, and one byte otherwise. See the `GREP_COLORS` bullet below the table. |
| `head`, `tail` <a id="utility-head-and-tail"></a> | Prints the first (`head`) or last (`tail`) ten lines (`-n` line count, `-c` byte count; both accept a suffix `b` for 512, `k`/`K`/`m`/`M`/`G`/`T`/`P`/`E`/`Z`/`Y`/`R`/`Q` for powers of 1024, and those letters followed by `B` for powers of 1000 or by `iB`, as in `1kB` and `1KiB`; a first argument such as `-5` means `-n 5`). For `head`, a leading dash prints all but the last N; for `tail`, a leading plus starts at line or byte N. Headers appear for several files (`-q`/`--quiet`/`--silent` suppress them, `-v`/`--verbose` forces them, the last wins); `-z`/`--zero-terminated` uses NUL line ends. `tail -f` keeps printing what is appended to each regular file; `-F` follows by name, reopens a replaced, removed, or new file, and implies `--retry`; `--follow=descriptor\|name` selects the mode; `-s`/`--sleep-interval` sets the longest wait between checks (`1` second); `--pid=PID` ends following after that process exits. An interrupted follow exits with status `130`. |
| `id` <a id="utility-id"></a> | Prints the user and group identities of the current process or of a user (`-u`/`--user` only the user, `-g`/`--group` only the group, `-G`/`--groups` every group, `-n`/`--name` print names, `-r`/`--real` use the real identities). Returns `1` for an unknown user. |
| `killall`, `pkill` <a id="utility-killall-and-pkill"></a> | Signals processes by exact name (`killall`) or by a pattern contained in the short command (`pkill`) (signal from `killall -s`/`--signal` or `pkill --signal`/`-SIGNAL`, `-l`/`--list` list signals, `pkill -s`/`--session` select comma-separated session IDs with `0` for its own and an optional pattern, `pkill --inverse` signal what the pattern and sessions do not select). Windows has no sessions: `0` is rejected and nothing matches another ID. `killall` never signals process 0, the shell, or the utility itself. |
| `less` <a id="utility-less"></a> | Shows files or standard input full screen on the alternate screen with keys from the terminal (`-N`/`--LINE-NUMBERS` number lines, `-S`/`--chop-long-lines` cut long lines instead of wrapping, `-i`/`--ignore-case` ignore case in searches). `/PATTERN` and `?PATTERN` search forward and backward with an extended regular expression, `n`/`N` repeat the search, and `q`/`Q` quit; an interrupt quits with status `130`. Input loads as needed and is kept in memory up to `128 MiB`, counting the line records; input beyond that is not shown, and the status row adds `(truncated)`. Several files follow headers of colons. When output is not a terminal, the input is copied unchanged. See the `less` keys bullet below the table. |
| `link`, `ln` <a id="utility-link"></a> <a id="utility-ln"></a> | Creates hard links (`link file1 file2` and `ln`; an existing `file2` returns `1` for `link`) or, with `ln -s`, symbolic links (`ln -f` remove an existing destination, `-L` follow a link source, `-P` link the link itself). |
| `locale` <a id="utility-locale"></a> | Prints `LANG` and the `LC_` variables as the environment sets them (`-a`/`--all-locales` list the available locales, `-m`/`--charmaps` the character maps). A keyword operand such as `charmap` prints its value (`-k`/`--keyword-name` add the keyword, `-c`/`--category-name` add its category). Returns `1` for an unknown keyword. |
| `logger` <a id="utility-logger"></a> | Sends a message, the operands or each line of `-f`/`--file`, to the system log (`-t`/`--tag` tag, `-p`/`--priority` facility and severity as in `user.notice`, `-i`/`--id` add the process ID, `-s`/`--stderr` also write the message to standard error). Returns `1` for an invalid priority. |
| `logname`, `whoami`, `who` <a id="utility-logname"></a> <a id="utility-whoami"></a> <a id="utility-who"></a> | Prints the login user (`logname`; can differ from the effective user), the effective user (`whoami`), or the logged-in users with their terminals and login times (`who`: `-a`/`-b`/`-d`/`-l`/`-p`/`-r`/`-t`/`-T` select records and fields as POSIX describes, `-H` headings, `-m` and `am i` the terminal of standard input, `-q` users and their count, `-s` short form, `-u` idle times). |
| `ls` <a id="utility-ls"></a> | Lists sorted entries (`-a`, `-A`, `-1`, `-l`, `-h`, `-F` append type indicators, `-p` append a slash to directories, `-d` list directories themselves, `-n` show numeric IDs, `-g`/`-o` omit the owner/group, `-t` sort by time, `-S` by size, `-r` reverse, `-R` recurse, `--tree` print a tree, `-L` limit depth, `--one-file-system` keep recursion and tree on one file system, `-k` accepted). Without a terminal, prints one entry per line, and `-l` prints a symbolic link without its target. |
| `make` <a id="utility-make"></a> | Runs make rules (`-f`/`--file`, `-C`/`--directory`, `-B` always rebuild, `-k` keep going, POSIX modes `-e`, `-i`, `-S`, `-n`, `-p`, `-q`, `-r`, `-s`, `-t`, and `-j` with an optional job count passed through `MAKEFLAGS`). Without `-f`, reads `Makefile` or `makefile`; `-f -` reads standard input. Operands of the form `NAME=VALUE` set variables, and the environment variables are also available. The `wildcard` function and a prerequisite holding `*`, `?`, or `[` use shell pathname expansion, and an unmatched prerequisite pattern stays as written. Recipes are printed before they run and use the `SHELL` of the makefile, and `MAKE` expands to `koshkit make`. A target without a rule returns `2`, and `-q` returns `1` when a target is out of date. See the `make` bullet below the table. |
| `man` <a id="utility-man"></a> | Prints the source of the manual page of each operand, from the section operand or the first section that has one, searching the directories of `MANPATH` (default `/usr/share/man` and `/usr/local/share/man`) (`-k`/`--keyword` list the pages whose first lines contain the keyword). Returns `1` for a missing page. |
| `mkdir`, `rmdir` <a id="utility-mkdir"></a> <a id="utility-rmdir"></a> | Creates directories (`mkdir`; `-p` create parents, `-m` octal mode or symbolic mode applied to `777`); removes empty directories (`rmdir`; `-p` also removes their empty parents). |
| `mkfifo`, `mknod` <a id="utility-mkfifo"></a> <a id="utility-mknod"></a> | Creates each FIFO with the mode of `-m`/`--mode`, or `0666` less the creation mask (`mkfifo`); creates FIFOs and character or block special files from POSIX type operands or `--type`, `--fifo`, `--character`, `--block` (`mknod`; device nodes take `--major` and `--minor`, `-m` sets the mode). |
| `more` <a id="utility-more"></a> | Shows files or standard input a screen at a time with a `--More--` prompt and keys from the terminal (`-n`/`--lines` lines per screen, `-c`/`--clear` clear before each page, `-e`/`--exit` exit after the last line, `-p`/`--command` start at the first line that matches a pattern, `-t`/`--tag` start at a tag of the tags file, `-s`/`--squeeze` collapse repeated blank lines, `-i`/`--ignore-case` ignore case in searches, `-u` accepted). Searches work as in `less`. Each of several files follows a header of colons, also when output is not a terminal. When output is not a terminal, the input is copied, changed only by `-p`, `-t`, and `-s`. A pattern of `-p` may begin with a slash. `-t` reads `./tags`, whose lines hold a tag, a file, and a line separated by tabs, and takes no file operand. `-p` and `-t` cannot be combined, and `more` needs a file when standard input is a terminal. Input is kept in memory up to `128 MiB`, counting the line records; input beyond that is not shown, and the status row adds `(truncated)`. See the `more` keys bullet below the table. |
| `mv` <a id="utility-mv"></a> | Renames or moves paths with `-f`, `-i`, and `-v`. |
| `nice`, `renice` <a id="utility-nice"></a> <a id="utility-renice"></a> | Runs a utility with its scheduling priority lowered by `10`, or by the increment of `-n`/`--increment` (`nice`; a utility that is not found returns `127`); adds the increment of the required `-n`/`--increment` to the priority of each process (`renice`; `-p`/`--pid` process IDs, the default, `-g`/`--pgrp` process groups, `-u`/`--user` all processes of each user). |
| `nl` <a id="utility-nl"></a> | Numbers lines within logical pages. A line of only the section delimiter repeated three, two, or one times starts a header, body, or footer and prints empty. Options: `-d` delimiter (default `\:`; a single character gets a colon added; empty disables sections), `-h`/`-b`/`-f` header/body/footer styles (default `n`, `t`, `n`; `a` every line, `t` nonempty lines, `n` none, `pBRE` lines matching the regular expression), `-v` starting number (each section restarts at it unless `-p` is given), `-i` increment (both may be negative), `-l` number every Nth empty line of a run under style `a`, `-w` width (at most `65536`), `-n` format (`ln`, `rn`, `rz`), `-s` separator. An overflowing line number stops with status `1`. |
| `nohup` <a id="utility-nohup"></a> | Runs a utility that ignores `SIGHUP`. When standard output is a terminal, appends the output to `nohup.out` in the current directory, or in `HOME` when that fails, created with mode `0600`. Returns `127` for a utility that is not found and `126` for one that cannot run. |
| `nproc` <a id="utility-nproc"></a> | Prints the available processor count; `--all` and `--ignore=COUNT` are supported. |
| `od` <a id="utility-od"></a> | Dumps bytes in the formats of `-t` (types `c`, `o`, `u`, `d`, `x` with an optional size; several types print one row each; `-b`/`-c`/`-d`/`-o`/`-s`/`-x` select `o1`/`c`/`u2`/`o2`/`d2`/`x2`; `-A` address radix `d`, `o`, `x`, or `n` for none; `-j` skip bytes; `-N` limit bytes read; `-v` print repeated lines otherwise shown as `*`). A line holds `16` bytes, or `-w[N]`/`--width[=N]` bytes, up to `65536`, or `32` without `N`; a width that is not a multiple of the largest unit is replaced by that unit with a warning. |
| `paste` <a id="utility-paste"></a> | Joins corresponding lines with tabs (`-d` delimiters used in turn, `-s` join each file into one line, `-z`/`--zero-terminated` NUL line ends). |
| `pathchk` <a id="utility-pathchk"></a> | Checks paths against the file system limits (`-p` use the POSIX portable limits and characters, `-P` reject an empty path and a component starting with a hyphen, `-W`/`--windows` reject what Windows cannot hold, which Windows builds always do). |
| `pr` <a id="utility-pr"></a> | Paginates each file or standard input into `66`-line pages with a header of the date, the file, and the page number (`+FIRST[:LAST]` select pages, `-COLUMNS` print that many columns, `-a`/`--across` fill them across, `-m`/`--merge` print the files side by side, `-t`/`--omit-header` omit headers, `-d`/`--double-space`, `-n`/`--number-lines`, `-o`/`--indent`, `-h`/`--header` header text, `-D`/`--date-format`, `-l`/`--length` page length, `-w`/`--width` page width, `-s`/`--separator` column separator, `-F`/`-f` separate pages with form feeds, `-r`/`--no-file-warnings` suppress file warnings). |
| `printenv` <a id="utility-printenv"></a> | Prints the environment, or the given variables, with status `1` if one is absent. |
| `ps` <a id="utility-ps"></a> | Lists processes with `-a`, `-u`, and `-x`; `-w` is accepted. |
| `readlink`, `realpath` <a id="utility-readlink"></a> <a id="utility-realpath"></a> | Prints the target of each symbolic link, with status `1` for a non-link (`readlink`; `-f`/`--canonicalize`, `-e`/`--canonicalize-existing`, `-m`/`--canonicalize-missing` print the canonical absolute path with every link resolved as `realpath` does without a flag, with `-e`, and with `-m`, the last of the three wins; `-n`/`--no-newline` omit the newline for a single file; `-q`/`--quiet`/`-s`/`--silent` suppress errors; `-z`/`--zero` end entries with NUL); prints the canonical absolute path (`realpath`; every component but the last must exist, all with `-e`, none with `-m`; links resolve as met or with `-P`/`--physical`, `-L`/`--logical` resolves `..` first, `-s`/`--no-symlinks` and `--strip` leave links alone, the last of the three wins; `--relative-to` prints paths relative to a directory, `--relative-base` prints paths below a directory relative to it and others absolute, and with both, paths are relative to the first when it is below the base; `-q`/`--quiet` omit errors; `-z`/`--zero` end entries with NUL). |
| `rm`, `unlink` <a id="utility-rm"></a> <a id="utility-unlink"></a> | Removes paths (`-r`/`-R`/`--recursive` directories and contents, `-d`/`--dir` empty directories, `-f`/`--force` ignore missing paths without asking, `-i` ask before each removal, `-I` ask once before removing more than three paths or a tree, `--interactive=never\|once\|always`, `-v`/`--verbose` print each removed path, `--dry-run` print what would be removed and remove nothing, `-x`/`--one-file-system` skip directories on other file systems with status `1`). Refuses `.`, `..`, and every spelling of a root directory or a Windows drive root unless `--no-preserve-root` is given; `--preserve-root=all` also refuses an operand on another device than its parent. A missing path returns `1` without `-f`. `unlink` removes one non-directory path. |
| `sed` <a id="utility-sed"></a> | Applies an editing script, from the first operand or from each `-e`/`--expression` and `-f`/`--file`, to the lines of each file or standard input (`-n`/`--quiet` suppress automatic printing, `-E`/`--extended-regexp` extended regular expressions). Commands: `s` with the `g` and `p` flags, `y`, `d`, `p`, `q`, `=`, and `a`/`i`/`c` (text on the same line or on the next lines after a backslash), with line, `$`, and regex addresses and ranges. Hold space, branch, and block commands are unsupported and return `1`; `-i` is an unknown flag with status `2`. |
| `seq` <a id="utility-seq"></a> | Prints numbers with a nonzero step (`-w`/`--equal-width` pad with zeros, `-s`/`--separator` separator, `-f`/`--format` apply one printf `%e`, `%f`, `%g`, or `%a` conversion; excludes `-w`). Integers print exactly, and decimal, exponent, and hexadecimal operands print with the most fraction digits of the first operand and the step. |
| `sleep`, `timeout`, `watch` <a id="utility-sleep"></a> <a id="utility-timeout"></a> <a id="utility-watch"></a> | Pauses for the sum of the durations, with optional `s`, `m`, `h`, or `d` suffixes (`sleep`); runs a command with a time limit and an optional duration suffix (`timeout`; `-s`/`--signal` signal, `-k`/`--kill-after` send `KILL` after a further duration, `-p`/`--preserve-status` return the command status; returns `124` on timeout and `127` when the command is not found); runs a command at an interval (`watch`; `-n` interval, `-t` omit the title, `-g` exit when output changes, `-e` exit after a command error, `-x` accepted). On a terminal, `q` or an interrupt exits `watch` with status `0`, and the command reads cooked terminal input. |
| `sort` <a id="utility-sort"></a> | Sorts lines by byte order or by `-k` keys of the form `F[.C][bdfghiMnrV][,F[.C][bdfghiMnrV]]` (`-t` separator, `-b` ignore leading blanks, `-d` compare only blanks and alphanumerics, `-f` fold case, `-i` compare only printable characters, `-n` compare numbers, `-r` reverse, `-g` compare floating numbers including exponents, infinities, and NaN with text first, `-h` compare numbers with `K`/`M`/`G`/`T`/`P`/`E`/`Z`/`Y`/`R`/`Q` suffixes, `-M` compare month abbreviations with other text first, `-V` compare version numbers, `-z` NUL line ends, `-u` keep the first of equal keys, `-c` report the first unsorted line, `-C` report only the status, `-o` write to a file, `-s` skip the last-resort whole-line comparison, `-m` accepted). At most one of `-g`, `-h`, `-M`, and `-n` applies to a key, and none combines with `-d`, `-i`, or `-V`. |
| `split` <a id="utility-split"></a> | Splits a file or standard input into pieces `xaa`, `xab`, and so on (or with the prefix operand) of `1000` lines, `-l`/`--lines` lines, or `-b`/`--bytes` bytes (`-a`/`--suffix-length` number of suffix letters). Returns `1` for a count of `0`. |
| `stat` <a id="utility-stat"></a> | Prints file or file system metadata (`-L` follow links, `-f` file system, `-t` terse output, `-c` apply a format, `--printf` apply a format without a trailing newline). |
| `strings` <a id="utility-strings"></a> | Prints each run of at least `4` printable characters in each file or standard input (`-n`/`--bytes` minimum length, `-t`/`--radix` prefix each string with its offset in `d`, `o`, or `x`, `-a`/`--all` scan the whole file). |
| `stty`, `tabs`, `tput`, `tty` <a id="utility-stty"></a> <a id="utility-tabs"></a> <a id="utility-tput"></a> <a id="utility-tty"></a> | Reports or changes terminal settings (`stty`; `-a` report all, `-g` print a state that restores them later); sets the terminal tab stops to every `8` columns, every N columns for `-N`, or the listed columns by writing escape sequences to standard output (`tabs`); writes the escape sequence of a terminal capability from a fixed table (`tput`; `cols` and `lines` print the terminal size and need a terminal, `longname` prints the description of `TERM` or of `-T`/`--terminal`, an unknown capability returns `1`); prints the terminal of standard input, or `not a tty` with status `1` (`tty`; `-s`/`--silent` print nothing). See the `tput` bullet below the table. |
| `sync` <a id="utility-sync"></a> | Flushes file system data (`-d` the data of each operand, `-f` the file system of each operand, or every file system without one). |
| `tee` <a id="utility-tee"></a> | Copies standard input to standard output and to files, appending with `-a`. |
| `touch` <a id="utility-touch"></a> | Updates file times (`-a` access time, `-c` skip missing files, `-m` modification time, `-r` copy a reference file, `-t` set a timestamp). |
| `tr` <a id="utility-tr"></a> | Translates bytes with ranges, classes, `[=c=]` equivalence classes, and `[c*n]` repeats (`-d` delete, `-c`/`-C` complement the first set, `-s` squeeze runs of a byte from the last set, `-t`/`--truncate-set1` truncate the first set instead of padding the shorter second set with its last byte). When translating, the second set accepts only `[:upper:]` and `[:lower:]`, each aligned with an `[:upper:]` or `[:lower:]` class in the first set, and one `[c*]` repeat that fills it to the length of the first; a `[c*]` in the first set is an error. |
| `tsort` <a id="utility-tsort"></a> | Prints a total order of the pairs of words in a file or standard input. A cycle prints a partial order and returns `1`. |
| `uname` <a id="utility-uname"></a> | Prints the system name, or the fields of `-m` (machine), `-n` (node), `-r` (release), `-s` (system), and `-v` (version), or all of them with `-a`. |
| `uniq` <a id="utility-uniq"></a> | Collapses adjacent equal lines (`-c` count, `-d` only repeated lines, `-u` only unique lines, `-D` every line of a repeated run, `-f`/`-s` skip fields/characters, `-w` compare at most N characters, `-i` ignore ASCII case, `--all-repeated[=none\|prepend\|separate]` act as `-D` with empty lines before or between runs, `--group[=separate\|prepend\|append\|both]` print every line with empty lines between, before, or after runs and exclude `-c`, `-d`, `-D`, and `-u`, `-z` NUL line ends). A second operand is the output file. |
| `wc` <a id="utility-wc"></a> | Counts lines (`-l`), words (`-w`), characters (`-m`), and bytes (`-c`). |
| `which` <a id="utility-which"></a> | Resolves builtins, `PATH` programs, and bundled utilities, with `-a`/`--all` and `-q`/`--quiet`. |
| `xargs` <a id="utility-xargs"></a> | Appends arguments from standard input to a command, which reads the null device (`-a`/`--arg-file` read arguments from a file and leave the command standard input, `-0` split on NUL, `-d`/`--delimiter` split on one character or escape such as `\n`, `\t`, `\x41`, or `\0`, both keeping quotes and backslashes literal and disabling the end marker, `-r` skip an empty input, `-E` set the end marker, `-e[EOF]`/`--eof[=EOF]` set or remove it, `-I` replace a string with each input line, `-i[R]`/`--replace[=R]` replace `R` or `{}`, `-L`/`-n`/`-s` limit each command to lines/arguments/bytes, `-P`/`--max-procs` run that many commands at once or as many as possible for `0` with output that may interleave, `-p` ask on the terminal before each command, `-t` trace, `-x` fail when a command does not fit). For `-L`, empty lines do not count and a trailing blank continues a line. Returns `123` when a command fails, `124` when one exits with `255`, `125` when a signal ends one, and `126` or `127` when one cannot run, which under `-P` stops the run after the running commands finish. |
| `yes` <a id="utility-yes"></a> | Repeats the joined operands, or `y`, until output closes. |

* **`grep` colors** - `GREP_COLORS` holds colon-separated GNU capabilities such as `ms=01;31`. The capabilities `ms`, `mt`, `sl`, `cx`, `fn`, `ln`, `se`, and `rv` apply, unknown ones are ignored, and an empty value turns that color off. `GREP_COLOR` sets the match color below `GREP_COLORS`.
* **`less` keys** - The keys come from the terminal.
  * Movement: Space, `f`, Ctrl-F, Ctrl-V, and Page Down move forward a page; `b`, Ctrl-B, and Page Up back a page; `d` and Ctrl-D forward half a page; `u` and Ctrl-U back half a page; `j`, `e`, Enter, Down, Ctrl-E, and Ctrl-N one row down; `k`, `y`, Up, Ctrl-P, and Ctrl-Y one row up. The mouse wheel scrolls three rows. `g`, `<`, and Home go to the top; `G`, `>`, and End go to the bottom; `r` and Ctrl-L redraw.
  * Search: `/PATTERN` and `?PATTERN` search forward and backward, and `n` and `N` repeat the search. An empty pattern repeats the previous one, Escape cancels the prompt, and an invalid pattern is reported.
  * Status row - Shows the file, the visible lines and their total, the position, and `(END)` at the end.
* **`more` keys** - Space and `f` show the next page; Enter, `j`, and Down the next line; `d` half a page; `b`, `k`, Page Up, and Up go further back; `g` and `G` go to the top and the bottom; the searches of `less` apply; `q`, or a forward key at the end, quits. The status row shows `--More--` and the position, or `(END)`.
* **`make` syntax** - Functions: `abspath`, `addprefix`, `addsuffix`, `and`, `basename`, `call`, `dir`, `error`, `eval`, `filter`, `filter-out`, `findstring`, `firstword`, `flavor`, `foreach`, `if`, `join`, `lastword`, `notdir`, `or`, `origin`, `patsubst`, `realpath`, `shell`, `sort`, `strip`, `subst`, `suffix`, `value`, `warning`, `wildcard`, `word`, `wordlist`, and `words`. Directives: `include`, `ifeq`, `ifneq`, `ifdef`, `ifndef`, `else`, `endif`, `define`, `override`, `export`, and `unexport`. Special targets: `.DEFAULT`, `.IGNORE`, `.PHONY`, `.POSIX`, `.PRECIOUS`, `.SCCS_GET`, `.SILENT`, and `.SUFFIXES`.
* **`tput` capabilities** - `bel`, `blink`, `bold`, `civis`, `clear`, `cnorm`, `cub1`, `cud1`, `cuf1`, `cuu1`, `dch1`, `dl1`, `ed`, `el`, `home`, `ich1`, `il1`, `ind`, `init`, `reset`, `rev`, `rmso`, `rmul`, `sgr0`, `smso`, `smul`, and `cup` with a row and a column.

### Windows platform semantics <a id="windows"></a>

On Windows, Koshka executes the exact same shell grammar with the following OS adaptations:
* **Virtual path translation** - `/dev/null` maps to the Windows `NUL` device (`compatibility.dev_null=on`), and `/tmp` (or paths beneath `/tmp`) maps to `$TEMP` (`compatibility.slash_tmp=on`). Both options are on by default. Bash startup files retain their POSIX paths.
* **Environment and process model** - Environment variable identifiers are case-insensitive, individual exported values truncate at `32,766` UTF-16 code units, `PATH` (or `Path`) uses `;` delimiters (ignoring empty segments), and user configuration falls back to `$USERPROFILE` when `$HOME` is unset. Subshells spawn in new OS processes, and background jobs run in dedicated console process groups. Process substitutions (`<(...)`, `>(...)`) use FIFO-like pipes where sequential readers continue from the prior read offset and sequential writers append to the stream. See [Process substitution](#process-substitution), [`echo`](#builtin-echo), [`pwd -W`](#builtin-pwd), and local [`assimilate`](#builtin-assimilate) for more Windows behavior.

### Runtime file paths <a id="runtime-files"></a>

| File path | Purpose |
| --- | --- |
| `/etc/kosh.conf` (POSIX), `%ProgramData%\kosh\kosh.conf` (Windows) | System-wide settings file. |
| `$XDG_CONFIG_HOME/kosh/kosh.conf`, `~/.config/kosh/kosh.conf` | User settings file. |
| `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh`, `~/.config/kosh/scripts/rc.kosh` | Interactive and login startup script for the `kosh` mood. |
| `~/.kosh_history`, `~/.kosh_directory_history`, `~/.kosh_calc_history` | Default command history, `z` directory history, and `calc` expression history files. Rewrites of the command history file go through temporary files beside it. |
| `/etc/profile`, `~/.profile`, `~/.bash_profile`, `~/.bash_login`, `~/.bashrc`, `/etc/bash.bashrc` | Compatibility startup files for `sh`, `bash`, and `bash-posix` moods. |

---

## Bash and POSIX language specification <a id="bash-and-posix-reference"></a>

### Invocation, operands, and compatibility startup <a id="invocation"></a> <a id="operands"></a> <a id="bash-and-posix-startup-files"></a>

* **Input source precedence** - Evaluated in strict order: `-s` (stdin), `-c` (inline command strings), `script` operand (where `-` selects stdin; a directory fails with `126`, an unopenable file fails with `127`), `-i` or terminal stdin (interactive editor), or non-interactive stdin. Selecting multiple sources emits a warning and uses the highest-priority source, except `-s -i` on a terminal starts the interactive editor. Passing `-i` without a terminal fails after startup files complete.
* **Positional parameter assignment (`$0` and `$1..$N`)** - For scripts, `$0` is the script path and remaining operands become `$1..$N`. For `-c`, the first operand after the command string(s) becomes `$0` and subsequent operands become `$1..$N`. For `-s` and interactive sessions, `$0` remains the shell invocation name and all operands become `$1..$N`.
* **Initial mood and login operation** - The invocation basename selects the initial mood (`sh`/`dash` select `sh`, `bash`/`rbash` select `bash`; `rbash` also requests restricted mode). A leading dash in argument zero or `-l` selects login operation.
* **`KOSH_FLAGS`** - Supplies options before the command line, which override the same option from it. Input-selecting and mode flags (`-c`, `-s`, `--rcfile`, `--init-file`, `--format`, `--apply`, `--as-language-server`), `--`, and operands are dropped (see [Koshka variables](#configuration-variables)). A secure execution removes it.
* **Invocation failure and rescue mode** - When `$0` begins with `-` (login shell) and option parsing fails, the shell warns and enters **rescue mode** (skipping all settings and startup files). A failing `KOSH_FLAGS` is dropped and the command-line options and input source are kept. A failing command line in a login shell retains only `-r` and reads commands from stdin (interactively on a terminal). All other invalid invocations, including an unknown value of `--mood`, `--tab-selector`, `--init-moods`, `-o`, or `-O`, exit with status `2`.

| Mood and session type | Startup files sourced (in order) |
| --- | --- |
| `sh` login | `/etc/profile`, then `~/.profile`. |
| `sh` interactive | File specified by expanded `$ENV` (no `PATH` search; skipped under `-p`). |
| `bash` / `bash-posix` login | `/etc/profile`, then the first readable file among `~/.bash_profile`, `~/.bash_login`, and `~/.profile`. |
| `bash` / `bash-posix` interactive non-login | First readable file among `/etc/bash/bashrc` and `/etc/bash.bashrc`, then `--rcfile` or `~/.bashrc`. If one was sourced, tab completion is on, no default `complete -D` spec exists, and `BASH_COMPLETION_VERSINFO` is unset, sources `/usr/share/bash-completion/bash_completion`. |
| `bash` non-interactive (and after `bash` login profiles or `-L bash`) | File specified by expanded `$BASH_ENV` once (no `PATH` search; not sourced in `bash-posix` or under `-p`). |

`--rcfile` applies only to an interactive non-login shell. On Windows these files keep their POSIX paths. Skipping rules are under [Startup and configuration file loading](#startup-files).

### Lexical grammar, reserved words, and quoting <a id="terminology"></a> <a id="reserved-words"></a> <a id="comments"></a> <a id="quoting"></a>

* **Lexical tokens** - **Blanks** are spaces and tabs. **Metacharacters** (`space`, `tab`, `newline`, `|`, `&`, `;`, `(`, `)`, `<`, `>`) separate unquoted words. **Control operators** are `newline`, `||`, `&&`, `&`, `;`, `;;`, `;&`, `;;&`, `|`, `|&`, `(`, and `)`. **Identifiers** consist of ASCII letters, digits, and `_`, not starting with a digit; variables and, in most contexts, functions are identifiers. A **word** (also a token) is a sequence of characters that the shell treats as one unit. **Comments** begin with an unquoted `#` at the start of a word and last to the end of the line (always enabled in scripts; governed by `interactive_comments`, default `on`, in interactive shells); a `#` inside a word, as in `a#b`, is an ordinary character.
* **Reserved words** - Recognized only when unquoted as the first word of a command, the third word of `case` or `for`, or immediately following another command-list-starting reserved word:
  `!` `case` `coproc` `do` `done` `elif` `else` `esac` `fi` `for` `function` `if` `in` `select` `then` `time` `until` `while` `{` `}` `[[` `]]`.
  `select` is reserved only in `bash` and `bash-posix`. The `sh` mood does not reserve `coproc`, `select`, or `time` (`time` is an ordinary command found through `PATH`), and rejects `[[` as a fatal dialect error. A reserved word in any other position is an ordinary word (`echo if then fi` prints its arguments).
* **Quoting mechanisms**:
  * **Backslash (`\`)** - Preserves the following character, or joins lines before a newline (a trailing `\` at EOF is a syntax error in `kosh` and literal in compatibility moods; on Windows, unquoted `\` in native paths acts as an escape and must be quoted or doubled).
  * **Single quotes (`'...'`)** - Preserve all characters literally; cannot contain `'` (a backslash does not escape it).
  * **Double quotes (`"..."`)** - Enable `$`, `` ` ``, and `\` (where `\` escapes only `$`, `` ` ``, `"`, `\`, or `newline`), preserving field boundaries for `"$@"` and `"${a[@]}"`.
  * **ANSI-C quoting (`$'...'`)** - Expands `\a`, `\b`, `\e`, `\E`, `\f`, `\n`, `\r`, `\t`, `\v`, `\\`, `\'`, `\"`, `\?`, `\xH`/`\xHH`, `\NNN` (octal), `\uHHHH`/`\UHHHHHHHH` (UTF-8), and `\cX` (control byte), terminating at any `NUL` byte. Any other escape stays as written, with its backslash. Unrecognized in the `sh` mood, which keeps the `$` as an ordinary character.
  * **Locale translation (`$"..."`)** - Behaves as double quotes (unrecognized in the `sh` mood, which keeps the `$` as an ordinary character).

### Shell grammar constructs <a id="shell-grammar"></a> <a id="simple-commands"></a> <a id="pipelines"></a> <a id="lists"></a> <a id="compound-commands"></a> <a id="coprocesses"></a> <a id="function-definitions"></a>

The `sh` mood enforces `dash` compatibility: it rejects `[[ ]]`, C-style `for (( ))`, `|&`, and `<<<` at parse time; parses `&>` as `&` followed by `>`; treats `$'...'`, `$"..."`, brace expansion, `(( ))`, `$[ ]`, `${ cmd; }`, `${v@op}`, and quotes inside `$(( ))` as literal/standard tokens; accepts `<(...)`, `function`, and `a[i]=v`; and evaluates compound array assignments `a=(x y)` as empty scalar assignments.

| Construct | Syntax and execution semantics across moods |
| --- | --- |
| Simple commands | `[var=val ...] [word \| redirection ...]`. All prefix assignment values expand *before* any assignment binds (in `a=7 b=$a cmd`, `b` receives the prior value of `a`). Prefix assignments apply to the environment of that command only (special builtins keep them in the shell in `kosh`, `bash-posix`, and `sh` but not in `bash`; functions never keep them). Assignments without a command word apply in the shell. Exit status is `128 + signo` if killed by a signal. |
| Pipelines | `[time [-p] [-R]] [!] cmd1 [ { \| \| \|& } cmd2 ... ]`. `\|&` pipes both stdout and stderr (`2>&1 \|`). In `kosh` and `sh`, a builtin in the final pipeline stage executes in the current shell process (`echo 5 \| read x` sets `x`); in `bash` and `bash-posix`, the final stage runs in the current shell only when `lastpipe=on` and job control (`monitor`) is `off`. Every other command, including a function or compound command in the last stage in `kosh` and `sh`, runs in its own process. Empty pipeline stages (`p=1 \| cat`) are errors in `kosh`, and compatibility moods apply the stage's redirections and give it status `0`. Status is the last stage's status, or the rightmost failing stage under `pipefail`, negated by `!`, with all stage statuses recorded in `PIPESTATUS[@]`. `time` reports real/user/sys time and follows `TIMEFORMAT` (see [Parameters](#shell-variables)); it prints nothing without a pipeline. The `kosh` mood adds a CPU row and, when reported and nonzero, a peak RSS row. `-R` adds the RSS row in any format, and `-p` selects the POSIX format without the CPU row. |
| Lists (`;`, `&`, `&&`, `\|\|`) | `&&` and `\|\|` share equal left-associative precedence and bind tighter than `;` and `&`, which share equal precedence. `a && b` runs `b` only when `a` returns `0`, and `a \|\| b` runs `b` only when `a` returns nonzero; the status of an and-or list is that of the last command that ran. A list ended by `;` or a newline runs in the foreground. Trailing `&` runs the entire preceding and-or list asynchronously in a background subshell (`a && b \|\| c &` is one job whose variable and directory changes stay in the job; status `0`, PID in `$!`). In `kosh`, words expand *before* spawning a background simple command; in compatibility moods, the process starts before words, assignments, and redirections expand, so an expansion error fails the job (status `127` for an unset parameter or `${name?word}` and `1` for a failed redirection in Bash moods, `2` in `sh`). |
| Subshell `(list)` & Group `{ list; }` | `(list)` executes in an isolated subshell environment; `{ list; }` executes in the current shell and requires a trailing `;` or newline before `}` (the braces are reserved words and must be separate words). Subshell changes to variables, the working directory, options, and traps do not reach the shell. The status is that of the list, and redirections after a compound command apply to the whole command. |
| Arithmetic `((expr))` & Conditional `[[ expr ]]` | `((expr))` returns `0` if `expr` evaluates nonzero, and `1` if zero or empty; the `sh` mood treats `((` as two subshell openings. `[[ expr ]]` disables word splitting and globbing on operands, returning `0`, `1`, or `2` when the expression cannot be evaluated. |
| Loops (`for`, `select`, `while`, `until`) | `for name [in word ...]; do list; done` (requires `do...done`, rejecting `{...}`; without `in`, iterates over the positional parameters). `for (( expr1; expr2; expr3 )); do list; done` evaluates `expr1`, then runs the list and evaluates `expr3` while `expr2` is nonzero, and treats empty expressions as `1` (a dialect error in `sh`). `select name [in word ...]; do list; done` (`bash`/`bash-posix` only) prints the expanded words as a numbered menu on stderr, prints `PS3`, and reads a line: a word number sets `name` to that word, any other line sets `name` to empty, `REPLY` holds the raw line, and an empty line prints the menu again. The body runs after each nonempty line until a `break` or the end of input. `while` loops while the condition returns `0` and `until` while it returns nonzero. The status of a loop is that of the last list or body that ran, or `0` when none ran. |
| Pattern branch `case` & Conditional `if` | `case word in [(] pat [\| pat]...) list ;;\|;&\|;;& esac` where `;;` stops, `;&` falls through without testing the next pattern, and `;;&` tests subsequent patterns (every mood accepts the three terminators). `if list; then list; [elif list; then list;]... [else list;] fi` runs the `then` list of the first condition returning `0`, or the `else` list. The status of `case` and `if` is that of the last list that ran, or `0` when no pattern matched or no branch ran. |
| Coprocess `coproc` | `coproc [NAME] command [redirections]` (unsupported in `sh`). Spawns `command` asynchronously with two pipes (> fd `9`, closed on `exec` and not inherited by subshells), storing read/write descriptors in `NAME[0]` / `NAME[1]` (`COPROC` default; `NAME` is recognized only before a compound command) and PID in `NAME_PID` and `$!`. The launch returns `0`, and the shell closes the descriptors and unsets `NAME` and `NAME_PID` when the coprocess finishes. |
| Functions `fname()` | `fname () command [redirections]` or `function fname [()] command [redirections]`. Stores `command` (any simple or compound command, as in `f() echo hi`) as the body, returns `0`, and attaches redirections to every invocation. A later definition replaces the body unless the function is read-only (`readonly -f`). Any unquoted word (`f.g`, `f-g`) or quoted word can be the function being defined; a reserved word after `function` is an error, and a word built by an expansion defines nothing. Variables are global unless declared `local` (`local var` without `=val` inherits the outer value in `kosh` and `sh`, and starts unset in Bash moods). Nesting caps at `900` function calls and `400` sourced files, and the caps shrink when the stack runs short (see [Functions](#functions)). |

### Parameters, variables, and arrays <a id="parameters"></a> <a id="positional-parameters"></a> <a id="special-parameters"></a> <a id="shell-variables"></a> <a id="arrays"></a>

* **Assignment evaluation** - `name=value` and `name+=value` undergo tilde expansion (after `=` and after any `:`), parameter/command/arithmetic expansion, and quote removal, without word splitting or globbing. `name+=value` appends to a string, adds to an integer, and appends elements to an array. `declare` and `local` give variables attributes, such as integer, read-only, exported, or a case conversion (see [Standard shell builtins reference](#builtins)). A variable is set when it has been assigned a value, which may be empty, and stays set until it is unset.
* **Special parameters**:
  * `$*` and `$@`: Positional parameters from `$1`. Unquoted, both split on `IFS`. `"$*"` joins with the first byte of `IFS`; `"$@"` expands to separate words. When joined into a single scalar (such as `v=$@`), `$@` joins with the first byte of `IFS` in `sh` and with a space in Bash/`kosh` moods. In the `sh` mood, `$@` and `$*` are always treated as set inside `${@-d}` and `${@+p}`.
  * Positional parameters are the arguments of the shell, a function, or a sourced file, numbered from `1`. A parameter above `9` needs braces: `$10` is `$1` followed by `0`, and `${10}` is the tenth parameter. `set --` replaces them and `shift` discards the first ones. They cannot be assigned directly, and `${@:=x}` is an error. `${@:offset:length}` selects parameters, and an offset of `0` includes `$0`.
  * `$#` (parameter count), `$?` (last foreground exit status), `$-` (active option flags, including `h` and `B` by default, `c` for `-c`, and `u` in `kosh`), `$$` (shell PID, preserved in subshells), `$!` (last background/coproc/process-substitution PID), `$0` (shell/script name, unchanged inside a function), and `$_` (final argument of previous simple command; empty at startup, and unchanged by an assignment with no command and by `[[ ]]`). `$!` is empty before the first background command. `$?` also reports the status of the last command substitution.
* **Indexed and associative arrays** - Indexed arrays (`a=(x y)`, `a=([2]=c)`, `a[i]=v`, `a+=(w)`) support sparse non-contiguous integer subscripts (`${a[i]}`, where negative indices count from the end, and scalar `$a` is `${a[0]}`). Associative arrays (`declare -A m`, `m[key]=v`) preserve insertion order when listing keys via `${!m[@]}`. Under `set -u`, `${a[@]}` and `${a[*]}` on an unset array expand cleanly to nothing, whereas `$a` and `${a[0]}` trigger an unbound variable error. Exporting an array (`export a` / `declare -x a`) records the `-x` attribute in `declare -p` without passing the array into child process environments; `declare -p` prints the attribute as `-ax` or `-Ax`.
* **Array details** - Indexed arrays have integer indices from `0` that may be missing (`a[5]=z` on a two-element array leaves `0`, `1`, `5`), `a=([2]=c [0]=a)` assigns explicit indices, and `${a[i]}` takes an arithmetic subscript. Associative arrays map string keys to values (`m[1+1]` holds the key `1+1`). `${a[@]}` and `${a[*]}` list elements, `${#a[@]}` counts them, and `${!a[@]}` lists the set indices or keys. `"${a[@]}"` keeps one field per element, `"${a[@]@A}"` yields the words of the declaration, `"${a[@]@K}"` yields keys and values, and `"${a[*]}"` joins with the first byte of `IFS`; unquoted `${a[@]}` and `${a[*]}` join the same way and split the result. `unset 'a[i]'` and `unset 'm[key]'` remove one element. Associative keys list in first-assignment order, which Bash can list differently, so a script that depends on the order should sort the keys. In the `sh` mood, a subscript assignment creates an element, a compound assignment assigns an empty string, and `unset` rejects a subscript.
* **Shell-maintained and configuration variables** - The tables below list these variables in addition to the [Koshka variables](#koshka-variables). The `kosh` and Bash moods maintain the first table, and the second table lists the variables that affect the shell. The `sh` mood sets only `PIPESTATUS`, `LINENO`, `OPTIND`, `PPID`, `SHLVL`, `PWD`, `OLDPWD`, `IFS`, `PS2`, and `PS4` from them.
* **Maintained variable assignment** - An assignment to a maintained variable, such as `BASHPID` or `HISTCMD`, is discarded unless the entry states otherwise. The shell ignores an inherited environment value of such a variable, and children receive that value unchanged.

#### Shell-maintained variables

| Variable | Behavior |
| --- | --- |
| `BASH`, `BASH_VERSION`, `BASH_VERSINFO` | Hold the Bash identity, set only in `bash` and `bash-posix` or in a session that initialized one of them (`BASH` is the executable, `BASH_VERSION` is `5.3.0(1)-kosh`, `BASH_VERSINFO` holds the version fields with the build host in element 5). |
| `BASH_ALIASES` | Holds the aliases (assigning an element defines an alias, unsetting an element does nothing, unsetting the array disconnects it). |
| `BASH_ARGC`, `BASH_ARGV` | Hold the argument count of each recorded frame and the arguments in reverse order, innermost first. The `extdebug` option records function and source calls in them. Without it, `BASH_ARGV` is empty and `BASH_ARGC` holds one `0` outside functions and is empty inside them. Assignments do nothing, and `unset` and `local` fail. |
| `BASH_ARGV0` | Holds argument zero of the shell (assignment does not change `$0`). |
| `BASH_COMMAND` | Holds the command that is running or about to run. |
| `BASH_EXECUTION_STRING` | Holds the command string of `-c`. |
| `BASH_LINENO`, `BASH_SOURCE`, `FUNCNAME` | Hold the call stack: `FUNCNAME[0]` is the running function, `BASH_SOURCE[i]` the file that defines `FUNCNAME[i]`, and `BASH_LINENO[i]` the line where `FUNCNAME[i]` was called. Empty in the `sh` mood. |
| `BASH_MONOSECONDS` | Holds a monotonic clock in milliseconds. |
| `BASH_REMATCH` | Holds the match and groups of the last successful `[[ =~ ]]`, and is empty after a failed match. |
| `BASH_SUBSHELL` | Holds the subshell depth (`0` in the shell). |
| `BASHOPTS` | Holds the read-only, colon-separated list of `shopt` options that are on; an environment value is ignored. |
| `BASHPID` | Holds the process id of the current shell or subshell. |
| `COMP_CWORD`, `COMP_LINE`, `COMP_POINT`, `COMP_WORDS` | Hold the cursor word index, line, cursor byte offset, and words for a completion function (see [`complete`](#builtin-complete)); `COMP_KEY` and `COMP_TYPE` are exported to a `complete -C` command only. |
| `COPROC`, `COPROC_PID` | Hold the descriptors and process of a coprocess without `NAME`. |
| `DIRSTACK` | Holds the current directory followed by the saved stack. Assigning an existing index other than `0` changes that entry; other assignments and element unsets do nothing, and unsetting the array disconnects it. |
| `EPOCHREALTIME`, `EPOCHSECONDS` | Hold the wall time in seconds (`EPOCHREALTIME` with microseconds). |
| `EUID`, `UID` | Hold the read-only effective and real user ids. |
| `GROUPS` | Holds the real group id. |
| `HISTCMD` | Holds the history number of the current command (`0` in a non-interactive shell). |
| `HOSTNAME`, `HOSTTYPE`, `MACHTYPE`, `OSTYPE` | Hold the host and platform. |
| `LINENO` | Holds the line of the running command; each command in a multi-line command substitution reports its own file line. Assignments are discarded, and `unset` does not remove the behavior. |
| `MAPFILE` | Holds the default array of `mapfile`. |
| `OLDPWD`, `PWD` | Hold the previous and current logical working directories, updated by `cd`, and are exported only when inherited exported or exported explicitly. A logical `PWD` is accepted at startup only when absolute and identifying the current directory. |
| `OPTARG`, `OPTIND` | Hold the argument of the last option and the index of the next argument for `getopts` (`OPTIND` starts at `1`). |
| `PIPESTATUS` | Holds the status of each stage of the last foreground pipeline. |
| `PPID` | Holds the parent process id (read-only except in the `sh` mood). |
| `RANDOM` | Yields a random integer from `0` to `32767`. Assignment seeds the sequence reproducibly, and `unset` makes it an ordinary variable. |
| `REPLY` | Holds the line read by `read` without a variable and by `select`. |
| `SECONDS` | Holds the seconds since the shell started. Assignment moves the base, and `unset` makes it an ordinary variable. |
| `SHELLOPTS` | Holds the read-only, colon-separated list of `set -o` options that are on; an environment value is ignored. |
| `SHLVL` | Holds the shell nesting level, incremented at startup (a negative inherited value becomes `1`, and an inherited value of `1000` or more restarts at `1`). |
| `SRANDOM` | Yields a random 32-bit integer from the system generator. |

#### Variables that affect the shell

| Variable | Behavior |
| --- | --- |
| `BASH_COMPLETION_VERSINFO` | Suppresses sourcing of the bash-completion script in an interactive `bash` mood shell when set. |
| `BASH_ENV`, `ENV` | Select startup files (see the startup table above); values undergo parameter expansion and command substitution. |
| `BASH_XTRACEFD` | Selects the descriptor that receives `xtrace` output. Standard error receives it when the variable is unset or not a nonnegative integer; a closed descriptor discards it. |
| `CDPATH` | Lists the colon-separated directories that `cd` searches for a relative operand. |
| `COLUMNS`, `LINES` | Hold the terminal size, set at the start of an interactive shell and not updated when the terminal changes size. |
| `FCEDIT`, `EDITOR`, `VISUAL` | Select editors: `FCEDIT`, then `EDITOR` for `fc`; `VISUAL`, then `EDITOR` for Ctrl-X Ctrl-E. |
| `GLOBIGNORE` | Lists colon-separated patterns of pathname expansion results to remove (see [Pathname expansion](#pathname-expansion)). |
| `HOME` | Holds the home directory used by `cd` and tilde expansion (on Windows the shell uses `USERPROFILE` when `HOME` is unset). |
| `IFS` | Holds the field separators and the joiner of `"$*"`, by default space, tab, and newline; an environment value is ignored. |
| `IGNOREEOF` | Sets the number of consecutive end-of-file inputs that an interactive shell needs before it exits while `ignoreeof` is on. Enabling the option sets an unset `IGNOREEOF` to `10`, assigning `IGNOREEOF` enables the option, and unsetting it disables the option. |
| `LANG`, `LC_ALL`, `LC_COLLATE`, `LC_CTYPE`, `LC_NUMERIC` | Select the locale (see [Locale](#locale)). |
| `MANPATH` | Lists the manual page directories that completion, hints, and `man` search. |
| `NO_COLOR` | Disables color in the prompt, diagnostics, editor, and bundled utilities when nonempty. |
| `OPTERR` | Silences `getopts` diagnostics when `0`. |
| `PATH` | Lists the colon-separated command search path. An empty element, an empty value, and an unset `PATH` search the current directory. The shell does not set `PATH` when the environment lacks it. On Windows the separator is a semicolon, an empty element is skipped, and `Path` counts as `PATH`. |
| `PROMPT_COMMAND`, `PS0`, `PS1`, `PS2`, `PS3`, `PS4`, `RPS1`, `RPROMPT` | Hold the prompts and the command run before each prompt (see [Prompt rendering](#prompting)); `PS3` is the `select` prompt, `#? ` by default. |
| `SHELL` | Holds the user shell path. An inherited value is kept, and an unset `SHELL` becomes the path of the executable. |
| `TERM`, `COLORTERM`, `TERM_PROGRAM` | Describe terminal capabilities (see [Interactive line editing](#line-editing)); `TERM=dumb` disables color and the terminal integration marks. |
| `TIMEFORMAT` | Selects the `time` report format: `%R`, `%U`, `%S` are elapsed, user, and system seconds, `%P` the processor percentage, `%3R` three decimals, `%lR` minutes and seconds, `%%` a percent sign. An empty value prints nothing, and an unset value selects the default report. |
| `TMPDIR` | Selects the directory for the temporary files of `fc`, Ctrl-X Ctrl-E, and `slurp`. The shell reads it from the environment, so an assignment needs `export`; an empty value is ignored, the default is `/tmp`, and on Windows the directory is `TEMP`. |
| `TZ` | Holds the time zone. The shell uses the inherited value, so an assignment in the shell changes the zone of later programs only. |
| `XDG_CONFIG_HOME` | Sets the base of the user settings directory (see [Startup and configuration file loading](#startup-files)); a relative or empty value is ignored. |

These Bash variables have no effect: `HISTCONTROL`, `HISTFILE`, `HISTFILESIZE`, `HISTIGNORE`, `HISTSIZE`, `HISTTIMEFORMAT`, `histchars`, `MAIL`, `MAILCHECK`, `MAILPATH`, `TMOUT`, `PROMPT_DIRTRIM`, `FIGNORE`, `EXECIGNORE`, `HOSTFILE`, `INPUTRC`, `CHILD_MAX`, `BASH_COMPAT`, `BASH_LOADABLES_PATH`, `GLOBSORT`, `FUNCNEST`, `auto_resume`, and `POSIXLY_CORRECT`.

### Word expansions, redirections, arithmetic, and conditionals <a id="expansions"></a> <a id="brace-expansion"></a> <a id="tilde-expansion"></a> <a id="parameter-expansion"></a> <a id="command-substitution"></a> <a id="arithmetic-expansion"></a> <a id="process-substitution"></a> <a id="field-splitting"></a> <a id="pathname-expansion"></a> <a id="pattern-matching"></a> <a id="quote-removal"></a> <a id="redirections"></a> <a id="arithmetic-evaluation"></a> <a id="conditional-expressions"></a>

* **Expansion order** - Brace expansion, tilde expansion, then parameter expansion, arithmetic expansion, command substitution, and process substitution from left to right, then field splitting, pathname expansion, and quote removal. Only brace expansion, field splitting, and pathname expansion can change the number of words.

#### Brace, tilde, process substitution, field splitting, and quote removal

* **Brace expansion** - Unquoted `{a,b,...}` produces one word per item with the surrounding text (`a{b,c}d` gives `abd` `acd`; items may be empty; braces nest).
  * Sequences `{x..y}` and `{x..y..step}` produce integers or single characters from `x` to `y` (a leading zero on either integer pads every result to one width, step `0` counts as `1`, the sign of the step is ignored, character ranges include every character between the ends as in `{Z..a}`).
  * An invalid list or sequence (`{a}`, `{a-z}`, `{1..a}`) and a sequence that holds an expansion stay literal.
  * Does not apply in an assignment value, here-string, `case` pattern, or redirection target; off in `sh` and while `braceexpand` is off.
* **Tilde expansion** - An unquoted leading `~` expands to `HOME`, `~user` to the home directory of `user`, `~+` to `PWD`, `~-` to `OLDPWD`. The prefix ends at the first slash.
  * Unknown users, `~-` with `OLDPWD` unset, and the directory stack forms `~N`, `~+N`, `~-N` stay literal. An empty `HOME` expands to an empty word, and an unset `HOME` is an error.
  * Also expands after `=` and `:` in assignments and in the words of `export`, `declare`, `local`, `readonly`, and (outside `sh`) in any argument that looks like an assignment such as `a=~`.
* **Process substitution** - `<(command)` and `>(command)` expand to a path that reads the output of `command` or writes its input, with `command` in a separate process. Expands anywhere in an unquoted word (`p=<(command)` assigns the path) in every mood.
  * The path stays open until the expanding command finishes (for a `for` word list, until the loop finishes); the shell then closes it and waits for the process.
  * Sets `$!` to the process, and `wait` on it returns its status.
  * POSIX path is `/dev/fd/63` or the next free descriptor above it. On Windows it is a FIFO-like pipe: each reader continues where the previous stopped and each writer appends to the same stream.
* **Field splitting** - Results of unquoted parameter expansion, command substitution, and arithmetic expansion split at the bytes of `IFS`. A run of `IFS` whitespace collapses and is ignored at both ends, each other `IFS` byte delimits a possibly empty field, and an empty `IFS` disables splitting. An unquoted expansion that yields nothing produces no field, and `""` produces an empty field.
* **Quote removal** - Removes unquoted backslashes, single quotes, and double quotes that did not result from an expansion.

#### Parameter and substitution forms

| Expansion syntax | Evaluation semantics and mood constraints |
| --- | --- |
| `${name:-word}`, `${name:=word}`, `${name:?word}`, `${name:+word}` | Default, assign-default, error-if-unset, and alternate-value forms (with `:` the test covers unset and empty, without it only unset; `word` expands only when used). `:=` cannot assign positional or special parameters, and `:?` prints an error with `word` and exits a non-interactive shell. Inside double quotes or here-documents, `word` of the `-`, `+`, and `=` forms follows double-quote rules (`"${v:-'a'}"` retains `'a'`). |
| `${#name}`, `${name:offset[:length]}` | Character length (`${#*}`, `${#@}` give the positional parameter count) and substring/array slice (negative `offset` counts from the end and needs a leading space as in `${v: -3}` to differ from `:-`; negative `length` excludes that many characters from the end; on an indexed array `${a[@]:offset:length}` starts at the first element with index at least `offset`). Braces are required for a positional parameter above `9`, an array element, or a name followed by a character that could continue it. |
| `${name#pat}`, `${name##pat}`, `${name%pat}`, `${name%%pat}` | Strip shortest/longest matching prefix (`#`/`##`) or suffix (`%`/`%%`). |
| `${name/pat/str}`, `${name//pat/str}`, `${name/#pat/str}`, `${name/%pat/str}` | Replace first, all (`//`), prefix (`/#`), or suffix (`/%`) matches. While `patsub_replacement=on` (default), unquoted `&` in `str`, or one from an unquoted expansion, stands for the matched text, and a quoted `&` or `\&` is literal. With the option off, every `&` is literal. |
| `${name^pat}`, `${name^^pat}`, `${name,pat}`, `${name,,pat}`, `${name~pat}`, `${name~~pat}` | Convert first or all matching characters to uppercase (`^`), lowercase (`,`), or inverted case (`~`). Without a pattern, every character matches. |
| `${name@op}` | Value transformations (unsupported in `sh`): `Q` (shell-quoted), `E` (`$'...'` escapes), `P` (prompt expansion), `A` (declaration statement), `a` (attribute flags), `U`/`L`/`u` (case conversion), `K`/`k` (array keys and values); `U`/`L` convert to upper/lower case and `u` uppercases the first character. Any other operator on a set parameter is a bad substitution. |
| `${!name}`, `${!prefix*}`, `${!prefix@}`, `${!name[@]}`, `${!name[*]}` | Indirect variable lookup (or nameref target name for `declare -n`), variable names matching `prefix`, and array indices/keys. For a name reference `${!name}` gives the target identifier, and an unset or empty `name` gives an empty value. |
| `$(cmd)`, `` `cmd` ``, `$(< file)` | Subshell command substitution (strips trailing newlines; inside backquotes a backslash keeps its meaning only before `$`, `` ` ``, or `\`; `$(< file)` expands to the contents of `file` without running a command; `kosh` preserves `NUL` bytes while compatibility moods drop them). Runs in a subshell, leaving shell state, options, directory stack, working directory, and creation mask unchanged. A syntax error inside a substitution is reported at its place in the outer source, and the script or command string holding it does not run. Nesting deeper than `64` levels is a syntax error. |
| `${ cmd; }`, `${\| cmd; }` | In-process function substitutions (supported in `kosh`, `bash`, `bash-posix`): `${ cmd; }` captures stdout and `${\| cmd; }` expands to `$REPLY` without spawning a subshell. Variable changes in both forms remain. |
| `$((expr))`, `$[expr]` | Arithmetic expansion, evaluated as described under [Arithmetic evaluation](#arithmetic-evaluation) (the expression first undergoes parameter expansion, command substitution, and quote removal; empty `$(( ))` yields `0`; `$[expr]` is an obsolete spelling outside `sh`, where it stays literal). |
| Non-printing values | A value or key with a byte that does not print in the locale (control byte, invalid UTF-8) takes the `$'...'` form in `${v@Q}`, `${v@A}`, `${a[@]@K}`, `printf %q`, `declare -p`, and `export -p`. |

#### Pathname globbing and pattern matching

* **Pathname expansion** - After field splitting, an unquoted word holding `*`, `?`, or `[` is a pattern replaced by the sorted matching pathnames. A leading dot and every slash must match explicitly, and `.` and `..` are never results.
  * Options: `noglob` disables expansion, `dotglob` lets `*` and `?` match a leading dot, `nocaseglob` ignores case, `globstar` makes `**` match any number of directories (`**/` matches directories only).
  * Sort order is byte value in `kosh` and `sh` and `LC_COLLATE` order in Bash moods.
  * A pattern with no matches is a fatal error in `kosh`, stays literal in compatibility moods by default, expands to nothing under `nullglob`, and errors under `failglob`.
* **`GLOBIGNORE`** - A nonempty value holds colon-separated patterns, and a result matching one is removed. A pattern whose results are all removed follows the no-match rules. Assigning a nonempty value enables `dotglob`, and unsetting it disables `dotglob`.
* **Extended globs (`extglob`)** - Enabled by default in `kosh`, always enabled inside `[[ == ]]` and `[[ != ]]`, and controlled by `shopt -s extglob` elsewhere. While on, `?(list)`, `*(list)`, `+(list)`, `@(list)`, and `!(list)` match zero or one, zero or more, one or more, exactly one, or anything except one of the `\|`-separated patterns.
* **Pattern syntax** - Used by pathname expansion, `case`, the `==` and `!=` operators of `[[ ]]`, and the pattern forms of parameter expansion.
  * `*` matches any string, `?` one character, `[...]` one character from a set (ranges such as `a-z`; classes `[:alnum:]`, `[:alpha:]`, `[:blank:]`, `[:cntrl:]`, `[:digit:]`, `[:graph:]`, `[:lower:]`, `[:print:]`, `[:punct:]`, `[:space:]`, `[:upper:]`, `[:xdigit:]`; leading `!` or `^` negates; `]` first is literal).
  * Equivalence classes, collating symbols, and reversed ranges match nothing. An unclosed `[` is literal. A backslash or quotes make a character literal.
  * Outside pathname expansion `*` and `?` also match a slash and a leading dot. `nocasematch` makes `case`, `[[ ]]`, and `=~` ignore case. The pattern of `[[ ]]` always accepts the extended forms.

#### Redirections

Redirections open, duplicate, or close descriptors for a command and process left to right. A number before the operator selects the descriptor, and `{var}` before it allocates a descriptor numbered `10` or above and stores the number in `var` (an array element is allowed; the number stays in `var` after the descriptor closes).

| Operator | Semantics |
| --- | --- |
| `[n]<word` | Opens `word` for reading on `n` (default `0`). |
| `[n]>word`, `[n]>\|word` | Opens `word` for writing on `n` (default `1`), creating or truncating it; `>` respects `noclobber` (refuses an existing regular file) and `>\|` overrides it. |
| `[n]>>word` | Appends. Inside a loop body the file opens once for the whole loop, so writes continue to the original file after it is renamed or removed. |
| `[n]<>word` | Opens for reading and writing on `n` (default `0`). |
| `&>word`, `>&word`, `&>>word` | Redirects standard output and standard error to a file (`&>` and `&>>` always open a file). Rejected in `sh`, where `&>` parses as `&` then `>` and `>&` takes only a descriptor. |
| `[n]<&m`, `[n]>&m` | Duplicates descriptor `m` (a word that is not a single open descriptor is an error). |
| `[n]<&-`, `[n]>&-` | Closes `n`. |
| `[n]<&m-`, `[n]>&m-` | Moves `m` to `n` and closes `m`. |
| `[n]<<word`, `[n]<<-word` | Here-document (see below). |
| `[n]<<<word` | Here-string: the expanded word plus a newline is the input. Rejected in `sh`. |

* **Target expansion** - The target undergoes the same expansions as an argument, except that pathname expansion yielding more than one result is not performed. A target that expands to several words is an error, and so is an unset variable in the `kosh` mood.
* **Failure status** - A failed redirection fails the command with `1` in the Bash moods and `2` in `kosh` and `sh`.
  * On a special builtin or compound command it ends the script with `1` in `kosh` and `2` in `sh`. The Bash moods continue, including `bash-posix` (where Bash itself exits).
* **Expansion scope** - In `kosh` and `sh` all targets expand in the parent shell. In Bash moods the redirections of a program, of a command with no command word, and of a subshell expand in the child (a side effect such as `${v:=x}` does not reach the shell), and those of a builtin, function, group, and `exec` expand in the shell.
* **Device paths** - `/dev/tcp` is not emulated; a device path opens as an ordinary file system path.

##### Here-documents

* **Body** - `[n]<<word` makes the lines after the command, up to a line that is exactly `word`, the input. A command can have several here-documents, read in order. An unterminated here-document ends at the end of the input.
* **Quoted delimiter** - When any part of `word` is quoted, the lines are literal.
* **Unquoted delimiter** - The lines undergo parameter expansion, command substitution, and arithmetic expansion. A backslash keeps its meaning only before `$`, `` ` ``, `\`, or a newline, and a backslash before a newline joins lines.
* **`<<-`** - Strips leading tabs from the lines and the terminator.

#### Arithmetic evaluation (`$(( ))`, `(( ))`, `let`)

* **Contexts** - `$(( ))`, `(( ))`, `let`, the C-style `for` loop, array subscripts, substring offsets, and assignments to integer variables.
* **Evaluation** - Precedence follows Bash (unary minus binds tighter than `**`; `**` groups from the right). Parentheses group, comparison and logical operators yield `1` or `0`, and `&&` and `||` evaluate the right side only when needed. `**=` and `0b` binary literals are Koshka extensions.

| Operators, from highest to lowest precedence | Meaning |
| --- | --- |
| `id++ id--` | post-increment, post-decrement |
| `++id --id` | pre-increment, pre-decrement |
| `- +` | unary minus, unary plus |
| `! ~` | logical and bitwise negation |
| `**` | exponentiation, grouping from the right |
| `* / %` | multiplication, division, remainder |
| `+ -` | addition, subtraction |
| `<< >>` | shifts |
| `<= >= < >` | comparisons |
| `== !=` | equality |
| `&` | bitwise and |
| `^` | bitwise exclusive or |
| `\|` | bitwise or |
| `&&` | logical and |
| `\|\|` | logical or |
| `a ? b : c` | conditional |
| `= *= /= %= += -= <<= >>= &= ^= \|= **=` | assignment |
| `,` | sequence |

* **Numbers** - Decimal, octal (leading `0`), hexadecimal (`0x`), binary (`0b`), or base `b` from `2` to `64` as `b#digits` (digits beyond `9` come from letters, `@`, `_`).
* **Variables** - A variable is replaced by its value, which is evaluated as an expression in turn (`x=1+2` makes `x*2` equal `6`). An empty value counts as `0`, and `$` or a backquote inside a value is an error. A subscript taken from a value is expanded again (with `q='a[$i]'`, `$((q))` is the element at `i`).
* **Quotes** - Outside `sh`, double quotes inside `$(( ))`, `(( ))`, a C-style `for` header, a subscript, or a substring offset are removed first (`$(( "1 + 2" * 3 ))` is `7`). `sh` rejects every quote there.
* **Errors** - Division by zero, an invalid digit, a missing operand, an unexpected token, and a negative shift count are errors (termination per mood under [Error termination matrix across moods](#shell-errors)). Shifts, bitwise operators, and the exponent need integers, and division of two integers is integer division.
* **Precision** - `interpreter.arithmetic_uses_big_numbers` (`--enable-extended-arithmetic`, on in the `kosh` mood) selects arbitrary-precision integers and finite decimals (`$((2**100))` is exact; `$((7.0/2))` is `3.5`). Without it arithmetic uses wrapping 64-bit signed integers.
* **Unset variables** - An unset variable in an expression is an error in the `kosh` mood and evaluates as `0` in the compatibility moods.

#### Conditional expressions (`[[ ]]`, `test`, `[ ]`)

| Primary | Semantics |
| --- | --- |
| `-a file`, `-e file` | Tests existence (unary `-a` only in `[[ ]]`). |
| `-b`, `-c`, `-d`, `-f`, `-p`, `-S` | Tests for block device, character device, directory, regular file, FIFO, socket. |
| `-h`, `-L` | Tests for symbolic link. |
| `-g`, `-u`, `-k` | Tests set-group-id, set-user-id, sticky bit. |
| `-r`, `-w`, `-x` | Tests whether the shell can read, write, execute the file. |
| `-O`, `-G` | Tests ownership by the effective user, group. |
| `-s` | True for a regular file with nonzero size, false for a directory. |
| `f1 -ef f2`, `f1 -nt f2`, `f1 -ot f2` | Same file, newer, older (`-nt` and `-ot` require both files to exist). |
| `-t [fd]` | True when `fd` is open on a terminal (`[ -t ]` without operand is true). |
| `-z s`, `-n s`, `s` | Empty string, nonempty string; a lone string is true when not empty. |
| `s1 = s2`, `s1 == s2`, `s1 != s2` | Compares strings (in `[[ ]]` the right side is a pattern unless quoted; `test` and `[` accept `==` only in the Bash moods and reject it with a hint to use `=` in `kosh` and `sh`). |
| `s1 < s2`, `s1 > s2` | Compares sort order. |
| `-v name` | True when a variable, array element, or positional parameter is set (`[[ ]]` only). |
| `-R name` | True for a name reference with a target (not in `sh`). |
| `-o option` | Tests a `set -o` option (`[[ ]]` in every mood; `test` and `[` only in the Bash moods). |
| `s =~ regex` | Matches an ERE (`[[ ]]` only; a quoted part is literal; `.` matches newlines; `BASH_REMATCH` receives the match and groups, an unmatched group is empty; an invalid regex is an error with status `1`, where Bash returns `2`). |
| `-eq`, `-ne`, `-lt`, `-le`, `-gt`, `-ge` | Compares integers (in `[[ ]]` each operand is an arithmetic expression; in `test` a decimal integer, blanks allowed around it). |
| `!`, `( )`, `&&`, `\|\|` | Negation, grouping, and combination inside `[[ ]]`. |

* **Words** - Inside `[[ ]]` words undergo no field splitting or pathname expansion.
* **`test` combination** - `test` and `[` use binary `-a` and `-o` for and/or, and the outcome depends on the number of arguments, as POSIX specifies.
* **Malformed `[[ ]]`** - Fails with status `2`.

### Aliases, functions, and command execution <a id="aliases"></a> <a id="functions"></a> <a id="command-execution"></a> <a id="command-execution-environment"></a> <a id="environment"></a>

* **Aliases** - Replace the first word of a simple command with a string, expanded when the command runs (an alias defined earlier on the same line or in the same compound command applies at once; an alias in a function body takes its value at each call).
  * A replacement ending with a blank also checks the next word. Chains are followed, a word already being replaced is not replaced again, and quoted words, words with a backslash, words produced by an expansion, and reserved words are never aliases.
  * Expansion requires `expand_aliases`, on in `kosh`, `bash-posix`, and `sh`, and in `bash` only in an interactive shell, not in a script (see [Default option states across moods](#moods)).
  * `alias` and `unalias` manage them, and `BASH_ALIASES` holds them.
* **Functions** - Run in the current shell with their own positional parameters (`$0` stays the script name). Status is the `return` value or the status of the last command.
  * Variables are global unless `local` or `declare` makes them local, and a local is visible to called functions. `local` without a value keeps the outer value visible in `kosh` and `sh`, and starts the variable unset in the Bash moods.
  * `FUNCNAME`, `BASH_SOURCE`, `BASH_LINENO`, and `BASH_COMMAND` expose the call stack. Calls nest at most `900` deep and sourced files `400` deep (less when the stack runs short); a deeper call fails with a diagnostic.
  * A function called from a guarded and-or operand or a negation keeps `errexit` suppressed throughout its body.
  * The `RETURN` trap fires at the end of every function body in `kosh`, for bodies that `functrace` follows in the Bash moods, and is rejected in `sh`.
  * `export -f` exports a function as `BASH_FUNC_name%%` for Bash children. Koshka does not import exported functions.
* **Command lookup** - A word holding a slash runs that path directly. Other words resolve in the order under Command execution lifecycle at the top of this manual. Running a command or `hash` caches its path until `PATH` is assigned, `hash -r` runs, or a directory change affects a relative `PATH` entry.
  * A program runs in a separate process with the exported variables as environment. A command not found returns `127`, a file that cannot run returns `126`, and a path ending in a directory separator fails with `126` when the target is not a directory.
  * A file without a recognized executable format runs as a shell script in a new shell, without the variables, functions, and traps of the caller.
* **Directory commands** - In an interactive shell a single directory path in command position changes to that directory as `cd` would, unless a function, builtin, program, or bundled utility of that name exists. A word of only dots moves up one directory per dot after the first. Controlled by `autocd` (on in every mood, never affects scripts).
* **Execution environment** - Consists of open files, working directory, creation mask, resource limits, traps, variables and attributes, functions, options, aliases, directory stack, and job table.
  * A subshell, command substitution, separate pipeline stage, or asynchronous list receives a copy whose changes do not reach the shell. Caught traps reset to defaults there, ignored traps stay ignored, and a subshell cannot wait for a job of its parent.
  * A program receives the open descriptors, working directory, creation mask, resource limits, ignored signals, and exported variables. Every descriptor the shell opened for itself is closed in the program.
* **Environment variables** - At startup the shell imports its environment and marks every variable for export. Inherited variables and those marked with `export` or `declare -x` pass to every program, and a prefix assignment adds a variable for one command. Shell-maintained values (`SECONDS`, `IFS`, `SHELLOPTS`, the Koshka variables) are not taken from the environment. Names beginning with `KOSH_INTERNAL_` are reserved.
  * On Windows, names are case-insensitive and a value holds at most `32,766` UTF-16 units, so longer values reach a child truncated.

### Error handling, exit statuses, signals, and job control <a id="shell-errors"></a> <a id="exit-status"></a> <a id="signals"></a> <a id="job-control"></a> <a id="history-expansion"></a> <a id="event-designators"></a> <a id="word-designators"></a> <a id="modifiers"></a>

#### Error termination matrix across moods

| Error category | `kosh` mood | `bash` mood | `bash-posix` mood | `sh` mood |
| --- | --- | --- | --- | --- |
| Syntax error in script, `-c`, `eval`, or `source` | Rejects a script or `-c` string with `2`; in `eval`/`source`, fails only that command with `2`. | Rejects script/`-c` with `2`; in `eval`/`source`, fails only that command with `2`. | Aborts non-interactive shell with status `2`. | Aborts non-interactive shell with status `2`. |
| Pre-run static analysis `strict` / `lenient` error | Aborts before execution with status `1`. | Off by default (`-W` emits warnings). | Off by default (`-W` emits warnings). | Off by default (`-W` emits warnings). |
| Arithmetic error in `$(( ))`, subscript, slice, or integer assignment | Aborts script with status `1`. | Abandons current input line (`$? = 1` on next line; does not trigger `set -e` or `ERR` trap). An error in `(( ))`, `let`, a `[[ ]]` operand, or a C-style `for` header fails only that command with `1`. | A `$(( ))` error outside a here-document aborts a non-interactive shell (`127` in `-c`, `1` in script). Subscript, slice, and integer-assignment errors abandon the line, and `(( ))`, `let`, `[[ ]]` operands, and `for` headers fail only that command with `1`. | Outside here-doc, aborts non-interactive shell with status `2`. |
| Expansion error: `${v?}`, `set -u`, or unknown `${v@Z}` | Aborts script with status `1`. | Aborts non-interactive shell (`127` in `-c`, `1` in script). | Aborts non-interactive shell (`127` in `-c`, `1` in script). | Aborts non-interactive shell with status `2`. |
| Bad substitution (`${x:}`) or read-only assignment | Aborts script with status `1`. | Abandons current input line. | Aborts non-interactive shell (`127` in `-c`, `1` in script). | Aborts non-interactive shell with status `2`. |
| Redirection failure on special builtin or compound command | Aborts script with status `1`. | Fails command with status `1` and continues. | Fails command with status `1` and continues. | Aborts non-interactive shell with status `2`. |
| Builtin usage error or operational failure | Usage errors (except `declare`, `trap`, `getopts`, `compopt`, `printf`) and operational failures of `bg`, `cd`, `dirs`, `fg`, `kill`, `let`, `local`, `mapfile`, `popd`, `pushd`, `read`, `set`, `test`, `ulimit`, `umask`, `z` abort script. | Sets nonzero status and continues (special builtin errors abort only where specified). | Special builtin errors abort or continue per Bash rules. | Special builtin or `local` error aborts with `2` (unless prefixed by `command`); regular builtin error returns `2` and continues. |

* **Syntax errors** - The parser reports every syntax error in one run. In `kosh`, analysis can also reject a script before it runs with status `1` (see [Static analysis and runtime diagnostics](#diagnostics)). An interactive shell rejects the line and reads the next one.
* **Kosh mood** - Every arithmetic and expansion error ends the script with `1`.
  * Also ends it: a failed redirection of a special builtin or compound command (`1`), a write error on standard output (`1`), an invalid regular expression (`1`), and a malformed `[[ ]]` (`2`).
  * A failure of another builtin, such as a missing alias, only sets the status.
* **Bash moods** - An arithmetic error in `$(( ))`, a subscript, a substring offset, or an integer-variable assignment abandons the rest of the input line, as in Bash.
  * Remaining commands on the line do not run, `$?` is `1` on the next line, and neither `set -e` nor an `ERR` trap acts on it. An `eval`, sourced file, subshell, and command substitution each count as a separate line.
  * A glob matching nothing under `failglob` abandons the line in `bash` and `bash-posix`. An interactive shell abandons the line instead of ending.
* **Eval, source, and containment** - In `sh` and `bash-posix`, a syntax error in an `eval` string or sourced file ends a non-interactive shell with `2`; in `kosh` and `bash` it fails only that command with `2`. An error that ends a non-interactive shell passes through `eval` and `source`, as in Bash and dash. A subshell, command substitution, trap action, and startup file contain it, and the command that started them receives the status.
* **Special builtins and exec** - In `sh`, an error of a regular builtin (missing `cd` directory, invalid `test` operand) fails that command with `2` and the script continues. An error of a special builtin or `local` (`shift` past the last parameter, `export` of an invalid identifier) ends a non-interactive shell with `2` unless `command` runs it. A failed `exec` ends a non-interactive shell in every mood.
* **Standard exit codes** - A command status is `0` to `255`.
  * `0` success (also an empty command, a compound command that ran no list, and an asynchronous command).
  * `1` general failure (false condition, failed builtin, analysis that rejected a script).
  * `2` usage error of a builtin or the shell, or syntax error.
  * `126` found but cannot be run (directory, file without execute permission, path ending in a separator that is not a directory).
  * `127` command not found, or a script operand that cannot be opened.
  * `128 + n` ended by signal `n` (`130` `SIGINT`, `141` `SIGPIPE`, `143` `SIGTERM`).
* **Shell exit status** - Status of the last command run, or the `exit` operand modulo `256`. When an expansion error ends a non-interactive shell the status is `1` in `kosh` and `2` in `sh`. In the Bash moods an unset parameter under `nounset`, `${name:?}`, and a bad substitution that ends the shell return `127` from a `-c` string, as in Bash, and `1` from a script.
* **Tool exit statuses** - `--lint`, `--format`, and `--apply` return `0` when every input succeeds (warning-only findings included), `1` when an input cannot be read, parsed, formatted, or replaced or analysis reports an error, and `2` for an invalid option combination.
* **Signals and PID 1 reaper** - The shell ignores `SIGPIPE` (reset to default in child processes), so a producer killed by it returns `141`. Interactive shells also ignore `SIGTERM`, `SIGQUIT`, `SIGTSTP`, and `SIGTTIN`.
  * `SIGINT` interrupts the running command or the line being edited and reports `Interrupted` with status `130`.
  * A shell that does not trap `SIGHUP`, and a non-interactive shell that does not trap `SIGTERM` or `SIGQUIT`, is terminated by the signal, so its parent sees `128` plus the signal number.
  * As process 1, `kosh` reaps orphaned children it did not start (before each command and while it waits). On `SIGTERM` it sends `SIGTERM` then `SIGCONT` to every other process, interrupts the running command, waits for every remaining child, and exits with `143`.
* **Signal reports and traps** - A command ended by a signal prints a line such as `[Process 2585: Killed, signal 9]` on standard error, once per killed pipeline stage, and its status is `128` plus the signal number (`SIGPIPE` and `SIGINT` print no line).
  * `trap` sets the action of a signal or of `EXIT`, `ERR`, `DEBUG`, `RETURN`. A trapped signal runs its action at the next command boundary.
  * The `CHLD` trap runs once per reaped child (under `set -m` a job that stops counts too).
  * A trapped signal that arrives while `wait` blocks makes `wait` return `128` plus the signal number, and the job keeps running.
  * On Windows a background process runs in its own console process group.
* **Jobs** - A job is a background pipeline or list, or a stopped foreground pipeline. Every stage of an asynchronous pipeline belongs to one job, and POSIX stages share a process group.
  * The final stage determines `$!`, the status, and the job identity (under `pipefail` the rightmost failing stage may determine the status).
  * A stopped stage is recorded even when it is not the last, and `fg` and `bg` resume every stage.
  * Job control is on in an interactive shell, where Ctrl-Z stops the foreground job.
* **Job specs** - `%n` job `n`; `%+`, `%%`, `%` the current job; `%-` the previous job; `%string` the job whose command begins with `string`; `%?string` the job whose command contains it. The lowest-numbered job wins among several matches, and a bare number is also a job number. In `kosh` an unquoted `%?string` is a glob and needs quotes.
* **Job reports** - `jobs` and job notifications print one line per job, as in Bash: job number with `+` (current) or `-` (previous), the process with `jobs -l`, the state, and the command. A job ended by a signal shows its description (`Terminated`), and an exited job shows `Done`, or `Exit` and its status. An interactive shell reports a job that finished or changed state before the next prompt.
* **Remembered statuses** - A job reported by `wait`, `jobs`, or a notification leaves the job table, and a later `wait` on its process returns the remembered status. The shell remembers the `1024` most recent statuses, and `wait` without operands discards them. A subshell, pipeline stage, asynchronous command, and coprocess cannot wait for a job of their parent.
* **Hangup** - The shell never sends `SIGHUP` to its jobs, so background jobs continue after it exits, and it exits even while jobs are stopped. `disown` removes a job from the table.

#### History expansion

* **Scope** - Interactive shells with `histexpand` on (default; `set +H` turns it off) replace each reference before parsing and print the replaced line before running it. Non-interactive shells do not expand history. A reference is an event, an optional word designator, and optional modifiers.
* **Quoting** - Expands inside double quotes. A single-quoted or backslash-escaped `!` stays literal. A `!` stays literal before a blank, `=`, `(`, or a quote, after `$`, inside `[...]` or `${...}`, and in a comment.
* **Errors** - Event not found, bad word specifier, unrecognized modifier, a substitution that finds no match (including `:&` before any substitution), a `:s` without a delimiter after `old`, and a `^old^new^` without a `^` after `old` or whose `old` is absent from the previous event. The message points at the reference, and the line neither runs nor enters the history.

##### Event designators

| Designator | Event |
| --- | --- |
| `!!` | Previous event. |
| `!n` | Event number `n`. |
| `!-n` | Event `n` before the current one. |
| `!string` | Newest event that begins with `string`. |
| `!?string?` | Newest event that contains `string` (the closing `?` may be omitted at the end of the line). |
| `!#` | Current line up to this reference (takes no word designators or modifiers). |
| `^old^new^` | At the start of a line, previous event with the first `old` replaced by `new`, as `!!:s/old/new/` does. |

##### Word designators

A colon separates a designator from the event and may be omitted before `^`, `$`, `*`, and `%`. Words count from `0`, the command. `!$`, `!^`, and `!*` select the last argument, first argument, and every argument of the previous event. A designator that selects a missing word is a bad word specifier.

| Designator | Words |
| --- | --- |
| `:^`, `:$`, `:*` | First argument, last argument, every argument. |
| `:%` | Word matched by the last `!?string?`. |
| `:n` | Word `n`. |
| `:n-m` | Words `n` through `m`. |
| `:-m` | Words `0` through `m`. |
| `:n*`, `:n-$` | Words `n` through the last. |
| `:n-` | Words `n` through the next to last. |

##### Modifiers

| Modifier | Effect |
| --- | --- |
| `:h`, `:t`, `:r`, `:e` | Removes the last path component, keeps only the last component, removes a trailing suffix, keeps only the suffix. |
| `:s/old/new/` | Replaces the first literal `old` (another delimiter may replace the slash, the last delimiter may be omitted at the end of the line, `&` in `new` stands for `old`, `\&` is a literal `&`, an empty `old` reuses the last substitution or search text). |
| `:gs`, `:as` | Replaces every occurrence. |
| `:Gs` | Replaces the first occurrence in each word. |
| `:&` | Repeats the last substitution (`:g&`, `:a&`, `:G&` with the matching scope). |
| `:q`, `:x` | Quotes the text as one argument, or each word separately. |
| `:p` | Prints the result and records it in the history without running it. |

### Standard shell builtins reference <a id="builtins"></a>

A builtin runs inside the shell. Special builtins are `:`, `.`, `break`, `continue`, `eval`, `exec`, `exit`, `export`, `readonly`, `return`, `set`, `shift`, `times`, `trap`, `unset`, and (in Bash moods) `source`. Assignments before a special builtin remain in the shell, as described under [Simple commands](#simple-commands), and their errors follow the rules under [Shell errors](#shell-errors). The `sh` mood hides `let` and `time`.

A builtin given `--help` before its other arguments prints its description, synopsis, and options (in the compatibility moods `echo` prints `--help` and `test` ignores it, and `[ --help` reports a missing `]` in every mood). Unless an entry states otherwise, a builtin returns `0` on success, `1` on failure, and `2` for an unknown option or a wrong number of operands. In the `sh` mood a failing regular builtin such as `cd`, `dirs`, `pushd`, `popd`, `bg`, `fg`, `jobs`, `disown`, `kill`, `umask`, or `ulimit` returns `2` where the other moods return `1`, and a failing special builtin or `local` ends a non-interactive shell with `2`. In the `kosh` mood the failures listed in the [error termination matrix](#shell-errors) end the script.

| Builtin and synopsis | Operational semantics and mood rules |
| --- | --- |
| `: [arg ...]`, `true [arg ...]`, `false [arg ...]` <a id="builtin-noop"></a> <a id="builtin-false"></a> | Does nothing and returns `0` for `:` and `true` (arguments are expanded and ignored, `--help` prints the help); `false` returns `1`. |
| `. [--mood[=]MOOD] file [arg ...]`, `source [--mood[=]MOOD] file [arg ...]` <a id="builtin-source"></a> | Runs `file` in the current shell and returns the status of its last command or the operand of a `return` in it (arguments become the positional parameters while it runs and are ignored in `sh`; `--mood` runs it in `MOOD` instead of the mood its shebang or name selects and restores the previous mood afterwards). With `sourcepath` on (default), a file without a slash is searched in `PATH`, then the current directory. A missing or unreadable file returns `1`, and ends a non-interactive shell with `2` in `sh` and `1` in `kosh`. In `kosh`, a Bash startup file (`.bashrc`, `.bash_profile`, `.bash_login`, `.bash_logout`, `.profile`, `/etc/profile`) runs in `bash` with its options and a file whose shebang selects another shell runs in that mood, while a file without a shebang runs in the current mood. The `kosh` mood returns when the file ends and keeps the variables, functions, and aliases it defined. Analysis does not follow such a file, a syntax error in it fails the command without ending the shell, and the files it sources keep its mood. |
| `[ expr ]`, `test expr` <a id="builtin-test"></a> | Evaluates conditional expression, see [Conditional expressions](#conditional-expressions) (`0` true, `1` false or no expression, `2` malformed expression, missing `]`, or non-decimal integer operand). Rejects `==` with a hint to use `=` in `kosh` and `sh`. |
| `alias [name[=val] ...]`, `unalias [-a] name ...` <a id="builtin-alias"></a> <a id="builtin-unalias"></a> | Defines aliases for names with a value, prints definitions of names without one, or prints every alias as an `alias` command without operands (`alias`), and removes aliases (`unalias`, `-a` every alias). A name may hold any character except `=`. An unknown alias or an empty name returns `1`. |
| `bg [job]`, `fg [job]`, `jobs [-lnprs] [job ...]`, `disown [-ahr] [job ...]` <a id="builtin-bg"></a> <a id="builtin-fg"></a> <a id="builtin-jobs"></a> <a id="builtin-disown"></a> | Job control builtins, see [Job control](#job-control): resume a stopped job in the background (`bg`) or foreground (`fg`, returns the job's status), list jobs (`jobs`: `-l` adds PIDs, `-p` only PIDs, `-r`/`-s` running or stopped jobs, `-n` jobs changed since the last report), or remove jobs from the job table (`disown`: `-a`/`--all` every job, `-r`/`--running` running jobs, `-h`/`--nohup` keeps the job in the table). `bg`, `fg`, and `disown` act on the current job by default. A missing job returns `1` (`2` in `sh`). |
| `wait [-fn] [-p var] [pid\|job ...]` <a id="builtin-wait"></a> | Waits for each process or job and returns the status of the last one, or waits for every child and returns `0` (`-n` next job to finish among the operands or all jobs, `127` when none is left; `-p var` stores the process whose status is returned or unsets the variable; `-f` waits until each job terminates). A stopped job returns `128` plus the stop signal and `wait -n` skips it. A process that is not a child of the shell or a job that does not exist returns `127` with a diagnostic (`2` for a missing job number in `sh`). |
| `suspend [-f]` <a id="builtin-suspend"></a> | Stops the shell until it receives `SIGCONT`. A login shell refuses without `-f`. |
| `bind [options] [keyseq:fn ...]` <a id="builtin-bind"></a> | Accepts readline options (`-lmrvpPsq`, `-f`, `-u`) and operands without changing key bindings and returns `0`; see [Line editing](#line-editing). |
| `break [n]`, `continue [n]` <a id="builtin-break"></a> | Leaves the innermost `n` enclosing loops (`break`) or starts their next iteration (`continue`), default `1`; a count above the nesting depth applies to the outermost loop. Count `0` returns `1` and leaves every enclosing loop in the Bash moods, only the innermost loop in `kosh`, and ends a non-interactive shell with `2` in `sh`. A non-numeric count ends a non-interactive shell with `2` in every mood. Outside a loop, and for a loop in a calling function, the commands leave nothing (the Bash moods print a diagnostic). |
| `builtin [--list] [name [arg ...]]` <a id="builtin-builtin"></a> | Runs builtin `name` even when a function of that name exists (a name that is not a builtin returns `1`; `--list` prints every builtin one per line; without an operand, `kosh` lists the builtins under POSIX, BASH, and Koshka headings). |
| `command [-pVv] name [arg ...]` <a id="builtin-command"></a> | Runs `name` as a builtin or program, skipping functions and aliases, and a special builtin run this way loses its special properties (`-p` searches the default `PATH`, `-v` prints the path, builtin, keyword, function, or alias definition `name` resolves to, `-V` describes it in words). With `-v` or `-V` returns `0` when any operand resolves and `1` when none does (`127` in `sh`). A command that is not found returns `127`, and one that cannot run `126`. |
| `type [-aPptV] name ...` <a id="builtin-type"></a> | Describes how each name is interpreted as a command (`-t` prints `alias`, `keyword`, `function`, `builtin`, or `file`, `-p`/`-P` the path of a program, `-a` every interpretation, `-V` everything the shell holds about the word, such as a function's definition site and body or the description of a builtin or utility). The Bash moods report a function as "is a function" followed by its body, the other moods as "is a shell function". A name that is not found returns `1` (`127` in `sh`). |
| `enable [-adnps] [-f file] [name ...]` <a id="builtin-enable"></a> | Enables builtins and bundled utilities, or disables them (`-n`). Without operands or with `-p` lists enabled builtins and reachable utilities as `enable` commands (`-n` disabled ones, `-a` both, `-s` special builtins only). Each builtin word is separate, so disabling `test` leaves `[` enabled, and a utility alias such as `egrep` is rejected and follows its utility. Lookup skips a disabled builtin and searches `PATH` and the bundled utilities. A disabled bundled utility never resolves as a command, though the `koshkit` builtin still runs it, and a disabled special builtin loses its special properties. A subshell inherits the state, and its changes do not reach the parent. A builtin the mood hides, such as `let` in `sh`, is neither listed nor accepted. Koshka has no loadable builtins, so `-f` with an operand, `-d`, and a name that is not a builtin or utility return `1`. |
| `hash [-rRt] [-p path] [name ...]` <a id="builtin-hash"></a> | Finds each name in `PATH` and remembers its location (`-r` forgets every location, `-R` also searches `PATH` again, `-p path` records `path`, `-t` prints the remembered location). A name that cannot be found returns `1`. |
| `help [-dms] [name ...]` <a id="builtin-help"></a> | Prints the help of each builtin as its `--help` does (`-d` short description, `-s` synopsis, `-m` manual page); without a name, lists the builtins. A name that is not a builtin returns `1`. |
| `caller [n]` <a id="builtin-caller"></a> | Prints line, function, and file of call frame `n` (`0` is the caller of the current function); a frame past the last returns `1`. Without an operand, prints line and file of the caller inside a function, sourced file, or script file (the top level prints `0 NULL`) and returns `1` in a command string or standard input. A non-whole-number operand returns `2`, and a second operand is ignored. |
| `cd [-L\|-P] [dir]` <a id="builtin-cd"></a> | Changes the working directory to `dir` (default `HOME`) and sets `PWD` and `OLDPWD`; `-` changes to `OLDPWD` and prints it. A relative `dir` that does not begin with `.` or `..` is searched in each `CDPATH` directory, and a directory found there is printed. A relative directory resolves against the logical `PWD`, with `..` removing its last component, unless `-P` or the `physical` option resolves symbolic links first (`-L` keeps the logical path). A missing directory or an unset `HOME` or `OLDPWD` returns `1` (`2` in `sh`), and more than one operand returns `2`. An empty operand returns `1` in the Bash moods and is accepted in the other moods. A restricted shell refuses `cd`. |
| `pwd [-LPW]` <a id="builtin-pwd"></a> | Prints the working directory (`-L` logical path, the default unless `physical` is on, `-P` path with symbolic links resolved, `-W` physical path in Windows form with forward slashes as MSYS does, and `-P` elsewhere). |
| `dirs [-clpv] [+N\|-N]` <a id="builtin-dirs"></a> | Prints the current directory and the saved directory stack on one line with `HOME` shown as `~` (`-c` clears the stack, `-l` full paths, `-p` one entry per line, `-v` entries numbered from `0`, `+N` the Nth entry from the top, `-N` the Nth from the bottom). An entry past the end returns `1` (`2` in `sh`). |
| `pushd [-n] [dir\|+N\|-N]` <a id="builtin-pushd"></a> | Pushes `dir` onto the stack and changes to it (`-n` adds `dir` below the current directory without changing directory). Without an operand swaps the two top entries, and `+N`/`-N` rotates the stack so that entry `N` is on top. A missing directory or an empty stack returns `1` (`2` in `sh`). |
| `popd [-n] [+N\|-N]` <a id="builtin-popd"></a> | Removes the top entry, or entry `N`, from the stack and changes to the new top (`-n` removes the entry below the current directory without changing directory). An empty stack returns `1` (`2` in `sh`). |
| `compgen [options] [word]` <a id="builtin-compgen"></a> | Prints the candidates the options generate that begin with `word`, one per line. Sources are `-a` aliases, `-b` builtins, `-c` commands, `-d` directories, `-e` exported variables, `-f` files, `-g` groups, `-j` jobs, `-k` reserved words, `-s` services, `-u` users, `-v` variables, and `-A action` (`alias`, `arrayvar`, `binding`, `builtin`, `command`, `directory`, `disabled`, `enabled`, `export`, `file`, `function`, `group`, `helptopic`, `hostname`, `job`, `keyword`, `running`, `service`, `setopt`, `shopt`, `signal`, `stopped`, `user`, `variable`). Also takes `-W wordlist` (splits the expanded list), `-G glob` (adds pathname matches), `-X pattern` (removes matches, or keeps only matches after a leading `!`; an unescaped `&` stands for the word), `-P prefix`/`-S suffix` (added after matching and filtering), and `-V array` (stores the candidates in an indexed array and prints nothing); `-o`, `-F`, and `-C` are accepted without effect. No candidate returns `1`, and an unknown action returns `2`. |
| `complete [options] [name ...]` <a id="builtin-complete"></a> | Registers how the arguments of each command complete from the actions of `compgen`, `-G`, `-W`, `-F function`, `-C command`, and `-o` options (`-p` prints specs in Bash order, `-r` removes the given specs or all, `-D` default, `-E` empty line, `-I` command word). A missing spec returns `1`, and an unknown `-o` option or `-A` action returns `2` and registers nothing. See the bullets below the table. |
| `compopt [-o\|+o opt] [-DEI] [name ...]` <a id="builtin-compopt"></a> | Enables (`-o option`) or disables (`+o option`) a `complete -o` option; if both forms give the same option, it is disabled. The change applies to each given command's spec, to the first of the `-D`, `-E`, and `-I` specs, or, inside a completion function with neither, to the current completion. Without `-o` or `+o` prints one `compopt` line per spec. An unknown option returns `2`, and a missing spec or a missing target outside a completion function returns `1`. |
| `declare [-aAfFgilnprtux] [name[=val] ...]`, `typeset ...` <a id="builtin-declare"></a> | Sets variables and attributes, local inside a function unless `-g` (`-a` indexed array, `-A` associative array, `-i` integer with arithmetic assignment, `-l`/`-u` lowercase or uppercase conversion, `-r` read-only, `-x` exported, `-n` name reference, `-t` accepted without effect). A plus removes an attribute, except that read-only stays. `-f`/`-F` print functions with or without definitions, and `-p` prints declarations (`-p` or an attribute option without operands lists every variable with those attributes). Without options or operands prints nothing. `-a` with `-A` returns `2`, and a missing variable or function or an invalid identifier returns `1`. See the name reference bullet below the table. |
| `local [-aAilnprux] [name[=val] ...]` <a id="builtin-local"></a> | Makes variables local to the running function with the attributes of the same `declare` options (`local -n` passes a variable of the caller by reference, a `-` operand saves the `set` options until the function returns, `-p` lists local variables). Outside a function returns `1`, and ends a non-interactive shell with `2` in `sh`. |
| `readonly [-aAfp] [name[=val] ...]` <a id="builtin-readonly"></a> | Marks variables read-only after assigning a given value (`-p` or no operand lists them reusably, `-a`/`-A` treat operands as arrays, `-f` marks functions, or lists them with bodies when given alone). Assigning a read-only variable is an error that ends a non-interactive shell in `sh`. |
| `export [-fnp] [name[=val] ...]` <a id="builtin-export"></a> | Marks variables for the environment of later commands after assigning a given value (`-n` removes the mark, `-f` marks functions, `-p` or no operand prints exported variables as `export` commands in `kosh` and `sh` and as `declare -x` commands in the Bash moods). An invalid identifier or a read-only variable returns `1`. |
| `unset [-fnv] name ...` <a id="builtin-unset"></a> | Removes variables or array elements (`-v`), functions (`-f`), or a name reference itself (`-n`). Without an option removes a variable, or a function when no such variable exists. A read-only variable returns `1`, and an unset name succeeds. |
| `echo [-neE] [arg ...]` <a id="builtin-echo"></a> | Joins arguments with spaces and a newline (`-n` omits the newline, `-e` interprets backslash escapes, `-E` disables them). Escapes are interpreted by default in `kosh` (backslashes stay literal without `-e` on Windows) and in `sh`, and in the Bash moods with `xpg_echo`. The `sh` mood accepts only `-n` and treats `\u` as an escape while `\e` and `\x` stay literal. |
| `printf [-v var] format [arg ...]` <a id="builtin-printf"></a> | Reuses `format` while arguments remain. Accepts the C conversions `d`, `i`, `o`, `u`, `x`, `X`, `e`, `E`, `f`, `F`, `g`, `G`, `c`, `s` with flags, width, and precision (`*` takes them from an argument), plus `%b` (backslash escapes), `%q` (shell-quoted), and `%(fmt)T` (strftime, `-1` for the current time). `-v` assigns the result to a variable or array element. The `sh` mood rejects `-v` and prints `%(fmt)T` as written. An invalid numeric argument prints `0` and sets status `1`, a missing format returns `2`, and a width or precision over `16777216` is an error. The numeric locale comes from the first nonempty value of `LC_ALL`, `LC_NUMERIC`, and `LANG` at each call. |
| `eval [arg ...]` <a id="builtin-eval"></a> | Joins arguments with spaces and runs the result in the current shell (status of the last command, `0` for empty input, `2` with an `eval:` location on a syntax error). |
| `exec [-cl] [-a name] [command [arg ...]]` <a id="builtin-exec"></a> | Replaces the shell with `command` (`-a` sets argument zero, `-l` prefixes it with a dash, `-c` clears the environment), or applies its redirections to the shell when no command is given. A command that is not found or cannot run ends a non-interactive shell with `127` or `126` in every mood. A restricted shell refuses `exec`. |
| `exit [n]`, `logout [n]`, `return [n]` <a id="builtin-exit"></a> <a id="builtin-logout"></a> <a id="builtin-return"></a> | Runs the `EXIT` trap and exits with `n` modulo `256` or the last status (`exit`; a non-numeric operand returns `2` without exiting, except in `sh` where the shell exits with `2`, and more than one operand exits with `1`). `logout` behaves as `exit` in a login shell and reports an error and returns `1` elsewhere. `return` leaves the running function or sourced file with `n` modulo `256`; outside them it returns `2`, except in `sh` where it ends a non-interactive shell with `n`. A non-numeric operand returns `2`, and ends a non-interactive shell with `2` in `sh`. |
| `fc [-e editor] [-lnr] [first [last]]`, `fc -s [old=new ...] [command]` <a id="builtin-fc"></a> | Lists (`-l`, previous sixteen events by default, `-n` without numbers, `-r` reversed), edits, and reruns history events selected by number, negative offset, or prefix. The editor comes from `-e`, `FCEDIT`, or `EDITOR`, defaulting to `vi` (`ed` in the POSIX moods); `-` runs the event unedited. `fc -s` applies each literal replacement to one event, prints it to standard error, runs it, and returns its status. A text that matches no event returns `1`. |
| `history [-cnrwaps] [-d selector] [operand]`, `history -S` <a id="builtin-history"></a> | Lists events (newest `N` with a numeric operand), loads the history file (`-r`, only new events with `-n`), writes it (`-w`), appends missing private events (`-a`), replaces the private list with the shared file (`-S`), clears the private list (`-c`), prints (`-p`) or stores (`-s`) its operands, and deletes events by number, negative offset, or inclusive range (`-d`). A restricted shell refuses file operands. |
| `getopts optstring name [arg ...]` <a id="builtin-getopts"></a> | Parses the next option from the arguments or positional parameters into `name`, its argument into `OPTARG`, and the next index into `OPTIND`. A letter followed by `:` takes an argument. An unknown option sets `name` to `?` and prints a diagnostic, which `OPTERR=0` or a leading `:` in `optstring` silences (the leading `:` also stores the letter in `OPTARG` and sets `name` to `:` for a missing argument). Returns `0` while options remain and `1` at their end. |
| `kill [-s sig \| -n num \| -sig] pid\|job ...`, `kill -l [sig\|status ...]` <a id="builtin-kill"></a> | Sends a signal (`SIGTERM` by default, given by number or by name with or without `SIG`) to each process or job. `-l` lists signals or translates numbers, statuses above `128`, and names. No operand returns `2`, and an invalid signal or a failed delivery returns `1` (`2` for an invalid signal in the `sh` mood). |
| `let expr ...` <a id="builtin-let"></a> | Evaluates each expression as described under [Arithmetic evaluation](#arithmetic-evaluation), returning `0` when the last value is nonzero and `1` when it is zero or absent. The `sh` mood has no `let`. |
| `mapfile [options] [array]`, `readarray [options] [array]` <a id="builtin-mapfile"></a> | Stores lines of standard input in an indexed array (`MAPFILE` by default). `-t` strips delimiters, `-n` limits the count (`0` for every line), `-s` skips leading lines, `-O` sets the first index, `-d` sets the delimiter byte (NUL when empty), `-u` reads a descriptor, and `-C` runs a callback with the index and line every `-c` lines (`5000` by default). |
| `newgrp [group]` <a id="builtin-newgrp"></a> | Replaces the shell with the `newgrp` program from `PATH` (`127` when missing, `126` when it cannot run). A restricted shell refuses `newgrp`. |
| `read [-ers] [-a array] [-d delim] [-n\|-N count] [-p prompt] [-t timeout] [-u fd] [-q] [name ...]` <a id="builtin-read"></a> | Reads a line and splits it at `IFS`, giving one field to each `name` and the rest of the line to the last (`REPLY` without a name). `-r` keeps backslashes, `-a` fills an indexed array, `-d` sets the delimiter (NUL when empty), `-n` sets a maximum byte count, `-N` reads exactly that many bytes unsplit, `-t` sets a fractional timeout, `-p` prompts at a terminal, `-s` disables echo, `-u` reads a descriptor, `-q` reads one byte and returns `0` for `y` or `Y`, and `-e` is accepted without effect. Returns `1` at end of input (even after storing a partial line) and `142` after a timeout. The `sh` mood requires a name and accepts only `-r` and `-p`. |
| `set [-+abefhkmnprtuvxBCEHPTIARNSGW] [-+o option] [-M mood] [-L moods] [--] [arg ...]` <a id="builtin-set"></a> | Changes options (a minus enables, a plus disables) and replaces the positional parameters with the arguments after the options or after `--` (`-` also ends the options). Without arguments prints the shell variables as sorted assignments; `set -o` prints option states and `set +o` prints reusable commands. `set -o option` accepts only Bash spellings, and a long option such as `--mood` is rejected with a pointer to `koshconf`. `-r` turns restricted mode on and `+r` fails with `1`. An unknown option returns `1` in the Bash moods and ends a non-interactive shell with `1` in `kosh` and `2` in `sh`. In `kosh`, a value other than the one held for `nounset`, `pipefail`, `failglob`, or `nullglob` is an error that ends the script with `1`. Letters and Koshka forms are listed below the table. |
| `shift [n]` <a id="builtin-shift"></a> | Discards the first `n` positional parameters (default `1`). A count above `$#` or below `0` returns `1`, and ends a non-interactive shell with `2` in the `sh` mood. |
| `shopt [-pqsuo] [option ...]` <a id="builtin-shopt"></a> | Shows or changes the options listed under [`shopt` options](kosh_5.md#shopt-options). `-s` enables and `-u` disables, or lists the options that are on or off without operands. Without `-s` or `-u`, prints each state and returns `1` when one is off. `-p` prints reusable commands, `-o` selects the `set -o` options, and `-q` prints nothing. An unknown option returns `1` after the valid ones apply. |
| `time [-p] [-R] pipeline`, `times` <a id="builtin-time"></a> <a id="builtin-times"></a> | Times a pipeline (`time`, a reserved word, see [Pipelines](#pipelines); hidden in `sh`) or prints the user and system times of the shell, then of its children (`times`). |
| `trap [-lp] [[action] condition ...]` <a id="builtin-trap"></a> | Sets `action` for each signal (by number or name, with or without `SIG`) or condition: `EXIT` or `0` at exit, `ERR` after a failing command where `errexit` would act, `DEBUG` before each simple command, and `RETURN` at the end of a function or sourced file. An empty action ignores the signal and `-` restores the default. Without arguments or with `-p`, prints reusable `trap` commands, and `-l` lists signals. A trap action runs with the status of the preceding command and leaves that status unchanged. An invalid signal returns `1`. The `sh` mood rejects `ERR`, `RETURN`, `-l`, and `-p`. |
| `ulimit [-HSa] [-cdflmnpstuvw] [limit]` <a id="builtin-ulimit"></a> | Shows or sets a resource limit (file size `-f` by default), with `-H` or `-S` selecting the hard or soft limit and `-a` printing all. The resources are core size (`-c`), data size (`-d`), file size (`-f`), locked memory (`-l`), resident set (`-m`), open files (`-n`), pipe size (`-p`), stack size (`-s`), CPU seconds (`-t`), processes (`-u`), virtual memory (`-v`), and file locks (`-w`). A limit is a number or `unlimited`, and an invalid limit or one above the hard limit returns `1` (`2` in `sh`). |
| `umask [-pS] [mask]` <a id="builtin-umask"></a> | Shows or sets the file creation mask in octal or symbolic form (`u=rwx,g=rx,o=`). `-S` prints it symbolically and `-p` as a reusable command. An invalid mask returns `1` (`2` in `sh`). |

* **`set` option letters** - `-a` allexport, `-b` notify (reports a finished job at once while the editor waits), `-e` errexit, `-f` noglob, `-h` hashall (accepted without effect), `-k` keyword (places assignment arguments in the command environment), `-m` monitor (job control), `-n` noexec, `-p` privileged, `-t` onecmd (stops after one top-level command), `-u` nounset, `-v` verbose, `-x` xtrace, `-B` braceexpand, `-C` noclobber, `-E` errtrace, `-H` histexpand, `-P` physical, `-T` functrace.
  * Options without a letter are `emacs`, `vi`, `history`, `ignoreeof`, `interactive-comments`, `nolog` (accepted without effect), `pipefail`, and `posix`. `set -o posix` selects the `bash-posix` mood, and `set +o posix` selects `bash` from `bash-posix` or `sh`. The options are described under [`set -o` options](kosh_5.md#set-o-options).
* **`set` Koshka forms** - `set -M MOOD` and `set -L MOOD,...` select the mood and source the startup files of other moods as the command-line options do; without a value they print the mood or the initialized moods.
  * `-I`, `-A`, `-R`, `-N`, `-S`, and `-G` set `interpreter.mimic_shebang`, `debug.print_syntax_tree`, `debug.print_lexed_word_escapes`, `debug.report_every_exit_code`, `debug.print_evaluation_statistics`, and `debug.print_memory_report_at_exit`. `-W`, `-WW`, and `-WWW` set `optimizer.warning_level`. Every other Koshka setting belongs to `koshconf`.
* **Name references (`declare -n`)** - A reference holds the target variable in its value, which may carry a subscript. Expansion, assignment, append, export, and unset act on the target, following chains, with the target's read-only, restricted, integer, and case rules.
  * `${!name}` gives the target, `unset -n` and `+n` remove the reference, and `[[ -R name ]]` is true for a reference with a value. A reference without a value binds at its first assignment.
  * A self-reference is an error at top level. A circular chain expands as unset without a message, and an assignment through it is an error. A read-only variable, `PATH`, `IFS`, and a dynamic variable other than `RANDOM` and `SECONDS` cannot become a reference.
* **`complete` specs** - A spec offers the candidates of its actions, `-G` glob, `-W` words, and, on an explicit Tab, the `COMPREPLY` of its `-F` function and the output lines of its `-C` command, in that order. The `-C` command runs as a command substitution with the command, the word, and the previous word as arguments and `COMP_LINE`, `COMP_POINT`, `COMP_KEY`, and `COMP_TYPE` exported. A completion function receives the same arguments and the `COMP_` variables described under [Shell variables](#shell-variables).
  * Trailing spaces are removed from each `COMPREPLY` entry, the spec's `-X`, `-P`, and `-S` apply, and with the `filenames` option a slash is appended to each directory. The ghost suggestion never runs a function or command.
* **`complete -o` options** - `default` and `bashdefault` fall back to filenames and to the shell completion when the spec finds nothing, `filenames` treats candidates as filenames, `dirnames` adds matching directories when nothing else matched, and `plusdirs` adds them always. `fullquote` quotes every candidate, `noquote` none, `nosort` keeps the generated order, and `nospace` adds no space.
  * A spec quotes inserted candidates only when it generates filenames, as in Bash. An empty result falls through to the next completion source.
* **`complete -D`, `-E`, `-I`** - With `progcomp_alias` on, a command without a spec uses the spec of the first word of its alias, and an explicit Tab follows a `PATH` command to its symlink target. A command still without a spec uses the `-D` spec, and when its function returns `124` completion restarts with the spec it registered.
  * `-E` serves an explicit Tab on an empty line and `-I` the command word. Their functions receive `_EmptycmD_` and `_InitialWorD_` as the command. Of several of `-D`, `-E`, and `-I`, the first in that order applies.

### Locale <a id="locale"></a>

The first nonempty value of `LC_ALL`, `LC_CTYPE`, and `LANG` selects the character locale. A `UTF-8` or `utf8` codeset selects UTF-8 when the platform supports it, and anything else selects bytes.

* **Character semantics** - In UTF-8, `?` and each step of `*` consume one character, bracket expressions compare code points, `${#v}` and `${v:offset:length}` count characters, and `${v^}`, `${v,}`, and `${v~}` change the case of multibyte letters. This applies to pathname expansion, `case`, `[[ == ]]`, `${v#pat}`, `${v%pat}`, `${v/pat/rep}`, `${v^pat}`, and `compgen -X`. Completion always matches by UTF-8 sequence.
* **Collation and numbers** - In the Bash moods, pathname expansion sorts by the collation order of `LC_ALL` or `LC_COLLATE`, read from the environment the first time a pattern is sorted. The first nonempty value of `LC_ALL`, `LC_NUMERIC`, and `LANG` selects the numeric locale of `printf`.

### Restricted shell <a id="restricted-shell"></a>

A shell started with `-r` or as `rbash` enters restricted mode after its startup files finish. No setting turns restricted mode off, child shells inherit it, and sourced scripts keep it. A shell script found through `PATH` and run as a command runs without it.

| Restricted area | Rejected operations |
| --- | --- |
| Directories and variables | Directory changes (including `pushd`, `popd`, and `z`), and changes to `SHELL`, `PATH`, `ENV`, and `BASH_ENV`, which become read-only. |
| Commands and paths | A directory separator in a command word or `source` operand, `exec`, `newgrp`, `bench`, `assimilate`, `slurp`, `hash -p`, `command -p`, `enable -f`, `enable -d`, and every form of `koshkit`. |
| Redirections | `>`, `>\|`, `<>`, `>>`, `&>`, `&>>`, and `>&` to a file. Numeric descriptor duplication stays available. |
| Settings | `set -M`, `set -I`, `set +I`, `set +r`, `source --mood`, history file operands, and the `koshconf` forms `create`, `set`, `load`, and `save --persist`. `set -o posix` stays available. |

* **Bundled utilities** - A command word falls back only to a bundled utility that a startup file enabled with `enable`. After startup, `enable` can disable a builtin but cannot enable one or change a bundled utility. An enabled utility that starts other programs, such as `env`, `xargs`, `find`, `nohup`, `timeout`, or `watch`, can start any program, so a startup file for a restricted shell should enable only utilities that start none.
* **Settings files** - A restricted shell ignores and removes `KOSHCONF` with a warning and applies only presentation and diagnostic options from the settings files. It ignores the mood, semantic options, `editor.history.file_path`, and `editor.history.max_entries` there with a warning.

### Related shells <a id="related-shells"></a>

The `bash`, `dash`, `sh`, `fish`, and `busybox` manuals describe their respective shells.
