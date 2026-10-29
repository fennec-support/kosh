# Koshka configuration reference

> Declarative specification for command-line flags, shell settings, settings files, startup scripts, and the KOSHCONF wire format.

2026-10-26

Prerequisites - Familiarity with [kosh(1) invocation and startup files](kosh_1.md#skipping-startup-files).

Koshka configures execution semantics and interactive line-editing through declarative settings files (`kosh.conf`), procedural startup scripts (`rc.kosh`), the `KOSHCONF` environment payload, and command-line flags. Every shell instance loads settings files before evaluating any startup script, regardless of whether the session is interactive. The configured `mood` also governs `kosh -c` invocations and scripts that lack a shebang.

## Core configuration primitives

| Primitive | Role in configuration |
| --- | --- |
| `kosh.conf` | Declarative key-value file that stores persistent shell option values. |
| `koshconf` | Builtin utility that reads, mutates, and persists option values. |
| `rc.kosh` | Procedural startup script sourced by interactive and login shells in the `kosh` mood. |
| `KOSHCONF` | Base64-encoded binary environment payload that propagates mood and interactive settings to child or remote shells. |
| `mood` | Top-level execution profile (`kosh`, `bash`, `sh`, or `bash-posix`) that sets parser grammar and baseline invariants. |

## Configuration resolution lifecycle

1. A new shell process inspects its invocation flags and execution privileges. If `--no-config`, `-p` (`--privileged`), or a setuid/setgid context is active, settings files and `KOSHCONF` are skipped and removed. See [Skipping startup files](kosh_1.md#skipping-startup-files) for the full exclusion list.
2. The shell parses the system settings file (`/etc/kosh.conf` or `%ProgramData%\kosh\kosh.conf`) if it passes ownership and permission checks.
3. The shell parses the user settings file (`$XDG_CONFIG_HOME/kosh/kosh.conf` or `~/.config/kosh/kosh.conf`), unless the POSIX effective user is `root`. User settings override system settings. The mood applies first, wherever its line appears, and the other lines are read under the resulting session mood.
4. The shell decodes and applies an inherited `KOSHCONF` environment variable, then removes `KOSHCONF` from the environment before any command runs.
5. The shell applies inherited `KOSH_HISTORY_FILE` and `KOSH_HISTORY_SIZE` variables and explicit command-line flags. Command-line flags override both settings files and `KOSHCONF`.
6. Interactive and login shells source the startup scripts selected by `-L` (`--init-moods`), `init_moods`, or the active session mood. Other shells source the startup files of the session mood, such as `ENV` or `BASH_ENV`.

## Override settings with command-line options <a id="command-line-options"></a>

An option passed on the command line (enabled or disabled) overrides settings files and `KOSHCONF`. Koshka-specific flags appear first in the table below, followed by general, POSIX, and Bash-compatible flags.

| Option | Effect |
| --- | --- |
| `-M, --mood=NAME` | Select the `kosh`, `bash`, `sh`, or `bash-posix` mood. |
| `-L, --init-moods=LIST` | Source the startup files of each mood in `LIST`, in order. |
| `-I, --enable-mimicry` | Execute a script started as a command under the mood selected by its shebang, following `--mood` precedence rules. |
| `--dumb` | Apply `--mood sh`, `--no-completion`, `--tab-selector plain`, and `--no-diagnostics`, and disable terminal color. |
| `--lint` | Analyze command strings, file operands, or standard input without execution, as specified in [Linting](kosh_1.md#linting). |
| `--format` | Format one file or standard input to standard output, as specified in [Formatting](kosh_1.md#formatting). |
| `--apply` | Write safe lint fixes under `--lint`, formatted source under `--format`, or both directly to file operands. |
| `--as-language-server` | Start the language server protocol daemon, as specified in [Language server](kosh_1.md#language-server). |
| `-Q, --no-init-files` | Skip all startup files and reset `PATH` to `/usr/bin:/bin`. |
| `--no-config` | Skip all settings files, and ignore and unset `KOSHCONF`. |
| `-W, -WW, -WWW` | Set the static analysis level. |
| `--list-diagnostics` | Print all static analysis checks and metadata, then exit. |
| `--no-diagnostics` | Disable static analysis, warnings, and pre-run diagnostics. |
| `--no-annoying-diagnostics` | Suppress the `annoying` diagnostic tier. |
| `--no-init-diagnostics` | Suppress diagnostics and warnings while startup files execute, then restore the prior state unless modified by a startup file. |
| `--no-traces` | Suppress source backtraces (`optimizer.show_source_traces=off`). |
| `-T, --no-completion` | Disable tab completion, ghost text, and syntax coloring (`completion.on_tab=off`). |
| `--no-syntax-highlighting` | Disable syntax coloring and ghost text while retaining tab completion (`editor.highlight_syntax_and_show_ghost_text=off`). |
| `--tab-selector MODE` | Set candidate menu presentation on `Tab` and history search presentation on `Ctrl-R`. |
| `--enable-koshkit` | Resolve bundled Koshkit utilities directly as commands after `PATH` (`interpreter.resolve_koshkit_applets_as_commands=on`). |
| `--enable-extended-arithmetic` | Enable arbitrary-precision integers and finite decimals (`interpreter.arithmetic_uses_big_numbers=on`). |
| `-A, --show-ast` | Print abstract syntax trees before execution and during formatting or linting. |
| `--show-optimizer-diagnostics` | Emit a warning for each constant-folded expression and eliminated dead branch. |
| `--show-exit-code` | Print a source-located diagnostic when a command returns a nonzero exit status (`debug.report_nonzero_exit_codes=on`). |
| `-N, --show-all-exit-codes` | Print the exit status of every command, including status `0`. |
| `-R, --show-lexed-words` | Print the lexical escape map of each parsed command. |
| `--show-stats` | Print evaluation counts, arena usage, variable/function/builtin/fork/exec counts, I/O byte and call totals, and resource usage after each command. |
| `--show-memory` | Print memory consumption for the syntax tree arena, function arenas, and heap at shell exit. |
| `--ftrace` | Enable function call tracing in Cosmopolitan builds. |
| `--strace` | Enable system-call tracing in Cosmopolitan builds. |
| `--version` | Print full version and copyright notices, then exit. |
| `-V, --short-version` | Print the short version string, then exit. |
| `--help` | Print command-line usage help, then exit. |
| `-i, --interactive` | Run in interactive mode. |
| `-s, --stdin` | Read commands from standard input. |
| `-c COMMAND, --command=COMMAND` | Execute `COMMAND` string. |
| `-e, --error-exit` | Exit immediately when an unguarded command returns a nonzero status. |
| `-f, --no-glob` | Disable pathname globbing. |
| `-t, --one-command` | Read and execute a single command, then exit. |
| `-v, --verbose` | Print each input line as it is read. |
| `-x, --xtrace` | Print each command trace after expansion. |
| `-a, --export-all` | Mark all subsequently assigned variables for environment export. |
| `-C, --no-clobber` | Prevent plain output redirection (`>`) from overwriting an existing regular file. |
| `-n, --no-exec` | Parse and analyze commands without executing them. |
| `-u, --no-unset` | Treat expansion of an unset variable as an error. |
| `-l, --login` | Run as a login shell. |
| `-h, -m, +h, +m` | Accepted for compatibility and ignored. |
| `-o NAME, +o NAME` | Enable (`-o`) or disable (`+o`) a POSIX or Bash `set -o` option. |
| `+a, +C, +e, +f, +n, +p, +t, +u, +v, +x` | Disable the corresponding single-letter option, matching `set +e` behavior. |
| `--` | Terminate option parsing. |
| `--init-file FILE, --rcfile FILE` | Source `FILE` instead of `~/.bashrc` in an interactive non-login `bash` mood, or instead of `rc.kosh` in an interactive non-login `kosh` mood. |
| `--norc` | Skip interactive non-login Bash rc files, `rc.kosh`, and any `--rcfile` target. |
| `-O NAME, +O NAME` | Enable (`-O`) or disable (`+O`) a `shopt` option, matching `shopt -s` and `shopt -u`. |
| `-r, --restricted` | Enter restricted shell mode after startup files complete. |
| `-p, --privileged` | Enable privileged execution mode. |
| `--posix` | Select the `bash-posix` mood, unless overridden by `--mood` or `--dumb`. |

### Command-line option constraints and precedence <a id="compatibility-option-details"></a>

| Option group | Option | Precedence, defaults, and failure behavior |
| --- | --- | --- |
| Compatibility | `-M, --mood=NAME` | Precedence order from highest to lowest is `--mood`, `--dumb`, `--posix` / `-o posix`, invocation basename (`sh`, `dash`, `bash`, or `rbash`), script shebang, and the configured mood (default `kosh`). Without `--mood`, the first line of a script file operand selects `bash` for a Bash shebang, `sh` for an `sh` or `dash` shebang, and `kosh` for a `kosh` shebang (directly or via `env`). Any other interpreter, or no shebang, leaves the configured mood. Shebang inspection is skipped for `-c`, standard input, and `--lint` without `-I`. |
| Compatibility | `-L, --init-moods=LIST` | Accepts comma-separated mood names or repeated `-L` flags. Defaults to `init_moods` from settings for interactive or login shells, and to the active execution mood for non-interactive shells. |
| Compatibility | `-I, --enable-mimicry` | A script started as a command runs inside Koshka under the mood its shell shebang selects, instead of under that interpreter. |
| Compatibility | `--dumb` | An explicit `--tab-selector` flag overrides the `plain` selector set by `--dumb`. |
| Auxiliary <a id="auxiliary-option-details"></a> | `--format` | Ignores `-c` command strings. Formatting more than one file operand requires `--apply`. |
| Auxiliary | `--apply` | Requires `--lint`, `--format`, or both, plus at least one file operand. Rejects standard input, `-s`, `-i`, `-c`, and `-`. |
| Auxiliary | `--as-language-server` | Rejects file operands, `-c`, `-s`, `-i`, `--lint`, `--format`, and `--apply`. |
| Koshka <a id="koshka-option-details"></a> | `--no-config` | Leaves `PATH` and the startup files unchanged. |
| Koshka | `-W, -WW, -WWW` | Analysis checks are grouped into `strict`, `lenient`, and `annoying` tiers. In the `kosh` mood, `strict` and `lenient` checks are errors and `annoying` checks are warnings by default. `-W` retains that baseline, `-WW` demotes `lenient` errors to warnings, and `-WWW` demotes both `lenient` and `strict` errors to warnings. In compatibility moods (`bash`, `sh`, `bash-posix`), static analysis is off by default; `-W` enables `strict` warnings, `-WW` enables `strict` and `lenient` warnings, and `-WWW` enables all three tiers as warnings. Levels set via `set` propagate to child shells through `KOSH_ANALYSIS`. Runtime diagnostics are unaffected by `-W`, except that compatibility moods warn on empty unset-variable expansion only when a warning level is active. |
| Koshka | `--tab-selector MODE` | Accepts `interactive` (default), `external`, or `plain`. See [Line editing](kosh_1.md#line-editing). |
| Koshka | `--enable-koshkit` | Defaults to `off`. Affects only compatibility moods because the `kosh` mood always resolves bundled Koshkit utilities after `PATH`. |
| Koshka | `--enable-extended-arithmetic` | Always enabled in the `kosh` mood. |
| Debug <a id="debug-option-details"></a> | `--show-optimizer-diagnostics` | Compatibility moods emit these warnings only when `-WW` or `-WWW` is active. |
| Debug | `-N, --show-all-exit-codes` | Can be toggled at runtime with `set -N` and `set +N`. |
| POSIX <a id="posix-option-details"></a> | `-s, --stdin` | Input processing order is specified in [Invocation](kosh_1.md#invocation). |
| POSIX | `-c COMMAND, --command=COMMAND` | May be repeated to execute multiple strings in sequence. A syntax error exits with status `2`, an analysis error exits with status `1`, and subsequent command strings are aborted. |
| POSIX | `-n, --no-exec` | Compatibility moods report only the diagnostic tiers enabled by `-W`. |
| POSIX | `-o NAME, +o NAME` | `-o posix` acts as `--posix`. An unknown option name exits with status `2`. In the `kosh` mood, `+o nounset` is ignored and `+o pipefail` exits with status `2`. |
| POSIX | `+a, +C, +e, +f, +n, +p, +t, +u, +v, +x` | `+h` and `+m` are accepted and ignored. Passing `set`-only flags (`-B`, `-E`, `-H`, `-P`, `-b`, `-k`) on the command line exits with status `2`. |
| POSIX | `--` | The operand immediately following `--` is treated as the script path. |
| Bash <a id="bash-option-details"></a> | `--norc` | Does not affect login shells, login profiles, or `BASH_ENV`. |
| Bash | `-O NAME, +O NAME` | An unknown `shopt` option exits with status `2`. Read-only options such as `login_shell` are ignored. In the `kosh` mood, `+O failglob` and `-O nullglob` exit with status `2`. |
| Bash | `-p, --privileged` | Skips `ENV`, `BASH_ENV`, interactive non-login `rc.kosh`, and all settings files, and unsets `KOSHCONF`. A secure execution, such as setuid or setgid, automatically drops elevated privileges and skips all startup and settings files. On Linux, a setuid/setgid process whose real user ID is not `root` also clears all capability sets (including ambient capabilities) and aborts if privilege restoration to `root` remains possible. |

## Configure shell settings via builtins and variables <a id="shell-settings"></a>

Shell settings are inspected and mutated via `set`, `shopt`, and `koshconf`, and their states are exposed through `[ -o OPTION ]`, `SHELLOPTS`, and `BASHOPTS`. Each setting has a permanent integer ID, a canonical `koshconf` dot-separated path, a scope (`i` for interactive editor/history/diagnostic behavior, or `s` for semantic script execution behavior), and optional POSIX/Bash spellings.

### Namespace hierarchy and legacy aliases

| Namespace prefix | Subsystem scope | Alias and mapping rules |
| --- | --- | --- |
| `mood`, `init_moods` | Top-level execution and startup mood selection | Canonical names only. |
| `interpreter` | Core shell execution semantics | Koshka-only options. Options that POSIX or Bash define carry a `posix.` or `bash.` origin prefix in every namespace (`posix.interpreter.*`, `bash.editor.*`, `bash.completion.*`). An option aliases to `posix.legacy.NAME` when POSIX `set -o` accepts NAME, and to `bash.legacy.NAME` otherwise. |
| `compatibility` | Windows path translation (`dev_null`, `slash_tmp`) | Koshka-only options. |
| `editor` | Line editor, `editor.history`, and `editor.hints` | `editor.base_mode` (`emacs` or `vi`) accepts the aliases `posix.legacy.vi` and `bash.legacy.emacs`. `set -o emacs` and `set -o vi` toggle `editor.base_mode` as in Bash. `interactive_comments` aliases to `bash.legacy.interactive-comments`. |
| `optimizer` | Static analysis tiers and source traces | Koshka-only options. |
| `completion` | Tab completion behavior and menu style | Bash programmable completion uses `bash.completion.*`. |
| `debug` | AST, lexer, exit code, and memory telemetry | POSIX tracing options use `posix.debug.*`. |

* The first component of a `koshconf` name is its topic. POSIX `set -o` options start with `posix` (`posix.interpreter.exit_on_command_failure` for `errexit`), other Bash `set -o` and `shopt` options start with `bash`, and every other option exists only in Koshka. `editor.base_mode` is the only Bash-derived option without a prefix.
* An option with a Bash spelling is also accepted as `posix.legacy.NAME` when POSIX `set -o` accepts `NAME` and as `bash.legacy.NAME` otherwise (`bash.legacy.extglob`). An alias is the same option. `editor.base_mode` has the aliases `posix.legacy.vi` and `bash.legacy.emacs`.
* Every option operand and `kosh.conf` entry accepts either the canonical name or its `posix.legacy.*` / `bash.legacy.*` alias.
* `editor.base_mode` covers the Bash options `emacs` and `vi` and is `vi` while `set -o vi` is on and `emacs` otherwise. `set -o emacs`, `set -o vi`, the `-o` test, and `SHELLOPTS` behave as in Bash.
* Error messages, `KOSHCONF`, `koshconf create`, and `koshconf list` emit only canonical names. Running `koshconf list --all` appends legacy aliases.
* The Bash options `hashall` and `nolog` have no canonical `koshconf` name. They are accepted by `set` and `-o`, appear in `koshconf list --all` as `posix.legacy.hashall` and `posix.legacy.nolog`, and are rejected by `koshconf get`, `koshconf set`, and `kosh.conf`.
* No-op `bash.legacy.*` `shopt` entries (such as `cdspell` or `histappend`) are accepted so legacy Bash startup files execute without errors, but they do not alter shell behavior. `shopt` accepts `array_expand_once` as the Bash 5.3 alias for `assoc_expand_once`.

### Settings mutability constraints

| Constraint category | Affected options | Behavior |
| --- | --- | --- |
| Read-only state | `bash.interpreter.login_shell`, `bash.interpreter.restricted_shell` | Fixed by shell invocation; cannot be modified in `kosh.conf` or at runtime. |
| Command-line fixed | `bash.interpreter.privileged_mode`, `posix.interpreter.parse_without_executing`, `bash.interpreter.exit_after_one_command` | Set from the command line or with `set` at runtime; rejected in `kosh.conf`. |
| Session and mood derived | `posix.interpreter.job_control`, `bash.editor.history.records_commands`, `bash.editor.history.bang_expansion`, `bash.interpreter.posixly_correct` | Follow the session type and mood by default and can change at runtime with `set`; rejected in `kosh.conf`. |

### Native Koshka options <a id="koshka-options"></a>

| ID | Scope | Koshconf name | Shell flag | Accepted values and defaults |
| --- | --- | --- | --- | --- |
| 1 | s | `mood` | `set -M` | `kosh`, `sh`, `bash`, or `bash-posix`. |
| 2 | i | `completion.menu_style` |  | `interactive`, `external`, or `plain`. |
| 3 | i | `editor.hints.show_command_synopsis` |  | Boolean (`on` or `off`). |
| 4 | i | `editor.hints.show_live_diagnostics` |  | Boolean (`on` or `off`). |
| 5 | i | `editor.auto_close_brackets_and_quotes` |  | Boolean (`on` or `off`). |
| 7 | i | `editor.request_extended_key_reports` |  | Boolean (`on` or `off`). |
| 8 | i | `editor.history.arrow_keys_search_by_typed_prefix` |  | Boolean (`on` or `off`). |
| 9 | i | `editor.history.file_path` |  | UTF-8 path string; maps to `KOSH_HISTORY_FILE`. |
| 10 | i | `editor.history.max_entries` |  | Integer `0` to `2147483647`; maps to `KOSH_HISTORY_SIZE`. |
| 11 | i | `completion.add_space_after_completed_word` |  | Boolean (`on` or `off`). |
| 12 | i | `optimizer.warning_level` | `set -W` | Integer `0` to `3`. |
| 13 | i | `optimizer.show_annoying_tier` |  | Boolean (`on` or `off`). |
| 14 | s | `optimizer.analyze_before_running` |  | Boolean (`on` or `off`). |
| 15 | s | `interpreter.resolve_koshkit_applets_as_commands` |  | Boolean (`on` or `off`). |
| 16 | s | `interpreter.arithmetic_uses_big_numbers` |  | Boolean (`on` or `off`). |
| 17 | s | `interpreter.mimic_shebang` | `set -I` | Boolean (`on` or `off`). |
| 18 | s | `debug.print_syntax_tree` | `set -A` | Boolean (`on` or `off`). |
| 19 | s | `debug.print_lexed_word_escapes` | `set -R` | Boolean (`on` or `off`). |
| 20 | s | `debug.report_nonzero_exit_codes` |  | Boolean (`on` or `off`). |
| 21 | s | `debug.report_every_exit_code` | `set -N` | Boolean (`on` or `off`). |
| 22 | s | `debug.print_evaluation_statistics` | `set -S` | Boolean (`on` or `off`). |
| 23 | s | `debug.print_memory_report_at_exit` | `set -G` | Boolean (`on` or `off`). |
| 24 | i | `completion.on_tab` |  | Boolean (`on` or `off`). |
| 25 | i | `editor.highlight_syntax_and_show_ghost_text` |  | Boolean (`on` or `off`). |
| 26 | i | `optimizer.show_source_traces` |  | Boolean (`on` or `off`). |
| 27 | s | `init_moods` |  | Comma-separated list of the moods `-L` accepts, without empty elements; empty by default, which sources the startup files of the session mood alone. Applies only to interactive or login startup, is overridden by `-L`, is ignored by restricted shells in settings files, and is excluded from `KOSHCONF`. |
| 174 | s | `compatibility.dev_null` |  | Boolean (`on` or `off`). |
| 175 | s | `compatibility.slash_tmp` |  | Boolean (`on` or `off`). |
| 179 | i | `editor.wrap_width` |  | Integer `0` to `65535`, default `80`. |

### Shared Bash and POSIX settings <a id="bash-and-posix-settings"></a>

These tables list the shared options by Koshka configuration path. Prefixes, aliases, scopes, and settings-file restrictions are described under [Shell settings](#shell-settings).

#### `set -o` options <a id="set-o-options"></a>

| ID | Scope | Koshconf name | Shell spelling |
| --- | --- | --- | --- |
| 64 | s | `posix.interpreter.export_every_assigned_variable` | `allexport, -a` |
| 65 | i | `posix.editor.jobs_report_status_immediately` | `notify, -b` |
| 66 | s | `posix.interpreter.exit_on_command_failure` | `errexit, -e` |
| 67 | s | `posix.interpreter.glob_disabled` | `noglob, -f` |
| 69 | s | `bash.interpreter.assignments_anywhere_in_command` | `keyword, -k` |
| 70 | i | `posix.interpreter.job_control` | `monitor, -m` |
| 71 | s | `posix.interpreter.parse_without_executing` | `noexec, -n` |
| 72 | s | `bash.interpreter.exit_after_one_command` | `onecmd, -t` |
| 73 | s | `bash.interpreter.privileged_mode` | `privileged, -p` |
| 74 | s | `posix.interpreter.unset_variable_is_error` | `nounset, -u` |
| 75 | s | `posix.debug.trace_input_lines` | `verbose, -v` |
| 76 | s | `posix.debug.trace_expanded_commands` | `xtrace, -x` |
| 77 | s | `bash.interpreter.brace_expansion` | `braceexpand, -B` |
| 78 | s | `posix.interpreter.redirect_refuses_to_overwrite_files` | `noclobber, -C` |
| 79 | s | `bash.interpreter.trap_err_inherited_by_functions` | `errtrace, -E` |
| 80 | i | `bash.editor.history.bang_expansion` | `histexpand, -H` |
| 81 | s | `bash.interpreter.cd_resolves_symlinks` | `physical, -P` |
| 82 | s | `bash.interpreter.trap_debug_and_return_inherited_by_functions` | `functrace, -T` |
| 83 | s | `posix.interpreter.pipeline_fails_on_any_stage` | `pipefail` |
| 84 | i | `bash.editor.history.records_commands` | `history` |
| 85 | i | `posix.editor.ctrl_d_does_not_exit` | `ignoreeof` |
| 87 | i | `editor.base_mode` | `emacs, vi` (`vi` while `set -o vi` is on) |
| 89 | s | `bash.interpreter.posixly_correct` | `posix` |

#### `shopt` options <a id="shopt-options"></a>

| ID | Scope | Koshconf name | Shell spelling |
| --- | --- | --- | --- |
| 128 | i | `bash.editor.cd_by_typing_directory_name` | `autocd` |
| 129 | s | `bash.legacy.assoc_expand_once` | `assoc_expand_once, array_expand_once` |
| 130 | s | `bash.interpreter.cd_to_variable_value` | `cdable_vars` |
| 131 | i | `bash.legacy.cdspell` |  |
| 132 | s | `bash.interpreter.verify_remembered_command_paths` | `checkhash` |
| 133 | i | `bash.legacy.checkjobs` |  |
| 134 | i | `bash.legacy.checkwinsize` |  |
| 135 | i | `bash.legacy.complete_fullquote` |  |
| 136 | i | `bash.legacy.direxpand` |  |
| 137 | i | `bash.legacy.dirspell` |  |
| 138 | s | `bash.interpreter.glob_includes_dotfiles` | `dotglob` |
| 139 | s | `bash.legacy.execfail` |  |
| 140 | s | `bash.interpreter.aliases_expand` | `expand_aliases` |
| 141 | s | `bash.debug.debugger_support` | `extdebug` |
| 142 | s | `bash.interpreter.glob_extended_patterns` | `extglob` |
| 143 | s | `bash.legacy.extquote` |  |
| 144 | s | `bash.interpreter.glob_no_match_is_error` | `failglob` |
| 145 | i | `bash.legacy.force_fignore` |  |
| 146 | s | `bash.legacy.globasciiranges` |  |
| 147 | s | `bash.interpreter.glob_never_matches_dot_and_dotdot` | `globskipdots` |
| 148 | s | `bash.interpreter.glob_double_star_recurses` | `globstar` |
| 149 | i | `bash.legacy.gnu_errfmt` |  |
| 150 | i | `bash.legacy.histreedit` |  |
| 151 | i | `bash.legacy.histverify` |  |
| 152 | i | `bash.legacy.hostcomplete` |  |
| 153 | i | `bash.legacy.huponexit` |  |
| 154 | s | `bash.interpreter.command_substitution_inherits_exit_on_failure` | `inherit_errexit` |
| 155 | i | `bash.editor.interactive_comments` | `interactive_comments` |
| 156 | s | `bash.interpreter.pipeline_last_stage_runs_in_shell` | `lastpipe` |
| 157 | s | `bash.interpreter.local_inherits_outer_value` | `localvar_inherit` |
| 158 | s | `bash.interpreter.local_unset_hides_outer` | `localvar_unset` |
| 159 | i | `bash.interpreter.login_shell` | `login_shell` |
| 160 | i | `bash.legacy.mailwarn` |  |
| 161 | i | `bash.legacy.no_empty_cmd_completion` |  |
| 162 | s | `bash.interpreter.glob_ignores_case` | `nocaseglob` |
| 163 | s | `bash.interpreter.match_ignores_case` | `nocasematch` |
| 164 | s | `bash.interpreter.glob_no_match_expands_to_nothing` | `nullglob` |
| 165 | s | `bash.interpreter.pattern_substitution_ampersand_is_match` | `patsub_replacement` |
| 166 | i | `bash.completion.programmable` | `progcomp` |
| 167 | i | `bash.completion.programmable_follows_aliases` | `progcomp_alias` |
| 168 | i | `bash.legacy.promptvars` |  |
| 169 | s | `bash.interpreter.restricted_shell` | `restricted_shell` |
| 170 | s | `bash.interpreter.shift_reports_overflow` | `shift_verbose` |
| 171 | s | `bash.interpreter.source_searches_path` | `sourcepath` |
| 172 | s | `bash.legacy.varredir_close` |  |
| 173 | s | `bash.interpreter.echo_interprets_backslash_escapes` | `xpg_echo` |
| 176 | i | `bash.legacy.histappend` |  |
| 177 | i | `bash.legacy.cmdhist` |  |
| 178 | i | `bash.legacy.lithist` |  |
| 180 | s | `bash.legacy.compat31` |  |
| 181 | s | `bash.legacy.compat32` |  |
| 182 | s | `bash.legacy.compat40` |  |
| 183 | s | `bash.legacy.compat41` |  |
| 184 | s | `bash.legacy.compat42` |  |
| 185 | s | `bash.legacy.compat43` |  |
| 186 | s | `bash.legacy.compat44` |  |
| 187 | s | `bash.legacy.noexpand_translation` |  |
| 188 | s | `bash.legacy.bash_source_fullpath` |  |

## Persist options in `kosh.conf` settings files <a id="settings-file-locations"></a>

Settings files apply globally to every shell process that loads them, including non-interactive scripts. If a semantic option (`s`) should apply only to an interactive session rather than all scripts, set it interactively with `set`, `shopt`, or `koshconf set` without `--persist`.

### Settings file examples <a id="configuration-examples"></a>

Configure the `bash` grammar with `vi` keybindings and a 20,000-entry history limit.

```ini
mood=bash
editor.history.max_entries=20000
editor.base_mode=vi
```

Keep the strict `kosh` execution mood while sourcing `~/.bashrc` under the `bash` mood at interactive startup.

```ini
init_moods=bash
```

Run `koshconf set <option> <value> --persist` to write these entries programmatically.

### File paths, security checks, and write semantics

| Tier | File path | Ownership, permissions, and write behavior |
| --- | --- | --- |
| System | `/etc/kosh.conf` (POSIX) or `%ProgramData%\kosh\kosh.conf` (Windows) | Loaded first, and only when no other user can write it. On POSIX, the file and its parent directory must be owned by `root` and not writable by group or others. On Windows, the file and its `kosh` directory must be owned by `SYSTEM`, `Administrators`, or `TrustedInstaller`, and no ACL entry may grant another account permission to modify, change permissions, or take ownership (the directory may allow others to add files). If the check fails, the file is skipped with a warning. |
| User | `$XDG_CONFIG_HOME/kosh/kosh.conf` or `~/.config/kosh/kosh.conf` | Loaded second. `XDG_CONFIG_HOME` is used only when set to an absolute path; on Windows, `~` falls back to `USERPROFILE` when `HOME` is unset. On POSIX, a shell running with an effective user ID of `root` neither reads nor writes this file. Written by `koshconf create`, `koshconf set --persist`, and `koshconf save --persist`. Writing preserves symlinks (updating the target), preserves existing file permissions (or applies `0644` minus `umask` for new files), creates missing parent directories, and backs up the prior file to `kosh.conf.bak`. |
| User rc script | `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh` or `~/.config/kosh/scripts/rc.kosh` | Sourced by a login or interactive `kosh` mood (see [Startup script](#startup-script)); skipped by the POSIX superuser. Startup files of the other moods are listed under [Bash and POSIX startup files](kosh_1.md#bash-and-posix-startup-files). |

### File grammar and serialization rules <a id="settings-file-syntax"></a>

* **Line structure** - Lines terminate with `LF` or `CRLF`, and a leading UTF-8 BOM is ignored. Blank lines and lines beginning with `#` (after optional whitespace) are ignored. Active lines follow the format `name=value`, ignoring surrounding whitespace around `name` and `value`. Values cannot span multiple lines.
* **Quoting** - A value beginning with `'` or `"` terminates at the matching closing quote, and only whitespace may follow the closing quote on that line. Surround a value with quotes to preserve leading or trailing whitespace.
* **Data types** - Booleans accept `on`, `off`, `true`, `false`, `1`, or `0`. Enumerations take their literal string identifier. Strings must be valid UTF-8 without `NUL` bytes. Integers are `editor.history.max_entries` (`0` to `2147483647`) and `editor.wrap_width` (`0` to `65535`). `init_moods` takes a comma-separated list of moods without empty elements. A name may be the canonical name or its legacy alias.
* **Evaluation order and mood locks** - The `mood` directive is evaluated first regardless of line position, and the other lines are read under the resulting session mood. When `mood=kosh` is active, any line that attempts to change an option locked by the `kosh` mood (see [Moods](kosh_1.md#moods)) is skipped with a warning. Later lines overwrite earlier lines, across the system file, the user file, and `KOSHCONF` in that order. A settings file may set every option except the read-only, command-line fixed, and session or mood derived options listed under [Shell settings](#shell-settings).
* **`koshconf` serialization** - When writing `kosh.conf`, `koshconf` quotes values that begin with a quote or have leading/trailing whitespace, and preserves `CRLF` line endings if already present. A value containing a newline, or requiring quotes while containing both `'` and `"`, cannot be serialized; `koshconf set --persist` rejects the operation before mutating session state. Persisting `mood=kosh` strips existing lines for options locked by the `kosh` mood. Unrecognized option lines from newer or custom versions are preserved at the bottom of the rewritten file.
* **Template generation (`koshconf create`)** - Writes all file-permitted options, each documented with a comment and preset value. Only `mood` is uncommented; all other options remain commented out so system defaults apply until explicitly overridden. String options without defaults are written as commented-out empty assignments. The `kosh` preset populates the exact values locked by the `kosh` mood so uncommenting them triggers no warnings.

### Settings file error handling <a id="settings-file-errors"></a>

A settings file error never aborts shell startup.

| Failure condition | Shell recovery behavior |
| --- | --- |
| File does not exist | Silently skipped. |
| File is unreadable, exceeds `1 MiB`, is not a regular file, or fails system ownership/ACL validation | Emits a warning and skips the entire file. |
| Line lacks `=`, specifies an unknown or disallowed option, contains an invalid value, has an unclosed quote, or has trailing non-whitespace characters after a closing quote | Emits a warning with the file path and line number, and skips that line. |

## Execute startup logic in `rc.kosh` <a id="startup-script"></a>

An interactive or login shell running in the `kosh` mood sources `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh` (or `~/.config/kosh/scripts/rc.kosh`) after loading settings files. The script can test `shopt -q login_shell` to run login-only commands. Koshka does not read a system rc script, `/etc/koshrc`, or `~/.koshrc`. Place procedural shell code in `rc.kosh` and declarative options in `kosh.conf`. See [Startup files](kosh_1.md#startup-files) for invocation flags that replace or skip `rc.kosh`, and [Bash and POSIX startup files](kosh_1.md#bash-and-posix-startup-files) for compatibility mood startup paths.

When `rc.kosh` is missing, `koshconf create` generates the following default script (existing files are never overwritten).

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

`KOSHCONF` is a dynamic shell variable that serializes the active `mood` and interactive (`i`) options into a padded standard Base64 string. Direct user assignments to `KOSHCONF` are ignored; reading or exporting `KOSHCONF` dynamically generates the current session payload.

* **Excluded options** - Semantic (`s`) options (other than `mood`), `init_moods`, `editor.history.file_path`, `posix.interpreter.job_control`, `bash.editor.history.bang_expansion`, `bash.editor.history.records_commands`, and `bash.interpreter.login_shell` are never serialized into `KOSHCONF`. Child shells load semantic options directly from settings files.
* **Export and ingestion** - `KOSHCONF` is not exported by default. After `export KOSHCONF`, or when passed as a command prefix (`KOSHCONF=x cmd` evaluates to the current dynamic payload), each started command receives the settings current at its start. A starting `kosh` process applies `KOSHCONF` after reading settings files and removes `KOSHCONF` from the environment before running commands. An active shell can ingest a payload at runtime via `koshconf load`.

Forward local interactive settings to a remote SSH session by passing `KOSHCONF` on the remote command line.

```sh
ssh host KOSHCONF="$KOSHCONF" kosh
```

### Binary wire format <a id="encoding-format"></a>

The option ids are listed under [Shell settings](#shell-settings) and never change meaning. Base64 decoding yields a contiguous sequence of binary records in the format `[option_id][value_length][value_bytes]`.

| Field or data type | Wire representation |
| --- | --- |
| `option_id` and `value_length` | Unsigned LEB128 varints (7 data bits per byte, least significant group first, high continuation bit `0x80` set on all bytes except the final byte). |
| Boolean | `1` byte (`0x00` for off, `0x01` for on). |
| Enumeration | Unsigned LEB128 varint representing the zero-based index of the value in its canonical list. |
| Integer | ASCII decimal digit bytes. |
| String | Raw UTF-8 bytes. |

* **Canonical validation** - The decoder enforces strict canonical encoding. Base64 padding (`=`) must be present, every LEB128 varint must use the minimum number of bytes, and `option_id` values must appear in strictly ascending numeric order.
* **Decode error recovery** - If a record contains an unknown `option_id`, an option excluded from `KOSHCONF`, or a value invalid in `kosh.conf`, that individual record is skipped with a warning. If the payload violates Base64 padding, minimal varint encoding, ascending ID order, or record length boundaries (truncation), the entire `KOSHCONF` payload is rejected with a warning.
