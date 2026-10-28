# Koshka configuration

> Command-line options, shell settings, startup scripts, and KOSHCONF.

2026-10-26

Koshka settings files hold option values. Every shell loads them before any
startup file, interactive or not, and the configured mood also applies to
`kosh -c` and to a script without a shebang. The `koshconf` builtin loads, changes, and writes them. The [Shell settings](#shell-settings)
section lists the options.

The [Startup files](kosh_1.md#skipping-startup-files) section lists the shells that apply no settings file
or KOSHCONF.

## Command-line options <a id="command-line-options"></a>

An option given on the command line, on or off, overrides the settings files and
`KOSHCONF`. Koshka-specific flags appear first, followed by the general, POSIX,
and Bash-compatible flags.

| Option | Effect |
| --- | --- |
| `-M, --mood=NAME` | Select the `kosh`, `bash`, `sh`, or `bash-posix` mood. |
| `-L, --init-moods=LIST` | Source the startup files of each mood in LIST, in order. |
| `-I, --enable-mimicry` | Run a script started as a command under the mood its shell shebang selects, by the precedence rules of `--mood`. |
| `--dumb` | Apply `--mood sh`, `--no-completion`, `--tab-selector plain`, and `--no-diagnostics`, and disable color. |
| `--lint` | Analyze the command strings, the file operands, or standard input without running them, as described under [Linting](kosh_1.md#linting). |
| `--format` | Format one file or standard input to standard output, as described under [Formatting](kosh_1.md#formatting). |
| `--apply` | Update the file operands with the safe lint fixes under `--lint`, the formatted source under `--format`, or both. |
| `--as-language-server` | Run the language server, as described under [Language server](kosh_1.md#language-server). |
| `-Q, --no-init-files` | Read no startup file, and set PATH to /usr/bin:/bin. |
| `--no-config` | Read no settings file, and ignore and remove KOSHCONF. |
| `-W, -WW, -WWW` | Set the analysis level. |
| `--list-diagnostics` | List the analysis checks and their metadata, then exit. |
| `--no-diagnostics` | Skip analysis and every warning and pre-run diagnostic. |
| `--no-annoying-diagnostics` | Suppress the annoying tier. |
| `--no-init-diagnostics` | Suppress diagnostics and warnings while startup files run, and restore them afterwards unless a startup file changed the setting. |
| `--no-traces` | Suppress source backtraces (optimizer.show_source_traces off). |
| `-T, --no-completion` | Disable tab completion, ghost text, and syntax coloring (completion.on_tab off). |
| `--no-syntax-highlighting` | Disable syntax coloring and ghost text and keep tab completion (editor.highlight_syntax_and_show_ghost_text off). |
| `--tab-selector MODE` | Select how an explicit Tab presents several candidates and how Ctrl-R presents the history. |
| `--enable-koshkit` | Resolve bundled utilities directly as commands, after PATH, as `koshconf set interpreter.resolve_koshkit_applets_as_commands on` does. |
| `--enable-extended-arithmetic` | Enable arbitrary-precision integers and finite decimal values, as `koshconf set interpreter.arithmetic_uses_big_numbers on` does. |
| `-A, --show-ast` | Print syntax trees before execution and during formatting or linting. |
| `--show-optimizer-diagnostics` | Report each constant-folded expression and eliminated branch as a warning. |
| `--show-exit-code` | Print a located diagnostic when a command returns a nonzero status (debug.report_nonzero_exit_codes). |
| `-N, --show-all-exit-codes` | Report every command status, including zero. |
| `-R, --show-lexed-words` | Print the escape map of each parsed command. |
| `--show-stats` | Print evaluation counts, arena use, the counts of shell variables, functions, builtins, forks, and execs, the bytes and calls read and written, and resource usage after each command. |
| `--show-memory` | Print the memory of the syntax tree arena, the function arenas, and the heap at exit. |
| `--ftrace` | Enable function tracing in a Cosmopolitan build. |
| `--strace` | Enable system-call tracing in a Cosmopolitan build. |
| `--version` | Print the version and notices, then exit. |
| `-V, --short-version` | Print the short version, then exit. |
| `--help` | Print help, then exit. |
| `-i, --interactive` | Run interactively. |
| `-s, --stdin` | Take commands from standard input. |
| `-c COMMAND, --command=COMMAND` | Run COMMAND. |
| `-e, --error-exit` | Exit after an unguarded failing command. |
| `-f, --no-glob` | Disable pathname expansion. |
| `-t, --one-command` | Read and run one command, then exit. |
| `-v, --verbose` | Print each input line as it is read. |
| `-x, --xtrace` | Print each command after expansion. |
| `-a, --export-all` | Mark each later assignment for export. |
| `-C, --no-clobber` | Keep a plain output redirection from overwriting an existing regular file. |
| `-n, --no-exec` | Parse and analyze commands without running them. |
| `-u, --no-unset` | Treat the expansion of an unset variable as an error. |
| `-l, --login` | Act as a login shell. |
| `-h, -m` | The shell accepts these options and ignores them. |
| `+h, +m` | The shell accepts these options and ignores them. |
| `-o NAME, +o NAME` | Enable or disable an option as `set -o` and `set +o` do. |
| `+a, +C, +e, +f, +n, +p, +t, +u, +v, +x` | Turn the option of that letter off, as `set +e` does. |
| `--` | End option parsing. |
| `--init-file FILE, --rcfile FILE` | Use FILE as the interactive rc file in place of ~/.bashrc in an interactive non-login Bash mood, and in place of the user rc script in an interactive non-login `kosh` mood. |
| `--norc` | Skip the interactive non-login Bash rc files, the user rc script, and a custom rc file. |
| `-O NAME, +O NAME` | Turn the shopt option NAME on or off, as `shopt -s` and `shopt -u` do. |
| `-r, --restricted` | Become restricted after the startup files finish. |
| `-p, --privileged` | Enable privileged mode. |
| `--posix` | Select the `bash-posix` mood, unless `--mood` or `--dumb` selects another. |

### Compatibility option details <a id="compatibility-option-details"></a>

**`-M, --mood=NAME`**

Without this option, the
first line of a script file operand selects the mood. A Bash shebang selects
`bash`, an sh or dash shebang selects `sh`, and a kosh shebang selects `kosh`,
directly or through `env`. Any other interpreter, or no shebang, leaves the
`kosh` mood.

`--mood`, `--dumb`, `--posix`, and an invocation basename such as
sh or bash take precedence over the shebang, and `--mood` also takes precedence
over `--posix` and `-o posix`. The shebang is not used for `-c`, standard input,
or `--lint` without `-I`.

**`-L, --init-moods=LIST`**

Moods are separated by
commas, or the option is repeated. Without this option, an interactive or login
shell uses init_moods from the settings, and any other shell uses the selected
mood.

**`-I, --enable-mimicry`**

A script file operand always uses the mood selected by its shebang and the
mood precedence rules.

**`--dumb`**

An explicit `--tab-selector` overrides the plain selector selected by `--dumb`.

### Auxiliary option details <a id="auxiliary-option-details"></a>

**`--format`**

Command strings are ignored, and more than one file needs
`--apply`.

**`--apply`**

`--apply` requires `--lint`, `--format`, or both, and at least one file.
Standard input, `-s`, `-i`, `-c`, and a lone `-` are rejected.

**`--as-language-server`**

File operands,
`-c`, `-s`, `-i`, `--lint`, `--format`, and `--apply` are rejected.

### Koshka option details <a id="koshka-option-details"></a>

**`--no-config`**

PATH and the startup
files are unchanged.

**`-W, -WW, -WWW`**

Analysis has strict, lenient, and annoying tiers. In the
`kosh` mood, strict and lenient findings are errors and annoying findings are
warnings. `-W` keeps that, `-WW` demotes lenient errors to warnings, and `-WWW`
also demotes strict errors. In the other moods no analysis runs until a level is
set, and `-W`, `-WW`, and `-WWW` enable the strict, the lenient, and the
annoying tier as warnings, each with the tiers before it.

A level set with `set`
passes to launched shells through `KOSH_ANALYSIS`. Runtime diagnostics do not
depend on the level, except that the compatibility moods warn about an unset
variable that expands to nothing only when a level is set.

**`--tab-selector MODE`**

MODE is `interactive`, the default, `external`, or `plain`. See
[Line editing](kosh_1.md#line-editing).

**`--enable-koshkit`**

The
option is off by default and affects only the compatibility moods, since the
`kosh` mood always resolves bundled utilities after PATH.

**`--enable-extended-arithmetic`**

The `kosh` mood keeps extended arithmetic on.

### Debug option details <a id="debug-option-details"></a>

**`--show-optimizer-diagnostics`**

A
compatibility mood shows these warnings only under `-WW`.

**`-N, --show-all-exit-codes`**

`set -N` and `set +N` toggle reporting of every command status.

### POSIX option details <a id="posix-option-details"></a>

**`-s, --stdin`**

The input order is described under [Invocation](kosh_1.md#invocation).

**`-c COMMAND, --command=COMMAND`**

The option may be repeated, and the strings run in order. A string
with a syntax error ends the shell with status 2, one that analysis rejects ends
it with status 1, and the later strings do not run.

**`-n, --no-exec`**

A compatibility mood reports
only the tiers that `-W` selects.

**`-o NAME, +o NAME`**

`-o posix` acts as
`--posix`. An unknown option exits with status 2. In the `kosh` mood,
`+o nounset` is ignored and `+o pipefail` exits with status 2.

**`+a, +C, +e, +f, +n, +p, +t, +u, +v, +x`**

+h and +m are accepted and
ignored. The `set` letters -B, -E, -H, -P, -b, and -k are not command-line
options and exit with status 2.

**`--`**

The next operand is the script.

### Bash option details <a id="bash-option-details"></a>

**`--norc`**

A login shell, its profiles, and BASH_ENV are unaffected.

**`-O NAME, +O NAME`**

An
unknown option exits with status 2, and a read-only option such as login_shell
is ignored. In the `kosh` mood, `+O failglob` and `-O nullglob` exit with status
2.

**`-p, --privileged`**

ENV, BASH_ENV, and the rc script of an interactive
non-login `kosh` mood shell are not sourced, KOSHCONF is ignored and removed,
and no settings file is read. A secure execution, such as a setuid or setgid
invocation, always drops the elevated ids and skips every startup and settings
file.

On Linux, such a shell whose real user is not root also clears its
capability sets, including the ambient set, and aborts if it can still switch to
root.

## Shell settings <a id="shell-settings"></a>

Shell settings can be read and changed through `set`, `shopt`, and `koshconf`,
and are reported by the `-o` test, SHELLOPTS, and BASHOPTS. Each setting has a
numeric id and a koshconf name. Some also have a Bash name or an option letter.
Interactive settings affect the editor, history, or diagnostic presentation.
Semantic settings can change script behavior. In the Scope column, `i` means
interactive and `s` means semantic.

The first component of a koshconf name is its topic. The `mood` and `init_moods`
options select moods, `interpreter` covers shell behavior, `compatibility`
covers Windows path mapping, `editor` covers the line editor and holds the
`editor.history` and `editor.hints` groups, `optimizer` covers analysis, and
`completion` and `debug` cover completion and debugging output.

The name of an
option from POSIX `set -o` starts with posix, and one from another Bash `set -o`
or `shopt` option starts with bash, as in
posix.interpreter.exit_on_command_failure for errexit. Every other option
exists only in Koshka.

An option with a Bash spelling is also accepted as posix.legacy.NAME when POSIX
`set -o` accepts NAME, and as bash.legacy.NAME otherwise, as in
bash.legacy.extglob. editor.base_mode has the alias posix.legacy.vi only. The
alias is the same option, and every option operand and kosh.conf line accepts
it.

Error messages, KOSHCONF, `koshconf create`, and `koshconf list` use only
the canonical name, and `koshconf list --all` adds each alias. The alias of
interactive_comments is bash.legacy.interactive-comments, spelled as the
`set -o` option.

A settings file may set every option except those fixed by the invocation or the
session. bash.interpreter.login_shell and bash.interpreter.restricted_shell
are read-only. bash.interpreter.privileged_mode,
posix.interpreter.parse_without_executing, and
bash.interpreter.exit_after_one_command come from the command line.
posix.interpreter.job_control, bash.editor.history.records_commands,
bash.editor.history.bang_expansion, and bash.interpreter.posixly_correct
follow the session and the mood.

editor.base_mode covers the Bash options emacs and vi. It is vi while
`set -o vi` is on and emacs otherwise, and `set -o emacs`, `set -o vi`, the `-o`
test, and SHELLOPTS behave as in Bash. The Bash options hashall and nolog have
no koshconf name. `set` and `-o` accept them, and `koshconf list --all` shows
them as posix.legacy.hashall and posix.legacy.nolog. koshconf get, koshconf set,
and the settings files reject these spellings. The bash.legacy shopt options in
the [Bash and POSIX settings](#bash-and-posix-settings) tables change nothing, so Bash startup files that set them run unchanged.
shopt accepts array_expand_once as the Bash 5.3 spelling of
assoc_expand_once.

### Koshka options <a id="koshka-options"></a>

| ID | Scope | Koshconf name | Shell spelling |
| --- | --- | --- | --- |
| 1 | s | `mood` | `set -M` |
| 2 | i | `completion.menu_style` |  |
| 3 | i | `editor.hints.show_command_synopsis` |  |
| 4 | i | `editor.hints.show_live_diagnostics` |  |
| 5 | i | `editor.auto_close_brackets_and_quotes` |  |
| 7 | i | `editor.request_extended_key_reports` |  |
| 8 | i | `editor.history.arrow_keys_search_by_typed_prefix` |  |
| 9 | i | `editor.history.file_path` |  |
| 10 | i | `editor.history.max_entries` |  |
| 11 | i | `completion.add_space_after_completed_word` |  |
| 12 | i | `optimizer.warning_level` | `set -W` |
| 13 | i | `optimizer.show_annoying_tier` |  |
| 14 | s | `optimizer.analyze_before_running` |  |
| 15 | s | `interpreter.resolve_koshkit_applets_as_commands` |  |
| 16 | s | `interpreter.arithmetic_uses_big_numbers` |  |
| 17 | s | `interpreter.mimic_shebang` | `set -I` |
| 18 | s | `debug.print_syntax_tree` | `set -A` |
| 19 | s | `debug.print_lexed_word_escapes` | `set -R` |
| 20 | s | `debug.report_nonzero_exit_codes` |  |
| 21 | s | `debug.report_every_exit_code` | `set -N` |
| 22 | s | `debug.print_evaluation_statistics` | `set -S` |
| 23 | s | `debug.print_memory_report_at_exit` | `set -G` |
| 24 | i | `completion.on_tab` |  |
| 25 | i | `editor.highlight_syntax_and_show_ghost_text` |  |
| 26 | i | `optimizer.show_source_traces` |  |
| 27 | s | `init_moods` |  |
| 174 | s | `compatibility.dev_null` |  |
| 175 | s | `compatibility.slash_tmp` |  |
| 179 | i | `editor.wrap_width` |  |

- `mood` takes kosh, sh, bash, or bash-posix.
- `completion.menu_style` takes interactive, external, or plain.
- `optimizer.warning_level` takes 0 to 3.
- `editor.base_mode` takes emacs or vi.
- `editor.history.file_path` and `editor.history.max_entries` correspond to
  KOSH_HISTORY_FILE and KOSH_HISTORY_SIZE.
- `editor.wrap_width` is an integer from 0 to 65535, 80 by default.

init_moods is a comma-separated list of the moods `-L` accepts, empty
by default, which sources the startup files of the session mood alone. It
applies only to an interactive or login shell at startup, `-L` replaces it, a
restricted shell ignores it in the settings files, and KOSHCONF never carries
it.

## Bash and POSIX settings <a id="bash-and-posix-settings"></a>

These tables list the shared options under their Koshka configuration names.
The names, aliases, scopes, and settings-file restrictions are described under
[Shell settings](#shell-settings).

### `set -o` options <a id="set-o-options"></a>

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
| 87 | i | `editor.base_mode` | `emacs, vi` |
| 89 | s | `bash.interpreter.posixly_correct` | `posix` |

### `shopt` options <a id="shopt-options"></a>

| ID | Scope | Koshconf name | Shell spelling |
| --- | --- | --- | --- |
| 128 | i | `bash.editor.cd_by_typing_directory_name` | `autocd` |
| 129 | s | `bash.legacy.assoc_expand_once` |  |
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

## Configuration examples <a id="configuration-examples"></a>

A user file that keeps the Bash grammar and edits with vi keys is

```text
mood=bash
editor.history.max_entries=20000
editor.base_mode=vi
```

A user file that keeps the `kosh` mood and sources ~/.bashrc in the `bash` mood
at interactive startup is

```text
init_moods=bash
```

`koshconf set` *option value* `--persist` writes the same lines.

## Settings file locations <a id="settings-file-locations"></a>

**`/etc/kosh.conf or %ProgramData%\kosh\kosh.conf`**

The system settings file, loaded first and only when no other user could have
written it.

On POSIX, the file and its directory must be owned by root and not
writable by group or others.

On Windows, the file and its *kosh* directory must
be owned by SYSTEM, Administrators, or TrustedInstaller, and no access control
entry may let another account modify them, change their permissions, or take
ownership. The directory may let others add files.

A file that fails the check
is skipped with a warning.

**`$XDG_CONFIG_HOME/kosh/kosh.conf or ~/.config/kosh/kosh.conf`**

The user settings file, loaded second. XDG_CONFIG_HOME applies when it is an
absolute path, and on Windows the directory falls back to USERPROFILE when HOME
is unset.

On POSIX, a shell whose effective user is root neither loads nor
writes it.

`koshconf create`, `koshconf set --persist`, and
`koshconf save --persist` write it.

A symbolic link is preserved and its target
is written. Existing permission bits are kept, and a new file receives mode 0644
less the creation mask. Missing directories are created. The previous file is
copied to *kosh.conf.bak* beside it.

**`$XDG_CONFIG_HOME/kosh/scripts/rc.kosh or ~/.config/kosh/scripts/rc.kosh`**

The user rc script of a login or interactive `kosh` mood is described under
[Startup script](#startup-script).

The Bash and POSIX startup files that the other moods source are listed in
the [shell reference](kosh_1.md#bash-and-posix-startup-files).

## Settings file syntax <a id="settings-file-syntax"></a>

A settings file is a sequence of lines ended by LF or CRLF, and a leading UTF-8
byte order mark is skipped. A line that is blank or starts with a number sign
after optional blanks is ignored. Every other line has the form *name*=*value*,
with blanks around the name and value ignored.

A value starting with a double or
single quote ends at the next quote of that kind, and only blanks may follow it,
so a value whose surrounding blanks must be kept needs quotes. A value cannot
span lines.

- A boolean is on or off, also accepted as true, false, 1, or 0.
- An enumeration is written as its value.
- A string must be valid UTF-8 without a NUL byte.
- `editor.history.max_entries` is an integer from 0 to 2147483647.
- `editor.wrap_width` is an integer from 0 to 65535.
- `init_moods` is a comma-separated list of moods without empty elements.

A name may be the canonical name or its legacy alias.

A file may set every option except those fixed by how the shell started, such as
bash.interpreter.login_shell, and those that follow the session, such as
posix.interpreter.job_control, as listed under [Shell settings](#shell-settings). A
semantic option in a file changes every shell that loads it, scripts included.
Unless every script should use such an option, it can be set for a session with
`set`, `shopt`, or `koshconf set`.

The mood applies first, wherever it appears, and the other lines are read under
it. In the `kosh` mood, a line that sets another value for an option the mood
holds, as listed under [Moods](kosh_1.md#moods), is skipped with a warning.

A later line
replaces an earlier one, and the user file and KOSHCONF replace the system file.

Command-line options and inherited KOSH_HISTORY_FILE or KOSH_HISTORY_SIZE
values take precedence over a settings file, as described under [Command-line options](#command-line-options)
and [History](kosh_1.md#history).

`koshconf` quotes a written value that starts with a quote or starts or ends
with a blank, and keeps the CRLF endings of a file that uses them. A value
holding a newline, or one that needs quoting and holds both quote characters,
cannot be written, and `koshconf set --persist` rejects it before changing the
session.

Persisting mood=kosh also removes the lines of the options the `kosh`
mood fixes. A line whose option this version does not recognize is kept at the
end of a rewritten file.

`koshconf create` writes every option permitted in a settings file except
init_moods. Each option has a help comment and its preset value. Only the mood
is active, and the remaining options are commented out, allowing the system
settings to apply until they are uncommented. A string option without a default
has an empty commented-out value. The `kosh` preset uses the values fixed by
that mood, so uncommenting those options produces no warning.

## Startup script <a id="startup-script"></a>

A login or interactive shell in the `kosh` mood sources scripts/rc.kosh in the
user configuration directory after the settings files. The script can test
`shopt -q login_shell` for login commands. No system rc script, ~/.koshrc, or
/etc/koshrc is read. The [Startup files](kosh_1.md#startup-files) section describes when the script
runs and the options that replace or skip it. Shell code belongs in the script, and
options in *kosh.conf*.

`koshconf create` writes the following script when it is missing and never
replaces it.

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

## Sharing settings with KOSHCONF <a id="sharing-settings-with-koshconf"></a>

`KOSHCONF` is a dynamic shell variable holding the mood and the interactive
options in a binary form encoded as standard base64 with padding.

It never carries the following options.

- `editor.history.file_path`
- `posix.interpreter.job_control`
- `bash.editor.history.bang_expansion`
- `bash.editor.history.records_commands`
- `bash.interpreter.login_shell`

An assignment to it is ignored.

It is not
exported unless the user exports it, and after `export KOSHCONF`, each started
command receives the settings current at its start. A prefix assignment such as
KOSHCONF=x before a command also passes the current settings to that command.

A
starting shell applies a KOSHCONF from its environment after the settings files
and removes it before any command runs, and `koshconf load` applies a value to a
running shell. The variable carries the settings to a remote shell in this way.

```sh
ssh host KOSHCONF="$KOSHCONF" kosh
```

### Encoding format <a id="encoding-format"></a>

The decoded bytes are a sequence of records, each an option id, a value length,
and the value. The id and the length are unsigned LEB128 varints, with seven
bits per byte, the least significant group first, and the high bit set on every
byte but the last.

- A boolean is one byte, 0 or 1.
- An enumeration is a varint holding the position of the value in its list.
- An integer is its decimal digits in ASCII.
- A string is its UTF-8 bytes.

The ids are listed under [Shell settings](#shell-settings)
and never change meaning. Semantic options never pass
through the environment, and a child shell loads the settings files itself.

A record with an unknown id, an option the encoding never carries, or a value a
settings file would reject is skipped with a warning. Only the canonical form is
accepted, so the base64 text carries its padding, every varint uses the fewest
bytes, and the ids strictly ascend. A value that breaks these rules, or has a
truncated record, is rejected as a whole with a warning.

## Settings file errors <a id="settings-file-errors"></a>

A missing settings file is skipped.

An unreadable file, a file over 1 MiB, one
that is not a regular file, and a system file that fails the ownership check
produce a warning and are skipped.

A line without an equals sign, an unknown
option, an invalid value, an unclosed quote, text after a closing quote, and an
option a file cannot hold each produce a warning with the file and line, and the
line is skipped. A settings file never stops the shell.
