# Koshka shell reference (kosh_1)

> Declarative and operational specification for the Koshka command shell, static analyzer, formatter, language server, interactive line editor, and bundled Koshkit utilities across Linux, macOS, and Windows[cite: 4].

2026-10-26[cite: 4]

Prerequisites - Familiarity with POSIX shell fundamentals and [kosh(5) configuration settings](kosh_5.md)[cite: 4].

`kosh` is the Koshka command shell[cite: 4]. The binary integrates a POSIX and Bash-compatible command interpreter, a pre-execution static analyzer, a source code formatter, a Language Server Protocol (LSP) daemon, and the `koshkit` suite of bundled POSIX and system utilities[cite: 4]. Identical parser grammar and execution semantics operate across Linux, macOS, and Windows[cite: 4].

## Core system primitives

| Primitive | Functional role |
| --- | --- |
| `kosh` | Primary shell interpreter, static analyzer (`--lint`), formatter (`--format`), and language server (`--as-language-server`)[cite: 4]. |
| `mood` | Execution profile (`kosh`, `bash`, `bash-posix`, or `sh`) governing parser dialect, static analysis enforcement, option locks, and error termination[cite: 4]. |
| `koshconf` | Builtin configuration manager that reads, mutates, and persists shell settings and `KOSHCONF` payloads[cite: 4]. |
| `koshkit` | Builtin and standalone dispatcher for 108 bundled POSIX, system inspection (`evil*`), and diagnostic (`good*`) utilities[cite: 4]. |
| `slurp` | Builtin modal/modeless terminal text editor supporting Emacs, Vi insert, and Vi normal keybindings[cite: 4]. |

## Command execution lifecycle

1. **Invocation and input selection** - The `kosh` binary resolves `KOSH_FLAGS` and command-line flags, sets the initial `mood` from `--mood` or the invocation basename (`sh`, `dash`, `bash`, `rbash`), and selects the input source in strict priority order (`-s`, `-c`, script file operand, `-i` or terminal stdin, or non-interactive stdin)[cite: 4].
2. **Configuration and startup loading** - Unless bypassed by `-Q`, `--no-config`, `--lint`, `--format`, `--as-language-server`, privileged mode (`-p`), or setuid/setgid execution, the shell loads `/etc/kosh.conf` (or `%ProgramData%\kosh\kosh.conf`), `$XDG_CONFIG_HOME/kosh/kosh.conf` (or `~/.config/kosh/kosh.conf`), and `KOSHCONF`, then sources the startup files for the active `init_moods` or session mood[cite: 4].
3. **Lexing, parsing, and static analysis** - The parser builds an abstract syntax tree (AST) and reports all syntax or dialect errors in a single pass[cite: 4]. In the default `kosh` mood (or when `-W` is set in compatibility moods), the static analyzer inspects the AST before execution and rejects scripts with `strict` or `lenient` errors (exit status `1`)[cite: 4].
4. **Word expansion** - For each simple command, words expand in fixed order: brace expansion, tilde expansion, parameter/arithmetic/command/process expansion (left to right), field splitting (`IFS`), pathname globbing, and quote removal[cite: 4].
5. **Command resolution and dispatch** - Commands containing a slash execute directly[cite: 4]. Words without a slash resolve in strict order: shell functions, enabled builtins, hashed `PATH` entries, `PATH` search, and (in the `kosh` mood or under `--enable-koshkit`) bundled `koshkit` utilities[cite: 4]. Interactive shells with `autocd` enabled fall back to changing directories if the word matches a directory path[cite: 4].

## Install and verify Koshka releases <a id="downloads"></a>

Download the release binary matching the target operating system and CPU architecture, replacing `X.Y.Z` with the release version string[cite: 4].

| Target platform | Release binary artifact |
| --- | --- |
| Linux on x86-64 | `kosh-linux-amd64-X.Y.Z`[cite: 4] |
| Linux on ARM64 | `kosh-linux-aarch64-X.Y.Z`[cite: 4] |
| macOS on Apple silicon | `kosh-darwin-aarch64-X.Y.Z`[cite: 4] |
| Windows on x86-64 | `kosh-win32-amd64-X.Y.Z.exe`[cite: 4] |

* **Release bundle contents** - Releases include `SHA256SUMS`, compressed manual pages (`kosh.1.zst` and `kosh.5.zst`), and the Bash completion script (`kosh.bash`)[cite: 4]. Verify the binary checksum against `SHA256SUMS` and run `kosh --version` to confirm the installed build[cite: 4].
* **Package manager and editor integrations** - On macOS, upgrade existing Homebrew installations via `brew update && brew upgrade kosh`[cite: 4]. The VS Code and Zed extensions automatically download `kosh` when absent from `PATH`[cite: 4]. The Neovim plugin is distributed as `kosh.nvim`[cite: 4].

## Execute common workflows <a id="getting-started"></a>

Launch an interactive session, execute an inline command string, or run a script file with positional arguments[cite: 4].

```sh
kosh
kosh -c 'printf "%s\n" hello'
kosh script.kosh argument
```

Run static analysis without execution, or format a script to standard output[cite: 4]. Append `--apply` to write safe lint fixes or formatted code directly to the file in place[cite: 4].

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

Koshka partitions shell behavior into four moods[cite: 4]. In this manual, `bash` and `bash-posix` are referred to as the **Bash moods**, `bash-posix` and `sh` (where `posix` is on) are the **POSIX moods**, and `bash`, `bash-posix`, and `sh` are the **compatibility moods**[cite: 4].

| Mood | Dialect and baseline behavior | Option mutability and transitions |
| --- | --- | --- |
| `kosh` (default) | Enables pre-run static analysis, resolves bundled `koshkit` utilities after `PATH`, and locks `nounset`, `pipefail`, `failglob`, and `extended arithmetic` to `on` and `nullglob` to `off`[cite: 4]. | `set`, `shopt`, and `koshconf` reject mutations to locked options with a hint to run `koshconf set mood bash`, but accept redundant no-op assignments so `set -euo pipefail` succeeds[cite: 4]. All other options are toggleable[cite: 4]. |
| `bash` | Enables Bash extensions with static analysis disabled by default[cite: 4]. | All Bash options are toggleable[cite: 4]. Explicit changes to `set -u`, `set -o pipefail`, `shopt -s failglob`, or `interpreter.arithmetic_uses_big_numbers` persist across mood switches between compatibility moods[cite: 4]. Running `set -o posix` switches to `bash-posix`[cite: 4]. |
| `bash-posix` | Enables Bash syntax with the `posix` option enabled and sources Bash startup files[cite: 4]. | Running `set +o posix` switches to `bash`[cite: 4]. |
| `sh` | Emulates `dash` grammar and semantics with `posix` enabled[cite: 4]. | Running `set +o posix` switches to `bash`[cite: 4]. |

#### Default option states across moods

