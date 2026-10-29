# Koshka configuration reference (kosh_5)

> Declarative specification for command-line flags, shell settings, settings files, startup scripts, and the KOSHCONF wire format[cite: 3].

2026-10-26[cite: 3]

Prerequisites - Familiarity with [kosh(1) invocation and startup files](kosh_1.md#skipping-startup-files)[cite: 3].

Koshka configures execution semantics and interactive line-editing through declarative settings files (`kosh.conf`), procedural startup scripts (`rc.kosh`), the `KOSHCONF` environment payload, and command-line flags[cite: 3]. Every shell instance loads settings files before evaluating any startup script, regardless of whether the session is interactive[cite: 3]. The configured `mood` also governs `kosh -c` invocations and scripts that lack a shebang[cite: 3].

## Core configuration primitives

| Primitive | Role in configuration |
| --- | --- |
| `kosh.conf` | Declarative key-value file that stores persistent shell option values[cite: 3]. |
| `koshconf` | Builtin utility that reads, mutates, and persists option values[cite: 3]. |
| `rc.kosh` | Procedural startup script sourced by interactive and login shells in the `kosh` mood[cite: 3]. |
| `KOSHCONF` | Base64-encoded binary environment payload that propagates mood and interactive settings to child or remote shells[cite: 3]. |
| `mood` | Top-level execution profile (`kosh`, `bash`, `sh`, or `bash-posix`) that sets parser grammar and baseline invariants[cite: 3]. |

## Configuration resolution lifecycle

1. A new shell process inspects its invocation flags and execution privileges[cite: 3]. If `--no-config`, `-p` (`--privileged`), or a setuid/setgid context is active, settings files and `KOSHCONF` are skipped and removed[cite: 3]. See [Skipping startup files](kosh_1.md#skipping-startup-files) for the full exclusion list[cite: 3].
2. The shell parses the system settings file (`/etc/kosh.conf` or `%ProgramData%\kosh\kosh.conf`) if it passes ownership and permission checks[cite: 3].
3. The shell parses the user settings file (`$XDG_CONFIG_HOME/kosh/kosh.conf` or `~/.config/kosh/kosh.conf`), unless the POSIX effective user is `root`[cite: 3]. User settings override system settings[cite: 3]. Within each file, the `mood` line is evaluated first regardless of its line number, and remaining lines are evaluated under that mood[cite: 3].
4. The shell decodes and applies an inherited `KOSHCONF` environment variable, then removes `KOSHCONF` from the environment before any command runs[cite: 3].
5. The shell applies inherited `KOSH_HISTORY_FILE` and `KOSH_HISTORY_SIZE` variables and explicit command-line flags[cite: 3]. Command-line flags override both settings files and `KOSHCONF`[cite: 3].
6. Interactive and login shells source the startup scripts selected by `-L` (`--init-moods`), `init_moods`, or the active session mood[cite: 3].

## Override settings with command-line options <a id="command-line-options"></a>

An option passed on the command line (enabled or disabled) overrides settings files and `KOSHCONF`[cite: 3]. Koshka-specific flags appear first in the table below, followed by general, POSIX, and Bash-compatible flags[cite: 3].

| Option | Effect |
| --- | --- |
| `-M, --mood=NAME` | Select the `kosh`, `bash`, `sh`, or `bash-posix` mood[cite: 3]. |
| `-L, --init-moods=LIST` | Source the startup files of each mood in `LIST`, in order[cite: 3]. |
| `-I, --enable-mimicry` | Execute a script started as a command under the mood selected by its shebang, following `--mood` precedence rules[cite: 3]. |
| `--dumb` | Apply `--mood sh`, `--no-completion`, `--tab-selector plain`, and `--no-diagnostics`, and disable terminal color[cite: 3]. |
| `--lint` | Analyze command strings, file operands, or standard input without execution, as specified in [Linting](kosh_1.md#linting)[cite: 3]. |
| `--format` | Format one file or standard input to standard output, as specified in [Formatting](kosh_1.md#formatting)[cite: 3]. |
| `--apply` | Write safe lint fixes under `--lint`, formatted source under `--format`, or both directly to file operands[cite: 3]. |
| `--as-language-server` | Start the language server protocol daemon, as specified in [Language server](kosh_1.md#language-server)[cite: 3]. |
| `-Q, --no-init-files` | Skip all startup files and reset `PATH` to `/usr/bin:/bin`[cite: 3]. |
| `--no-config` | Skip all settings files, and ignore and unset `KOSHCONF`[cite: 3]. |
| `-W, -WW, -WWW` | Set the static analysis warning tier[cite: 3]. |
| `--list-diagnostics` | Print all static analysis checks and metadata, then exit[cite: 3]. |
| `--no-diagnostics` | Disable static analysis, warnings, and pre-run diagnostics[cite: 3]. |
| `--no-annoying-diagnostics` | Suppress the `annoying` diagnostic tier[cite: 3]. |
| `--no-init-diagnostics` | Suppress diagnostics and warnings while startup files execute, then restore the prior state unless modified by a startup file[cite: 3]. |
| `--no-traces` | Suppress source backtraces (`optimizer.show_source_traces=off`)[cite: 3]. |
| `-T, --no-completion` | Disable tab completion, ghost text, and syntax coloring (`completion.on_tab=off`)[cite: 3]. |
| `--no-syntax-highlighting` | Disable syntax coloring and ghost text while retaining tab completion (`editor.highlight_syntax_and_show_ghost_text=off`)[cite: 3]. |
| `--tab-selector MODE` | Set candidate menu presentation on `Tab` and history search presentation on `Ctrl-R`[cite: 3]. |
| `--enable-koshkit` | Resolve bundled Koshkit utilities directly as commands after `PATH` (`interpreter.resolve_koshkit_applets_as_commands=on`)[cite: 3]. |
| `--enable-extended-arithmetic` | Enable arbitrary-precision integers and finite decimals (`interpreter.arithmetic_uses_big_numbers=on`)[cite: 3]. |
| `-A, --show-ast` | Print abstract syntax trees before execution and during formatting or linting[cite: 3]. |
| `--show-optimizer-diagnostics` | Emit a warning for each constant-folded expression and eliminated dead branch[cite: 3]. |
| `--show-exit-code` | Print a source-located diagnostic when a command returns a nonzero exit status (`debug.report_nonzero_exit_codes=on`)[cite: 3]. |
| `-N, --show-all-exit-codes` | Print the exit status of every command, including status `0`[cite: 3]. |
| `-R, --show-lexed-words` | Print the lexical escape map of each parsed command[cite: 3]. |
| `--show-stats` | Print evaluation counts, arena usage, variable/function/builtin/fork/exec counts, I/O byte and call totals, and resource usage after each command[cite: 3]. |
| `--show-memory` | Print memory consumption for the syntax tree arena, function arenas, and heap at shell exit[cite: 3]. |
| `--ftrace` | Enable function call tracing in Cosmopolitan builds[cite: 3]. |
| `--strace` | Enable system-call tracing in Cosmopolitan builds[cite: 3]. |
| `--version` | Print full version and copyright notices, then exit[cite: 3]. |
| `-V, --short-version` | Print the short version string, then exit[cite: 3]. |
| `--help` | Print command-line usage help, then exit[cite: 3]. |
| `-i, --interactive` | Run in interactive mode[cite: 3]. |
| `-s, --stdin` | Read commands from standard input[cite: 3]. |
| `-c COMMAND, --command=COMMAND` | Execute `COMMAND` string[cite: 3]. |
| `-e, --error-exit` | Exit immediately when an unguarded command returns a nonzero status[cite: 3]. |
| `-f, --no-glob` | Disable pathname globbing[cite: 3]. |
| `-t, --one-command` | Read and execute a single command, then exit[cite: 3]. |
| `-v, --verbose` | Print each input line as it is read[cite: 3]. |
| `-x, --xtrace` | Print each command trace after expansion[cite: 3]. |
| `-a, --export-all` | Mark all subsequently assigned variables for environment export[cite: 3]. |
| `-C, --no-clobber` | Prevent plain output redirection (`>`) from overwriting an existing regular file[cite: 3]. |
| `-n, --no-exec` | Parse and analyze commands without executing them[cite: 3]. |
| `-u, --no-unset` | Treat expansion of an unset variable as a fatal error[cite: 3]. |
| `-l, --login` | Run as a login shell[cite: 3]. |
| `-h, -m, +h, +m` | Accepted for compatibility and ignored[cite: 3]. |
| `-o NAME, +o NAME` | Enable (`-o`) or disable (`+o`) a POSIX or Bash `set -o` option[cite: 3]. |
| `+a, +C, +e, +f, +n, +p, +t, +u, +v, +x` | Disable the corresponding single-letter option, matching `set +e` behavior[cite: 3]. |
| `--` | Terminate option parsing[cite: 3]. |
| `--init-file FILE, --rcfile FILE` | Source `FILE` instead of `~/.bashrc` in an interactive non-login `bash` mood, or instead of `rc.kosh` in an interactive non-login `kosh` mood[cite: 3]. |
| `--norc` | Skip interactive non-login Bash rc files, `rc.kosh`, and any `--rcfile` target[cite: 3]. |
| `-O NAME, +O NAME` | Enable (`-O`) or disable (`+O`) a `shopt` option, matching `shopt -s` and `shopt -u`[cite: 3]. |
| `-r, --restricted` | Enter restricted shell mode after startup files complete[cite: 3]. |
| `-p, --privileged` | Enable privileged execution mode[cite: 3]. |
| `--posix` | Select the `bash-posix` mood, unless overridden by `--mood` or `--dumb`[cite: 3]. |

### Command-line option constraints and precedence <a id="compatibility-option-details"></a>

| Option group | Option | Precedence, defaults, and failure behavior |
| --- | --- | --- |
| Compatibility <a id="compatibility-option-details"></a> | `-M, --mood=NAME` | Precedence order from highest to lowest is `--mood`, `--dumb`, invocation basename (`sh` or `bash`), `--posix` / `-o posix`, script shebang, and settings file / default (`kosh`)[cite: 3]. Without `--mood`, the first line of a script file operand selects `bash` for a Bash shebang, `sh` for an `sh` or `dash` shebang, and `kosh` for a `kosh` shebang (directly or via `env`)[cite: 3]. Any other interpreter or missing shebang defaults to `kosh`[cite: 3]. Shebang inspection is skipped for `-c`, standard input, and `--lint` without `-I`[cite: 3]. |
| Compatibility | `-L, --init-moods=LIST` | Accepts comma-separated mood names or repeated `-L` flags[cite: 3]. Defaults to `init_moods` from settings for interactive or login shells, and to the active execution mood for non-interactive shells[cite: 3]. |
| Compatibility | `-I, --enable-mimicry` | Forces script file operands invoked as commands to run under the mood resolved by their shebang and `--mood` precedence rules[cite: 3]. |
| Compatibility | `--dumb` | An explicit `--tab-selector` flag overrides the `plain` selector set by `--dumb`[cite: 3]. |
| Auxiliary <a id="auxiliary-option-details"></a> | `--format` | Ignores `-c` command strings[cite: 3]. Formatting more than one file operand requires `--apply`[cite: 3]. |
| Auxiliary | `--apply` | Requires `--lint`, `--format`, or both, plus at least one file operand[cite: 3]. Rejects standard input, `-s`, `-i`, `-c`, and `-`[cite: 3]. |
| Auxiliary | `--as-language-server` | Rejects file operands, `-c`, `-s`, `-i`, `--lint`, `--format`, and `--apply`[cite: 3]. |
| Koshka <a id="koshka-option-details"></a> | `--no-config` | Leaves `PATH` and startup script loading unchanged[cite: 3]. |
| Koshka | `-W, -WW, -WWW` | Analysis checks are grouped into `strict`, `lenient`, and `annoying` tiers[cite: 3]. In the `kosh` mood, `strict` and `lenient` checks are errors and `annoying` checks are warnings by default[cite: 3]. `-W` retains that baseline, `-WW` demotes `lenient` errors to warnings, and `-WWW` demotes both `lenient` and `strict` errors to warnings[cite: 3]. In compatibility moods (`bash`, `sh`, `bash-posix`), static analysis is off by default; `-W` enables `strict` warnings, `-WW` enables `strict` and `lenient` warnings, and `-WWW` enables all three tiers as warnings[cite: 3]. Levels set via `set` propagate to child shells through `KOSH_ANALYSIS`[cite: 3]. Runtime diagnostics are unaffected by `-W`, except that compatibility moods warn on empty unset-variable expansion only when a warning level is active[cite: 3]. |
| Koshka | `--tab-selector MODE` | Accepts `interactive` (default), `external`, or `plain`[cite: 3]. See [Line editing](kosh_1.md#line-editing)[cite: 3]. |
| Koshka | `--enable-koshkit` | Defaults to `off`[cite: 3]. Affects only compatibility moods because the `kosh` mood always resolves bundled Koshkit utilities after `PATH`[cite: 3]. |
| Koshka | `--enable-extended-arithmetic` | Always enabled in the `kosh` mood[cite: 3]. |
| Debug <a id="debug-option-details"></a> | `--show-optimizer-diagnostics` | Compatibility moods emit these warnings only when `-WW` or `-WWW` is active[cite: 3]. |
| Debug | `-N, --show-all-exit-codes` | Can be toggled at runtime with `set -N` and `set +N`[cite: 3]. |
| POSIX <a id="posix-option-details"></a> | `-s, --stdin` | Input processing order is specified in [Invocation](kosh_1.md#invocation)[cite: 3]. |
| POSIX | `-c COMMAND, --command=COMMAND` | May be repeated to execute multiple strings in sequence[cite: 3]. A syntax error exits with status `2`, an analysis error exits with status `1`, and subsequent command strings are aborted[cite: 3]. |
| POSIX | `-n, --no-exec` | Compatibility moods report only the diagnostic tiers enabled by `-W`[cite: 3]. |
| POSIX | `-o NAME, +o NAME` | `-o posix` acts as `--posix`[cite: 3]. An unknown option name exits with status `2`[cite: 3]. In the `kosh` mood, `+o nounset` is ignored and `+o pipefail` exits with status `2`[cite: 3]. |
| POSIX | `+a, +C, +e, +f, +n, +p, +t, +u, +v, +x` | `+h` and `+m` are accepted and ignored[cite: 3]. Passing `set`-only flags (`-B`, `-E`, `-H`, `-P`, `-b`, `-k`) on the command line exits with status `2`[cite: 3]. |
| POSIX | `--` | The operand immediately following `--` is treated as the script path[cite: 3]. |
| Bash <a id="bash-option-details"></a> | `--norc` | Does not affect login shells, login profiles, or `BASH_ENV`[cite: 3]. |
| Bash | `-O NAME, +O NAME` | An unknown `shopt` option exits with status `2`[cite: 3]. Read-only options such as `login_shell` are ignored[cite: 3]. In the `kosh` mood, `+O failglob` and `-O nullglob` exit with status `2`[cite: 3]. |
| Bash | `-p, --privileged` | Skips `ENV`, `BASH_ENV`, interactive non-login `rc.kosh`, and all settings files, and unsets `KOSHCONF`[cite: 3]. A setuid or setgid execution automatically drops elevated privileges and skips all startup and settings files[cite: 3]. On Linux, a setuid/setgid process whose real user ID is not `root` also clears all capability sets (including ambient capabilities) and aborts if privilege restoration to `root` remains possible[cite: 3]. |

## Configure shell settings via builtins and variables <a id="shell-settings"></a>

Shell settings are inspected and mutated via `set`, `shopt`, and `koshconf`, and their states are exposed through `[ -o OPTION ]`, `SHELLOPTS`, and `BASHOPTS`[cite: 3]. Each setting has a permanent integer ID, a canonical `koshconf` dot-separated path, a scope (`i` for interactive editor/history/diagnostic behavior, or `s` for semantic script execution behavior), and optional POSIX/Bash spellings[cite: 3].

### Namespace hierarchy and legacy aliases

| Namespace prefix | Subsystem scope | Alias and mapping rules |
| --- | --- | --- |
| `mood`, `init_moods` | Top-level execution and startup mood selection[cite: 3] | Canonical names only[cite: 3]. |
| `interpreter` | Core shell execution semantics[cite: 3] | POSIX `set -o` options use `posix.interpreter.*` and alias to `posix.legacy.NAME`[cite: 3]. Bash options use `bash.interpreter.*` and alias to `bash.legacy.NAME`[cite: 3]. |
| `compatibility` | Windows path translation (`dev_null`, `slash_tmp`)[cite: 3] | Koshka-only options[cite: 3]. |
| `editor` | Line editor, `editor.history`, and `editor.hints`[cite: 3] | `editor.base_mode` (`emacs` or `vi`) accepts the alias `posix.legacy.vi`[cite: 3]. `set -o emacs` and `set -o vi` toggle `editor.base_mode` as in Bash[cite: 3]. `interactive_comments` aliases to `bash.legacy.interactive-comments`[cite: 3]. |
| `optimizer` | Static analysis tiers and source traces[cite: 3] | Koshka-only options[cite: 3]. |
| `completion` | Tab completion behavior and menu style[cite: 3] | Bash programmable completion uses `bash.completion.*`[cite: 3]. |
| `debug` | AST, lexer, exit code, and memory telemetry[cite: 3] | POSIX tracing options use `posix.debug.*`[cite: 3]. |

* Every option operand and `kosh.conf` entry accepts either the canonical name or its `posix.legacy.*` / `bash.legacy.*` alias[cite: 3].
* Error messages, `KOSHCONF`, `koshconf create`, and `koshconf list` emit only canonical names[cite: 3]. Running `koshconf list --all` appends legacy aliases[cite: 3].
* The Bash options `hashall` and `nolog` have no canonical `koshconf` name[cite: 3]. They are accepted by `set` and `-o`, appear in `koshconf list --all` as `posix.legacy.hashall` and `posix.legacy.nolog`, and are rejected by `koshconf get`, `koshconf set`, and `kosh.conf`[cite: 3].
* No-op `bash.legacy.*` `shopt` entries (such as `cdspell` or `histappend`) are accepted so legacy Bash startup files execute without errors, but they do not alter shell behavior[cite: 3]. `shopt` accepts `array_expand_once` as the Bash 5.3 alias for `assoc_expand_once`[cite: 3].

### Settings mutability constraints

| Constraint category | Affected options | Behavior |
| --- | --- | --- |
| Read-only state | `bash.interpreter.login_shell`, `bash.interpreter.restricted_shell` | Fixed by shell invocation; cannot be modified in `kosh.conf` or at runtime[cite: 3]. |
| Command-line fixed | `bash.interpreter.privileged_mode`, `posix.interpreter.parse_without_executing`, `bash.interpreter.exit_after_one_command` | Determined at invocation; rejected in `kosh.conf`[cite: 3]. |
| Session and mood derived | `posix.interpreter.job_control`, `bash.editor.history.records_commands`, `bash.editor.history.bang_expansion`, `bash.interpreter.posixly_correct` | Controlled dynamically by session type and active mood; rejected in `kosh.conf`[cite: 3]. |

### Native Koshka options <a id="koshka-options"></a>

| ID | Scope | Koshconf name | Shell flag | Accepted values and defaults |
| --- | --- | --- | --- | --- |
| 1 | s | `mood` | `set -M` | `kosh`, `sh`, `bash`, or `bash-posix`[cite: 3]. |
| 2 | i | `completion.menu_style` |  | `interactive`, `external`, or `plain`[cite: 3]. |
| 3 | i | `editor.hints.show_command_synopsis` |  | Boolean (`on` or `off`)[cite: 3]. |
| 4 | i | `editor.hints.show_live_diagnostics` |  | Boolean (`on` or `off`)[cite: 3]. |
| 5 | i | `editor.auto_close_brackets_and_quotes` |  | Boolean (`on` or `off`)[cite: 3]. |
| 7 | i | `editor.request_extended_key_reports` |  | Boolean (`on` or `off`)[cite: 3]. |
| 8 | i | `editor.history.arrow_keys_search_by_typed_prefix` |  | Boolean (`on` or `off`)[cite: 3]. |
| 9 | i | `editor.history.file_path` |  | UTF-8 path string; maps to `KOSH_HISTORY_FILE`[cite: 3]. |
| 10 | i | `editor.history.max_entries` |  | Integer `0` to `2147483647`; maps to `KOSH_HISTORY_SIZE`[cite: 3]. |
| 11 | i | `completion.add_space_after_completed_word` |  | Boolean (`on` or `off`)[cite: 3]. |
| 12 | i | `optimizer.warning_level` | `set -W` | Integer `0` to `3`[cite: 3]. |
| 13 | i | `optimizer.show_annoying_tier` |  | Boolean (`on` or `off`)[cite: 3]. |
| 14 | s | `optimizer.analyze_before_running` |  | Boolean (`on` or `off`)[cite: 3]. |
| 15 | s | `interpreter.resolve_koshkit_applets_as_commands` |  | Boolean (`on` or `off`)[cite: 3]. |
| 16 | s | `interpreter.arithmetic_uses_big_numbers` |  | Boolean (`on` or `off`)[cite: 3]. |
| 17 | s | `interpreter.mimic_shebang` | `set -I` | Boolean (`on` or `off`)[cite: 3]. |
| 18 | s | `debug.print_syntax_tree` | `set -A` | Boolean (`on` or `off`)[cite: 3]. |
| 19 | s | `debug.print_lexed_word_escapes` | `set -R` | Boolean (`on` or `off`)[cite: 3]. |
| 20 | s | `debug.report_nonzero_exit_codes` |  | Boolean (`on` or `off`)[cite: 3]. |
| 21 | s | `debug.report_every_exit_code` | `set -N` | Boolean (`on` or `off`)[cite: 3]. |
| 22 | s | `debug.print_evaluation_statistics` | `set -S` | Boolean (`on` or `off`)[cite: 3]. |
| 23 | s | `debug.print_memory_report_at_exit` | `set -G` | Boolean (`on` or `off`)[cite: 3]. |
| 24 | i | `completion.on_tab` |  | Boolean (`on` or `off`)[cite: 3]. |
| 25 | i | `editor.highlight_syntax_and_show_ghost_text` |  | Boolean (`on` or `off`)[cite: 3]. |
| 26 | i | `optimizer.show_source_traces` |  | Boolean (`on` or `off`)[cite: 3]. |
| 27 | s | `init_moods` |  | Comma-separated mood list, empty by default[cite: 3]. Applies only to interactive or login startup, is overridden by `-L`, is ignored by restricted shells in settings files, and is excluded from `KOSHCONF`[cite: 3]. |
| 174 | s | `compatibility.dev_null` |  | Boolean (`on` or `off`)[cite: 3]. |
| 175 | s | `compatibility.slash_tmp` |  | Boolean (`on` or `off`)[cite: 3]. |
| 179 | i | `editor.wrap_width` |  | Integer `0` to `65535`, default `80`[cite: 3]. |

### Shared Bash and POSIX settings <a id="bash-and-posix-settings"></a>

#### `set -o` options <a id="set-o-options"></a>

| ID | Scope | Koshconf name | Shell spelling |
| --- | --- | --- | --- |
| 64 | s | `posix.interpreter.export_every_assigned_variable` | `allexport, -a`[cite: 3] |
| 65 | i | `posix.editor.jobs_report_status_immediately` | `notify, -b`[cite: 3] |
| 66 | s | `posix.interpreter.exit_on_command_failure` | `errexit, -e`[cite: 3] |
| 67 | s | `posix.interpreter.glob_disabled` | `noglob, -f`[cite: 3] |
| 69 | s | `bash.interpreter.assignments_anywhere_in_command` | `keyword, -k`[cite: 3] |
| 70 | i | `posix.interpreter.job_control` | `monitor, -m`[cite: 3] |
| 71 | s | `posix.interpreter.parse_without_executing` | `noexec, -n`[cite: 3] |
| 72 | s | `bash.interpreter.exit_after_one_command` | `onecmd, -t`[cite: 3] |
| 73 | s | `bash.interpreter.privileged_mode` | `privileged, -p`[cite: 3] |
| 74 | s | `posix.interpreter.unset_variable_is_error` | `nounset, -u`[cite: 3] |
| 75 | s | `posix.debug.trace_input_lines` | `verbose, -v`[cite: 3] |
| 76 | s | `posix.debug.trace_expanded_commands` | `xtrace, -x`[cite: 3] |
| 77 | s | `bash.interpreter.brace_expansion` | `braceexpand, -B`[cite: 3] |
| 78 | s | `posix.interpreter.redirect_refuses_to_overwrite_files` | `noclobber, -C`[cite: 3] |
| 79 | s | `bash.interpreter.trap_err_inherited_by_functions` | `errtrace, -E`[cite: 3] |
| 80 | i | `bash.editor.history.bang_expansion` | `histexpand, -H`[cite: 3] |
| 81 | s | `bash.interpreter.cd_resolves_symlinks` | `physical, -P`[cite: 3] |
| 82 | s | `bash.interpreter.trap_debug_and_return_inherited_by_functions` | `functrace, -T`[cite: 3] |
| 83 | s | `posix.interpreter.pipeline_fails_on_any_stage` | `pipefail`[cite: 3] |
| 84 | i | `bash.editor.history.records_commands` | `history`[cite: 3] |
| 85 | i | `posix.editor.ctrl_d_does_not_exit` | `ignoreeof`[cite: 3] |
| 87 | i | `editor.base_mode` | `emacs, vi`[cite: 3] |
| 89 | s | `bash.interpreter.posixly_correct` | `posix`[cite: 3] |

#### `shopt` options <a id="shopt-options"></a>

| ID | Scope | Koshconf name | Shell spelling |
| --- | --- | --- | --- |
| 128 | i | `bash.editor.cd_by_typing_directory_name` | `autocd`[cite: 3] |
| 129 | s | `bash.legacy.assoc_expand_once` | `assoc_expand_once, array_expand_once`[cite: 3] |
| 130 | s | `bash.interpreter.cd_to_variable_value` | `cdable_vars`[cite: 3] |
| 131 | i | `bash.legacy.cdspell` | [cite: 3] |
| 132 | s | `bash.interpreter.verify_remembered_command_paths` | `checkhash`[cite: 3] |
| 133 | i | `bash.legacy.checkjobs` | [cite: 3] |
| 134 | i | `bash.legacy.checkwinsize` | [cite: 3] |
| 135 | i | `bash.legacy.complete_fullquote` | [cite: 3] |
| 136 | i | `bash.legacy.direxpand` | [cite: 3] |
| 137 | i | `bash.legacy.dirspell` | [cite: 3] |
| 138 | s | `bash.interpreter.glob_includes_dotfiles` | `dotglob`[cite: 3] |
| 139 | s | `bash.legacy.execfail` | [cite: 3] |
| 140 | s | `bash.interpreter.aliases_expand` | `expand_aliases`[cite: 3] |
| 141 | s | `bash.debug.debugger_support` | `extdebug`[cite: 3] |
| 142 | s | `bash.interpreter.glob_extended_patterns` | `extglob`[cite: 3] |
| 143 | s | `bash.legacy.extquote` | [cite: 3] |
| 144 | s | `bash.interpreter.glob_no_match_is_error` | `failglob`[cite: 3] |
| 145 | i | `bash.legacy.force_fignore` | [cite: 3] |
| 146 | s | `bash.legacy.globasciiranges` | [cite: 3] |
| 147 | s | `bash.interpreter.glob_never_matches_dot_and_dotdot` | `globskipdots`[cite: 3] |
| 148 | s | `bash.interpreter.glob_double_star_recurses` | `globstar`[cite: 3] |
| 149 | i | `bash.legacy.gnu_errfmt` | [cite: 3] |
| 150 | i | `bash.legacy.histreedit` | [cite: 3] |
| 151 | i | `bash.legacy.histverify` | [cite: 3] |
| 152 | i | `bash.legacy.hostcomplete` | [cite: 3] |
| 153 | i | `bash.legacy.huponexit` | [cite: 3] |
| 154 | s | `bash.interpreter.command_substitution_inherits_exit_on_failure` | `inherit_errexit`[cite: 3] |
| 155 | i | `bash.editor.interactive_comments` | `interactive_comments`[cite: 3] |
| 156 | s | `bash.interpreter.pipeline_last_stage_runs_in_shell` | `lastpipe`[cite: 3] |
| 157 | s | `bash.interpreter.local_inherits_outer_value` | `localvar_inherit`[cite: 3] |
| 158 | s | `bash.interpreter.local_unset_hides_outer` | `localvar_unset`[cite: 3] |
| 159 | i | `bash.interpreter.login_shell` | `login_shell`[cite: 3] |
| 160 | i | `bash.legacy.mailwarn` | [cite: 3] |
| 161 | i | `bash.legacy.no_empty_cmd_completion` | [cite: 3] |
| 162 | s | `bash.interpreter.glob_ignores_case` | `nocaseglob`[cite: 3] |
| 163 | s | `bash.interpreter.match_ignores_case` | `nocasematch`[cite: 3] |
| 164 | s | `bash.interpreter.glob_no_match_expands_to_nothing` | `nullglob`[cite: 3] |
| 165 | s | `bash.interpreter.pattern_substitution_ampersand_is_match` | `patsub_replacement`[cite: 3] |
| 166 | i | `bash.completion.programmable` | `progcomp`[cite: 3] |
| 167 | i | `bash.completion.programmable_follows_aliases` | `progcomp_alias`[cite: 3] |
| 168 | i | `bash.legacy.promptvars` | [cite: 3] |
| 169 | s | `bash.interpreter.restricted_shell` | `restricted_shell`[cite: 3] |
| 170 | s | `bash.interpreter.shift_reports_overflow` | `shift_verbose`[cite: 3] |
| 171 | s | `bash.interpreter.source_searches_path` | `sourcepath`[cite: 3] |
| 172 | s | `bash.legacy.varredir_close` | [cite: 3] |
| 173 | s | `bash.interpreter.echo_interprets_backslash_escapes` | `xpg_echo`[cite: 3] |
| 176 | i | `bash.legacy.histappend` | [cite: 3] |
| 177 | i | `bash.legacy.cmdhist` | [cite: 3] |
| 178 | i | `bash.legacy.lithist` | [cite: 3] |
| 180 | s | `bash.legacy.compat31` | [cite: 3] |
| 181 | s | `bash.legacy.compat32` | [cite: 3] |
| 182 | s | `bash.legacy.compat40` | [cite: 3] |
| 183 | s | `bash.legacy.compat41` | [cite: 3] |
| 184 | s | `bash.legacy.compat42` | [cite: 3] |
| 185 | s | `bash.legacy.compat43` | [cite: 3] |
| 186 | s | `bash.legacy.compat44` | [cite: 3] |
| 187 | s | `bash.legacy.noexpand_translation` | [cite: 3] |
| 188 | s | `bash.legacy.bash_source_fullpath` | [cite: 3] |

## Persist options in `kosh.conf` settings files <a id="settings-file-locations"></a>

Settings files apply globally to every shell process that loads them, including non-interactive scripts[cite: 3]. If a semantic option (`s`) should apply only to an interactive session rather than all scripts, set it interactively with `set`, `shopt`, or `koshconf set` without `--persist`[cite: 3].

### Settings file examples <a id="configuration-examples"></a>

Configure the `bash` grammar with `vi` keybindings and a 20,000-line history limit[cite: 3].

```ini
mood=bash
editor.history.max_entries=20000
editor.base_mode=vi
```

Keep the strict `kosh` execution mood while sourcing `~/.bashrc` under the `bash` mood at interactive startup[cite: 3].

```ini
init_moods=bash
```

Run `koshconf set <option> <value> --persist` to write these entries programmatically[cite: 3].

### File paths, security checks, and write semantics

| Tier | File path | Ownership, permissions, and write behavior |
| --- | --- | --- |
| System | `/etc/kosh.conf` (POSIX) or `%ProgramData%\kosh\kosh.conf` (Windows)[cite: 3] | Loaded first[cite: 3]. On POSIX, the file and its parent directory must be owned by `root` and not writable by group or others[cite: 3]. On Windows, the file and its `kosh` directory must be owned by `SYSTEM`, `Administrators`, or `TrustedInstaller`, and no ACL entry may grant another account permission to modify, change permissions, or take ownership (the directory may allow others to add files)[cite: 3]. If the check fails, the file is skipped with a warning[cite: 3]. |
| User | `$XDG_CONFIG_HOME/kosh/kosh.conf` or `~/.config/kosh/kosh.conf`[cite: 3] | Loaded second[cite: 3]. `XDG_CONFIG_HOME` is used only when set to an absolute path; on Windows, `~` falls back to `USERPROFILE` when `HOME` is unset[cite: 3]. On POSIX, a shell running with an effective user ID of `root` neither reads nor writes this file[cite: 3]. Written by `koshconf create`, `koshconf set --persist`, and `koshconf save --persist`[cite: 3]. Writing preserves symlinks (updating the target), preserves existing file permissions (or applies `0644` minus `umask` for new files), creates missing parent directories, and backs up the prior file to `kosh.conf.bak`[cite: 3]. |

### File grammar and serialization rules <a id="settings-file-syntax"></a>

* **Line structure** - Lines terminate with `LF` or `CRLF`, and a leading UTF-8 BOM is ignored[cite: 3]. Blank lines and lines beginning with `#` (after optional whitespace) are ignored[cite: 3]. Active lines follow the format `name=value`, ignoring surrounding whitespace around `name` and `value`[cite: 3]. Values cannot span multiple lines[cite: 3].
* **Quoting** - A value beginning with `'` or `"` terminates at the matching closing quote, and only whitespace may follow the closing quote on that line[cite: 3]. Surround a value with quotes to preserve leading or trailing whitespace[cite: 3].
* **Data types** - Booleans accept `on`, `off`, `true`, `false`, `1`, or `0`[cite: 3]. Enumerations take their literal string identifier[cite: 3]. Strings must be valid UTF-8 without `NUL` bytes[cite: 3].
* **Evaluation order and mood locks** - The `mood` directive is evaluated first regardless of line position[cite: 3]. When `mood=kosh` is active, any line that attempts to change an option locked by the `kosh` mood (see [Moods](kosh_1.md#moods)) is skipped with a warning[cite: 3]. Later lines in the same file overwrite earlier lines[cite: 3].
* **`koshconf` serialization** - When writing `kosh.conf`, `koshconf` quotes values that begin with a quote or have leading/trailing whitespace, and preserves `CRLF` line endings if already present[cite: 3]. A value containing a newline, or requiring quotes while containing both `'` and `"`, cannot be serialized; `koshconf set --persist` rejects the operation before mutating session state[cite: 3]. Persisting `mood=kosh` strips existing lines for options locked by the `kosh` mood[cite: 3]. Unrecognized option lines from newer or custom versions are preserved at the bottom of the rewritten file[cite: 3].
* **Template generation (`koshconf create`)** - Writes all file-permitted options except `init_moods`, each documented with a comment and preset value[cite: 3]. Only `mood` is uncommented; all other options remain commented out so system defaults apply until explicitly overridden[cite: 3]. String options without defaults are written as commented-out empty assignments[cite: 3]. The `kosh` preset populates the exact values locked by the `kosh` mood so uncommenting them triggers no warnings[cite: 3].

### Settings file error handling <a id="settings-file-errors"></a>

A settings file error never aborts shell startup[cite: 3].

| Failure condition | Shell recovery behavior |
| --- | --- |
| File does not exist | Silently skipped[cite: 3]. |
| File is unreadable, exceeds `1 MiB`, is not a regular file, or fails system ownership/ACL validation | Emits a warning and skips the entire file[cite: 3]. |
| Line lacks `=`, specifies an unknown or disallowed option, contains an invalid value, has an unclosed quote, or has trailing non-whitespace characters after a closing quote | Emits a warning with the file path and line number, and skips that line[cite: 3]. |

## Execute startup logic in `rc.kosh` <a id="startup-script"></a>

An interactive or login shell running in the `kosh` mood sources `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh` (or `~/.config/kosh/scripts/rc.kosh`) after loading settings files[cite: 3]. Koshka does not read a system rc script, `/etc/koshrc`, or `~/.koshrc`[cite: 3]. Place procedural shell code in `rc.kosh` and declarative options in `kosh.conf`[cite: 3]. See [Startup files](kosh_1.md#startup-files) for invocation flags that replace or skip `rc.kosh`, and [Bash and POSIX startup files](kosh_1.md#bash-and-posix-startup-files) for compatibility mood startup paths[cite: 3].

When `rc.kosh` is missing, `koshconf create` generates the following default script (existing files are never overwritten)[cite: 3].

```sh
#!/usr/bin/env kosh
if shopt -q login_shell; then
  [ -r /etc/profile ] && . /etc/profile
  if [ -r ~/.bash_profile ]; then
    . ~/.bash_profile
  elif [ -r ~/.bash_login ]; then
    . ~/.bash_login
  elif [ -r ~/.profile ]; then
    . ~/.profile
  fi
else
  [ -r ~/.profile ] && . ~/.profile
  [ -r ~/.bashrc ] && . ~/.bashrc
fi
```

## Propagate interactive settings with `KOSHCONF` <a id="sharing-settings-with-koshconf"></a>

`KOSHCONF` is a dynamic shell variable that serializes the active `mood` and interactive (`i`) options into a padded standard Base64 string[cite: 3]. Direct user assignments to `KOSHCONF` are ignored; reading or exporting `KOSHCONF` dynamically generates the current session payload[cite: 3].

* **Excluded options** - Semantic (`s`) options (other than `mood`), `init_moods`, `editor.history.file_path`, `posix.interpreter.job_control`, `bash.editor.history.bang_expansion`, `bash.editor.history.records_commands`, and `bash.interpreter.login_shell` are never serialized into `KOSHCONF`[cite: 3]. Child shells load semantic options directly from settings files[cite: 3].
* **Export and ingestion** - `KOSHCONF` is not exported by default[cite: 3]. After `export KOSHCONF`, or when passed as a command prefix (`KOSHCONF=x cmd` evaluates to the current dynamic payload), child commands inherit the serialized state[cite: 3]. A starting `kosh` process applies `KOSHCONF` after reading settings files and removes `KOSHCONF` from the environment before running commands[cite: 3]. An active shell can ingest a payload at runtime via `koshconf load`[cite: 3].

Forward local interactive settings to a remote SSH session by passing `KOSHCONF` on the remote command line[cite: 3].

```sh
ssh host KOSHCONF="$KOSHCONF" kosh
```

### Binary wire format <a id="encoding-format"></a>

Base64 decoding yields a contiguous sequence of binary records in the format `[option_id][value_length][value_bytes]`[cite: 3].

| Field or data type | Wire representation |
| --- | --- |
| `option_id` and `value_length` | Unsigned LEB128 varints (7 data bits per byte, least significant group first, high continuation bit `0x80` set on all bytes except the final byte)[cite: 3]. |
| Boolean | `1` byte (`0x00` for off, `0x01` for on)[cite: 3]. |
| Enumeration | Unsigned LEB128 varint representing the zero-based index of the value in its canonical list[cite: 3]. |
| Integer | ASCII decimal digit bytes[cite: 3]. |
| String | Raw UTF-8 bytes[cite: 3]. |

* **Canonical validation** - The decoder enforces strict canonical encoding[cite: 3]. Base64 padding (`=`) must be present, every LEB128 varint must use the minimum number of bytes, and `option_id` values must appear in strictly ascending numeric order[cite: 3].
* **Decode error recovery** - If a record contains an unknown `option_id`, an option excluded from `KOSHCONF`, or a value invalid in `kosh.conf`, that individual record is skipped with a warning[cite: 3]. If the payload violates Base64 padding, minimal varint encoding, ascending ID order, or record length boundaries (truncation), the entire `KOSHCONF` payload is rejected with a warning[cite: 3].