Every option not listed in the table below shares an identical default across all four moods[cite: 4]. Mood differences in syntax, expansion, builtins, and error handling are specified under [Shell grammar](#shell-grammar), [Expansions](#expansions), [Builtins](#builtins), and [Shell errors](#shell-errors)[cite: 4].

| Option | `kosh` | `bash` | `bash-posix`, `sh` |
| --- | --- | --- | --- |
| `expand_aliases` | `on`[cite: 4] | `off` in scripts, `on` in interactive shells[cite: 4] | `on`[cite: 4] |
| `extglob` | `on`[cite: 4] | `off`[cite: 4] | `off`[cite: 4] |
| `failglob` | `on` (locked)[cite: 4] | `off`[cite: 4] | `off`[cite: 4] |
| `nounset` | `on` (locked)[cite: 4] | `off`[cite: 4] | `off`[cite: 4] |
| `pipefail` | `on` (locked)[cite: 4] | `off`[cite: 4] | `off`[cite: 4] |
| `extended arithmetic` | `on` (locked)[cite: 4] | `off`[cite: 4] | `off`[cite: 4] |
| `posix` | `off`[cite: 4] | `off`[cite: 4] | `on`[cite: 4] |

### Options and settings reference <a id="options"></a> <a id="shell-settings"></a>

Command-line flags override both settings files and `KOSHCONF`[cite: 4]. Runtime settings are read and mutated through `set`, `shopt`, and `koshconf`[cite: 4]. See the [kosh(5) configuration manual](kosh_5.md#command-line-options) for the exhaustive list of command-line flags, option IDs, namespaces, aliases, and mutability restrictions[cite: 4].

### Static analysis and runtime diagnostics <a id="diagnostics"></a>

All diagnostics write to standard error in an identical format across every mood (with colored labels and codes on color-capable terminals)[cite: 4].

* **Diagnostic layout** - Each entry prints `file:line:column: severity: message (CODE)`, followed by the source line, a caret (`^`) under the offending column, and optional `note:` remediation lines[cite: 4]. Dynamic runtime sources prefix locations as `eval:line:col:`, `trap:line:col:`, or `-c:line:col:`, while stdin and interactive lines omit the source label[cite: 4].
* **Stack backtraces** - Each active function call, sourced file, `eval`, trap action, subshell, and command/function/process substitution between the top level and the failing command appends a `trace:` frame[cite: 4]. Consecutive identical frames collapse into a single frame with a repeat count[cite: 4]. When multiple `-c` strings or files are processed, the top-level command-line frame prints once after all diagnostics for that input[cite: 4].
* **Multi-error syntax reporting** - Linting, the language server, and script execution report all syntax errors in a single pass[cite: 4]. Dialect violations (such as `[[` in the `sh` mood) are reported while still parsing the inner contents for additional syntax errors[cite: 4].
* **Pre-run static analysis tiers** - Analysis categorizes checks into `strict`, `lenient`, and `annoying` tiers (configured via `-W` and listed via `--list-diagnostics`)[cite: 4].
  * In the `kosh` mood, analysis runs automatically before execution; `strict` and `lenient` findings abort the script with exit status `1` before any command executes[cite: 4].
  * In compatibility moods, analysis runs only when enabled by `-W` or `set -W`, and all findings emit as non-fatal warnings[cite: 4]. When the warning level is nonzero, script operands lacking a shebang are flagged[cite: 4].
  * Interactive shells analyze each submitted line prior to execution[cite: 4].
* **Core static checks** - Analysis detects variables expanded but never assigned, functions referencing positional arguments that are invoked without arguments, functions invoked prior to their definition, and non-`local` variable assignments inside functions (in the `sh` mood, which lacks `local`, only variables first assigned inside a function and referenced outside it are flagged)[cite: 4].
* **Scope and traversal** - The analyzer follows static `source` operands into readable files, evaluates command/backtick/process substitutions as subshells, and evaluates `${ ...; }` function substitutions in the current shell environment[cite: 4]. When multiple findings occur, a summary line (`Encountered M warning and N errors.`) appends to the output[cite: 4].
* **Inline suppression directives** - Comments matching `# shellcheck disable=SC2086,unquoted-command-substitution` suppress static analysis findings[cite: 4]. Numeric codes (`SC2086`) suppress all variants under that ID, slug names suppress only that exact variant, `all` suppresses all checks, and ranges (`SC2000-SC2100`) suppress numeric spans[cite: 4]. A directive placed before the first command applies to the entire file; placed later, it applies to the immediately following and-or list[cite: 4]. Directives never suppress syntax or runtime errors[cite: 4].
* **Path and internal fault reporting** - Missing commands or file paths report the first non-existent path component when identifiable[cite: 4]. Internal interpreter faults emit an `error:` line and `note:` lines with process and build metadata before aborting[cite: 4].

### Offline linting and host extraction (`--lint`) <a id="linting"></a>

`kosh --lint` performs static analysis without executing code, enabling all three diagnostic tiers (`strict`, `lenient`, `annoying`) at the severity rules of the active mood regardless of `-W`, `--no-diagnostics`, or `--no-annoying-diagnostics`[cite: 4].

* **Input order** - Analyzes `-c` command strings in order, then file operands, or standard input if neither is provided[cite: 4]. Startup and settings files are skipped[cite: 4]. Diagnostics write to stderr and stdout remains empty[cite: 4].
* **Polyglot host file extraction** - Extracts and analyzes embedded `sh`, `bash`, and `kosh` scripts from CI workflows, Ansible playbooks, Dockerfiles, Docker Compose files, Markdown documents, Makefiles, justfiles, and Kubernetes manifests[cite: 4]. Files are identified by extension or shebang; unrecognized file formats fail with status `1` and print the supported format list[cite: 4].
  * Host interpreter declarations (`shell:` step keys, `SHELL` instructions, shebangs, or executable flags) select the analysis mood; fragments targeting non-shell interpreters are skipped[cite: 4].
  * `exec` arrays extract only the script operand of a shell `-c` invocation[cite: 4].
  * `justfile` recipe lines (joined across `\` continuations) are analyzed as individual commands, while shebang or `[script]` recipes are analyzed as single scripts[cite: 4].
  * Jinja shell templates (`*.sh.j2`) and host template interpolations/editor variables are masked as plain words[cite: 4]. Unresolved command checks are suppressed on lines containing host interpolations[cite: 4]. Host-injected variables (CI variables, exported recipe arguments, Dockerfile `ARG`/`ENV`) are pre-marked as assigned[cite: 4]. All diagnostic line and column numbers map back to the outer host file[cite: 4].
* **Automated remediation (`--apply`)** - Writes safe structured AST fixes to file operands, prints a fix summary, and outputs only remaining unresolvable diagnostics[cite: 4]. If a transformation would produce invalid syntax, the file is left unmodified[cite: 4]. Exits with status `0` when only warnings remain, and status `1` on remaining errors or I/O failures[cite: 4].

### Source code formatting (`--format`) <a id="formatting"></a>

`kosh --format` normalizes shell source code without executing it[cite: 4].

* **Formatting invariants** - Places structural keywords and compound braces on dedicated lines, indents nested blocks by `2` spaces, rewrites unshadowed `test` invocations into `[ ... ]` bracket commands, wraps lines at `80` columns at safe token boundaries, and normalizes line endings to `LF`[cite: 4].
* **Input and host behavior** - With no file operand or `-`, reads stdin and writes to stdout (with syntax coloring when stdout is a terminal)[cite: 4]. In polyglot host files, formats only mutable shell regions (such as Markdown fenced code blocks)[cite: 4]. Syntax errors emit diagnostics and exit with status `1`[cite: 4]. When combined with `--lint`, safe lint fixes apply before formatting[cite: 4].
* **Atomic in-place updates (`--apply`)** - Writes formatted output to a temporary file alongside each target and atomically renames it over the original, preserving file permissions and symbolic links[cite: 4]. Rejects non-regular files and files modified externally during processing[cite: 4].

### Language Server Protocol daemon (`--as-language-server`) <a id="language-server"></a>

`kosh --as-language-server` serves the LSP specification over standard input and output[cite: 4].

* **Capabilities** - Publishes live diagnostics on document change, tab completion (triggered by space or `-`), quick fixes, a `fix-all` code action for non-conflicting safe fixes, document formatting, go-to-definition, hover documentation, semantic token highlighting, document symbols/outline, and workspace-local rename[cite: 4].
* **Polyglot and cross-file resolution** - Operates inside embedded shell regions of all host formats supported by `--lint` and follows relative `source` paths from the document directory[cite: 4]. Rename updates variables, functions, and aliases across the open document while preserving `$name` vs `${name}` bracing; commands can be renamed only when defined as a function or alias within the open document[cite: 4].
* **Document mood resolution** - Resolves mood in priority order: document shebang, LSP language ID (`kosh`, `bash`, `sh`, `bash-posix`, where `rbash` and `shellscript` map to `bash`), file extension (`.kosh`, `.bash`, `.sh`, `.dash`), or fallback `kosh`[cite: 4]. Skips startup/settings files and exits with status `0` on clean LSP shutdown or `1` otherwise[cite: 4].

### Startup and configuration file loading <a id="startup-files"></a>

Every shell instance loads declarative settings before sourcing startup scripts[cite: 4]:
1. System settings file: `/etc/kosh.conf` (POSIX) or `%ProgramData%\kosh\kosh.conf` (Windows)[cite: 4].
2. User settings file: `$XDG_CONFIG_HOME/kosh/kosh.conf` or `~/.config/kosh/kosh.conf` (skipped when the POSIX effective user is `root`)[cite: 4].
3. Environment payload: `KOSHCONF` (unset from the environment immediately after ingestion)[cite: 4].

In the `kosh` mood, login and interactive shells source `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh` (or `~/.config/kosh/scripts/rc.kosh`); no `/etc/koshrc` or `~/.koshrc` exists, and the POSIX `root` user skips user `rc.kosh`[cite: 4]. In interactive non-login `kosh` shells, `--norc`, `--rcfile`, and `-p` (`--privileged`) govern `rc.kosh` identically to `~/.bashrc`[cite: 4]. Any Bash startup file sourced from `rc.kosh` automatically executes under the `bash` mood and returns to `kosh` upon completion (see [`source`](#builtin-source))[cite: 4]. Passing `-L` or setting `init_moods` overrides which mood startup sequences run (for example, `-L bash,kosh` runs Bash startup files in the `bash` mood, switches to `kosh`, and then runs `rc.kosh`)[cite: 4].

#### Startup bypass and post-startup enforcement (`skipping-startup-files`) <a id="skipping-startup-files"></a>

| Execution context | Files skipped and enforcement rules |
| --- | --- |
| `-Q` (`--no-init-files`), rescue mode, `--lint`, `--format`, `--as-language-server`, setuid/setgid (`EUID != UID` or `EGID != GID`), standalone `koshkit` / utility symlink | Skips all settings files, `KOSHCONF`, and all startup scripts[cite: 4]. |
| `--no-config` | Skips settings files and `KOSHCONF`; startup scripts still run[cite: 4]. |
| `-p` (`--privileged`) | Skips settings files, `KOSHCONF`, `ENV`, `BASH_ENV`, and interactive non-login `rc.kosh`[cite: 4]. |
| Subshells | Inherit settings directly from the parent shell without re-reading files[cite: 4]. |
| Startup execution window | Restricted mode (`-r`), `nounset`, `pipefail`, and pre-run static analysis activate only *after* startup files finish, though `set`, `shopt`, and `koshconf` inside startup files immediately reject option values locked by the target mood[cite: 4]. Missing default startup files are silently skipped; unreadable custom `--rcfile` or `BASH_ENV` targets emit an error[cite: 4]. |

### Interactive line editing <a id="line-editing"></a>

The interactive line editor defaults to `emacs` mode (`set -o vi` and `set -o emacs` switch modes)[cite: 4].

* **Viewport and rendering** - Multi-line buffers taller than the terminal viewport scroll around the cursor row, reserving `2` bottom rows for hints and hiding ghost text and the right prompt[cite: 4]. Control bytes render in caret notation (`^[` for `ESC`), filenames containing control bytes complete in `$'...'` syntax, and bracketed paste inserts text literally without executing newlines[cite: 4]. When the cursor rests on or immediately after an unquoted, unescaped bracket `( ) [ ] { }` outside comments and here-documents, its matching partner highlights[cite: 4].
* **Kill ring, undo, and external editing** - Kill operations push erased text onto a 16-entry session kill ring, merging consecutive kills into a single entry[cite: 4]. `Ctrl-K` on an empty line joins the next line, or deletes an empty final line[cite: 4]. Undo reverts a yank or `Alt-.` insertion and all its cycled replacements in one step[cite: 4]. `Ctrl-X Ctrl-E` (or `:e` in Vi mode) opens the current buffer in `$VISUAL`, `$EDITOR`, or `vi`; exiting the external editor with status `0` replaces the buffer without executing it, while nonzero exits preserve the original buffer and print a message[cite: 4].
* **Extended key reporting and undo/redo bindings** - While collecting input, `Ctrl-Z` undoes edits (during command execution, `Ctrl-Z` suspends the foreground job)[cite: 4]. `Ctrl-Shift-Z` redoes edits when reported distinctly via the kitty keyboard protocol or xterm `modifyOtherKeys` (forwarded by `tmux` when `extended-keys` is `on`)[cite: 4]. Disable protocol requests via `koshconf set editor.request_extended_key_reports off`[cite: 4]. On the Windows console, both `Ctrl-Z` and `Ctrl-Shift-Z` undo, and `Ctrl-^` redoes[cite: 4].
* **Prefix history recall and auto-pairing** - With `editor.history.arrow_keys_search_by_typed_prefix=on` (default), pressing `Up` on a non-empty line recalls only history entries matching the prefix preceding the first `Up` press, while an empty line steps through all history[cite: 4]. Setting `editor.auto_close_brackets_and_quotes=on` automatically inserts matching closers for `(`, `[`, `{`, `'`, and `"`, steps over typed closers, and deletes empty pairs on `Backspace`[cite: 4].

#### Live synopsis hints and pre-run diagnostics <a id="hints"></a>

When the cursor advances past the command word, up to `16` hint rows appear below the input line (hidden while completion menus or searches are active)[cite: 4]:
* **Command synopsis (`editor.hints.show_command_synopsis=on`)** - Displays command, subcommand, and flag documentation under `synopsis`, marking the active argument with `>`[cite: 4]. Sources data from builtin/`koshkit` help, alias definitions, function source locations, section 1 man pages (`man` / `manpath`), subcommand man pages (`git-commit(1)`), or `--help` output for allowlisted binaries[cite: 4]. External `man` and allowlisted binaries execute only from absolute directories owned by the user or `root` and not writable by group or others (never from empty or relative `PATH` entries)[cite: 4]. Pending multi-key prefixes (`Ctrl-X` or Vi operators) display valid completion keys[cite: 4].
* **Live diagnostics (`editor.hints.show_live_diagnostics=on`)** - Displays the first static analysis finding (`error`, `warning`, `note`) or parse error (`syntax error`) after a brief pause without opening files or following `source` targets[cite: 4]. Because pressing `Enter` submits the entire buffer without a `PS2` continuation prompt, incomplete constructs report immediately as syntax errors[cite: 4]. Disabling both hint options also hides pending-key hints[cite: 4].

#### Tab completion and candidate selectors <a id="completion"></a>

* **Ghost text** - Suggests inline prefix matches from completion and history; `Right` or `End` at the end of the buffer accepts the full suggestion, and `Ctrl-Right` or `Alt-F` accepts the next word[cite: 4].
* **Context-sensitive candidate generation** - `Tab` computes exact-prefix, smart-case, and subsequence matches[cite: 4]:
  * Empty command word: shell keywords, builtins, functions, aliases, bundled utilities, and `PATH` executables[cite: 4].
  * Arguments: file paths (directories append `/`), `$var` / `${var}` variables, `~user` accounts, and `-flag` options[cite: 4].
  * Specialized commands: programmable `complete` specs take top priority; `cd` and `pushd` complete directories across `CDPATH` (unless starting with `/`, `.`, or `~`); `make` extracts targets statically from `Makefile`; `cmake --build <dir> --target` extracts targets from `<dir>/CMakeFiles/TargetDirectories.txt` without executing `make`, `cmake`, or `$(shell ...)`[cite: 4].
* **Insertion and selector modes (`--tab-selector` / `completion.menu_style`)** - Pressing `Tab` inserts the longest common prefix[cite: 4]. A single match replaces the token and appends a space according to `completion.add_space_after_completed_word` (`off` default, `on`, or `on-excluding-trailing-slash`)[cite: 4]. Multiple candidates open the configured selector[cite: 4]:

| Selector mode | Completion and `Ctrl-R` history search behavior |
| --- | --- |
| `interactive` (default) | Renders an in-editor menu under `selecting completions` or `incremental history search`[cite: 4]. Typing filters candidates (commands, paths, and `koshconf` options retain the highest non-empty tier among exact-prefix, smart-case-prefix, and subsequence matches; other lists use case-insensitive prefix matching; history matches prefix, substring, or ordered subsequence bytes)[cite: 4]. `Backspace` widens results; typing space, `/`, or deleting past token start refreshes candidates; accepting a directory opens its contents[cite: 4]. |
| `external` | Pipes multiple candidates into `$KOSH_FZF_COMPLETION_COMMAND` (`fzf` default) and replaces the token with stdout[cite: 4]. Falls back to `plain` listing with a diagnostic if only one candidate exists, no controlling tty is attached, the command is empty/missing, or the command exits with a status other than `0`, `1`, or `130`[cite: 4]. |
| `plain` | Prints candidates in a multi-column list (always used on terminals lacking decoration support) and keeps `Ctrl-R` history search inline[cite: 4]. |

* **History search (`Ctrl-R`)** - Searches a snapshot of the shared history file newest-first using the current line as the initial query without mutating the in-memory private history list[cite: 4].

#### Terminal shell integration marks <a id="terminal-integration"></a>

Interactive sessions attached to a terminal emit `OSC 133` (`A`, `B`, `C`, `D`) prompt/command/status marks, `OSC 7` working directory URIs, and `OSC 633` marks when `TERM_PROGRAM=vscode`[cite: 4]. Emission is disabled when `TERM=dumb` or `KOSH_SHELL_INTEGRATION=0`[cite: 4].

#### Editor keybinding reference <a id="emacs-mode-keys"></a> <a id="vi-insert-mode-keys"></a> <a id="vi-command-mode-keys"></a> <a id="vi-visual-and-rectangle-keys"></a> <a id="vi-colon-commands"></a> <a id="emacs-m-x-commands"></a> <a id="multi-cursor-keys"></a> <a id="completion-menu-keys"></a> <a id="history-search-keys"></a>

##### Emacs mode and Vi insert mode keys

Vi insert mode supports all Emacs bindings below except `Ctrl-V`, `Alt-X`, and `Ctrl-X` chords; arrow keys in Vi insert mode recall history, and `Escape` transitions to Vi command mode (moving 1 character left)[cite: 4]. Unlisted `Shift` or `Alt` combinations with `Up`, `Down`, `Left`, `Right`, `Home`, or `End` behave as the unmodified key, and `Ctrl-Backspace` deletes 1 character left[cite: 4].

| Keybinding | Action |
| --- | --- |
| `Ctrl-A`, `Home` | Move to start of current line[cite: 4]. |
| `Ctrl-E`, `End` | Move to end of current line, or accept full ghost suggestion at end of buffer[cite: 4]. |
| `Ctrl-Home`, `Ctrl-Up`, `Ctrl-PageUp` | Move to start of buffer[cite: 4]. |
| `Ctrl-End`, `Ctrl-Down` | Move to end of buffer[cite: 4]. |
| `PageUp`, `PageDown` | Move cursor up or down by screen height[cite: 4]. |
| `Ctrl-B`, `Left` | Move 1 character left[cite: 4]. |
| `Right` | Move 1 character right, or accept full ghost suggestion at end of buffer[cite: 4]. |
| `Alt-B`, `Ctrl-Left`, `Alt-Left` | Move to start of previous word[cite: 4]. |
| `Alt-F`, `Ctrl-Right`, `Alt-Right` | Move past next word, or accept next word of ghost suggestion[cite: 4]. |
| `Ctrl-P`, `Up` | Move up 1 row, or recall older history entry from top row (requires 2 presses on a multi-line buffer without `\` line continuations)[cite: 4]. |
| `Ctrl-N`, `Down` | Move down 1 row, or recall newer history entry from bottom row[cite: 4]. |
| `Alt-<`, `Alt-,`, `Alt->` | Recall oldest or newest history entry (or move to start/end of file in `slurp`)[cite: 4]. |
| `Enter`, `Ctrl-J`, `Ctrl-M`, `Shift-Enter` | Execute buffer, continue after trailing `\`, or execute uncontinued multi-line buffer on second press[cite: 4]. |
| `Alt-Enter` | Insert literal line break[cite: 4]. |
| `Tab`, `Shift-Tab`, `Ctrl-Space` (POSIX) | Trigger token completion[cite: 4]. |
| `Backspace`, `Ctrl-H`, `Delete` | Delete character before (`Backspace`/`Ctrl-H`) or under (`Delete`) cursor[cite: 4]. |
| `Ctrl-D` | Delete character under cursor, or exit shell on an empty buffer[cite: 4]. |
| `Ctrl-W`, `Alt-Backspace`, `Alt-H` | Kill previous word to kill ring[cite: 4]. |
| `Alt-D`, `Ctrl-Delete` | Kill from cursor to next word boundary[cite: 4]. |
| `Ctrl-K`, `Ctrl-U` | Kill to end (`Ctrl-K`) or start (`Ctrl-U`) of line[cite: 4]. |
| `Ctrl-Y`, `Alt-Y` | Yank newest kill (`Ctrl-Y`), or cycle to next older kill (`Alt-Y`)[cite: 4]. |
| `Ctrl-T`, `Alt-T` | Transpose characters (`Ctrl-T`) or words (`Alt-T`) around cursor[cite: 4]. |
| `Alt-U`, `Alt-L`, `Alt-C` | Uppercase, lowercase, or capitalize from cursor to end of word[cite: 4]. |
| `Alt-\` | Delete all spaces and tabs around cursor[cite: 4]. |
| `Alt-.`, `Alt-_` | Insert last argument of older history entries on each press[cite: 4]. |
| `Ctrl-_`, `Ctrl-Z`, `Ctrl-X Ctrl-U` | Undo last edit[cite: 4]. |
| `Ctrl-^`, `Ctrl-Shift-Z` | Redo last undone edit[cite: 4]. |
| `Ctrl-X Ctrl-E` | Edit buffer in `$VISUAL`, `$EDITOR`, or `vi`[cite: 4]. |
| `Alt-X`, `Ctrl-X z` | Open `M-x` command minibuffer (`Alt-X`), or repeat last `M-x` command (`Ctrl-X z`)[cite: 4]. |
| `Ctrl-X Ctrl-C` | Exit shell (or `slurp`) with last command status[cite: 4]. |
| `Ctrl-X s`, `Ctrl-X Ctrl-S` | Save file in `slurp`[cite: 4]. |
| `Ctrl-S` | Incremental forward buffer search (smart-case, wraps around; `Ctrl-S`/`Ctrl-R` step matches, `Enter` commits cursor, `Escape` restores cursor)[cite: 4]. |
| `Ctrl-R`, `Ctrl-F` | Search command history (in `slurp`, `Ctrl-R` searches buffer backward)[cite: 4]. |
| `Ctrl-L` | Clear and redraw terminal screen[cite: 4]. |
| `Ctrl-C` | Discard current input buffer[cite: 4]. |
| `Ctrl-V` | Enter multi-cursor editing mode[cite: 4]. |

##### Vi command, visual, and rectangle mode keys

In Vi command mode, arrow keys move across buffer rows without recalling history[cite: 4].

| Keybinding | Action |
| --- | --- |
| `1-9` then `0-9`, `"x` | Set numeric repeat count, or select alpha register `x` (`a-z`, `A-Z`) for delete/change/yank/paste[cite: 4]. |
| `h`, `l`, `Left`, `Right`, `Backspace`, `Space`, `Ctrl-B`, `Ctrl-F` | Move cursor left or right[cite: 4]. |
| `w`, `W`, `b`, `B`, `e`, `E` | Move to word or `blank`-delimited WORD start/end[cite: 4]. |
| `0`, `^`, `$`, `Home`, `End`, `Ctrl-A`, `Ctrl-E` | Move to line start (`0`), first non-blank (`^`), or line end (`$`)[cite: 4]. |
| `fx`, `Fx`, `tx`, `Tx`, `;`, `,` | Find character `x` forward/backward on line, stop before `x`, or repeat find[cite: 4]. |
| `k`, `j`, `Up`, `Down`, `Ctrl-P`, `Ctrl-N` | Move between rows (`k` and `j` recall history from top/bottom row)[cite: 4]. |
| `gg`, `G`, `{`, `}` | Move to first row (`gg`), last row or row `N` (`G`), or previous/next empty line (`{` / `}`)[cite: 4]. |
| `d`, `c`, `y` | Delete, change, or yank over motion (doubled `dd`/`cc`/`yy` or paired with vertical motions acts on full lines)[cite: 4]. |
| `gq`, `gqq` | Reflow lines over motion (`gq`) or current line (`gqq`) to `editor.wrap_width` (`80`), preserving first-line paragraph indentation[cite: 4]. |
| `x`, `X`, `D`, `C`, `s`, `S`, `rx`, `~` | Delete char under/before cursor, delete/change to line end, change char/line, replace char with `x`, or toggle case[cite: 4]. |
| `p`, `P`, `o`, `O`, `J` | Paste after/before cursor, open line below/above, or join next line with 1 space[cite: 4]. |
| `i`, `a`, `I`, `A`, `R`, `.` | Enter insert mode at cursor, after cursor, at first non-blank, at line end, in non-overwriting replace mode (`R`), or repeat last edit (`.`)[cite: 4]. |
| `u`, `Ctrl-_`, `Ctrl-Z` / `Ctrl-R`, `Ctrl-^`, `Ctrl-Shift-Z` | Undo / Redo[cite: 4]. |
| `/`, `?`, `n`, `N`, `:` | Search history (or buffer text in `slurp` with `n`/`N` repeats), or open `:` command prompt[cite: 4]. |
| `v`, `V`, `Ctrl-V` | Enter character visual (`v`), line visual (`V`), or rectangular block (`Ctrl-V`) selection[cite: 4]. Visual modes accept `d`, `x`, `c`, `y`, `gq`, and `:` (`:'<,'>`); rectangle mode accepts `d`, `x`, and multi-line left-column insert via `I`, `c`, `C`[cite: 4]. |

##### Vi colon (`:`) commands, Emacs `M-x` commands, and modal sub-menus

| Mode / Menu | Supported commands and bindings |
| --- | --- |
| Vi `:` commands | `:q`, `:q!`, `:quit`, `:wq`, `:wq!`, `:wqa`, `:wqa!`, `:x` (exit shell, or save and exit in `slurp`); `:w`, `:write` (save in `slurp`); `:N`, `:$` (jump to row `N` or end); `:s/old/new/[g]` and `:%s/old/new/[g]` (BRE substitution with `&` and `\1`-`\9`, repeatable via `:&`, `:&&`, `:s`, `:%&`, `:%&&`); `:set option[=value\|?]`, `:set nooption` (mutate/query `koshconf` with `Tab` completion); `:save [option]` (persist to `kosh.conf`); `:format`, `:%format` (format row or buffer); `:e`, `:edit`, `:u`, `:undo`, `:red`, `:redo`; `:h`, `:help`[cite: 4]. |
| Emacs `M-x` commands | `help`, `replace-string`, `replace-regexp` (supports `\&` and `\1`-`\9`), `repeat`, `set-option`, `get-option`, `save-option`, `format`, `format-buffer`, `fill-paragraph`, `isearch-forward`, `isearch-backward`, `kill-kosh`, `undo`, `undo-redo`, `beginning-of-line`, `end-of-line`, `forward-word`, `backward-word`, `scroll-down-command`, `scroll-up-command`, `beginning-of-history`, `end-of-history`, `delete-char`, `backward-delete-char`, `kill-word`, `backward-kill-word`, `kill-line`, `unix-line-discard`, `yank`, `yank-last-arg`, `transpose-chars`, `transpose-words`, `upcase-word`, `downcase-word`, `capitalize-word`, `delete-horizontal-space`, `clear-screen`[cite: 4]. |
| Multi-cursor (`Ctrl-V`) | `Up`/`Down`/`Ctrl-P`/`Ctrl-N` add cursors vertically; `Left`/`Right`/`Home`/`End`/`Ctrl-B`/`Ctrl-F`/`Ctrl-A`/`Ctrl-E` move all cursors; typing and `Backspace` edit at all cursors; `Ctrl-G` or `Escape` exits multi-cursor mode[cite: 4]. |
| Completion menu | `Down`/`Ctrl-N`/`Up`/`Ctrl-P`/`Shift-Tab` cycle rows; `Tab` accepts (opening directories); `Right` accepts and chains completion; `Enter` closes menu and runs command; typing/`Backspace` filters; `Escape` keeps edited line; `Ctrl-G` restores original line[cite: 4]. |
| History search | In `interactive` mode: `Enter`/`Tab` accept without running; `Escape`/`Ctrl-G` cancel and restore[cite: 4]. In `plain` mode: `Down`/`Ctrl-N`/`Ctrl-R` step older; `Up`/`Ctrl-P`/`Ctrl-F`/`Shift-Tab` step newer; `Enter`/`Tab` accept; `Escape`/`Ctrl-G` cancel[cite: 4]. |

### Prompt rendering and escape sequences <a id="prompting"></a>

* **Prompt lifecycle** - Before reading interactive input, the shell executes `PROMPT_COMMAND` (a single command or an indexed array of commands executed sequentially, preserving the prior command's `$?` and `$_`) and prints `PS1` and the right prompt (`RPS1`, or `RPROMPT` if `RPS1` is unset/empty, defaulting to dim `\L \M`)[cite: 4]. After reading input and before execution, `PS0` prints[cite: 4]. `PS2` defaults to `> ` (unused by the interactive editor, which submits full multi-line buffers on `Enter`), `PS3` prompts for `select` (`#? ` default), and `PS4` prefixes `xtrace` lines (`+ ` default, repeating its first byte per nesting level of substitution, `eval`, `source`, or trap)[cite: 4].
* **Expansion and transient prompts** - `PS0`, `PS1`, `PS2`, and `RPS1`/`RPROMPT` process backslash escapes first, then undergo parameter, command, and arithmetic expansion (`PS4` prints literally)[cite: 4]. Setting non-empty `KOSH_TRANSIENT_PS1` or `KOSH_TRANSIENT_RPS1` replaces `PS1` and `RPS1` in scrollback upon `Enter` or `Ctrl-C` (`KOSH_TRANSIENT_RPS1` defaults to `\[\]`, erasing the right prompt after submission)[cite: 4]. The terminal window title displays `user: directory` while idle and the active command while executing[cite: 4].

| Escape | Read-only variable equivalent | Rendered output |
| --- | --- | --- |
| `\u`, `\h`, `\H` | `KOSH_PS1_USER`, `KOSH_PS1_HOST`, `KOSH_PS1_HOST_FULL` | Current username, short hostname (to first `.`), and full hostname[cite: 4]. |
| `\k`, `\K` |  | `\h` and `\H` styled with terminal color[cite: 4]. |
| `\w`, `\W`, `\P` | `KOSH_PS1_CWD`, `KOSH_PS1_CWD_BASE`, `KOSH_PS1_CWD_SHORT` | Working directory (`~` for `$HOME`), basename, and `\w` truncated to `...` plus final 21 bytes when exceeding 24 bytes[cite: 4]. |
| `\g` | `KOSH_PS1_GIT_BRANCH` | Git branch or short detached `HEAD` hash (matches `KOSH_GIT_BRANCH`)[cite: 4]. |
| `\$`, `\?`, `\j` | `KOSH_PS1_PROMPT_SIGN`, `KOSH_PS1_STATUS`, `KOSH_PS1_JOBS` | `#` for root or `$` otherwise, last exit status, and active job count[cite: 4]. |
| `\L`, `\M` | `KOSH_PS1_DURATION`, `KOSH_PS1_MEMORY` | Last command duration (empty under `5 ms`) and peak resident memory of its process tree (or shell heap growth; empty if memory was only reused)[cite: 4]. |
| `\.` | `KOSH_PS1_DOT` | Status bullet: white after `0`, bold red after `1`, shifting toward magenta across 4 steps for higher exit codes on truecolor terminals[cite: 4]. |
| `\t`, `\T`, `\@`, `\A`, `\d`, `\D{fmt}` | `KOSH_PS1_TIME`, `KOSH_PS1_TIME_12`, `KOSH_PS1_TIME_AMPM`, `KOSH_PS1_TIME_SHORT`, `KOSH_PS1_DATE` | 24h `HH:MM:SS`, 12h `HH:MM:SS`, 12h `HH:MM AM/PM`, 24h `HH:MM`, `Weekday Month Day`, and custom `strftime` format (`HH:MM:SS` if `fmt` is empty)[cite: 4]. |
| `\s`, `\v`, `\V`, `\l` | `KOSH_PS1_SHELL`, `KOSH_PS1_VERSION`, `KOSH_PS1_VERSION_FULL`, `KOSH_PS1_TTY` | Invocation basename (stripped of leading `-` and `.exe`), `major.minor` version, full version, and stdin tty basename[cite: 4]. |
| `\!`, `\#` | `KOSH_PS1_HISTORY_NUMBER`, `KOSH_PS1_COMMAND_NUMBER` | Next history number and session command number (counting from `1`)[cite: 4]. |
| `\n`, `\r`, `\e`, `\a`, `\\`, `\nnn`, `\[`, `\]` |  | Newline, CR, ESC, bell, literal `\`, octal byte `nnn`, and zero-width non-printing delimiters (raw `0x01`/`0x02` bytes act identically to `\[`/`\]`)[cite: 4]. |

### Command history persistence <a id="history"></a>

Interactive shells with `history` enabled (default) record each submitted buffer as a single line-escaped event (`\n` for newlines, `\\` for backslashes) in `$KOSH_HISTORY_FILE` (`~/.kosh_history` default, mode `0600`, directory-locked during writes)[cite: 4].
* **Filtering and synchronization** - Non-interactive shells never record events automatically (only via `history -w` or `history -a`)[cite: 4]. Consecutive duplicate events, history expansion errors, and records exceeding `65535` bytes are skipped[cite: 4]. Interactive shells load the history file at startup and append each submitted event immediately, without auto-importing events appended by concurrent shells (`Ctrl-R` and `history -S` read the shared file snapshot)[cite: 4].
* **Size bounds** - `$KOSH_HISTORY_SIZE` (`4096` default; `0` disables history) caps both the in-memory list and the on-disk file, which is truncated to the newest `$KOSH_HISTORY_SIZE` events upon shell exit[cite: 4]. Environment values of `KOSH_HISTORY_FILE` and `KOSH_HISTORY_SIZE` override `editor.history.file_path` and `editor.history.max_entries` from `kosh.conf`[cite: 4]. Legacy Bash history variables (`HISTFILE`, `HISTSIZE`, etc.) are ignored[cite: 4].

### Koshka environment and shell variables <a id="koshka-variables"></a>

#### Shell-maintained Koshka variables <a id="shell-maintained-variables"></a>

| Variable | Mutability and export | Value and behavior |
| --- | --- | --- |
| `KOSH` | Unexported | Invocation path of the binary; `test -n "$KOSH"` identifies Koshka in any mood[cite: 4]. |
| `KOSH_VERSION`, `KOSH_COMMIT`, `KOSH_BUILD_MODE`, `KOSH_OS` | Unexported | Release version, Git commit hash, build profile, and target OS[cite: 4]. |
| `KOSH_IDENTITY` | Read-only, exported | Lowercase hexadecimal CRC-32 checksum of the running binary[cite: 4]. |
| `KOSH_ANSI_*` | Overwritable, unexported | Terminal escape sequences (`BLACK`, `RED`, `GREEN`, `YELLOW`, `BLUE`, `MAGENTA`, `CYAN`, `WHITE`, `BRIGHT_*`, `BOLD`, `DIM`, `RESET`); empty when color is disabled[cite: 4]. |
| `KOSH_ANALYSIS` | Exported | Space-separated analysis state (`mimicry` for `-I`, `warnings=N` for `-W`, `no-annoying`, `no-diagnostics`), updated by `set` and inherited by child shells[cite: 4]. |
| `KOSH_GIT_BRANCH`, `KOSH_GIT_AHEAD`, `KOSH_GIT_BEHIND` | Branch overwritable | Active Git branch (or short detached commit hash) and upstream ahead/behind counts (empty when `0` or outside Git; respects `GIT_CEILING_DIRECTORIES`)[cite: 4]. |
| `KOSH_PS1_*` | Read-only | Evaluated text of each prompt escape sequence[cite: 4]. |
| `KOSHCONF` | Read-only, unexported | Dynamic Base64-encoded binary snapshot of active mood and interactive settings[cite: 4]. |

#### User-configurable Koshka variables <a id="configuration-variables"></a>

| Variable | Default value | Effect |
| --- | --- | --- |
| `KOSH_HISTORY_FILE`, `KOSH_HISTORY_SIZE` | `~/.kosh_history`, `4096` | Command history file path and maximum event count[cite: 4]. |
| `KOSH_DIRECTORY_HISTORY` | `~/.kosh_directory_history` | Visited directory database ranked by the `z` builtin[cite: 4]. |
| `KOSH_CALC_HISTORY` | `~/.kosh_calc_history` | Interactive expression history file for the `calc` utility[cite: 4]. |
| `KOSH_FLAGS` | Unset | Default command-line flags parsed before argv (ignored in secure setuid/setgid execution; ignores `-c`, `-s`, `--format`, `--apply`, `--as-language-server`, `--rcfile`, `--init-file`, `--`, and operands)[cite: 4]. |
| `KOSH_FZF_COMPLETION_COMMAND`, `KOSH_FZF_COMPLETION_OPTS` | `fzf`, `--multi --layout=reverse --height=~40% --min-height=3` | External completion selector binary and blank-split flags[cite: 4]. |
| `KOSH_SHELL_INTEGRATION` | Enabled | Set to `0` to suppress `OSC 133`, `OSC 7`, and `OSC 633` terminal marks[cite: 4]. |
| `KOSH_WELCOME`, `KOSH_FAREWELL` | Mood-dependent | Startup and exit banners (`Welcome :3` in `kosh`, `Bash me harder!` in Bash moods, `POSIX me harder!` in `sh`, and `Goodbye :c` on exit)[cite: 4]. Suppress by exporting an empty string or zero-width string (`printf -v KOSH_WELCOME "\r"`)[cite: 4]. |

### Native Koshka builtins <a id="koshka-builtins"></a>

The six native Koshka builtins (`assimilate`, `bench`, `koshconf`, `koshkit`, `slurp`, `z`) are available in every mood[cite: 4].

| Builtin and synopsis | Operational specification and exit codes |
| --- | --- |
| `assimilate [options] [TARGET]` <a id="builtin-assimilate"></a> | Deploys the running `kosh` binary locally (when `TARGET` is omitted) or to a remote host over `scp` into the first writable `PATH` directory (excluding `sbin` paths unless `--install-directory=DIR` is passed)[cite: 4]. Locally, creates relative symlinks (POSIX) or `.exe` hard links (Windows) for all non-conflicting bundled utilities and `--link-mood=MOOD` (`bash`, `dash`, `sh`, `kosh`) names, warning once per group of skipped conflicting names[cite: 4]. Supports `--ssh-command=CMD`, `--scp-command=CMD`, `-x` (`--trace`), and `-y` (`--yes` to skip confirmation)[cite: 4]. Validates the copied binary before atomic replacement and rolls back on failure[cite: 4]. |
| `bench [options] command ...` <a id="builtin-bench"></a> | Benchmarks each `command` via `kosh -c` and reports mean, standard deviation, and range for wall time, peak RSS, and platform performance counters[cite: 4]. Stops and returns `1` on nonzero command exit unless `--ignore-exit-code` is set (`130` on interrupt)[cite: 4]. Options: `--show-output`, `-N` (`--no-shell` direct exec), `-n` (`--runs` `1`..`100000`), `-t` (`--duration` ms, default `3000`), `-w` (`--warmup` `1`..`100000`; without argument runs 1-3 warmups until two agree within 10%; default 1 warmup), `-p` (`--profile`, alias for `--runs 1 --extended`), and `-E` (`--extended` CPU/wait time, context switches, faults, I/O, plus Linux traced syscalls/threads/execs, macOS energy/wakeups, and Windows Job Object tree counters)[cite: 4]. |
| `koshconf <subcommand> ...` <a id="builtin-koshconf"></a> | Manages configuration settings[cite: 4]:<br>- `create PRESET [--force]`: Writes commented `kosh`, `bash`, or `sh` preset to user `kosh.conf` (and default `rc.kosh` if missing)[cite: 4].<br>- `set OPTION VALUE [--persist]`: Mutates session option, and with `--persist` updates user `kosh.conf` (backing up `kosh.conf.bak`)[cite: 4].<br>- `get OPTION` / `list [-a\|--all]`: Prints single value or sorted `name=value` list (`-a` includes legacy aliases, `hashall`, and `nolog`)[cite: 4].<br>- `load ENCODED` / `save [--persist]`: Applies or outputs `KOSHCONF` Base64 payload (`save --persist` writes non-default session settings to `kosh.conf`)[cite: 4].<br>Returns `0` on success, `1` on invalid option/value or I/O error, and `2` on syntax error[cite: 4]. Restricted shells reject `create`, `set`, `load`, and `save --persist`[cite: 4]. |
| `koshkit [--color when] [utility [arg ...]]` <a id="builtin-koshkit"></a> | Invokes a bundled utility (`--color` takes `always`, `never`, or `auto`), or prints the categorized utility catalog when invoked without arguments (`--list` prints one utility per line; `--list-flags` prints utilities and flags)[cite: 4]. |
| `slurp [-e\|-v\|-V] [+LINE] FILE ...` <a id="builtin-slurp"></a> | Opens each `FILE` sequentially in the full-screen alternate-buffer editor (`-e` Emacs mode, `-v` Vi insert mode, `-V` Vi normal mode; `+LINE` jumps to line)[cite: 4]. Preserves syntax highlighting and `:format` support for shell scripts, supports mouse positioning/selection/scrolling, and writes files atomically via sibling temp files (or in-place with a private backup in `$HOME` or `$TMPDIR` for multi-link, foreign-owned, or non-sticky world-writable directories) while preserving CRLF endings, final newlines, permissions, and symlinks[cite: 4]. Rejects directories, special/unreadable files, files containing `NUL` or invalid UTF-8, files `> 16 MiB`, restricted shells, or non-tty stdio with status `1` (`2` on usage errors, `130` on `Ctrl-C`)[cite: 4]. |
| `z [query ...]` <a id="builtin-z"></a> | Jumps to a directory recorded in `$KOSH_DIRECTORY_HISTORY`[cite: 4]. A single existing directory path opens directly; multiple operands match case-insensitive substrings ranked by visit count and recency; no operand jumps to `$HOME`[cite: 4]. Returns `1` when no match exists[cite: 4]. |

### Bundled Koshkit utilities <a id="bundled-utilities"></a> <a id="standard-utilities"></a>

Koshka bundles 108 utilities plus 4 aliases (`egrep` and `fgrep` for `grep`; `vi` and `emacs` for `slurp -V` and `slurp -e`)[cite: 4].

* **Resolution and invocation** - In the `kosh` mood (or when `--enable-koshkit` / `interpreter.resolve_koshkit_applets_as_commands=on` is set), bundled utilities resolve directly as commands *after* `PATH` lookup[cite: 4]. Invoking the binary via a symlink/hardlink named after a utility (case-sensitive, stripping `.exe`) or as `koshkit <utility>` executes the utility directly, bypassing shell startup files, settings files, and `KOSH_FLAGS`[cite: 4].
* **Option parsing and exit status invariants** - Every utility accepts `--help` (and `--version` when invoked under its own binary name)[cite: 4]. Flags may appear before or after operands (`--` ends option parsing), except for command-wrapping utilities (`calc`, `env`, `expr`, `flock`, `nice`, `nohup`, `retry`, `timeout`, `watch`, `xargs`), which stop parsing options at the first positional operand[cite: 4]. Utilities return `0` on success, `1` on operand/processing failure (continuing to remaining file operands), and `2` on usage errors (which terminates scripts in the `kosh` mood)[cite: 4]. Interactive prompts (`rm -i`, `cp -i`, `mv -i`, `assimilate`) print `[y/N]` or `[Y/n]` to stderr and read one line from stdin[cite: 4].

#### Koshka-specific and system inspection utilities

| Utility | Operational specification and flags |
| --- | --- |
| `calc` <a id="utility-calc"></a> | Evaluates arbitrary-precision integer and finite decimal expressions from operands, `-i` (`--interactive` with `Ctrl-R` history), or `-p` (`--pipe`)[cite: 4]. Builtin functions: `abs`, `atan`, `ceil`, `cmp`, `cos`, `exp`, `fact`, `fib`, `floor`, `frac`, `gcd`, `int`, `iseven`, `isint`, `isodd`, `length`, `lcm`, `ln`, `max`, `min`, `scale`, `sgn`, `sin`, `sqrt`[cite: 4]. |
| `evil` <a id="utility-evil"></a> | Prints system identity, uptime, CPU, memory, swap, filesystem, process, and network summary (`-a` detailed, `-s` select identity fields, `-u` local users)[cite: 4]. |
| `evildisk` | Prints disk capacity, filesystem identity, and error/retry totals (`-a` adds labels, UUIDs, error counters, and SMART telemetry; accepts optional device/file filters)[cite: 4]. |
| `evilfiles` | Lists open file descriptors filtered by `-p` (PID), `-u` (owner), `-c` (command prefix), `-i` (sockets), `-t` (terse PIDs), `-w`/`--wide` (untruncated), and optional path operands[cite: 4]. |
| `evilfs` | Prints mounted filesystems[cite: 4]. |
| `evilio` | Prints memory, swap, and cumulative disk I/O counters (`-a` samples 1s CPU/paging/scheduler/PSI/disk/swap rates; `-h`/`--human-readable` binary units; `--ps`, `-n`, `-p`, `-NUMBER` per-process I/O tables)[cite: 4]. Supports `-l`/`--live[=N]`, `--cumulative[=M]`, and `--sort` (`pid`, `read`, `write`, `read-ops`, `write-ops`, `busy`, `read-latency`, `write-latency`, `average-queue`, `queue`, `errors`, `retries`)[cite: 4]. |
| `eviliso` | Inspects isolation primitives: namespaces (`-n`), cgroups (`-c`), login sessions (`-s`), remote sockets (`-r`), container runtimes (`-k`, `--container`), and Kubernetes pods (`--kubernetes`, reading `KUBERNETES_SERVICE_HOST`, service-account namespace, and `kubepods` cgroups), with `-a` for full detail[cite: 4]. |
| `evillogs` | Reports core dump configuration, crash reports (`--cores`), and system log directories (`--logs`)[cite: 4]. |
| `evilnet` | Reports IPv4/IPv6 interface addresses (`-a` adds traffic, errors, drops, and TCP counters; `-f` TCP failure/loss only; `-t`/`--traffic` traffic table only; supports `-l`/`--live[=N]`, `--cumulative[=M]`, and `--sort` by `name`, `bytes`, `packets`, `errors`, `drops`)[cite: 4]. |
| `evilps` | Renders process hierarchy tree (`-p` PIDs, `-n` numeric sort, `-a` all fields, `-A` command lines, `-U` owners, `-c` CPU, `-M` memory, `-w`/`--wide` untruncated, `-NUMBER` row limit, `-l`/`--live[=N]` with `s` sort cycle and `/` search, `-C`/`--cumulative[=M]`, and `--sort` by `name`, `pid`, `cpu`, `memory`)[cite: 4]. |
| `evilss` | Lists sockets filtered by `-l` (listening), `-a` (all), `-t` (TCP), `-u` (UDP), `-x` (Unix), `-4`, `-6`, `-p` (owning process), `-n` (numeric addresses), `-H` (no header), and `--live`[cite: 4]. |
| `goodcore` <a id="utility-goodcore"></a> | Captures live process core (`-p PID`) or packages an existing core and binary (`-b`), bundling `dump/core`, shared libraries under `root/`, and `debug-gdb.sh` / `debug-lldb.sh` wrappers into `goodcore-<core>.tar.zst` (or `.tar.gz` / `-o PATH` / `--no-compress`, `-q` quiet)[cite: 4]. |
| `goodfsw` <a id="utility-goodfsw"></a> | Monitors path metadata changes (via `inotify` plus 30-period full rescans on Linux, or periodic scans elsewhere)[cite: 4]. Supports `-h` (human timestamps/labels), `-m` (numeric mask), `--timezone`, `--precision`, `-r` (recursive), `--event-flags`, `-x` (one filesystem), `-1` (single batch), `-l` (latency period), and `-e` (exclude substring)[cite: 4]. |
| `goodnode` <a id="utility-goodnode"></a> | Reports inode identity, allocation blocks, filesystem capabilities, `--verify` native fs verification status, and CRC32C checksums (`-i` locates an inode under `-r PATH`)[cite: 4]. |
| `goodstat` <a id="utility-goodstat"></a> | Prints labeled file metadata (`-L` dereference symlinks, `-c` include CRC32C checksum, `-f` filesystem capacity report)[cite: 4]. |
| `meow` <a id="utility-meow"></a> | Renders an animated pixel-art cat screensaver (`-f`/`--fps` `1`..`240`, default `60`; `-s`/`--speed` `0.1`..`10`; `-n`/`--cats` `1`..`8`; `Space` pauses, `+`/`-` adjusts speed)[cite: 4]. |
| `retry` <a id="utility-retry"></a> | Re-executes a failing command using `-n` (max attempts), `-d` (initial delay), `-b` (backoff multiplier), `-m` (max delay), and `-q` (quiet)[cite: 4]. |
| `slurp`, `vi`, `emacs` <a id="utility-slurp-vi-emacs"></a> | Invokes the [`slurp`](#builtin-slurp) editor (`vi` implies `-V`; `emacs` implies `-e`)[cite: 4]. |

#### Standard POSIX and Unix utilities

| Utility | Supported flags and behavioral specification |
| --- | --- |
| `basename` <a id="utility-basename"></a> | Strips directory prefix and optional suffix (`-a`/`--multiple`, `-s`/`--suffix` implies `-a`, `-z`/`--zero` NUL terminator)[cite: 4]. |
| `bc` <a id="utility-bc"></a> | Arbitrary-precision calculator language (`-l` loads math library `s`, `c`, `a`, `l`, `e` with `scale=20`; `-q` quiet; division by zero exits status `1`)[cite: 4]. |
| `cal` <a id="utility-cal"></a> | Prints Gregorian calendar (`-a`/`--today` starts weeks on Monday and prints current date string below)[cite: 4]. |
| `cat` <a id="utility-cat"></a> | Concatenates files (`-n`, `-b`, `-s`, `-v`, `-E`, `-T`, `-e`, `-t`, `-A`, `-u`, and `--syntax-highlighting` for terminal shell source)[cite: 4]. |
| `chgrp`, `chown` <a id="utility-chgrp-and-chown"></a> | Modifies group or owner\[:group\] (`-R` recursive, `-H`/`-L`/`-P` symlink traversal, `-h` no-dereference, `-x`/`--one-file-system`)[cite: 4]. |
| `chmod` <a id="utility-chmod"></a> | Applies octal or symbolic permissions (`-R`/`--recursive` without following symlinks, `--one-file-system`)[cite: 4]. |
| `cksum` <a id="utility-cksum"></a> | Prints POSIX CRC checksum, byte count, and filename[cite: 4]. |
| `cmp` <a id="utility-cmp"></a> | Compares two files byte-for-byte (`-l`/`--verbose` octal byte dump, `-s`/`--silent`; returns `0` identical, `1` different, `2` error)[cite: 4]. |
| `comm` <a id="utility-comm"></a> | Compares two sorted files into 3 columns (`-1`, `-2`, `-3`, `--check-order`, `--nocheck-order`, `--output-delimiter`, `--total`, `-z`)[cite: 4]. |
| `cp` <a id="utility-cp"></a> | Copies files and directory trees (`-r`, `-R`, `-f`, `-i`, `-p`, `-v`, `-L`, `-H`, `-P`, `-x`/`--one-file-system`; skips cyclic links with status `1`)[cite: 4]. |
| `csplit` <a id="utility-csplit"></a> | Splits file by line number or `/regex/` into `xx00`..`xxNN` (`-f`/`--prefix`, `-n`/`--digits`, `-s`/`--silent`, `-k`/`--keep-files`)[cite: 4]. |
| `cut` <a id="utility-cut"></a> | Extracts byte (`-b`, `-n`), character (`-c`), or delimited field (`-f`, `-d`, `-s`) ranges (`--complement`, `--output-delimiter`, `-z`)[cite: 4]. |
| `date` <a id="utility-date"></a> | Prints or formats (`+format`) system time (`-u`/`--utc`; rejects clock-setting operands)[cite: 4]. |
| `df` <a id="utility-df"></a> | Reports mounted filesystem space (`-k`, `-P`, `-h`/`--human-readable` base-1024, `-H`/`--si` base-1000)[cite: 4]. |
| `diff` <a id="utility-diff"></a> | Compares files line-by-line (`-u` unified, `-w` ignore whitespace, `-a` text, `-L` label, `--color[=WHEN]`; returns `0`, `1`, or `2`)[cite: 4]. |
| `dirname` <a id="utility-dirname"></a> | Strips final path component (`-z`/`--zero` NUL terminator)[cite: 4]. |
| `du` <a id="utility-du"></a> | Summarizes disk usage sorted descending by size (`-s`, `-h`, `-x`/`--one-file-system`, `--tree`, `-T`/`--top-largest`)[cite: 4]. |
| `env` <a id="utility-env"></a> | Prints environment or runs command under modified environment (`-i` empty, `-u VAR` unset)[cite: 4]. |
| `expand`, `unexpand` <a id="utility-expand-and-unexpand"></a> | Converts tabs to spaces or spaces to tabs (`-t` tab stops with `/N` multiple or `+N` offset extensions; `unexpand -a`)[cite: 4]. |
| `expr` <a id="utility-expr"></a> | Evaluates token arguments using `\|`, `&`, `=`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`, and anchored BRE `:`[cite: 4]. |
| `file` <a id="utility-file"></a> | Classifies file types (`-h`/`--no-dereference`, `-L`/`--dereference`, `-i`/`--regular-only`, `-m`/`--magic-file`, `-M`/`--magic-only`, `-d`)[cite: 4]. |
| `find` <a id="utility-find"></a> | Walks sorted directory trees (`-name`, `-iname`, `-type`, `-maxdepth`, `-mindepth`, `-print`, `-print0`, `-xdev`, `-mount`, `-exec ... ;`, `-exec ... {} +`)[cite: 4]. |
| `flock` <a id="utility-flock"></a> | Acquires file/directory lock for command (`-n`/`--nonblock`, `--transaction-held-lock`)[cite: 4]. |
| `fold` <a id="utility-fold"></a> | Wraps input lines (`-w`/`--width` default `80`, `-s`/`--spaces`, `-b`/`--bytes`)[cite: 4]. |
| `fuser` <a id="utility-fuser"></a> | Lists PIDs accessing files (`-c`, `-f`, `-u`)[cite: 4]. |
| `getconf` <a id="utility-getconf"></a> | Queries POSIX system/path configuration variables (`-a`/`--all`, `-v`/`--specification`)[cite: 4]. |
| `grep`, `egrep`, `fgrep` <a id="utility-grep-egrep-and-fgrep"></a> | Matches BRE, ERE (`-E`/`egrep`), or fixed strings (`-F`/`fgrep`) with `-e`, `-f`, `-i`, `-v`, `-x`, `-n`, `-h`, `-r`, `--one-file-system`, `-c`, `-l`, `-q`, `-s`, `-a`/`--text`, `--color`, `GREP_COLORS`, and `GREP_COLOR`[cite: 4]. |
| `head`, `tail` <a id="utility-head-and-tail"></a> | Prints leading or trailing lines (`-n`) or bytes (`-c` with `b`, `k`/`K`..`Q`, `*B`, `*iB` suffixes; `-q`, `-v`, `-z`; `tail -f`, `-F`, `--follow`, `-s`, `--pid`)[cite: 4]. |
| `id` <a id="utility-id"></a> | Prints user and group IDs (`-u`, `-g`, `-G`, `-n`, `-r`)[cite: 4]. |
| `killall`, `pkill` <a id="utility-killall-and-pkill"></a> | Signals processes by exact name (`killall`) or command substring (`pkill`) with `-s`/`--signal`, `-l`, `pkill -s` session ID, and `--inverse`[cite: 4]. |
| `less` <a id="utility-less"></a> | Full-screen pager buffering up to `128 MiB` (`-N` line numbers, `-S` chop long lines, `-i` ignore case; `/`, `?`, `n`, `N` ERE search)[cite: 4]. |
| `link`, `ln` <a id="utility-link"></a> <a id="utility-ln"></a> | Creates hard links (`link`, `ln`) or symlinks (`ln -s`, `-f`, `-L`, `-P`)[cite: 4]. |
| `locale` <a id="utility-locale"></a> | Inspects locale settings (`-a`, `-m`, `-k`, `-c`)[cite: 4]. |
| `logger` <a id="utility-logger"></a> | Writes messages to syslog (`-f`, `-t`, `-p`, `-i`, `-s`)[cite: 4]. |
| `logname`, `whoami`, `who` <a id="utility-logname"></a> <a id="utility-whoami"></a> <a id="utility-who"></a> | Prints login username (`logname`), effective username (`whoami`), or active sessions (`who -abdHlmpqrstTu`, `am i`)[cite: 4]. |
| `ls` <a id="utility-ls"></a> | Lists directory contents (`-a`, `-A`, `-1`, `-l`, `-h`, `-F`, `-p`, `-d`, `-n`, `-g`, `-o`, `-t`, `-S`, `-r`, `-R`, `--tree`, `-L`, `--one-file-system`, `-k`)[cite: 4]. |
| `make` <a id="utility-make"></a> | Evaluates Makefiles (`-f`, `-C`, `-B`, `-k`, `-e`, `-i`, `-S`, `-n`, `-p`, `-q`, `-r`, `-s`, `-t`, `-j`; sets `MAKE="koshkit make"`)[cite: 4]. |
| `man` <a id="utility-man"></a> | Prints manual page source from `MANPATH` (`-k`/`--keyword` searches headers)[cite: 4]. |
| `mkdir`, `rmdir` <a id="utility-mkdir"></a> <a id="utility-rmdir"></a> | Creates directories (`mkdir -p`, `-m`) or removes empty directories (`rmdir -p`)[cite: 4]. |
| `mkfifo`, `mknod` <a id="utility-mkfifo"></a> <a id="utility-mknod"></a> | Creates FIFOs (`mkfifo -m`) or special device nodes (`mknod --type`, `--major`, `--minor`, `-m`)[cite: 4]. |
| `more` <a id="utility-more"></a> | Screen-by-screen pager buffering up to `128 MiB` (`-n`, `-c`, `-e`, `-p`, `-t`, `-s`, `-i`, `-u`)[cite: 4]. |
| `mv` <a id="utility-mv"></a> | Renames/moves paths (`-f`, `-i`, `-v`)[cite: 4]. |
| `nice`, `renice` <a id="utility-nice"></a> <a id="utility-renice"></a> | Runs command with adjusted niceness (`nice -n`) or mutates live process niceness (`renice -n`, `-p`, `-g`, `-u`)[cite: 4]. |
| `nl` <a id="utility-nl"></a> | Numbers lines across logical page sections (`-d`, `-h`, `-b`, `-f`, `-v`, `-p`, `-i`, `-l`, `-w`, `-n`, `-s`)[cite: 4]. |
| `nohup` <a id="utility-nohup"></a> | Runs command immune to `SIGHUP`, redirecting terminal stdout to `nohup.out` (`0600`)[cite: 4]. |
| `nproc` <a id="utility-nproc"></a> | Prints available CPU count (`--all`, `--ignore=COUNT`)[cite: 4]. |
| `od` <a id="utility-od"></a> | Dumps file bytes in octal/hex/decimal/char formats (`-t`, `-b`, `-c`, `-d`, `-o`, `-s`, `-x`, `-w`, `-A`, `-j`, `-N`, `-v`)[cite: 4]. |
| `paste` <a id="utility-paste"></a> | Merges corresponding file lines (`-d`, `-s`, `-z`)[cite: 4]. |
| `pathchk` <a id="utility-pathchk"></a> | Validates path portability (`-p`, `-P`, `-W`/`--windows`)[cite: 4]. |
| `pr` <a id="utility-pr"></a> | Paginates files into 66-line headed pages (`+first[:last]`, `-columns`, `-a`, `-m`, `-t`, `-d`, `-n`, `-o`, `-h`, `-D`, `-l`, `-w`, `-s`, `-F`, `-f`, `-r`)[cite: 4]. |
| `printenv` <a id="utility-printenv"></a> | Prints environment variables[cite: 4]. |
| `ps` <a id="utility-ps"></a> | Lists active processes (`-a`, `-u`, `-x`, `-w`)[cite: 4]. |
| `readlink`, `realpath` <a id="utility-readlink"></a> <a id="utility-realpath"></a> | Resolves symlinks and canonical paths (`-f`, `-e`, `-m`, `-n`, `-q`, `-s`, `-z`; `realpath -P`, `-L`, `--relative-to`, `--relative-base`)[cite: 4]. |
| `rm`, `unlink` <a id="utility-rm"></a> <a id="utility-unlink"></a> | Removes directory entries (`rm -r`, `-R`, `-d`, `-f`, `-i`, `-I`, `--interactive`, `-v`, `--dry-run`, `--no-preserve-root`, `--preserve-root=all`, `-x`; `unlink` removes 1 non-directory)[cite: 4]. |
| `sed` <a id="utility-sed"></a> | Stream editor supporting `s` (`g`, `p`), `y`, `d`, `p`, `q`, `=`, `a`, `i`, `c` with `-n`, `-E`, `-e`, `-f` (hold space, branches, blocks, and `-i` are unsupported and return `1`)[cite: 4]. |
| `seq` <a id="utility-seq"></a> | Generates numeric sequences (`-w`, `-s`, `-f`)[cite: 4]. |
| `sleep`, `timeout`, `watch` <a id="utility-sleep"></a> <a id="utility-timeout"></a> <a id="utility-watch"></a> | Delays execution (`sleep`), bounds runtime (`timeout -s`, `-k`, `-p`), or repeats periodically (`watch -n`, `-t`, `-g`, `-e`, `-x`)[cite: 4]. |
| `sort` <a id="utility-sort"></a> | Sorts input lines (`-k`, `-t`, `-b`, `-d`, `-f`, `-i`, `-n`, `-r`, `-g`, `-h`, `-M`, `-V`, `-z`, `-u`, `-c`, `-C`, `-o`, `-s`, `-m`)[cite: 4]. |
| `split` <a id="utility-split"></a> | Splits files into fixed line (`-l`) or byte (`-b`) chunks (`-a` suffix length)[cite: 4]. |
| `stat` <a id="utility-stat"></a> | Prints file or filesystem status (`-L`, `-f`, `-t`, `-c`, `--printf`)[cite: 4]. |
| `strings` <a id="utility-strings"></a> | Extracts printable character sequences (`-n` min length default `4`, `-t` radix, `-a`)[cite: 4]. |
| `stty`, `tabs`, `tput`, `tty` <a id="utility-stty"></a> <a id="utility-tabs"></a> <a id="utility-tput"></a> <a id="utility-tty"></a> | Inspects/sets terminal modes (`stty -a`, `-g`), sets tab stops (`tabs`), emits terminfo escapes (`tput`), or prints tty path (`tty -s`)[cite: 4]. |
| `sync` <a id="utility-sync"></a> | Flushes filesystem buffers (`-d`, `-f`)[cite: 4]. |
| `tee` <a id="utility-tee"></a> | Duplicates stdin to files and stdout (`-a` append)[cite: 4]. |
| `touch` <a id="utility-touch"></a> | Updates file access/modification timestamps (`-a`, `-c`, `-m`, `-r`, `-t`)[cite: 4]. |
| `tr` <a id="utility-tr"></a> | Translates, squeezes (`-s`), or deletes (`-d`) bytes (`-c`, `-C`, `-t`)[cite: 4]. |
| `tsort` <a id="utility-tsort"></a> | Performs topological sort on word pairs (returns `1` on cycle)[cite: 4]. |
| `uname` <a id="utility-uname"></a> | Prints OS/kernel metadata (`-a`, `-m`, `-n`, `-r`, `-s`, `-v`)[cite: 4]. |
| `uniq` <a id="utility-uniq"></a> | Filters adjacent duplicate lines (`-c`, `-d`, `-u`, `-D`, `-f`, `-s`, `-w`, `-i`, `--all-repeated`, `--group`, `-z`)[cite: 4]. |
| `wc` <a id="utility-wc"></a> | Counts lines (`-l`), words (`-w`), characters (`-m`), and bytes (`-c`)[cite: 4]. |
| `which` <a id="utility-which"></a> | Resolves builtins, `PATH` binaries, and bundled utilities (`-a`, `-q`)[cite: 4]. |
| `xargs` <a id="utility-xargs"></a> | Builds and executes command lines from stdin (`-a`, `-0`, `-d`, `-r`, `-E`, `-e`, `-I`, `-i`, `-L`, `-n`, `-s`, `-P`, `-p`, `-t`, `-x`)[cite: 4]. |
| `yes` <a id="utility-yes"></a> | Repeatedly outputs joined operands or `y`[cite: 4]. |

### Windows platform semantics <a id="windows"></a>

On Windows, Koshka executes the exact same shell grammar with the following OS adaptations[cite: 4]:
* **Virtual path translation** - `/dev/null` maps to the Windows `NUL` device (`compatibility.dev_null=on`), and `/tmp` (or paths beneath `/tmp`) maps to `$TEMP` (`compatibility.slash_tmp=on`)[cite: 4]. Bash startup files retain their POSIX paths[cite: 4].
* **Environment and process model** - Environment variable identifiers are case-insensitive, individual exported values truncate at `32,766` UTF-16 code units, `PATH` (or `Path`) uses `;` delimiters (ignoring empty segments), and user configuration falls back to `$USERPROFILE` when `$HOME` is unset[cite: 4]. Subshells spawn in new OS processes, and background jobs run in dedicated console process groups[cite: 4]. Process substitutions (`<(...)`, `>(...)`) use FIFO-like pipes where sequential readers continue from the prior read offset and sequential writers append to the stream[cite: 4].

### Runtime file paths <a id="runtime-files"></a>

| File path | Purpose |
| --- | --- |
| `/etc/kosh.conf` (POSIX), `%ProgramData%\kosh\kosh.conf` (Windows) | System-wide settings file[cite: 4]. |
| `$XDG_CONFIG_HOME/kosh/kosh.conf`, `~/.config/kosh/kosh.conf` | User settings file[cite: 4]. |
| `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh`, `~/.config/kosh/scripts/rc.kosh` | Interactive and login startup script for the `kosh` mood[cite: 4]. |
| `~/.kosh_history`, `~/.kosh_directory_history`, `~/.kosh_calc_history` | Default command history, `z` directory history, and `calc` expression history files[cite: 4]. |
| `/etc/profile`, `~/.profile`, `~/.bash_profile`, `~/.bash_login`, `~/.bashrc`, `/etc/bash.bashrc` | Compatibility startup files for `sh`, `bash`, and `bash-posix` moods[cite: 4]. |

---

## Bash and POSIX language specification <a id="bash-and-posix-reference"></a>

### Invocation, operands, and compatibility startup <a id="invocation"></a> <a id="operands"></a> <a id="bash-and-posix-startup-files"></a>

* **Input source precedence** - Evaluated in strict order: `-s` (stdin), `-c` (inline command strings), `script` operand (where `-` selects stdin; a directory fails with `126`, an unopenable file fails with `127`), `-i` or terminal stdin (interactive editor), or non-interactive stdin[cite: 4]. Selecting multiple sources emits a warning and uses the highest-priority source, except `-s -i` on a terminal starts the interactive editor[cite: 4]. Passing `-i` without a terminal fails after startup files complete[cite: 4].
* **Positional parameter assignment (`$0` and `$1..$N`)** - For scripts, `$0` is the script path and remaining operands become `$1..$N`[cite: 4]. For `-c`, the first operand after the command string(s) becomes `$0` and subsequent operands become `$1..$N`[cite: 4]. For `-s` and interactive sessions, `$0` remains the shell invocation name and all operands become `$1..$N`[cite: 4].
* **Invocation failure and rescue mode** - When `$0` begins with `-` (login shell) and option parsing fails, the shell warns and enters **rescue mode** (skipping all settings and startup files)[cite: 4]. If only `KOSH_FLAGS` fails to parse, `KOSH_FLAGS` is dropped[cite: 4]. If command-line flags fail to parse in a login shell, only `-r` is retained and commands are read from stdin[cite: 4]. All other invalid option combinations exit with status `2`[cite: 4].

| Mood and session type | Startup files sourced (in order) |
| --- | --- |
| `sh` login | `/etc/profile`, then `~/.profile`[cite: 4]. |
| `sh` interactive | File specified by expanded `$ENV` (no `PATH` search; skipped under `-p`)[cite: 4]. |
| `bash` / `bash-posix` login | `/etc/profile`, then the first readable file among `~/.bash_profile`, `~/.bash_login`, and `~/.profile`[cite: 4]. |
| `bash` / `bash-posix` interactive non-login | First readable file among `/etc/bash/bashrc` and `/etc/bash.bashrc`, then `--rcfile` or `~/.bashrc`[cite: 4]. If one was sourced, tab completion is on, no default `complete -D` spec exists, and `BASH_COMPLETION_VERSINFO` is unset, sources `/usr/share/bash-completion/bash_completion`[cite: 4]. |
| `bash` non-interactive (and after `bash` login profiles or `-L bash`) | File specified by expanded `$BASH_ENV` once (no `PATH` search; not sourced in `bash-posix` or under `-p`)[cite: 4]. |

### Lexical grammar, reserved words, and quoting <a id="terminology"></a> <a id="reserved-words"></a> <a id="comments"></a> <a id="quoting"></a>

* **Lexical tokens** - **Blanks** are spaces and tabs[cite: 4]. **Metacharacters** (`space`, `tab`, `newline`, `|`, `&`, `;`, `(`, `)`, `<`, `>`) separate unquoted words[cite: 4]. **Control operators** are `newline`, `||`, `&&`, `&`, `;`, `;;`, `;&`, `;;&`, `|`, `|&`, `(`, and `)`[cite: 4]. **Identifiers** consist of ASCII letters, digits, and `_`, not starting with a digit[cite: 4]. **Comments** begin with an unquoted `#` at the start of a word (always enabled in scripts; governed by `interactive_comments`, default `on`, in interactive shells)[cite: 4].
* **Reserved words** - Recognized only when unquoted as the first word of a command, the third word of `case` or `for`, or immediately following another command-list-starting reserved word[cite: 4]:
  `!` `case` `coproc` `do` `done` `elif` `else` `esac` `fi` `for` `function` `if` `in` `select` `then` `time` `until` `while` `{` `}` `[[` `]]`[cite: 4].
  `select` is reserved only in `bash` and `bash-posix`[cite: 4]. The `sh` mood does not reserve `coproc`, `select`, or `time`, and rejects `[[` as a fatal dialect error[cite: 4].
* **Quoting mechanisms**:
  * **Backslash (`\`)** - Preserves the following character, or joins lines before a newline (a trailing `\` at EOF is a syntax error in `kosh` and literal in compatibility moods; on Windows, unquoted `\` in native paths acts as an escape and must be quoted or doubled)[cite: 4].
  * **Single quotes (`'...'`)** - Preserve all characters literally; cannot contain `'`[cite: 4].
  * **Double quotes (`"..."`)** - Enable `$`, `` ` ``, and `\` (where `\` escapes only `$`, `` ` ``, `"`, `\`, or `newline`), preserving field boundaries for `"$@"` and `"${a[@]}"`[cite: 4].
  * **ANSI-C quoting (`$'...'`)** - Expands `\a`, `\b`, `\e`, `\E`, `\f`, `\n`, `\r`, `\t`, `\v`, `\\`, `\'`, `\"`, `\?`, `\xH`/`\xHH`, `\NNN` (octal), `\uHHHH`/`\UHHHHHHHH` (UTF-8), and `\cX` (control byte), terminating at any `NUL` byte[cite: 4]. Unrecognized in the `sh` mood[cite: 4].
  * **Locale translation (`$"..."`)** - Behaves as double quotes (unrecognized in the `sh` mood)[cite: 4].

### Shell grammar constructs <a id="shell-grammar"></a> <a id="simple-commands"></a> <a id="pipelines"></a> <a id="lists"></a> <a id="compound-commands"></a> <a id="coprocesses"></a> <a id="function-definitions"></a>

The `sh` mood enforces `dash` compatibility: it rejects `[[ ]]`, C-style `for (( ))`, `|&`, and `<<<` at parse time; treats `$'...'`, `$"..."`, brace expansion, `&>`, `&>>`, `>&file`, `(( ))`, `$[ ]`, `${ cmd; }`, `${v@op}`, and quotes inside `$(( ))` as literal/standard tokens; accepts `<(...)`, `function`, and `a[i]=v`; and evaluates compound array assignments `a=(x y)` as empty scalar assignments[cite: 4].

| Construct | Syntax and execution semantics across moods |
| --- | --- |
| Simple commands | `[var=val ...] [word \| redirection ...]`[cite: 4]. All prefix assignment values expand *before* any assignment binds (in `a=7 b=$a cmd`, `b` receives the prior value of `a`)[cite: 4]. Prefix assignments on special builtins persist in the shell in `kosh`, `bash-posix`, and `sh`, but are local to the builtin in `bash`; prefix assignments on functions are temporary in all moods[cite: 4]. Exit status is `128 + signo` if killed by a signal[cite: 4]. |
| Pipelines | `[time [-p] [-R]] [!] cmd1 [ { \| \| \|& } cmd2 ... ]`[cite: 4]. `\|&` pipes both stdout and stderr (`2>&1 \|`)[cite: 4]. In `kosh` and `sh`, a builtin in the final pipeline stage executes in the current shell process (`echo 5 \| read x` sets `x`); in `bash` and `bash-posix`, the final stage runs in the current shell only when `lastpipe=on` and job control (`monitor`) is `off`[cite: 4]. Empty pipeline stages (`p=1 \| cat`) are errors in `kosh` and no-op status `0` in compatibility moods[cite: 4]. Status is the last stage's status, or the rightmost failing stage under `pipefail`, negated by `!`, with all stage statuses recorded in `PIPESTATUS[@]`[cite: 4]. `time` reports real/user/sys time (plus CPU % and peak RSS in `kosh` or with `-R`; `-p` uses POSIX format; follows `TIMEFORMAT`)[cite: 4]. |
| Lists (`;`, `&`, `&&`, `\|\|`) | `&&` and `\|\|` share equal left-associative precedence and bind tighter than `;` and `&`[cite: 4]. Trailing `&` runs the entire preceding and-or list asynchronously in a background subshell (status `0`, PID in `$!`)[cite: 4]. In `kosh`, words expand *before* spawning a background simple command; in compatibility moods, expansion occurs inside the spawned child (failing with `127` or `1` in Bash moods and `2` in `sh`)[cite: 4]. |
| Subshell `(list)` & Group `{ list; }` | `(list)` executes in an isolated subshell environment; `{ list; }` executes in the current shell and requires a trailing `;` or newline before `}`[cite: 4]. |
| Arithmetic `((expr))` & Conditional `[[ expr ]]` | `((expr))` returns `0` if `expr` evaluates nonzero, and `1` if zero or empty[cite: 4]. `[[ expr ]]` disables word splitting and globbing on operands, returning `0`, `1`, or `2` on invalid syntax[cite: 4]. |
| Loops (`for`, `select`, `while`, `until`) | `for name [in word ...]; do list; done` (requires `do...done`, rejecting `{...}`)[cite: 4]. `for (( expr1; expr2; expr3 )); do list; done` treats empty expressions as `1`[cite: 4]. `select name [in word ...]; do list; done` (`bash`/`bash-posix` only) prints a numbered menu on stderr using `PS3` and stores raw input in `REPLY`[cite: 4]. `while` and `until` loop while the condition returns `0` or nonzero[cite: 4]. |
| Pattern branch `case` & Conditional `if` | `case word in [(] pat [\| pat]...) list ;;\|;&\|;;& esac` where `;;` stops, `;&` falls through without testing the next pattern, and `;;&` tests subsequent patterns[cite: 4]. `if list; then list; [elif list; then list;]... [else list;] fi`[cite: 4]. |
| Coprocess `coproc` | `coproc [NAME] command [redirections]` (unsupported in `sh`)[cite: 4]. Spawns `command` asynchronously with two pipes (> fd `9`, closed on `exec` and not inherited by subshells), storing read/write descriptors in `NAME[0]` / `NAME[1]` (`COPROC` default; `NAME` is recognized only before a compound command) and PID in `NAME_PID` and `$!`[cite: 4]. |
| Functions `fname()` | `fname () command [redirections]` or `function fname [()] command [redirections]`[cite: 4]. Stores `command` (any simple or compound command) and attaches redirections to every invocation[cite: 4]. Variables are global unless declared `local` (`local var` without `=val` inherits the outer value in `kosh` and `sh`, and starts unset in Bash moods)[cite: 4]. Nesting caps at `900` function calls and `400` sourced files[cite: 4]. |

### Parameters, variables, and arrays <a id="parameters"></a> <a id="positional-parameters"></a> <a id="special-parameters"></a> <a id="shell-variables"></a> <a id="arrays"></a>

* **Assignment evaluation** - `name=value` and `name+=value` undergo tilde expansion (after `=` and after any `:`), parameter/command/arithmetic expansion, and quote removal, without word splitting or globbing[cite: 4].
* **Special parameters**:
  * `$*` and `$@`: Positional parameters from `$1`[cite: 4]. Unquoted, both split on `IFS`[cite: 4]. `"$*"` joins with the first byte of `IFS`; `"$@"` expands to separate words[cite: 4]. When joined into a single scalar (such as `v=$@`), `$@` joins with the first byte of `IFS` in `sh` and with a space in Bash/`kosh` moods[cite: 4]. In the `sh` mood, `$@` and `$*` are always treated as set inside `${@-d}` and `${@+p}`[cite: 4].
  * `$#` (parameter count), `$?` (last foreground exit status), `$-` (active option flags, including `h` and `B` by default, `c` for `-c`, and `u` in `kosh`), `$$` (shell PID, preserved in subshells), `$!` (last background/coproc/process-substitution PID), `$0` (shell/script name), and `$_` (final argument of previous simple command)[cite: 4].
* **Indexed and associative arrays** - Indexed arrays (`a=(x y)`, `a=([2]=c)`, `a[i]=v`, `a+=(w)`) support sparse non-contiguous integer subscripts (`${a[i]}`, where negative indices count from the end, and scalar `$a` is `${a[0]}`)[cite: 4]. Associative arrays (`declare -A m`, `m[key]=v`) preserve insertion order when listing keys via `${!m[@]}`[cite: 4]. Under `set -u`, `${a[@]}` and `${a[*]}` on an unset array expand cleanly to nothing, whereas `$a` and `${a[0]}` trigger an unbound variable error[cite: 4]. Exporting an array (`export a` / `declare -x a`) records the `-x` attribute in `declare -p` without passing the array into child process environments[cite: 4].
* **Shell-maintained and configuration variables** - Beyond the Koshka variables in [Koshka variables](#koshka-variables), Koshka maintains `BASH`, `BASH_VERSION` (`5.3.0(1)-kosh`), `BASH_VERSINFO`, `BASH_ALIASES`, `BASH_ARGC`/`BASH_ARGV` (populated under `extdebug`), `BASH_ARGV0`, `BASH_COMMAND`, `BASH_EXECUTION_STRING`, `BASH_LINENO`/`BASH_SOURCE`/`FUNCNAME` (empty in `sh`), `BASH_MONOSECONDS`, `BASH_REMATCH`, `BASH_SUBSHELL`, `BASHOPTS`, `BASHPID`, `COMP_*`, `COPROC`/`COPROC_PID`, `DIRSTACK`, `EPOCHREALTIME`/`EPOCHSECONDS`, `EUID`/`UID`, `GROUPS`, `HISTCMD`, `HOSTNAME`/`HOSTTYPE`/`MACHTYPE`/`OSTYPE`, `LINENO`, `MAPFILE`, `OLDPWD`/`PWD`, `OPTARG`/`OPTIND`, `PIPESTATUS`, `PPID` (read-only except in `sh`), `RANDOM`, `REPLY`, `SECONDS`, `SHELLOPTS`, `SHLVL` (resets to `1` if negative or `>= 1000`), and `SRANDOM`, and honors `BASH_COMPLETION_VERSINFO`, `BASH_ENV`/`ENV`, `BASH_XTRACEFD`, `CDPATH`, `COLUMNS`/`LINES`, `FCEDIT`/`EDITOR`/`VISUAL`, `GLOBIGNORE`, `HOME`, `IFS`, `IGNOREEOF`, `LANG`/`LC_*`, `MANPATH`, `NO_COLOR`, `OPTERR`, `PATH`, `PROMPT_COMMAND`/`PS0`..`PS4`/`RPS1`/`RPROMPT`, `SHELL`, `TERM`/`COLORTERM`/`TERM_PROGRAM`, `TIMEFORMAT`, `TMPDIR`, `TZ`, and `XDG_CONFIG_HOME`[cite: 4].

### Word expansions, redirections, arithmetic, and conditionals <a id="expansions"></a> <a id="brace-expansion"></a> <a id="tilde-expansion"></a> <a id="parameter-expansion"></a> <a id="command-substitution"></a> <a id="arithmetic-expansion"></a> <a id="process-substitution"></a> <a id="field-splitting"></a> <a id="pathname-expansion"></a> <a id="pattern-matching"></a> <a id="quote-removal"></a> <a id="redirections"></a> <a id="arithmetic-evaluation"></a> <a id="conditional-expressions"></a>

#### Parameter and substitution forms

| Expansion syntax | Evaluation semantics and mood constraints |
| --- | --- |
| `${name:-word}`, `${name:=word}`, `${name:?word}`, `${name:+word}` | Default, assign-default, error-if-unset, and alternate-value forms (omitting `:` tests only for unset, not empty `""`)[cite: 4]. Inside double quotes or here-documents, `word` follows double-quote rules (`"${v:-'a'}"` retains `'a'`)[cite: 4]. |
| `${#name}`, `${name:offset[:length]}` | Character length and substring/array slice (negative `offset` requires leading space `${v: -3}` to disambiguate from `:-`; negative `length` trims from end)[cite: 4]. |
| `${name#pat}`, `${name##pat}`, `${name%pat}`, `${name%%pat}` | Strip shortest/longest matching prefix (`#`/`##`) or suffix (`%`/`%%`)[cite: 4]. |
| `${name/pat/str}`, `${name//pat/str}`, `${name/#pat/str}`, `${name/%pat/str}` | Replace first, all (`//`), prefix (`/#`), or suffix (`/%`) matches[cite: 4]. While `patsub_replacement=on` (default), unquoted `&` in `str` expands to the matched text[cite: 4]. |
| `${name^pat}`, `${name^^pat}`, `${name,pat}`, `${name,,pat}`, `${name~pat}`, `${name~~pat}` | Convert first or all matching characters to uppercase (`^`), lowercase (`,`), or inverted case (`~`)[cite: 4]. |
| `${name@op}` | Value transformations (unsupported in `sh`): `Q` (shell-quoted), `E` (`$'...'` escapes), `P` (prompt expansion), `A` (declaration statement), `a` (attribute flags), `U`/`L`/`u` (case conversion), `K`/`k` (array key-value pairs)[cite: 4]. |
| `${!name}`, `${!prefix*}`, `${!prefix@}`, `${!name[@]}`, `${!name[*]}` | Indirect variable lookup (or nameref target name for `declare -n`), variable names matching `prefix`, and array indices/keys[cite: 4]. |
| `$(cmd)`, `` `cmd` ``, `$(< file)` | Subshell command substitution (strips trailing newlines; `kosh` preserves `NUL` bytes in output while compatibility moods drop `NUL` bytes; max nesting depth `64`)[cite: 4]. |
| `${ cmd; }`, `${\| cmd; }` | In-process function substitutions (supported in `kosh`, `bash`, `bash-posix`): `${ cmd; }` captures stdout and `${\| cmd; }` expands to `$REPLY` without spawning a subshell[cite: 4]. |

#### Pathname globbing and pattern matching

* **Sort order and no-match behavior** - Glob results sort by byte value in `kosh` and `sh`, and by `LC_COLLATE` order in Bash moods[cite: 4]. A pattern with no matches is a fatal error in `kosh`, stays literal in compatibility moods by default, expands to nothing under `nullglob`, and errors under `failglob`[cite: 4]. Assigning non-empty `GLOBIGNORE` filters matching results and automatically enables `dotglob`[cite: 4].
* **Extended globs (`extglob`)** - `?()`, `*()`, `+()`, `@()`, and `!()` are enabled by default in `kosh`, always enabled inside `[[ == ]]` and `[[ != ]]`, and controlled by `shopt -s extglob` elsewhere[cite: 4].

#### Redirections

Redirections process left-to-right; prefixing `{var}` allocates a descriptor `>= 10` and stores the integer in `var`[cite: 4].
* **Operators** - `[n]<word`, `[n]>word` (respects `noclobber`), `[n]>|word` (overrides `noclobber`), `[n]>>word` (inside a loop body, `>>` opens the file once for the entire loop lifetime), `[n]<>word`, `&>word` / `>&word` / `&>>word` (rejected in `sh`), `[n]<&m` / `[n]>&m`, `[n]<&-` / `[n]>&-` (close), `[n]<&m-` / `[n]>&m-` (move and close `m`), `[n]<<word` / `[n]<<-word` (here-document), and `[n]<<<word` (here-string, rejected in `sh`)[cite: 4]. `/dev/tcp` is not emulated[cite: 4].
* **Expansion scope** - In `kosh` and `sh`, all redirection targets expand in the parent shell[cite: 4]. In Bash moods, redirections on external programs, bare redirections, and subshells expand in the child process[cite: 4].

#### Arithmetic evaluation (`$(( ))`, `(( ))`, `let`)

Supports standard C operator precedence plus `**` and `**=` (exponentiation) and `0b` binary literals alongside decimal, `0` octal, `0x` hex, and `base#digits` (`2`..`64`)[cite: 4].
* **Precision and unset variables** - In `kosh` (or under `--enable-extended-arithmetic` / `interpreter.arithmetic_uses_big_numbers=on`), arithmetic uses arbitrary-precision integers and finite decimals (`$((2**100))` is exact; `$((7.0/2))` is `3.5`), and expanding an unset variable is an error[cite: 4]. In compatibility moods by default, arithmetic uses wrapping 64-bit signed integers and evaluates unset variables as `0`[cite: 4].

#### Conditional expressions (`[[ ]]`, `test`, `[ ]`)

Supports file primaries (`-a`, `-e`, `-b`, `-c`, `-d`, `-f`, `-p`, `-S`, `-h`, `-L`, `-g`, `-u`, `-k`, `-r`, `-w`, `-x`, `-O`, `-G`, `-s`, `-t`, `-ef`, `-nt`, `-ot`), string primaries (`-z`, `-n`, `==`, `=`, `!=`, `<`, `>`), variable/option primaries (`-v` in `[[ ]]` only, `-R` outside `sh`, `-o`), ERE matching (`=~` in `[[ ]]` only, populating `BASH_REMATCH` where `.` matches newlines and invalid regexes return `1`), and integer comparisons (`-eq`, `-ne`, `-lt`, `-le`, `-gt`, `-ge`)[cite: 4]. In `kosh` and `sh`, `test` / `[` rejects `==` with a hint to use `=`[cite: 4].

### Aliases, functions, and command execution <a id="aliases"></a> <a id="functions"></a> <a id="command-execution"></a> <a id="command-execution-environment"></a> <a id="environment"></a>

* **Aliases** - An alias replaces the first word of a simple command with a string, expanded when the command runs, so an alias defined earlier on the same line or in the same compound command applies at once, and an alias in a function body takes its value at each call. A replacement ending with a blank also checks the next word. Chains are followed, a word already being replaced is not replaced again, and quoted words, words with a backslash, words produced by an expansion, and reserved words are never aliases. Aliases expand while `expand_aliases` is on (see [Default option states across moods](#moods)), `alias` and `unalias` manage them, and `BASH_ALIASES` holds them.
* **Functions** - A function runs in the current shell with its own positional parameters, and `$0` stays the script name. Its status is the `return` value or the status of the last command. A function called from a guarded and-or operand or a negation keeps `errexit` suppressed throughout its body. The `RETURN` trap fires at the end of every function body in the `kosh` mood, for bodies that `functrace` follows in the Bash moods, and is rejected in the `sh` mood. `export -f` exports a function as `BASH_FUNC_name%%` for Bash children, and Koshka does not import exported functions.
* **Command lookup** - After expansion, a word holding a slash runs that path directly. Other words resolve in the order given under Command execution lifecycle at the top of this manual, and running a command or `hash` caches its path until `PATH` is assigned, `hash -r` runs, or a directory change affects a relative `PATH` entry. A program runs in a separate process with the exported variables as its environment. A command that is not found returns `127`, a file that cannot run returns `126`, and a command path ending in a directory separator fails with `126` when the target is not a directory. A file without a recognized executable format runs as a shell script in a new shell, without the variables, functions, and traps of the caller.
* **Directory commands** - In an interactive shell, a single directory path in command position changes to that directory as `cd` would, unless a function, builtin, program, or bundled utility of that name exists. A word of only dots moves up one directory per dot after the first. `autocd` controls this, is on in every mood, and never affects scripts.
* **Execution environment** - The shell environment consists of open files, the working directory, the file creation mask, resource limits, traps, variables and their attributes, functions, options, aliases, the directory stack, and the job table. A subshell, command substitution, separate pipeline stage, or asynchronous list receives a copy whose changes do not reach the shell. Caught traps reset to their defaults there, ignored traps stay ignored, and a subshell cannot wait for a job of its parent. A program receives the open descriptors, working directory, creation mask, resource limits, ignored signals, and exported variables, and every descriptor the shell opened for itself is closed in the program.
* **Environment variables** - At startup the shell imports its environment and marks every variable for export. Inherited variables and variables marked with `export` or `declare -x` pass to every program, and a prefix assignment adds a variable for one command. Shell-maintained values such as `SECONDS`, `IFS`, `SHELLOPTS`, and the Koshka variables are not taken from the environment. On Windows, variable names are not case-sensitive and a value holds at most `32,766` UTF-16 units, so longer values reach a child truncated. Names beginning with `KOSH_INTERNAL_` are reserved.

### Error handling, exit statuses, signals, and job control <a id="shell-errors"></a> <a id="exit-status"></a> <a id="signals"></a> <a id="job-control"></a> <a id="history-expansion"></a> <a id="event-designators"></a> <a id="word-designators"></a> <a id="modifiers"></a>

#### Error termination matrix across moods

| Error category | `kosh` mood | `bash` mood | `bash-posix` mood | `sh` mood |
| --- | --- | --- | --- | --- |
| Syntax error in script, `-c`, `eval`, or `source` | Rejects entire unit with status `2` (in `source` of a foreign-mood file, fails only the `source` command)[cite: 4]. | Rejects script/`-c` with `2`; in `eval`/`source`, fails only that command with `2`[cite: 4]. | Aborts non-interactive shell with status `2`[cite: 4]. | Aborts non-interactive shell with status `2`[cite: 4]. |
| Pre-run static analysis `strict` / `lenient` error | Aborts before execution with status `1`[cite: 4]. | Off by default (`-W` emits warnings)[cite: 4]. | Off by default (`-W` emits warnings)[cite: 4]. | Off by default (`-W` emits warnings)[cite: 4]. |
| Arithmetic error in `$(( ))`, subscript, slice, or integer assignment | Aborts script with status `1`[cite: 4]. | Abandons current input line (`$? = 1` on next line; does not trigger `set -e` or `ERR` trap)[cite: 4]. | Outside here-doc, aborts non-interactive shell (`127` in `-c`, `1` in script)[cite: 4]. | Outside here-doc, aborts non-interactive shell with status `2`[cite: 4]. |
| Expansion error: `${v?}`, `set -u`, or unknown `${v@Z}` | Aborts script with status `1`[cite: 4]. | Aborts non-interactive shell (`127` in `-c`, `1` in script)[cite: 4]. | Aborts non-interactive shell (`127` in `-c`, `1` in script)[cite: 4]. | Aborts non-interactive shell with status `2`[cite: 4]. |
| Bad substitution (`${x:}`) or read-only assignment | Aborts script with status `1`[cite: 4]. | Abandons current input line[cite: 4]. | Aborts non-interactive shell (`127` in `-c`, `1` in script)[cite: 4]. | Aborts non-interactive shell with status `2`[cite: 4]. |
| Redirection failure on special builtin or compound command | Aborts script with status `1`[cite: 4]. | Fails command with status `1` and continues[cite: 4]. | Fails command with status `1` and continues[cite: 4]. | Aborts non-interactive shell with status `2`[cite: 4]. |
| Builtin usage error or operational failure | Usage errors (except `declare`, `trap`, `getopts`, `compopt`, `printf`) and operational failures of `bg`, `cd`, `dirs`, `fg`, `kill`, `let`, `local`, `mapfile`, `popd`, `pushd`, `read`, `set`, `shift`, `test`, `ulimit`, `umask`, `z` abort script[cite: 4]. | Sets nonzero status and continues (special builtin errors abort only where specified)[cite: 4]. | Special builtin errors abort or continue per Bash rules[cite: 4]. | Special builtin or `local` error aborts with `2` (unless prefixed by `command`); regular builtin error returns `2` and continues[cite: 4]. |

* **Standard exit codes** - `0` (success), `1` (general/analysis failure), `2` (syntax/usage error), `126` (found but non-executable or directory), `127` (command/script not found), `128 + n` (terminated by signal `n`: `130` `SIGINT`, `141` `SIGPIPE`, `143` `SIGTERM`)[cite: 4].
* **Signals and PID 1 reaper** - The shell ignores `SIGPIPE` (resetting it to default in child processes) and interactive shells additionally ignore `SIGTERM`, `SIGQUIT`, `SIGTSTP`, and `SIGTTIN`[cite: 4]. When running as PID `1`, `kosh` automatically reaps adopted orphan processes and on `SIGTERM` broadcasts `SIGTERM` + `SIGCONT` to all processes, waits for children, and exits with `143`[cite: 4].
* **Job control and history expansion** - Background jobs are addressed via `%n`, `%+`, `%%`, `%`, `%-`, `%prefix`, or `%?substring` (requires quoting in `kosh` mood to prevent globbing)[cite: 4]. The shell retains the 1024 most recent child exit statuses and never sends `SIGHUP` to background jobs on exit[cite: 4]. Interactive shells with `histexpand=on` expand `!` history event designators (`!!`, `!n`, `!-n`, `!str`, `!?str?`, `!#`, `^old^new^`), word designators (`:^`, `:$`, `:*`, `:n`, `:n-m`), and modifiers (`:h`, `:t`, `:r`, `:e`, `:s/old/new/`, `:gs`, `:as`, `:Gs`, `:&`, `:q`, `:x`, `:p`) before parsing[cite: 4].

### Standard shell builtins reference <a id="builtins"></a>

Special builtins are `:`, `.`, `break`, `continue`, `eval`, `exec`, `exit`, `export`, `readonly`, `return`, `set`, `shift`, `times`, `trap`, `unset`, and (in Bash moods) `source`[cite: 4]. Every builtin accepts `--help` as its first argument (except `echo` and `test` in compatibility moods, and `[` in all moods)[cite: 4].

| Builtin and synopsis | Operational semantics and mood rules |
| --- | --- |
| `: [arg ...]`, `true [arg ...]`, `false [arg ...]` <a id="builtin-noop"></a> <a id="builtin-false"></a> | `:` and `true` return `0`; `false` returns `1`[cite: 4]. |
| `. [--mood[=]MOOD] file [arg ...]`, `source [--mood[=]MOOD] file [arg ...]` <a id="builtin-source"></a> | Executes `file` in current shell (searching `PATH` then `.` when `sourcepath=on`)[cite: 4]. In the `kosh` mood, sourcing standard Bash startup filenames or files with foreign shebangs automatically switches to that mood for the duration of the file and restores `kosh` afterwards (`--mood` overrides explicitly)[cite: 4]. |
| `[ expr ]`, `test expr` <a id="builtin-test"></a> | Evaluates conditional expression (`0` true, `1` false, `2` syntax/integer error)[cite: 4]. |
| `alias [name[=val] ...]`, `unalias [-a] name ...` <a id="builtin-alias"></a> <a id="builtin-unalias"></a> | Defines, prints, or removes command aliases[cite: 4]. |
| `bg [job]`, `fg [job]`, `jobs [-lnprs] [job ...]`, `disown [-ahr] [job ...]`, `wait [-fn] [-p var] [pid\|job ...]`, `suspend [-f]` <a id="builtin-bg"></a> <a id="builtin-fg"></a> <a id="builtin-jobs"></a> <a id="builtin-disown"></a> <a id="builtin-wait"></a> <a id="builtin-suspend"></a> | Job control builtins: resume in background (`bg`) or foreground (`fg`), list jobs (`jobs`), remove from job table (`disown`), wait for completion (`wait -n` waits for next job, `-p var` records PID, `-f` waits for termination across stops), or stop current shell (`suspend`)[cite: 4]. |
| `bind [options] [keyseq:fn ...]` <a id="builtin-bind"></a> | Accepts readline flags (`-lmrvpPsq`, `-f`, `-u`) as a compatibility no-op and returns `0`[cite: 4]. |
| `break [n]`, `continue [n]` <a id="builtin-break"></a> | Exits or advances `n` enclosing loops (`break 0` exits all loops with status `1` in Bash moods, innermost loop with `1` in `kosh`, and aborts non-interactive shell with `2` in `sh`)[cite: 4]. |
| `builtin [--list] [name [arg ...]]`, `command [-pVv] name [arg ...]`, `type [-aPptV] name ...`, `enable [-adnps] [-f file] [name ...]`, `hash [-rRt] [-p path] [name ...]`, `help [-dms] [name ...]` <a id="builtin-builtin"></a> <a id="builtin-command"></a> <a id="builtin-type"></a> <a id="builtin-enable"></a> <a id="builtin-hash"></a> <a id="builtin-help"></a> | Command resolution and introspection: force builtin dispatch (`builtin`), bypass functions/aliases or inspect resolution (`command`), describe command kind/body (`type -V` prints full source/metadata), enable/disable builtins and bundled utilities (`enable`), manage `PATH` cache (`hash`), or view builtin docs (`help`)[cite: 4]. |
| `caller [n]` <a id="builtin-caller"></a> | Prints call stack frame `n` (`line function file`)[cite: 4]. |
| `cd [-L\|-P] [dir]`, `pwd [-LPW]`, `dirs [-clpv] [+N\|-N]`, `pushd [-n] [dir\|+N\|-N]`, `popd [-n] [+N\|-N]` <a id="builtin-cd"></a> <a id="builtin-pwd"></a> <a id="builtin-dirs"></a> <a id="builtin-pushd"></a> <a id="builtin-popd"></a> | Working directory and `DIRSTACK` management (`pwd -W` prints Windows physical path with `/` separators)[cite: 4]. |
| `compgen [options] [word]`, `complete [options] [name ...]`, `compopt [-o\|+o opt] [-DEI] [name ...]` <a id="builtin-compgen"></a> <a id="builtin-complete"></a> <a id="builtin-compopt"></a> | Programmable completion generators and spec registration (`complete` supports `-A`, `-G`, `-W`, `-F`, `-C`, `-X`, `-P`, `-S`, `-o`, `-D`, `-E`, `-I`, `-p`, `-r`)[cite: 4]. |
| `declare [-aAfFgilnprtux] [name[=val] ...]`, `typeset ...`, `local [-aAilnprux] [name[=val] ...]`, `readonly [-aAfp] [name[=val] ...]`, `export [-fnp] [name[=val] ...]`, `unset [-fnv] name ...` <a id="builtin-declare"></a> <a id="builtin-local"></a> <a id="builtin-readonly"></a> <a id="builtin-export"></a> <a id="builtin-unset"></a> | Variable attribute, scope (`local -` saves `set` options for function lifetime), nameref (`-n`), export, and removal builtins[cite: 4]. |
| `echo [-neE] [arg ...]`, `printf [-v var] format [arg ...]` <a id="builtin-echo"></a> <a id="builtin-printf"></a> | Formatted output builtins. `echo` joins its arguments with spaces and a newline (`-n` omits the newline, `-e` interprets backslash escapes, and `-E` disables them). The `kosh` mood interprets escapes by default, except on Windows, where backslashes stay literal without `-e`. The `sh` mood accepts only `-n`, always interprets escapes, and treats `\u` as an escape while `\e` and `\x` stay literal. `xpg_echo` makes the Bash moods interpret escapes by default. `printf` reuses `format` while arguments remain and accepts the C conversions `d`, `i`, `o`, `u`, `x`, `X`, `e`, `E`, `f`, `F`, `g`, `G`, `c`, and `s` with flags, width, and precision, plus `%b` (backslash escapes), `%q` (shell-quoted), and `%(fmt)T` (strftime, with `-1` for the current time). `-v` assigns the result to a variable or array element and is rejected in the `sh` mood. An invalid numeric argument prints `0` and sets status `1`, a missing format returns `2`, and a width or precision over `16777216` is an error. The numeric locale comes from the first nonempty value of `LC_ALL`, `LC_NUMERIC`, and `LANG` at each call. |
| `eval [arg ...]`, `exec [-cl] [-a name] [command [arg ...]]` <a id="builtin-eval"></a> <a id="builtin-exec"></a> | `eval` joins its arguments with spaces and runs the result in the current shell (status of the last command, `0` for empty input, and `2` with an `eval:` location on a syntax error). `exec` replaces the shell with `command` (`-a` sets argument zero, `-l` prefixes it with a dash, and `-c` clears the environment), or applies its redirections to the shell when no command is given. A command that is not found or cannot run ends a non-interactive shell with `127` or `126` in every mood. A restricted shell refuses `exec`. |
| `exit [n]`, `logout [n]`, `return [n]` <a id="builtin-exit"></a> <a id="builtin-logout"></a> <a id="builtin-return"></a> | `exit` runs the `EXIT` trap and exits with `n` modulo `256`, or with the last status. A non-numeric operand returns `2` without exiting, except in the `sh` mood, where the shell exits with `2`, and more than one operand exits with `1`. `logout` behaves as `exit` in a login shell and returns `1` elsewhere. `return` leaves the running function or sourced file with `n` modulo `256`, and returns `2` outside them or on a non-numeric operand. |
| `fc [-e editor] [-lnr] [first [last]]`, `fc -s [old=new ...] [command]`, `history [-cnrwaps] [-d selector] [operand]`, `history -S` <a id="builtin-fc"></a> <a id="builtin-history"></a> | `fc` lists (`-l`, the previous sixteen events by default, `-n` without numbers, `-r` reversed), edits, and reruns history events selected by number, negative offset, or prefix. The editor comes from `-e`, `FCEDIT`, or `EDITOR`, defaulting to `vi` (`ed` in the POSIX moods), and `-` runs the event unedited. `fc -s` applies each literal replacement, prints the event to standard error, and runs it. `history` lists events (the newest `N` with a numeric operand), loads the history file (`-r`, or only new events with `-n`), writes it (`-w`), appends missing private events (`-a`), replaces the private list with the shared file (`-S`), clears the private list (`-c`), prints (`-p`) or stores (`-s`) its operands, and deletes events by number, negative offset, or inclusive range (`-d`). A restricted shell refuses file operands. |
| `getopts optstring name [arg ...]` <a id="builtin-getopts"></a> | Parses the next option from the arguments or positional parameters into `name`, its argument into `OPTARG`, and the next index into `OPTIND`. A letter followed by `:` takes an argument. An unknown option sets `name` to `?` and prints a diagnostic, which `OPTERR=0` or a leading `:` in `optstring` silences (the leading `:` also stores the letter in `OPTARG` and sets `name` to `:` for a missing argument). Returns `0` while options remain and `1` at their end. |
| `kill [-s sig \| -n num \| -sig] pid\|job ...`, `kill -l [sig\|status ...]` <a id="builtin-kill"></a> | Sends a signal (`SIGTERM` by default, given by number or by name with or without `SIG`) to each process or job. `-l` lists signals or translates numbers, statuses above `128`, and names. No operand returns `2`, and an invalid signal or a failed delivery returns `1` (`2` for an invalid signal in the `sh` mood). |
| `let expr ...` <a id="builtin-let"></a> | Evaluates each expression as described under [Arithmetic evaluation](#arithmetic-evaluation), returning `0` when the last value is nonzero and `1` when it is zero or absent. The `sh` mood has no `let`. |
| `mapfile [options] [array]`, `readarray [options] [array]` <a id="builtin-mapfile"></a> | Stores lines of standard input in an indexed array (`MAPFILE` by default). `-t` strips delimiters, `-n` limits the count (`0` for every line), `-s` skips leading lines, `-O` sets the first index, `-d` sets the delimiter byte (NUL when empty), `-u` reads a descriptor, and `-C` runs a callback with the index and line every `-c` lines (`5000` by default). |
| `newgrp [group]` <a id="builtin-newgrp"></a> | Replaces the shell with the `newgrp` program from `PATH` (`127` when missing, `126` when it cannot run). A restricted shell refuses `newgrp`. |
| `read [-ers] [-a array] [-d delim] [-n\|-N count] [-p prompt] [-t timeout] [-u fd] [-q] [name ...]` <a id="builtin-read"></a> | Reads a line and splits it at `IFS`, giving one field to each `name` and the rest of the line to the last (`REPLY` without a name). `-r` keeps backslashes, `-a` fills an indexed array, `-d` sets the delimiter (NUL when empty), `-n` sets a maximum byte count, `-N` reads exactly that many bytes unsplit, `-t` sets a fractional timeout, `-p` prompts at a terminal, `-s` disables echo, `-u` reads a descriptor, `-q` reads one byte and returns `0` for `y` or `Y`, and `-e` is accepted without effect. Returns `1` at end of input (even after storing a partial line) and `142` after a timeout. The `sh` mood requires a name and accepts only `-r` and `-p`. |
| `set [-+abefhkmnprtuvxBCEHPTIARNSGW] [-+o option] [-M mood] [-L moods] [--] [arg ...]` <a id="builtin-set"></a> | Changes options (a minus enables, a plus disables) and replaces the positional parameters with the remaining arguments. Without arguments, prints the shell variables as sorted assignments, and `set -o` / `set +o` print option states or reusable commands. Option letters and long names are listed under [`set -o` options](kosh_5.md#set-o-options), and `set -o posix` / `set +o posix` switch between `bash-posix` and `bash`. `-M` and `-L` select the mood and source other moods' startup files, `-I`, `-A`, `-R`, `-N`, `-S`, and `-G` toggle mimicry and debug settings, and `-W` through `-WWW` set the warning level. Every other Koshka setting belongs to `koshconf`. `+r` fails with `1`. An unknown option returns `1` in the Bash moods and ends a non-interactive shell with `1` in `kosh` and `2` in `sh`. |
| `shift [n]` <a id="builtin-shift"></a> | Discards the first `n` positional parameters (default `1`). A count above `$#` or below `0` returns `1`, and ends a non-interactive shell with `2` in the `sh` mood. |
| `shopt [-pqsuo] [option ...]` <a id="builtin-shopt"></a> | Shows or changes the options listed under [`shopt` options](kosh_5.md#shopt-options). `-s` enables and `-u` disables, or lists the options that are on or off without operands. Without `-s` or `-u`, prints each state and returns `1` when one is off. `-p` prints reusable commands, `-o` selects the `set -o` options, and `-q` prints nothing. An unknown option returns `1` after the valid ones apply. |
| `time [-p] [-R] pipeline`, `times` <a id="builtin-time"></a> <a id="builtin-times"></a> | `time` is a reserved word described under [Shell grammar](#shell-grammar). `times` prints the user and system times of the shell, then of its children. |
| `trap [-lp] [[action] condition ...]` <a id="builtin-trap"></a> | Sets `action` for each signal (by number or name, with or without `SIG`) or condition: `EXIT` or `0` at exit, `ERR` after a failing command where `errexit` would act, `DEBUG` before each simple command, and `RETURN` at the end of a function or sourced file. An empty action ignores the signal and `-` restores the default. Without arguments or with `-p`, prints reusable `trap` commands, and `-l` lists signals. A trap action runs with the status of the preceding command and leaves that status unchanged. An invalid signal returns `1`. The `sh` mood rejects `ERR`, `RETURN`, `-l`, and `-p`. |
| `ulimit [-HSa] [-cdflmnpstuvw] [limit]` <a id="builtin-ulimit"></a> | Shows or sets a resource limit (file size `-f` by default), with `-H` or `-S` selecting the hard or soft limit and `-a` printing all. The resources are core size (`-c`), data size (`-d`), file size (`-f`), locked memory (`-l`), resident set (`-m`), open files (`-n`), pipe size (`-p`), stack size (`-s`), CPU seconds (`-t`), processes (`-u`), virtual memory (`-v`), and file locks (`-w`). A limit is a number or `unlimited`, and an invalid limit or one above the hard limit returns `1`. |
| `umask [-pS] [mask]` <a id="builtin-umask"></a> | Shows or sets the file creation mask in octal or symbolic form (`u=rwx,g=rx,o=`). `-S` prints it symbolically and `-p` as a reusable command. An invalid mask returns `1`. |

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
