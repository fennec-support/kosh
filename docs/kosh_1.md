# Koshka

> A reference to shell commands, interactive editing, diagnostics,
> and bundled utilities on Linux, macOS, and Windows.

2026-10-26

`kosh` is the Koshka shell, with a formatter, a language server, and script
diagnostics. It accepts POSIX shell syntax and the Bash extensions described in
this manual. The same grammar is available on Linux, macOS, and Windows.

## Downloads <a id="downloads"></a>

Koshka releases are available for Linux, macOS, and Windows. Choose the
binary for your operating system and processor. Replace `X.Y.Z` below with
the release version.

| System | Release file |
| ------ | ------------ |
| Linux on x86-64 | `kosh-linux-amd64-X.Y.Z` |
| Linux on ARM64 | `kosh-linux-aarch64-X.Y.Z` |
| macOS on Apple silicon | `kosh-darwin-aarch64-X.Y.Z` |
| Windows on x86-64 | `kosh-win32-amd64-X.Y.Z.exe` |

The release also includes `SHA256SUMS`, the compressed manuals `kosh.1.zst`
and `kosh.5.zst`, and the Bash completion file `kosh.bash`. Check your
download against its entry in `SHA256SUMS`. Run `kosh --version` to see the
installed version.

On macOS, an existing Homebrew installation can be updated with
`brew update && brew upgrade kosh`.

The VS Code and Zed extensions download Koshka when it is missing from
`PATH`. The Neovim plugin is available as `kosh.nvim`.

## Getting started <a id="getting-started"></a>

Start an interactive shell, run a command, or run a script with arguments.

```sh
kosh
kosh -c 'printf "%s\n" hello'
kosh script.kosh argument
```

Check a script without running it, or format it to standard output.

```sh
kosh --lint script.kosh
kosh --format script.kosh
```

Add `--apply` to write the lint fixes or formatted result back to the file.

## Synopsis <a id="synopsis"></a>

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

## Koshka features <a id="koshka-features"></a>

### Moods <a id="moods"></a>

The `kosh` mood is the default. It enables strict checks and analysis and holds
nounset, pipefail, failglob, and extended arithmetic on and nullglob off. `set`,
`shopt`, and `koshconf` reject any other value of these with a hint to use
`koshconf set mood bash`, but accept the value already held, so
`set -euo pipefail` still runs. Every other option can be toggled. The `kosh`
mood also resolves bundled utilities as commands after PATH.

The `bash` mood accepts Bash extensions with analysis off by default. The
`bash-posix` mood is Bash syntax with the POSIX option on, and sources the Bash
startup files. The `sh` mood follows dash. In this manual the `bash` and
`bash-posix` moods are the Bash moods, the `bash-posix` and `sh` moods, where
the posix option is on, are the POSIX moods, and every mood other than `kosh` is
a compatibility mood.

In the compatibility moods every Bash option can be
toggled, and an explicit `set -u`, `set -o pipefail`, `shopt -s failglob`, or
`koshconf set interpreter.arithmetic_uses_big_numbers` survives a later change
between them. `set -o posix` selects `bash-posix`, and `set +o posix` selects
`bash` from the `bash-posix` or `sh` mood.

The options whose defaults differ between the moods are these.

| Option | `kosh` | `bash` | `bash-posix`, `sh` |
| --- | --- | --- | --- |
| `expand_aliases` | on | off in a script | on |
| `extglob` | on | off | off |
| `failglob` | on | off | off |
| `nounset` | on | off | off |
| `pipefail` | on | off | off |
| `extended arithmetic` | on | off | off |
| `posix` | off | off | on |

In the `bash` mood, expand_aliases is on in an interactive shell. Every other
option has the same default in every mood. The moods also differ in grammar,
expansion, builtins, and errors, as described under [Shell grammar](#shell-grammar), [Expansions](#expansions), [Builtins](#builtins), and
[Shell errors](#shell-errors).

### Options <a id="options"></a>

The [options and configuration reference](kosh_5.md#command-line-options) lists command-line flags
and shell settings, including their aliases and restrictions.

An option given on the command line, on or off, overrides the settings files and
`KOSHCONF`.

### Diagnostics <a id="diagnostics"></a>

A diagnostic is written to standard error. It begins with the file, line, and
column, then `error`, `warning`, or `note`, and the message, followed by the
source line and a caret under the column. A finding of analysis ends with its
check code in parentheses, such as (SC2086), and a `note:` line may suggest a
fix.

Text built at run time is its own source, so a location in an eval string
appears as `eval:2:7:`, in a trap action as `trap:1:5:`, and in a `-c` string as
`-c:1:5:`. Standard input and interactive lines have no label.

Each function
call, sourced file, eval, trap action, subshell, and command, function, or
process substitution between the top level and the failing command adds a
backtrace frame beginning with `trace:`, and a repeated frame is printed once
with a repeat count. With several `-c` strings or files, the frame pointing into
the command line is printed once after the diagnostics of that string or file.

Diagnostics read the same in every mood. On a color terminal the labels and
codes are colored.

Linting, the language server, and script diagnostics report every syntax error
in one run. A construct unavailable in the selected mood, such as `[[` in the
`sh` mood, is reported, and its contents are checked for further syntax errors.

Analysis checks a script before it runs. It has strict, lenient, and annoying
tiers, which `-W` selects as described under [command-line options](kosh_5.md#command-line-options), and
`--list-diagnostics` lists every check. In the `kosh` mood analysis runs by
default, and strict and lenient findings reject the script, which then exits
with status 1 without running.

They include the following cases.

- A variable is expanded but never assigned.
- A function that uses arguments is called without arguments.
- A function is called before its definition.

An assignment in a function to a
variable not declared `local` is reported. In the `sh` mood, which has no
`local`, only a variable first set in a function and also used outside it is
reported.

In the other moods analysis runs only at a level that `-W` or `set -W`
selects, and its findings are warnings. An interactive shell analyzes each line
before it runs it. A script operand without a shebang is reported when the level
is not 0.

Analysis follows static source operands into readable files. It checks a
command, backtick, or process substitution as a subshell, and a function
substitution in the current shell. When more than one finding is reported, a
summary such as `Encountered 1 warning and 2 errors.` follows them.

A comment such as `# shellcheck disable=SC2086,unquoted-command-substitution`
suppresses analysis checks. A numeric code suppresses every variant under it, a
slug only its own variant, and the value may be `all` or a range such as
`SC2000-SC2100`. Before the first command it covers the file, and later it
covers the next and-or command. It does not suppress parser or runtime
diagnostics.

An unavailable command or path is reported through its first unavailable
component when that can be identified. An internal error stops the shell with an
`error:` line and `note:` lines identifying the process and build, and the shell
aborts.

### Linting <a id="linting"></a>

`kosh --lint` analyzes shell source without running it, with every diagnostic tier enabled at the severity of the mood, regardless of `-W`, `--no-diagnostics`, and `--no-annoying-diagnostics`. Each command string is analyzed in order, then each file operand, or standard input when neither is given. Startup and settings files are skipped. The diagnostics go to standard error, and standard output stays empty.

Shell commands are also extracted from CI configurations, Ansible, Dockerfile,
Compose, Markdown, Makefile, justfile, Kubernetes, and similar hosts, and only
the extracted sh, bash, or kosh source is checked. A file counts as shell source
by its extension or shebang, and a file in no recognized format fails with
status 1 and a list of the formats.

A host interpreter setting, such as a step
shell, a SHELL assignment or instruction, a shebang, or an executable option,
selects the shell, and a command for another interpreter is skipped. An exec
array contributes only the script of a shell `-c` command. A justfile recipe
line, joined across backslash continuations, is one command, and a shebang or
script recipe is one script.

A Jinja template of a shell script, such as
*name*.sh.j2, is checked with its template syntax masked. Host template
expressions and editor variables are masked in every format, interpolated values
count as plain words, and an unresolved command is not reported in a fragment
that holds an interpolation.

Variables that the host provides, such as CI
variables, exported recipe parameters, and Dockerfile ARG and ENV values, count
as set. Diagnostics point into the host file.

With `--apply`, safe structured fixes are written to each file, a summary of the
fixes is printed, and only the diagnostics that remain are reported. A fixed
file that would no longer parse is left unchanged. A warning-only result exits
with status 0, and a remaining error or an input or write failure exits with
status 1.

### Formatting <a id="formatting"></a>

`kosh --format` formats shell source without running it. Structural keywords and
compound braces go on separate lines, each nested level indents by two spaces,
direct `test` commands become bracket commands unless an earlier alias or
function shadows them, code wraps at 80 columns at a safe token boundary, and
lines end in LF.

With no file or a lone `-`, standard input is formatted, and
otherwise one file is formatted to standard output, highlighted when it is a
terminal. In a host format, only the shell fragments that the host allows to
change are formatted, such as Markdown code blocks. A syntax error is reported
and exits with status 1. Combined with `--lint`, the lint fixes are applied
first.

`--apply` replaces each file operand with the result. A changed file is replaced
atomically through a temporary file beside it, and its mode and symbolic links
are kept. A file that is not a regular file, or one that changed while it was
processed, is refused.

### Language server <a id="language-server"></a>

`kosh --as-language-server` runs a language server over standard input and
output for editor integration. It reports diagnostics after each document change
and provides completion, quick fixes, a fix-all action for non-conflicting safe
fixes, formatting, definition lookup, hover information, semantic highlighting,
a document outline, and rename. Completion is triggered by a space or a dash.

The server works inside the shell regions of the host formats that `--lint`
recognizes and follows relative sources from the document directory. Rename
updates variables, functions, and aliases in the open document and keeps the
sigil and braces of `$name` and `${name}`. A command can be renamed only when
the document defines it as a function or alias.

The mood of a document comes
from a recognized shebang, then from a language identifier of kosh, bash, sh, or
bash-posix, with rbash and shellscript meaning bash, then from the extension
.kosh, .bash, .sh, or .dash, and otherwise it is `kosh`. The server skips
startup and settings files, and it exits with status 0 after a shutdown request
and 1 otherwise.

### Shell settings <a id="shell-settings"></a>

Shell settings can be read and changed through `set`, `shopt`, and `koshconf`.
See the [settings reference](kosh_5.md#shell-settings) for option names, ids, scopes, and value
restrictions.

### Startup files <a id="startup-files"></a>

Every shell applies the settings files before any startup file. They are the
system file /etc/kosh.conf (%ProgramData%\\kosh\\kosh.conf on Windows), then the
user file $XDG_CONFIG_HOME/kosh/kosh.conf or ~/.config/kosh/kosh.conf, then a
`KOSHCONF` variable. The shell removes KOSHCONF from the environment after it
applies it.

These files hold settings, and [Configuration files](kosh_5.md#settings-file-syntax) describes them. A mood set
there selects the startup files below unless the command line or the invocation
name selects one, and options given on the command line keep their values. The
superuser on POSIX has no user settings file and no rc script.

A login or interactive shell in the `kosh` mood sources the rc script,
$XDG_CONFIG_HOME/kosh/scripts/rc.kosh or ~/.config/kosh/scripts/rc.kosh, and
no system rc file. In an interactive non-login shell, `--norc`, `--rcfile`, and
privileged mode apply to it as they apply to ~/.bashrc. A Bash startup file that
the script sources runs in the `bash` mood, as described under [`source`](#builtin-source).

`-L` on
the command line, or init_moods in a settings file, replaces the list of moods
whose startup files run, so `-L bash` makes a `kosh` session source only the
Bash startup files, in the `bash` mood, and then return to the `kosh` mood, and
`-L bash,kosh` also sources the rc script afterwards.

The compatibility moods use the files described under [Bash and POSIX startup files](#bash-and-posix-startup-files).

#### Skipping startup files <a id="skipping-startup-files"></a>

Clean (`-Q`), rescue, lint, format, and language server shells skip every
startup and settings file, as does a shell whose real and effective ids differ.
`--no-config` and privileged mode skip the settings files and KOSHCONF. A
subshell receives its settings from its parent, and an executable started as a
bundled utility or as koshkit applies none.

Privileged mode suppresses ENV,
BASH_ENV, and the rc script of an interactive non-login `kosh` mood shell.
Restricted mode, nounset, pipefail, and analysis take effect after startup, but
set, shopt, and koshconf in a startup file already reject a value that the mood
does not hold. A missing startup file is skipped, and an unreadable custom rc or
BASH_ENV file is also reported.

### Line editing <a id="line-editing"></a>

The interactive editor starts in emacs mode. Use `set -o vi` to select vi mode
and `set -o emacs` to return. The prompts are described under [Prompting](#prompting).
Input taller than the terminal shows the rows around the caret and leaves two
rows for hints, without the ghost suggestion and right prompt.

A control
character is drawn in caret notation, such as ^\[ for ESC, and a filename
holding one completes in the $'...' form. Bracketed paste inserts text without
running it.

Kill commands save the erased text in a 16-entry kill ring for the session, and
consecutive kills join into one entry. Ctrl-K on an empty line joins the next
line, and on an empty last line removes it. Undo reverts a yank or a last-word
insertion with all its replacements in one step. Ctrl-X Ctrl-E, or `:e` in vi
mode, opens the line in VISUAL, EDITOR, or vi; when the editor exits with status
0 the saved text replaces the line without running it, and otherwise the line is
unchanged and a message appears.

Ctrl-Z undoes while the editor collects a line, and stops a foreground program
otherwise. Ctrl-Shift-Z redoes when the terminal reports it apart from Ctrl-Z;
the editor requests such reports through the kitty keyboard protocol and xterm
modifyOtherKeys, which tmux passes on with its `extended-keys` option on.
`koshconf set editor.request_extended_key_reports off` stops the requests. On
the Windows console both keys undo and Ctrl-^ redoes.

On a non-empty line, Up recalls only history entries that begin with the text
typed before the first Up, and Down moves back; an empty line steps through all
history. editor.history.arrow_keys_search_by_typed_prefix, on by default,
controls this. `koshconf set editor.auto_close_brackets_and_quotes on` makes a
typed (, \[, {, or quote insert its closer, step over a typed closer, and delete
an empty pair on Backspace.

When the caret is on or just after a bracket, the bracket and its partner are
highlighted; a bracket inside quotes, a comment, a here-document, or escaped
never pairs.

#### Hints <a id="hints"></a>

Once the caret passes the command word, hints appear below the input under a
`synopsis` heading. Up to 16 rows explain the command, its subcommand, and its
flags. The current word is marked with `>`. Hints come from manual pages,
builtin or koshkit help, alias definitions, and function locations.

After a key
prefix such as Ctrl-X or a vi operator, the hints list the keys that complete
it. Hints are hidden while a menu or search is open.

After a short pause, the hint of an external program loads its section 1 manual
page, a subcommand page such as `git-commit`(1), or, for an allowlisted command,
its help output.

`man`, `manpath`, and the program run only from an absolute
directory owned by the user or root that no group or other user can write, and
never through an empty or relative PATH element. A keystroke never runs the
command itself. A Tab that needs a load shows `loading...` while it waits.

After the same pause, the first diagnostic for the current input appears as
`error`, `warning`, or `note`. This check does not follow sourced files, look up
commands, or open files. If the input cannot be parsed, its first error appears
under `syntax error`. Enter submits the whole buffer without a continuation
prompt. An unfinished construct is therefore reported as an error.

Set editor.hints.show_command_synopsis to off to hide command explanations and
stop their documentation loads. Set editor.hints.show_live_diagnostics to off
to hide findings and syntax errors. Both options are on by default. Turning off
both also hides the pending-key rows.

#### Completion <a id="completion"></a>

Ghost text offers prefix matches from completion and history; the right arrow or
End at the end of the line accepts it, and Ctrl-Right or Alt-F its next word.

Tab also offers smart-case and subsequence matches.

- An empty command position offers keywords, builtins, functions, aliases,
  bundled utilities, and PATH programs.
- Other words complete as paths.
- A word beginning with $ or ${ completes as a variable.
- A word beginning with ~ completes as a user.
- A word beginning with a dash completes as a flag of the command.

A spec registered with `complete` takes precedence for
its command. A directory candidate ends in a slash. The operand of `cd` or
`pushd` completes directories, also under each CDPATH entry unless it is
absolute or starts with a dot or tilde. Targets of `make` come from the
Makefile, and those after `cmake --build` *dir* `--target` from
*dir*/CMakeFiles/TargetDirectories.txt, without running either program or any
$(shell ...) function.

An explicit Tab first inserts the longest common prefix. A sole candidate replaces the token, followed by a space as completion.add\_space\_after\_completed\_word selects: `off` (the default), `on`, or `on-excluding-trailing-slash`. Several candidates go to the selector of `--tab-selector` or completion.menu\_style.

The interactive selector draws a menu below the line, headed
`selecting completions` or, for Ctrl-R, `incremental history search`.

Typing
narrows the menu: command, path, and koshconf option lists keep the best of the
exact-prefix, smart-case-prefix, and subsequence tiers, and other lists keep
every case-insensitive prefix match.

A history entry matches when it begins with
the query, contains the query, or contains the query's bytes in order.

Backspace
widens the list. Typing a blank or slash, or erasing past the start of the
token, refreshes the candidates after a pause. Accepting a directory opens a
menu of its entries.

The external selector passes the candidates to `KOSH_FZF_COMPLETION_COMMAND` and
replaces the token with its output. Dismissing the selector leaves the line
unchanged. A single candidate or a session without a controlling terminal uses
the plain listing. An empty selector command, a command that cannot be found, or
an exit status other than 0, 1, or 130 also uses the plain listing after a
message.

The plain selector prints the candidates as a column list, which a terminal
without decoration support always receives; for Ctrl-R it keeps the incremental
search inside the editor.

Ctrl-R searches the history through the selected selector, newest first, with
the line as the query; an accepted entry replaces the line. The search uses a
snapshot of the shared history file and does not change the private history.

#### Terminal integration <a id="terminal-integration"></a>

An interactive shell with terminal output writes OSC 133 A, B, C, and D marks
and OSC 7 working directory marks, plus OSC 633 marks when TERM_PROGRAM is
vscode, so terminals can find prompts, commands, statuses, and the directory. No
mark is written when TERM is dumb or `KOSH_SHELL_INTEGRATION` is 0.

#### Emacs mode keys <a id="emacs-mode-keys"></a>

**`Ctrl-A, Home`**

Move to the start of the line.

**`Ctrl-E, End`**

Move to the end of the line, or accept the ghost suggestion at the end of the
buffer.

**`Ctrl-Home, Ctrl-End, Ctrl-Up, Ctrl-Down, Ctrl-PageUp`**

Move to the start or end of the buffer. Ctrl-PageUp moves to the start.

**`PageUp, PageDown`**

Move the cursor up or down by the rows of the screen.

**`Ctrl-B, Left`**

Move one character left.

**`Right`**

Move one character right, or accept the ghost suggestion at the end of the
buffer.

**`Alt-B, Ctrl-Left, Alt-Left`**

Move to the start of the previous word.

**`Alt-F, Ctrl-Right, Alt-Right`**

Move past the next word, or accept the next word of the ghost suggestion.

**`Ctrl-P, Up`**

Move up a row, or recall an older entry from the first row. A typed or edited
multi-line buffer that joins no line with a backslash needs a second press.

**`Ctrl-N, Down`**

Move down a row, or recall a newer entry from the last row.

**`Alt-<, Alt-,, Alt->`**

Recall the oldest or newest entry, or in `slurp` move to the start or end of the
file.

**`Enter, Ctrl-J, Ctrl-M, Shift-Enter`**

Run the line, or continue it after a trailing backslash. A typed or edited
multi-line buffer that joins no line with a backslash runs on a second press.

**`Alt-Enter`**

Insert a line break.

**`Tab, Shift-Tab, Ctrl-Space`**

Complete the token. Ctrl-Space does so only on POSIX.

**`Backspace, Ctrl-H, Delete`**

Delete the character before or under the cursor.

**`Ctrl-D`**

Delete the character under the cursor, or exit on an empty line.

**`Ctrl-W, Alt-Backspace, Alt-H`**

Kill the previous word.

**`Alt-D, Ctrl-Delete`**

Kill to the next word boundary, which is the rest of the word under the cursor,
or the blanks before the next word.

**`Ctrl-K, Ctrl-U`**

Kill to the end or start of the line.

**`Ctrl-Y, Alt-Y`**

Yank the newest kill, or replace the yank with the next older kill.

**`Ctrl-T, Alt-T`**

Swap the characters or words around the cursor.

**`Alt-U, Alt-L, Alt-C`**

Uppercase, lowercase, or capitalize to the end of the word.

**`Alt-\`**

Delete blanks around the cursor.

**`Alt-., Alt-_`**

Insert the last word of an older history entry on each press.

**`Ctrl-_, Ctrl-Z, Ctrl-X Ctrl-U`**

Undo.

**`Ctrl-^, Ctrl-Shift-Z`**

Redo.

**`Ctrl-X Ctrl-E`**

Edit the line in an external editor.

**`Alt-X, Ctrl-X z`**

Open the M-x minibuffer, or repeat the last M-x command.

**`Ctrl-X Ctrl-C`**

Exit with the status of the last command.

**`Ctrl-X s, Ctrl-X Ctrl-S`**

Save the file in `slurp`.

**`Ctrl-S`**

Find text in the buffer as it is typed, forward and wrapping around; text
without capitals matches either case. Ctrl-S and Ctrl-R move to the next and
previous match, and on empty text reuse the last search. Enter keeps the cursor
there, and Escape returns it.

**`Ctrl-R, Ctrl-F`**

Search the history. In `slurp`, Ctrl-R finds text backward.

**`Ctrl-L`**

Clear the screen.

**`Ctrl-C`**

Abandon the line.

**`Ctrl-V`**

Enter multi-cursor mode.

Shift or Alt with Up, Down, Left, Right, Home, or End acts as the key alone,
except where an entry above gives it a meaning, and Ctrl-Backspace deletes one
character.

#### Vi insert mode keys <a id="vi-insert-mode-keys"></a>

Every emacs key works except Ctrl-V, Alt-X, and the Ctrl-X chords. Arrow keys
recall history.

**`Escape`**

Enter command mode and move one character left.

#### Vi command mode keys <a id="vi-command-mode-keys"></a>

In command mode the arrow keys move between rows without recalling history.

**`1-9, then 0-9`**

Count for the next command.

**`"x`**

Use register *x* (a to z, either case) for the next delete, change, yank, or
paste.

**`h, l, Left, Right, Backspace, Space, Ctrl-B, Ctrl-F`**

Move left or right.

**`w, W, b, B, e, E`**

Move to the next start, previous start, or end of a word or blank-separated
word.

**`0, ^, $, Home, End, Ctrl-A, Ctrl-E`**

Move to the line start, first non-blank, or line end.

**`fx, Fx, tx, Tx, ;, ,`**

Find *x* forward or backward, stop before it, or repeat the find.

**`k, j, Up, Down, Ctrl-P, Ctrl-N`**

Move between rows; k and j recall history from the first or last row.

**`gg, G`**

Move to the first or last row, or row N with a count.

**`d, c, y`**

Delete, change, or yank over a motion; doubled, or with j, k, Up, or Down, over
whole lines.

**`gq, gqq`**

Wrap the lines of a motion, or the current line, at editor.wrap_width (80),
keeping the indentation of each paragraph's first line.

**`x, X, D, C, s, S`**

Delete under or before the cursor, delete or change to the line end, change one
character, or change the line.

**`rx, ~`**

Replace the character, or toggle its case and move right.

**`p, P`**

Paste after or before the cursor.

**`o, O, J`**

Open a line below or above, or join the next line with one space.

**`{, }`**

Move to the previous or next empty line.

**`i, a, I, A, R`**

Insert before or after the cursor, at the first non-blank, or at the line end; R
inserts without overwriting.

**`.`**

Repeat the last change other than a yank.

**`u, Ctrl-_, Ctrl-Z`**

Undo.

**`Ctrl-R, Ctrl-^, Ctrl-Shift-Z`**

Redo.

**`/, ?, n, N`**

Search the history, or in `slurp` find text forward or backward and repeat the
find in either direction.

**`:`**

Open the colon command line.

**`v, V, Ctrl-V`**

Select characters, lines, or a rectangle.

**`Enter, Ctrl-D, Ctrl-C, Ctrl-L`**

Act as in emacs mode.

#### Vi visual and rectangle keys <a id="vi-visual-and-rectangle-keys"></a>

Motions move the end of the selection, or the rectangle corner, without a count.
Visual mode accepts `d`, `x` (delete), `c` (change), `y` (yank), `gq` (wrap,
repeatable with .), and `:` (colon commands on the selected lines, shown as
:'\<,'\>). Rectangle mode accepts `d`, `x` (delete), and `I`, `c`, `C` (insert
at the left column of every line when insert mode ends). Escape, `v` or Ctrl-V,
and any other unrecognized key leave the selection, and Enter, Ctrl-D, and
Ctrl-C leave it and act as in emacs mode.

#### Vi colon commands <a id="vi-colon-commands"></a>

Enter runs the command, and Escape, Ctrl-C, Ctrl-D, or Backspace on an empty
line closes it. Unknown commands do nothing.

**`:q, :q!, :quit, :wq, :wq!, :wqa, :wqa!, :x`**

Exit with the status of the last command. In `slurp`, the w forms save first.

**`:N, :$`**

Move to row N of the buffer, or to the last row.

**`:w, :write`**

Save the file in `slurp`.

**`:s/old/new/[g]`**

Replace the first match of the basic regular expression *old* on the caret row,
or every match with `g`; `:%s` acts on every row. Any punctuation can delimit,
and in *new* `&` is the match and `\1`-`\9` its groups. `:&`, `:&&`, and `:s`
repeat the last substitution, `:&&` with its `g` flag, and `:%&` and `:%&&` on
every row.

**`:set option[=value|?], :set nooption`**

Set, show, or turn off a `koshconf` option; Tab completes it.

**`:save [option]`**

Write the option, or every option, to *kosh.conf*.

**`:format, :%format`**

Format the caret row or the buffer as `--format` does; a source that does not
parse is kept and its error shown.

**`:e, :edit, :u, :undo, :red, :redo`**

Edit in an external editor, undo, or redo.

**`:h, :help`**

List the colon commands until the next key.

#### Emacs M-x commands <a id="emacs-m-x-commands"></a>

Tab completes a command, Enter runs it, and Escape, Ctrl-C, Ctrl-D, or Backspace
on an empty minibuffer closes it. The hint rows list the matching commands, each
with what it does when six or fewer match. The commands are `help` (list the
commands), `replace-string` and `replace-regexp` (replace every match in the
buffer, with \\& and \\1-\\9 in a regexp replacement), `repeat` (as Ctrl-X z),
`set-option`, `get-option`, and `save-option` (as :set and :save), `format` and
`format-buffer` (as :format and :%format), `fill-paragraph` (as gq on the
paragraph), `isearch-forward` and `isearch-backward`, `kill-kosh` (as Ctrl-X
Ctrl-C), `undo` and `undo-redo`, and the commands of the matching keys:
`beginning-of-line`, `end-of-line`, `forward-word`, `backward-word`,
`scroll-down-command`, `scroll-up-command`, `beginning-of-history`,
`end-of-history`, `delete-char`, `backward-delete-char`, `kill-word`,
`backward-kill-word`, `kill-line`, `unix-line-discard`, `yank`, `yank-last-arg`,
`transpose-chars`, `transpose-words`, `upcase-word`, `downcase-word`,
`capitalize-word`, `delete-horizontal-space`, and `clear-screen`.

#### Multi-cursor keys <a id="multi-cursor-keys"></a>

Up, Down, Ctrl-P, and Ctrl-N add a cursor on the row above or below. Left,
Right, Home, End, Ctrl-B, Ctrl-F, Ctrl-A, and Ctrl-E move every cursor, and
typed characters and Backspace act at every cursor. Ctrl-G, Escape, and any
other unrecognized key leave the mode, and Enter, Ctrl-D, and Ctrl-C leave it
and act as in emacs mode.

#### Completion menu keys <a id="completion-menu-keys"></a>

**`Down, Ctrl-N, Up, Ctrl-P, Shift-Tab`**

Highlight the next or previous row, wrapping around.

**`Tab`**

Accept the row, opening a directory.

**`Right`**

Accept the row and offer the completions that follow it.

**`Enter`**

Close the menu and run the line as it stands.

**`character, Backspace`**

Narrow or widen the rows.

**`Escape`**

Close the menu and keep the edited line.

**`Ctrl-G`**

Close the menu and restore the original line, as does any other unrecognized
key.

**`other`**

Close the menu, then act on the line.

#### History search keys <a id="history-search-keys"></a>

In the interactive selector, Enter and Tab accept the entry without running it,
Escape, Ctrl-G, and other unrecognized keys restore the line, and the other keys
act as in the completion menu. The plain selector accepts:

**`Down, Ctrl-N, Ctrl-R`**

Step to an older match.

**`Up, Ctrl-P, Ctrl-F, Shift-Tab`**

Step to a newer match.

**`character, Backspace`**

Extend or shorten the query.

**`Enter, Tab`**

Accept the match without running it.

**`Escape, Ctrl-G`**

Cancel and restore the line, as does any other unrecognized key.

**`Ctrl-C, Ctrl-D`**

Cancel, restore the line, and act on it.

**`other`**

Accept the match, then act on the line.

### Prompting <a id="prompting"></a>

An interactive shell prints PS1 before it reads a command. PS0 is printed after
a command is read and before it runs. PROMPT_COMMAND runs before each prompt.
It is a command or, as an indexed array, a list of commands, which run in order.
Each sees the status and $\_ of the previous command, and the hook does not
change them.

PS2 is "\> " by default, but the editor submits the whole buffer on
Enter with no continuation prompt. PS3 is the prompt of `select`. PS4 is printed
before each line of `xtrace` output, "+ " by default, and its first byte repeats
once per enclosing substitution, eval, source, and trap action.

PS0, PS1, PS2, and the right prompt undergo parameter expansion, command
substitution, and arithmetic expansion each time they are printed, after the
backslash escapes below are replaced. PS4 is printed as written. The default PS1
shows the git branch with ahead and behind counts, the user, host, and
directory, and a bullet.

RPS1, or RPROMPT when RPS1 is unset or empty, is a right prompt drawn on the
first input row. It is expanded like PS1, blanks at either end are dropped, and
it is hidden when there is no room or when it holds a control character other
than a color sequence. An interactive shell started with neither set uses a dim
"\\L \\M".

A nonempty KOSH_TRANSIENT_PS1 replaces PS1 on a line that Enter
submits or Ctrl-C interrupts, so KOSH_TRANSIENT_PS1='\\$ ' leaves "$ " before
each command in the scrollback. KOSH_TRANSIENT_RPS1 does the same for the
right prompt. An interactive shell started without it sets it to \\\[\\\], which
erases the right prompt, and KOSH_TRANSIENT_RPS1='\\L' keeps each duration.

The prompts recognize these escapes.

**`\u, \h, \H`**

The user, the host up to the first dot, and the full host.

**`\k, \K`**

\\h and \\H, colored on a color terminal.

**`\w, \W, \P`**

The working directory with HOME shown as ~, its last component, and the \\w form
shortened to ... and its last 21 bytes when it is longer than 24 bytes.

**`\g`**

The git branch, as in KOSH_GIT_BRANCH.

**`\$, \?`**

\# for the superuser and $ otherwise, and the status of the last command.

**`\j`**

The number of jobs.

**`\L, \M`**

The duration of the last command, empty under 5 milliseconds, and the resident
memory it used. \\M is the highest peak of its processes and their descendants,
or the growth of the shell during the command if larger, and it is empty when
the shell only reused memory.

**`\.`**

A bullet colored by the last status. It is white after 0, bold red after 1, and
on a truecolor terminal it shifts toward magenta in four steps for higher
statuses.

**`\t, \T, \@, \A, \d`**

The time as HH:MM:SS in 24-hour and 12-hour form, the 12-hour time as HH:MM with
AM or PM, the time as HH:MM, and the date as weekday, month, and day.

**`\D{format}`**

The time formatted by strftime, or as HH:MM:SS for an empty format.

**`\s, \v, \V`**

The basename of the invocation path without a login dash or .exe, the major and
minor version, and the full version.

**`\!, \#`**

The history number of the next command, and the command number, counting from 1
for each line read from a terminal, script, or standard input.

**`\l`**

The last component of the terminal of standard input, or tty.

**`\n, \r, \e, \a, \\`**

A newline, a carriage return, an escape, a bell, and a backslash.

**`\nnn`**

The byte with octal value nnn, of one to three digits.

**`\[, \]`**

Begin and end a region that takes no space on the screen, such as a color
sequence. A raw 001 or 002 byte in PS0, PS1, RPS1, KOSH_TRANSIENT_PS1, or
KOSH_TRANSIENT_RPS1 does the same.

Any other backslash sequence is printed as written. Text inserted by an escape
is never expanded, and the user, host, directory, branch, shell, and version
draw a control byte in caret notation and a byte that is not UTF-8 as \\xHH. The
variables KOSH_PS1_USER (\\u), KOSH_PS1_HOST (\\h), KOSH_PS1_HOST_FULL
(\\H), KOSH_PS1_CWD (\\w), KOSH_PS1_CWD_BASE (\\W), KOSH_PS1_CWD_SHORT
(\\P), KOSH_PS1_GIT_BRANCH (\\g), KOSH_PS1_PROMPT_SIGN (\\$),
KOSH_PS1_STATUS (\\?), KOSH_PS1_JOBS (\\j), KOSH_PS1_DURATION (\\L),
KOSH_PS1_MEMORY (\\M), KOSH_PS1_DOT (\\.), KOSH_PS1_TIME (\\t),
KOSH_PS1_TIME_12 (\\T), KOSH_PS1_TIME_AMPM (\\@), KOSH_PS1_TIME_SHORT
(\\A), KOSH_PS1_DATE (\\d), KOSH_PS1_SHELL (\\s), KOSH_PS1_VERSION (\\v),
KOSH_PS1_VERSION_FULL (\\V), KOSH_PS1_TTY (\\l), KOSH_PS1_HISTORY_NUMBER
(\\!), and KOSH_PS1_COMMAND_NUMBER (\\#) hold the current text of each
escape, and assignments to them are ignored.

The terminal title shows the user and directory while the shell waits, and the
command while a foreground command runs. Output of PROMPT_COMMAND or PS1 can
replace the idle title.

### History <a id="history"></a>

An interactive shell records each submitted command line as one event, even when
it spans several lines, while the `history` option is on, which is the default.
A non-interactive shell records no events, whatever the option says. A line that
repeats the previous event is not recorded again. A line with a history
expansion error is not recorded.

The history file is KOSH_HISTORY_FILE, ~/.kosh_history by default. An
interactive shell loads it at startup into its own list and appends each command
to the file as soon as it is submitted, so other running shells can read it. It
does not import the commands that other shells append later. Ctrl-R searches a
snapshot of the whole file. A non-interactive shell writes the file only through
`history -w` or `history -a`.

The file holds one event per line, with a newline in an event written as \\n and
a backslash as \\\\. It has mode 0600, and the shell locks its directory while
it writes. A record over 65535 bytes is skipped when the file is read.
KOSH_HISTORY_SIZE is the number of events kept in the list and in the file,
4096 by default, and 0 disables history. When the shell exits, it rewrites the
file with only the newest KOSH_HISTORY_SIZE events.

editor.history.file_path and editor.history.max_entries set the two variables
from the settings files, and a value inherited from the environment takes
precedence. The variables are exported only when they were inherited exported or
are exported explicitly. The Bash history variables have no effect, as
listed under [Shell variables](#shell-variables). `history` and `fc` list, edit, and run events.

### Koshka variables <a id="koshka-variables"></a>

The following variables identify Koshka and control its features.

#### Shell-maintained variables <a id="shell-maintained-variables"></a>

The assignment and inheritance rules under [Shell variables](#shell-variables) apply to these
variables.

**`KOSH`**

The invocation path. Every mood sets it without exporting it, so a line guarded
by `test -n "$KOSH"` runs only in Koshka.

**`KOSH_VERSION, KOSH_COMMIT, KOSH_BUILD_MODE, KOSH_OS`**

The version, build commit, build mode, and build host. They are not exported.

**`KOSH_IDENTITY`**

The read-only, exported CRC-32 of the running executable, in lowercase hex.

**`KOSH_ANSI_*`**

Terminal escapes in BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE, their
BRIGHT\_ forms, BOLD, DIM, and RESET, empty when color is disabled. An
assignment replaces the value.

**`KOSH_ANALYSIS`**

The analysis settings changed with `set`, exported as a list of words. The word
`mimicry` stands for `-I`, `warnings=`*N* for the `-W` level, `no-annoying` for
optimizer.show_annoying_tier off, and `no-diagnostics` for
optimizer.analyze_before_running off. Each `set` rewrites it, and it is
removed when empty. A launched shell applies it, and an explicit `-W` replaces
the inherited level. Command-line options do not set it.

**`KOSH_GIT_BRANCH, KOSH_GIT_AHEAD, KOSH_GIT_BEHIND`**

The branch of the repository holding the working directory, a short hash for a
detached HEAD, or empty, and the unpushed and unpulled commit counts against the
upstream, empty when zero or unavailable. The search stops at
GIT_CEILING_DIRECTORIES. An assignment replaces the branch.

**`KOSH_PS1_*`**

The current text of each prompt escape, as described under [Prompting](#prompting).

**`KOSHCONF`**

The current mood and interactive options in the encoding of [Configuration files](kosh_5.md#encoding-format).
Assignments are discarded, and the shell never exports it by itself.

#### Configuration variables <a id="configuration-variables"></a>

**`KOSH_HISTORY_FILE, KOSH_HISTORY_SIZE`**

The history file and its size, as described under [History](#history).

**`KOSH_DIRECTORY_HISTORY`**

The file of visited directories that `z` ranks, ~/.kosh_directory_history by
default.

**`KOSH_CALC_HISTORY`**

The history file of `calc`, ~/.kosh_calc_history by default.

**`KOSH_FLAGS`**

Default command-line options, as described under [Invocation](#invocation).

**`KOSH_FZF_COMPLETION_COMMAND, KOSH_FZF_COMPLETION_OPTS`**

The external selector program, fzf by default, and its options, by default
--multi --layout=reverse --height=~40% --min-height=3, split on blanks without
expansion.

**`KOSH_SHELL_INTEGRATION`**

A value of 0 turns off the terminal integration marks.

**`KOSH_WELCOME, KOSH_FAREWELL`**

Messages printed with the shell prefix at interactive startup and exit. An
interactive shell sets them unless the environment holds them, to Welcome :3 in
the `kosh` mood, Bash me harder! in the Bash moods, POSIX me harder! in the `sh`
mood, and Goodbye :c. An exported empty value, or a value of zero display width
such as one set by printf -v KOSH_WELCOME "\\r", disables a message, and a
startup file can set one, as in KOSH_WELCOME=$(date).

### Koshka builtins <a id="koshka-builtins"></a>

The Koshka builtins assimilate, bench, koshconf, koshkit, slurp, and z exist in
every mood. The common help and exit-status rules are described under
[Builtins](#builtins).

#### `assimilate` <a id="builtin-assimilate"></a>

**`assimilate [options] [TARGET]`**

Copy the running executable through scp to TARGET and install it as `kosh` in
the first usable remote PATH directory, skipping a path that contains sbin.
Without TARGET, install it the same way into the first usable local PATH
directory and create a relative symbolic link to `kosh` for each bundled utility
and utility alias whose name is free there or already links to it.

On Windows
the local form installs kosh.exe and makes a hard link with the .exe suffix for
each utility and link mood, and a later run with a changed binary makes those
links again. A name that another entry in that directory holds, or that a
program elsewhere on PATH provides, stays unlinked, and each such group is
reported in one warning.

`--link-mood=`*MOOD* adds a relative bash, dash, sh, or kosh link. It may be
repeated and takes comma-separated values. `--install-directory=`*DIR* installs
into DIR, even one containing sbin. `--ssh-command=`*COMMAND* and
`--scp-command=`*COMMAND* replace ssh and scp and need TARGET. `-x`, `--trace`
prints progress.

The shell asks for confirmation with the target and the full
path of the new binary, as described under [Bundled utilities](#bundled-utilities), and `-y`,
`--yes` skips the question. The copy is validated before the installed binary is
replaced, and a failed installation restores the previous file or link.

#### `bench` <a id="builtin-bench"></a>

**`bench [options] command ...`**

Run each command through `kosh -c` and report the wall time, peak resident set, and available performance counters as a mean, standard deviation, and range. Sampling stops after a nonzero status, and bench returns 1, unless `--ignore-exit-code` is given. `--show-output`, `-N` (`--no-shell`), `-n` (`--runs`), and `-t` (`--duration`) control output, direct execution, the sample count, and the time budget in milliseconds, 3000 by default.

`-w` (`--warmup`) sets the number of unmeasured runs before sampling. Without a value it runs one to three until two runs agree within 10%, and without the option one unmeasured run precedes sampling. The run and warmup counts range from 1 to 100000. A note follows the report when a sample is an outlier.

`-p` (`--profile`) is `--runs 1 --extended` and cannot be combined with `-n`. A missing command, a count out of range, and other usage errors return 1, and an interrupt returns 130.

`-E` (`--extended`) also reports CPU time and usage, waiting time (wall time
minus user and system time, or the average active task count times wall time
minus CPU time for a parallel command), context switches, page faults, block
operations, and read and write calls and bytes. Each platform adds what it can
measure.

Linux adds scheduler and hardware counters and, from one extra untimed
traced run, system calls, threads, processes, execs, and task counts. macOS adds
processor counters, run-queue wait, wakeups, energy, and storage figures.
Windows adds whole process-tree counters through a job object. Traced rows are
marked (traced), a row marked \* counts only the measured process, and a row the
platform cannot report is omitted.

#### `koshconf` <a id="builtin-koshconf"></a>

**`koshconf create PRESET [--force]`**

**`koshconf set OPTION VALUE [--persist]`**

**`koshconf get OPTION`**

**`koshconf list [-a|--all]`**

**`koshconf load ENCODED`**

**`koshconf save [--persist]`**

Read and change the Koshka settings.

- `create` writes the default file of the `kosh`, `bash`, or `sh` preset to the user settings file, with every setting except the mood commented out, and refuses to replace an existing file without `--force`. It also writes the default user rc script when that is missing.
- `set` changes the option for the session, and with `--persist` also rewrites its line in the user settings file, saving the previous file as *kosh.conf.bak*.
- `get` prints the value.
- `list` prints every option as *name*=*value* in alphabetical order, and `-a` adds a line for each alias and for the `set` options hashall and nolog, which only `set` accepts.
- `load` applies a `KOSHCONF` value to the session.
- `save` prints the current mood and interactive options as a `KOSHCONF` value, so a kosh started with `KOSHCONF=$(koshconf save)` adopts them. With `--persist`, it writes the user settings file instead, holding each setting that differs from the system file or, where the system file does not set it, from the default of the mood.

A boolean is on or off, also accepted as true, false, 1, or 0. An unknown option
or value, or a file that cannot be read or written, returns 1. Invalid command
syntax returns 2. An interactive shell reports the file it wrote on standard
error. A restricted shell refuses create, set, load, and save --persist.

#### `koshkit` <a id="builtin-koshkit"></a>

**`koshkit [--color when] [utility [argument ...]]`**

Run a bundled utility, as described under [Bundled utilities](#bundled-utilities).

#### `slurp` <a id="builtin-slurp"></a>

**`slurp [-e|-v|-V] [+LINE] FILE ...`**

Edit each FILE in the line editor, one after another. A missing FILE starts
empty, and Ctrl-C ends the whole run. `+`*LINE* opens the next FILE at that
line.

`-e`, `--emacs` selects the emacs key set, `-v`, `--vi` the vi key set in
insert mode, and `-V`, `--vi-normal` the vi key set in normal mode on the first
line. The bundled utilities vi and emacs run slurp -V and slurp -e.

The session
runs on the alternate screen. A shell script, recognized by its shebang or
extension, keeps syntax highlighting, and the format commands change only such a
file.

The top row repeats the prompt and the command, the file fills the rows below,
and the last two rows show the file kind, line endings, size, and caret
position, then the key set and the last save result or the keys of the key menu.
A hint, a message, or a command line that needs more rows takes up to eight
while it shows.

Enter starts a new line, and Up and Down do not recall history.
Tab inserts a tab at the start of a line or in a file that is not a shell
script, and completes otherwise. Tabs and trailing blanks are drawn shaded.

The
mouse moves the caret, selects by dragging, and scrolls with the wheel. With the
emacs key set, Shift with a movement key also selects.

Ctrl-X s, Ctrl-X Ctrl-S, `:w`, and `:write` save and keep the session open.
`:wq`, `:wq!`, `:wqa`, `:wqa!`, and `:x` save and leave when the save succeeds.

`:q`, Ctrl-X Ctrl-C, and Ctrl-D on an empty buffer leave without saving, and
Ctrl-C leaves with status 130. While the buffer has unsaved changes, `:q`
refuses, the other keys and M-x kill-kosh must be pressed twice, and `:q!`
leaves at once.

`:`N and `:$` move to line N and the last line. Ctrl-S and
Ctrl-R, or `/` and `?`, find text as it is typed. `help` in M-x and `:help` list
the commands.

A save writes a complete copy beside the file and renames it over the file,
keeping its mode and following a symbolic link. A file that another user owns, a
file with several links, or a file in a directory that neither the user nor root
owns, or that others can write without the sticky bit, is rewritten in place,
keeping its owner, group, and links, after a private backup copy is made in the
home or temporary directory.

The copy is removed after a successful write and
reported after a failed one, and without a copy nothing is written. A save is
refused when another file has replaced the loaded one. A failed save is reported
and the edits stay open. CRLF line endings and the final newline are kept.

An empty operand, a directory, a special or unreadable file, a file with a NUL
byte or invalid UTF-8, and a file over 16 MiB are refused with status 1, and the
next FILE still opens. A restricted shell, or standard input or output that is
not a terminal, is refused with status 1. A missing FILE, a `+`*LINE* without a
FILE, or more than one mode option returns 2.

#### `z` <a id="builtin-z"></a>

**`z [query ...]`**

Change to a directory that interactive shells have visited. A single operand
that is an existing directory is used as it is. Otherwise the operands form one
case-insensitive substring query, ranked by visit count and recency. With no
operand, z changes to the home directory. No matching directory returns 1.

### Bundled utilities <a id="bundled-utilities"></a>

The `koshkit` builtin runs a bundled utility, and an unknown utility is an
error. With no utility it groups the catalog under POSIX and Koshka headings.
`koshkit --list` prints the utilities one per line, and `koshkit --list-flags`
prints each utility and alias with its short and long flags.

The bundled set is basename, bc, cal, calc, cat, chgrp, chmod, chown, cksum,
cmp, comm, cp, csplit, cut, date, df, diff, dirname, du, env, evil, evildisk,
evilfiles, evilfs, evilio, eviliso, evillogs, evilnet, evilps, evilss, expand,
expr, file, find, flock, fold, fuser, getconf, goodcore, goodfsw, goodnode,
goodstat, grep, head, id, killall, less, link, ln, locale, logger, logname, ls,
make, man, meow, mkdir, mkfifo, mknod, more, mv, nice, nl, nohup, nproc, od,
paste, pathchk, pkill, pr, printenv, ps, readlink, realpath, renice, retry, rm,
rmdir, sed, seq, sleep, slurp, sort, split, stat, strings, stty, sync, tabs,
tail, tee, timeout, touch, tput, tr, tsort, tty, uname, unexpand, uniq, unlink,
watch, wc, which, who, whoami, xargs, and yes, with the aliases egrep and fgrep
for grep and vi and emacs for slurp.

Every utility supports `--help`, and `--version` when invoked under its own
name. Options may come before and after operands, and `--` ends them. calc, env,
expr, flock, nice, nohup, retry, timeout, watch, and xargs end their options at
the first operand, so the command after it keeps its own options.

A utility with a styled report, such as cal, cat, du, ls, watch, meow, and the
evil and good utilities, uses color only when its output is a terminal and
NO_COLOR is empty or unset. `koshkit --color` *when* selects always, never, or
auto for the utility that follows, and diff and grep also take `--color`. tabs
and tput write escape sequences wherever their output goes.

A utility returns 0 on success, 1 when an operand fails, and 2 for a usage
error, such as an unknown flag or a missing operand. A failure on one file is
reported and the utility goes on with the next. In the `kosh` mood a usage error
ends the script, as described under [Shell errors](#shell-errors). An unknown utility returns
1\. A diagnostic names the utility, as in Koshkit \`seq\`: for the builtin and
seq: for a utility started under its own name.

A confirmation question, such as one from `rm -i`, `cp -i`, `mv -i`, or
`assimilate`, is written to standard error after the shell prefix and followed
by \[y/N\] or \[Y/n\], the default in capitals. The answer is read from standard
input up to a newline. An empty line takes the default, an answer beginning with
y or Y confirms, and any other answer or end of input declines.

When direct koshkit lookup is enabled, a PATH program takes precedence over a
bundled utility. Started under a utility name, the executable runs that utility.
The name is matched with its extension removed and with its case kept. Started
as `koshkit`, the link that `make install` places beside `kosh`, it runs the
utility in its first operand, as in `koshkit ls -l`, and otherwise acts as the
koshkit builtin. Either form skips shell startup, settings, and KOSH_FLAGS.

#### `calc` <a id="utility-calc"></a>

**`calc`**

Evaluate arithmetic operands with arbitrary-precision integers and finite
decimals. `-i`, `--interactive` and `-p`, `--pipe` read expressions
interactively or from a pipe. The functions are abs, atan, ceil, cmp, cos, exp,
fact, fib, floor, frac, gcd, int, iseven, isint, isodd, length, lcm, ln, max,
min, scale, sgn, sin, and sqrt. Ctrl-R searches the calc history.

#### `evil` <a id="utility-evil"></a>

**`evil and the evil utilities`**

`evil` prints system identity, uptime, processor, memory, swap, file system,
process, and network information; `-a` adds details, `-s` selects identity
fields, `-u` lists local users.

`evildisk` prints capacity, file system
identity, and disk error and retry totals, and with `-a` labels, UUIDs, error
counters, and SMART data where available.

`evilfiles` lists open files, filtered
by `-p` (process), `-u` (owner), `-c` (command prefix), `-i` (sockets), and `-t`
(terse pids), with `-w`, `--wide` for unshortened columns.

`evilfs` prints
mounted file systems.

`evillogs` prints core dump settings, dump reports, and
log directories, or only one with `--cores` or `--logs`.

`eviliso` prints
namespaces (`-n`), cgroups (`-c`), login sessions (`-s`), remote sockets (`-r`),
container runtime data (`-k`), `--container`, and `--kubernetes` sections, all
of them without a selector, with `-a` for detailed rows. The Kubernetes section
reads KUBERNETES_SERVICE_HOST, the service-account namespace file, and
kubepods cgroups.

`evilss` prints TCP, UDP, and Unix sockets. `-l` (listening),
`-a` (all), `-t` (TCP), `-u` (UDP), `-x` (Unix), `-4`, and `-6` select sockets,
`-p` adds the owning process, `-n` keeps addresses numeric, and `-H` omits the
header.

`evilps` and `evildisk` take optional process ids and files, and
`evilfiles` optional paths, to limit their reports.

`evilio` prints memory, swap, and cumulative disk counters; `-a` adds processor,
paging, scheduler, pressure, disk activity, and swap rates from a one-second
sample, `-h`, `--human-readable` (the default) uses binary units, and `--ps`,
`-n`, `-p`, and `-`*NUMBER* print per-process counters for every process, a
limited table, one process, or NUMBER rows.

`evilnet` prints IPv4 and IPv6
addresses; `-a` adds traffic, errors, drops, and TCP counters, `-f` TCP failure
and loss counters only, and `-t`, `--traffic` the traffic table only.

`evilps`
prints processes as a tree; `-p`, `-n`, `-a`, `-A`, `-U`, `-c`, `-M`, and `-w`,
`--wide` show pids, sort numerically, show every field, command lines, owners,
CPU, memory, and full-length lines, and `-`*NUMBER* limits the rows.

For evilio, evilnet, evilps, and evilss, `-l`, `--live`\[=*N*\] (only `--live`
for evilss) refreshes every N seconds, 0.5 by default, on the alternate screen
until q (status 0) or an interrupt (status 130); a tall frame scrolls with j, k,
the arrow and page keys, g, G, and the mouse wheel. In evilio, evilnet, and
evilps, `--cumulative`\[=*M*\] (`-C` in evilps) reports the change over M
seconds, 1 by default, or a rolling M-second window with `--live`.

`--sort`
orders rows by a metric or its shortest unambiguous prefix: pid, read, write,
read-ops, and write-ops for processes; also busy, read-latency, write-latency,
average-queue, queue, errors, and retries for disks; name, bytes, packets,
errors, or drops for traffic; and name, pid, cpu, or memory in evilps, where s
cycles the key and / searches in live view.

#### `goodcore` <a id="utility-goodcore"></a>

**`goodcore`**

Capture a running process with `-p`, or package an existing core with its
executable with `-b`. The archive holds the core in dump/core, the executable
and its libraries under root/, and debug-gdb.sh and debug-lldb.sh, which open
the core in gdb or lldb with root/ as the system root and pass their arguments
to the debugger.

The archive is goodcore-*core*.tar.zst, or .tar.gz when zstd is
missing, or the path of `-o`, `--output`, and `--no-compress` writes a plain
tar. `-q`, `--quiet` prints only errors. The host tar and zstd create the
archive, and gdb finds the libraries.

#### `goodfsw` <a id="utility-goodfsw"></a>

**`goodfsw`**

Report path changes by scanning metadata, at most once per latency period. On
Linux a scan follows an inotify event and a quiet tree is rescanned every 30
periods; elsewhere, or when a watch fails, a scan runs every period. Each record
holds the scan time, a numeric event mask, and the path.

`-h`,
`--human-readable` uses timestamps and event labels, and `-m`,
`--machine-readable` the numeric form; `--timezone` and `--precision` format
human timestamps. `-r`, `--event-flags`, `-x`, `-1`, `-l`, and `-e` select
recursion, appended labels, one file system, one batch, the latency, and an
excluded substring.

#### `goodnode` <a id="utility-goodnode"></a>

**`goodnode`**

Print inode identity, allocation, file system capabilities, verification state,
and a CRC32C checksum for each path. `-i` locates an inode beneath the `-r`
path, and `--verify` runs the native file system verification where supported.

#### `goodstat` <a id="utility-goodstat"></a>

**`goodstat`**

Print file metadata as a labeled report. `-L` follows links, `-c` adds a CRC32C
checksum, and `-f` a file system capacity report.

#### `meow` <a id="utility-meow"></a>

**`meow`**

Show a screensaver of pixel-art cats. `-f`, `--fps` sets frames per second (1 to
240, default 60), `-s`, `--speed` the speed (0.1 to 10), and `-n`, `--cats` the
number of cats (1 to 8). Space pauses, + and - change speed, and any other key
exits.

#### `retry` <a id="utility-retry"></a>

**`retry`**

Repeat a command until it succeeds or the attempts run out. `-n`, `-d`, `-b`,
`-m`, and `-q` set the attempt count, initial delay, backoff factor, maximum
delay, and quiet mode.

#### `slurp, vi, emacs` <a id="utility-slurp-vi-emacs"></a>

**`slurp, vi, emacs`**

Edit text files as the [`slurp`](#builtin-slurp) builtin does, with the same options and +*LINE*
operand. As `vi` it runs with `-V`, and as `emacs` with `-e`.

### Windows <a id="windows"></a>

On Windows the shell runs the same grammar. A path of /dev/null opens the null
device, and /tmp or a path under it maps into the directory in TEMP. The options
compatibility.dev_null and compatibility.slash_tmp, both on, govern the two
mappings. Variable names are not case-sensitive, PATH uses semicolons, and the
user settings directory falls back to USERPROFILE when HOME is unset.

The Bash
startup files keep their POSIX paths. A subshell runs in a new process. [Process substitution](#process-substitution), [`echo`](#builtin-echo),
[`pwd -W`](#builtin-pwd), and the local form of
[`assimilate`](#builtin-assimilate) behave as their entries describe.

### Runtime files <a id="runtime-files"></a>

**`/etc/kosh.conf, %ProgramData%\kosh\kosh.conf`**

The system settings file on POSIX and on Windows, described in [Configuration files](kosh_5.md#settings-file-locations).

**`$XDG_CONFIG_HOME/kosh/kosh.conf, ~/.config/kosh/kosh.conf`**

The user settings file.

**`$XDG_CONFIG_HOME/kosh/scripts/rc.kosh, ~/.config/kosh/scripts/rc.kosh`**

The rc script of an interactive or login `kosh` mood shell.

**`~/.kosh_history`**

The default history file, as described under [History](#history). The shell writes
temporary files beside it while it rewrites it.

**`~/.kosh_directory_history`**

The default directory history of `z`.

**`~/.kosh_calc_history`**

The default history of `calc`.

**`/etc/profile, ~/.profile, ~/.bash_profile, ~/.bash_login, ~/.bashrc, /etc/bash.bashrc`**

The startup files of the `sh` and Bash moods, as described under
[Bash and POSIX startup files](#bash-and-posix-startup-files).

## Bash and POSIX reference <a id="bash-and-posix-reference"></a>

### Invocation <a id="invocation"></a>

The shell selects the input source in the following order. The `-s` option
selects standard input, one or more `-c` options select their command strings, a
script operand selects that file, and `-i` or a terminal on standard input
selects the interactive editor. Otherwise the shell takes commands from standard
input.

When the options select several sources, the shell prints a warning and
uses the first of them in that order, except that `-s` with `-i` starts the
interactive editor when standard input is a terminal. The interactive editor
needs a terminal, so `-i` without one fails after the startup files run.

The invocation basename selects an initial mood. The basename `sh` or `dash`
selects the `sh` mood, and `bash` or `rbash` selects the `bash` mood. `rbash`
also requests restricted mode. A dash at the start of argument zero, or `-l`,
selects login operation.

`KOSH_FLAGS` supplies options before the command line, and a command-line option
overrides the same option from it. `-c`, `-s`, `--format`, `--apply`,
`--as-language-server`, `--rcfile`, `--init-file`, `--`, and every operand are
ignored there. A secure execution ignores and removes `KOSH_FLAGS`.

When argument zero begins with a dash and the options cannot be parsed, the
shell reports the error and enters rescue mode, which skips the startup and
settings files. When only KOSH_FLAGS fails to parse, the shell drops it and
keeps the command-line options and input source. When the command line fails to
parse, the shell keeps only `-r` and takes commands from standard input,
interactively on a terminal. Any other invalid invocation, including an unknown
value of `--mood`, `--tab-selector`, `--init-moods`, `-o`, or `-O`, exits with
status 2.

### Operands <a id="operands"></a>

**`script`**

When neither `-s` nor `-c` selects another source, the first operand is the
script file. A lone `-` selects standard input. A directory fails with status
126, and a file that cannot be opened fails with status 127.

**`argument`**

For a script, the script path becomes $0 and later arguments become positional
parameters. For `-c`, the first operand after COMMAND becomes $0. For `-s` and
interactive operation, $0 stays the invocation name and every operand becomes a
positional parameter.

### Bash and POSIX startup files <a id="bash-and-posix-startup-files"></a>

A login shell in the `sh` mood sources /etc/profile and ~/.profile. An
interactive `sh` shell sources the file in ENV, without a PATH search, unless
privileged mode is on.

A login shell in the `bash` or `bash-posix` mood sources /etc/profile, then the
first readable file of ~/.bash_profile, ~/.bash_login, and ~/.profile. An
interactive non-login shell in those moods sources the first readable file of
/etc/bash/bashrc and /etc/bash.bashrc, then the custom rc file or ~/.bashrc.

When it has sourced one of them, tab completion is on, no default completion
spec exists, and BASH_COMPLETION_VERSINFO is unset, the shell then sources
/usr/share/bash-completion/bash_completion. A non-interactive shell in the
`bash` mood, including a login shell after its profiles and a `kosh` session
started with `-L bash`, sources BASH_ENV once, without a PATH search.
`bash-posix` does not. A custom rc file applies only to an interactive non-login
shell. On Windows these files keep their POSIX paths.

The rules for skipping startup files are described under [Startup files](#startup-files).

### Terminology <a id="terminology"></a>

**`blank`**

A space or a tab.

**`word`**

A sequence of characters that the shell treats as one unit, also called a token.

**`identifier`**

A word of letters, digits, and underscores that does not begin with a digit.
Variables and, in most contexts, functions are identifiers.

**`metacharacter`**

A character that separates words when it is unquoted. The metacharacters are a
blank, a newline, and | & ; ( ) \< \>.

**`control operator`**

A token that performs a control function. The control operators are a newline
and ||, &&, &, ;, ;;, ;&, ;;&, |, |&, (, and ).

### Reserved words <a id="reserved-words"></a>

A reserved word has a special meaning when it is unquoted and appears as the
first word of a command, as the third word of a `case` or `for` command, or
after another reserved word that starts a command list. The reserved words are

! case coproc do done elif else esac fi for function if in select then time
until while { } \[\[ \]\]

`select` is reserved in the `bash` and `bash-posix` moods only. The `sh` mood
does not reserve `coproc`, `select`, or `time`, and it reports \[\[ as a dialect
error. A reserved word in any other position is an ordinary word, so
`echo if then fi` prints its arguments.

### Shell grammar <a id="shell-grammar"></a>

The shell accepts the POSIX Shell Command Language and the Bash extensions
described in this manual. The `sh` mood follows dash. It reports \[\[ \]\], the
C-style for loop, |&, and \<\<\< as dialect errors, and a script holding one
does not run. It does not recognize $'...', $"...", brace expansion, &\>, &\>\>,
\>&*file*, (( )), $\[ \], ${ } function substitution, the ${*v*@*op*}
transformations, or quotes inside $(( )).

It still accepts process substitution,
the `function` keyword, and subscript assignment, and it assigns an empty string
for a compound assignment such as a=(x y).

#### Simple commands <a id="simple-commands"></a>

A simple command is a sequence of optional variable assignments followed by
words and redirections, ended by a control operator. The first word is the
command, and the remaining words are its arguments. The status of a simple
command is its exit status, or 128 plus the signal number when a signal ends it.

An assignment before the command word applies to the environment of that command
only. The values of the assignments in one prefix are expanded before any of
them is made, so in `a=7 b=$a cmd` the variable b receives the previous value of
a. After a special builtin, the assignments remain in the shell in the `kosh`,
`bash-posix`, and `sh` moods, and they are removed in the `bash` mood. After a
function call they are removed in every mood. A command with assignments and no
command word makes the assignments in the shell.

#### Pipelines <a id="pipelines"></a>

A pipeline is a sequence of one or more commands separated by | or |&, in the
following form.

\[`time` \[`-p`\] \[`-R`\]\] \[`!`\] *command* \[ { | | |& } *command* ... \]

The standard output of each command is connected to the standard input of the
next. The form |& also connects standard error, as 2\>&1 | does. Each command
runs in its own process, with these exceptions. In the `kosh` and `sh` moods, a
builtin in the last stage runs in the shell, so `echo 5 | read x` sets x.

In the
`bash` and `bash-posix` moods, the last stage runs in the shell only while the
`lastpipe` option is on and job control is off. A function or a compound command
in the last stage runs in its own process in the `kosh` and `sh` moods.

The status of a pipeline is the status of its last command. While the `pipefail`
option is on, it is the status of the rightmost command that failed, or 0. A
leading ! negates the status. PIPESTATUS holds the status of each stage of the
last foreground pipeline. A stage that expands to no command, such as
`p=1 | cat`, is an error in the `kosh` mood, and the other moods apply its
redirections and give it status 0.

The reserved word `time` reports the elapsed, user, and system time of the
pipeline after it finishes. In the `kosh` mood the report also has a processor
usage row and, when it is nonzero and the platform reports it, the peak resident
set of the pipeline and its descendants. `-p` selects the POSIX format, which
has no processor row, and `-R` adds the resident set row in every format.

TIMEFORMAT selects the format, as described under [Shell variables](#shell-variables). `time` with no
pipeline prints nothing. In the `sh` mood, `time` is an ordinary command found
through PATH.

#### Lists <a id="lists"></a>

A list is a sequence of pipelines separated by ;, &, &&, or ||, and optionally
ended by ;, &, or a newline. The operators && and || have equal precedence and
group from the left. They bind more tightly than ; and &, which also have equal
precedence.

*command1* `&&` *command2* runs command2 only when command1 returns 0, and
*command1* `||` *command2* runs command2 only when command1 returns nonzero. The
status of an and-or list is the status of the last command that ran.

A list ended by ; or a newline runs in the foreground, and the shell waits for
it. An ampersand runs the whole and-or list before it asynchronously, so
`a && b || c &` is one background job, and its variable and directory changes
stay in the job. The status of an asynchronous list is 0, and $! holds its
process id.

In the Bash and `sh` moods, an asynchronous simple command starts
its process before it expands its words, assignments, and redirections, so an
expansion error fails the job. The status is 127 for an unset parameter or
${name?word} and 1 for a failed redirection in the Bash moods, and 2 in the `sh`
mood. The `kosh` mood expands the words before it starts the job.

#### Compound commands <a id="compound-commands"></a>

A compound command can be followed by redirections, which apply to the whole
command.

**`(list)`**

Run list in a subshell. Changes to variables, the working directory, options,
and traps do not reach the shell. The status is the status of list.

**`{ list; }`**

Run list in the shell. The braces are reserved words, so they must be separate
words, and list must end with a semicolon or a newline.

**`((expression))`**

Evaluate expression as described under [Arithmetic evaluation](#arithmetic-evaluation). The status is
0 when the value is nonzero, and 1 when the value is zero or the expression is
empty. The `sh` mood treats (( as two subshell openings.

**`[[ expression ]]`**

Evaluate a conditional expression, as described under [Conditional expressions](#conditional-expressions). The status is 0 or 1, and 2 when the expression cannot be
evaluated.

**`for name [ [in [word ...]] ; ] do list ; done`**

Expand the words and run list once for each resulting field, with name set to
the field. Without `in`, the loop iterates over the positional parameters. The
status is the status of the last list that ran, or 0 when the loop did not
iterate. The body must be a `do` ... `done` list, and a brace group is rejected.

**`for (( expr1 ; expr2 ; expr3 )) ; do list ; done`**

Evaluate expr1, then run list and evaluate expr3 as long as expr2 is nonzero. An
empty expression counts as 1. The `sh` mood reports this form as a dialect
error.

**`select name [in word ...] ; do list ; done`**

Print the expanded words as a numbered menu on standard error, print PS3, and
read a line. A number of a word sets name to that word, any other line sets name
to empty, and REPLY holds the line. The body runs after each nonempty line until
a `break` or the end of input, and an empty line prints the menu again. Only the
`bash` and `bash-posix` moods accept select.

**`case word in [ [(] pattern [ | pattern ] ... ) list terminator ] ... esac`**

Expand word and run the list of the first pattern that matches it, as described
under [Pattern matching](#pattern-matching). The terminator ;; ends the command, ;& also runs the
next list without testing its pattern, and ;;& tests the next patterns. The
status is the status of the last list that ran, or 0 when no pattern matched.
Every mood accepts the three terminators.

**`if list; then list; [ elif list; then list; ] ... [ else list; ] fi`**

Run the then list of the first condition list that returns 0, or the else list.
The status is the status of the last list that ran, or 0 when no branch ran.

**`while list; do list; done`**

**`until list; do list; done`**

Run the body as long as the condition list returns 0, or, for until, nonzero.
The status is the status of the last body that ran, or 0.

#### Coprocesses <a id="coprocesses"></a>

```text
coproc [NAME] command [redirections]
```

Every mood except `sh` accepts a coprocess. The command runs asynchronously,
connected to the shell by two pipes. The shell receives the output of the
coprocess from NAME\[0\] and writes its input to NAME\[1\], and NAME_PID holds
the process id. NAME defaults to COPROC, and a word before the command is NAME
only when a compound command follows it.

The launch returns status 0, and $!
holds the process id. The descriptors are above 9, close across an exec, and are
not passed to subshells. When the coprocess finishes, the shell closes them and
unsets NAME and NAME_PID.

#### Function definitions <a id="function-definitions"></a>

```text
fname () command [redirections]
 function fname [()] command [redirections]
```

A function definition stores command as the body of fname and returns 0. The
body can be any command, including a simple command, as in `f() echo hi`. The
redirections apply to every call. A later definition replaces the body. A
function made read-only with `readonly -f` cannot be redefined.

A function name
can hold any characters that form one unquoted word, such as f.g or f-g, and a
quoted word also works as a name. A reserved word after `function` is an error,
and a name built by an expansion defines nothing. Calling a function is
described under [Functions](#functions).

### Comments <a id="comments"></a>

An unquoted # at the start of a word begins a comment that lasts to the end of
the line. A # inside a word, as in a#b, is an ordinary character. An interactive
shell recognizes comments while the `interactive_comments` option is on. The
option is on by default.

### Quoting <a id="quoting"></a>

Quoting removes the special meaning of characters and keeps a word from being
split or expanded.

An unquoted backslash preserves the next character, except that a backslash
before a newline joins the two lines. A backslash at the very end of the input
is an error in the `kosh` mood and a literal backslash in the other moods. On
Windows an unquoted backslash is also an escape, so a native path needs a quoted
or doubled backslash or a forward slash.

Single quotes preserve every enclosed character. A single quote cannot appear
between single quotes, even after a backslash.

Double quotes preserve every enclosed character except $, \`, and \\. Parameter
expansion, command substitution, and arithmetic expansion take place inside
them. A backslash inside double quotes keeps its meaning only before $, \`, ",
\\, or a newline. Inside double quotes, "$@" and "${a\[@\]}" still produce one
word per element.

A word of the form $'*string*' expands backslash escapes and is otherwise
single-quoted. The escapes are \\a, \\b, \\e and \\E (escape), \\f, \\n, \\r,
\\t, \\v, \\\\, \\', \\", \\?, \\x*H* and \\x*HH* (hexadecimal), \\*NNN* (one to
three octal digits), \\u*HHHH* and \\U*HHHHHHHH* (a Unicode character in UTF-8),
and \\c*X* (a control character). Any other escape stays as written, with its
backslash. A NUL byte ends the string.

A word of the form $"*string*" marks the string for translation, and it acts as
a double-quoted string. The `sh` mood recognizes neither form and keeps the $ as
an ordinary character.

### Parameters <a id="parameters"></a>

A parameter holds a value. A variable is a parameter denoted by an identifier, a
positional parameter is denoted by a number, and a special parameter by one of
the characters listed below. A variable is set when it has been assigned a
value, which may be empty, and it stays set until it is unset.

An assignment has the form *name*=*value*. The value undergoes tilde expansion,
parameter expansion, command substitution, arithmetic expansion, and quote
removal, but no field splitting or pathname expansion. A tilde expands after the
= and after each colon of the value. The form *name*+=*value* appends to a
string, adds to an integer, and appends elements to an array.

`declare` and
`local` give variables attributes, such as integer, read-only, exported, or a
case conversion, as described under [Builtins](#builtins).

#### Positional parameters <a id="positional-parameters"></a>

The positional parameters are the arguments of the shell, a function, or a
sourced file, numbered from 1. A parameter above 9 needs braces, so $10 is $1
followed by a 0 and ${10} is the tenth parameter. `set --` replaces them, and
`shift` discards the first ones. They cannot be assigned directly, and ${@:=x}
is an error.

An unquoted $@ or $\* is subject to field splitting. "$@" keeps one field per
parameter, and "$\*" joins the parameters with the first byte of IFS. A $@ that
becomes one string, as in an assignment, joins with the first byte of IFS in the
`sh` mood and with a space in the Bash moods. In the `sh` mood, as in dash, $@
and $\* are always set, so "${@-d}" and "${@+p}" never test for missing
parameters. ${@:*offset*:*length*} selects parameters, and an offset of 0
includes $0.

#### Special parameters <a id="special-parameters"></a>

**`*`**

The positional parameters, joined as described under
[Special parameters](#special-parameters).

**`@`**

The positional parameters, one field each inside double quotes.

**`#`**

The number of positional parameters.

**`?`**

The status of the last command, pipeline, or command substitution.

**`-`**

The letters of the options that are on, as described under the [`set`](#builtin-set) builtin. The
letters h and B are on by default, c appears for a command string, and u appears
in the `kosh` mood.

**`$`**

The process id of the shell. A subshell and a command substitution keep the
value of the shell that started them.

**`!`**

The process id of the last asynchronous command, coprocess, or process
substitution. It is empty before the first one.

**`0`**

The name of the shell or the script, as described under [Operands](#operands). Inside a
function it stays the shell or script name.

**`_`**

The last argument of the previous simple command. It is empty at startup. An
assignment with no command and a \[\[ \]\] command do not change it.

#### Shell variables <a id="shell-variables"></a>

The shell sets the following variables. The `sh` mood sets only PIPESTATUS,
LINENO, OPTIND, PPID, SHLVL, PWD, OLDPWD, IFS, PS2, and PS4 from this list,
along with the shell-maintained variables listed under [Koshka variables](#koshka-variables).

An assignment to a variable that the shell maintains, such as BASHPID or
HISTCMD, is discarded unless the entry states
otherwise. The shell ignores a value of such a variable inherited from the
environment, and children receive the inherited value unchanged.

**`BASH, BASH_VERSION, BASH_VERSINFO`**

A Bash identity, set when the mood or an initialized mood is `bash` or
`bash-posix`. BASH holds the executable, BASH_VERSION is 5.3.0(1)-kosh, and
BASH_VERSINFO holds the version fields, with the build host in element 5.

**`BASH_ALIASES`**

The aliases. Assigning an element defines an alias, unsetting an element does
nothing, and unsetting the array disconnects it.

**`BASH_ARGC, BASH_ARGV`**

The argument count of each recorded frame, and the arguments in reverse order,
innermost first. The `extdebug` option records function and source calls in
them. Without it BASH_ARGV is empty and BASH_ARGC holds one 0. Assignments do
nothing, and unset and local fail.

**`BASH_ARGV0`**

Argument zero of the shell. An assignment does not change $0.

**`BASH_COMMAND`**

The command that is running or about to run.

**`BASH_EXECUTION_STRING`**

The command string of `-c`.

**`BASH_LINENO, BASH_SOURCE, FUNCNAME`**

The call stack. FUNCNAME\[0\] is the running function, BASH_SOURCE\[i\] is the
file that defines FUNCNAME\[i\], and BASH_LINENO\[i\] is the line where
FUNCNAME\[i\] was called. They are empty in the `sh` mood.

**`BASH_MONOSECONDS`**

A monotonic clock in milliseconds.

**`BASH_REMATCH`**

The match and the groups of the last successful \[\[ =~ \]\], and empty after a
failed match.

**`BASH_SUBSHELL`**

The subshell depth, 0 in the shell.

**`BASHOPTS`**

The read-only, colon-separated list of the `shopt` options that are on. A value
in the environment is ignored.

**`BASHPID`**

The process id of the current shell or subshell.

**`COMP_CWORD, COMP_LINE, COMP_POINT, COMP_WORDS`**

The cursor word index, the line, the cursor byte offset, and the words, for a
completion function, as described under [`complete`](#builtin-complete). `COMP_KEY` and `COMP_TYPE`
are exported to a `complete -C` command only.

**`COPROC, COPROC_PID`**

The descriptors and process of a coprocess without NAME.

**`DIRSTACK`**

The current directory followed by the saved stack. Assigning an existing index
other than 0 changes that entry, other assignments and element unsets do
nothing, and unsetting the array disconnects it.

**`EPOCHREALTIME, EPOCHSECONDS`**

The wall time in seconds, with microseconds in EPOCHREALTIME.

**`EUID, UID`**

The read-only effective and real user ids.

**`GROUPS`**

The real group id.

**`HISTCMD`**

The history number of the current command, 0 in a non-interactive shell.

**`HOSTNAME, HOSTTYPE, MACHTYPE, OSTYPE`**

The host and the platform.

**`LINENO`**

The line of the running command. Inside a command substitution that spans
several lines, each command reports its own line in the file. Assignments are
discarded, and unset keeps its meaning.

**`MAPFILE`**

The default array of `mapfile`.

**`OLDPWD, PWD`**

The previous and current logical working directories, updated by `cd`. They are
exported only when they were inherited exported or are exported explicitly. A
logical PWD is accepted at startup only when it is absolute and identifies the
current directory.

**`OPTARG, OPTIND`**

The argument of the last option and the index of the next argument for `getopts`. OPTIND starts at 1.

**`PIPESTATUS`**

The status of each stage of the last foreground pipeline.

**`PPID`**

The process id of the parent of the shell. It is read-only except in the `sh`
mood.

**`RANDOM`**

A random integer from 0 to 32767. Assigning it seeds the sequence reproducibly,
and unsetting it makes it an ordinary variable.

**`REPLY`**

The line read by `read` without a variable, and the line read by `select`.

**`SECONDS`**

The seconds since the shell started. Assigning it moves its base, and unsetting
it makes it an ordinary variable.

**`SHELLOPTS`**

The read-only, colon-separated list of the `set -o` options that are on. A value
in the environment is ignored.

**`SHLVL`**

The nesting level of shells, incremented at startup. A negative inherited value
becomes 1, and an inherited value of 1000 or more restarts at 1.

**`SRANDOM`**

A random 32-bit integer from the system generator.

The following variables affect the shell.

**`BASH_COMPLETION_VERSINFO`**

When set, an interactive `bash` mood shell does not source the bash-completion
script.

**`BASH_ENV, ENV`**

Startup files, as described under [Bash and POSIX startup files](#bash-and-posix-startup-files). Their
values undergo parameter expansion and command substitution.

**`BASH_XTRACEFD`**

The descriptor that receives `xtrace` output. Standard error receives it when
the variable is unset or invalid.

**`CDPATH`**

The colon-separated directories that `cd` searches for a relative operand.

**`COLUMNS, LINES`**

The terminal size, set at the start of an interactive shell. They are not
updated when the terminal changes size.

**`FCEDIT, EDITOR, VISUAL`**

FCEDIT, then EDITOR, selects the editor of `fc`. VISUAL, then EDITOR, selects
the editor of Ctrl-X Ctrl-E.

**`GLOBIGNORE`**

Colon-separated patterns of pathname expansion results to remove, as described
under [Pathname expansion](#pathname-expansion).

**`HOME`**

The home directory, used by `cd` and tilde expansion. On Windows the shell uses
USERPROFILE when HOME is unset.

**`IFS`**

The field separators and the joiner of "$\*", by default space, tab, and
newline. A value in the environment is ignored.

**`IGNOREEOF`**

The number of consecutive end-of-file inputs that an interactive shell needs
before it exits while `ignoreeof` is on. Enabling the option sets an unset
IGNOREEOF to 10, assigning IGNOREEOF enables the option, and unsetting it
disables the option.

**`LANG, LC_ALL, LC_COLLATE, LC_CTYPE, LC_NUMERIC`**

The locale, as described under [Locale](#locale).

**`MANPATH`**

The manual page directories that completion, hints, and `man` search.

**`NO_COLOR`**

A nonempty value disables color in the prompt, diagnostics, the editor, and the
bundled utilities.

**`OPTERR`**

A value of 0 silences `getopts` diagnostics.

**`PATH`**

The colon-separated command search path. An empty element, an empty value, and
an unset PATH search the current directory. The shell does not set PATH when the
environment lacks it. On Windows the separator is a semicolon, an empty element
is skipped, and Path counts as PATH.

**`PROMPT_COMMAND, PS0, PS1, PS2, PS3, PS4, RPS1, RPROMPT`**

The prompts and the command run before each prompt, as described under
[Prompting](#prompting). PS3 is the prompt of `select`, "#? " by default.

**`SHELL`**

The path of the user shell. An inherited value is kept, and an unset SHELL is
set to the path of the executable.

**`TERM, COLORTERM, TERM_PROGRAM`**

Terminal capabilities, as described under [Line editing](#line-editing). TERM=dumb disables
color and the terminal integration marks.

**`TIMEFORMAT`**

The format of the `time` report. %R, %U, and %S are the elapsed, user, and
system seconds, %P is the processor percentage, %3R selects three decimals, %lR
selects minutes and seconds, and %% is a percent sign. An empty value prints
nothing, and an unset value selects the default report.

**`TMPDIR`**

The directory for the temporary files of `fc`, Ctrl-X Ctrl-E, and `slurp`, read
from the environment, so an assignment needs `export`. An empty value is
ignored, and /tmp is the default. On Windows the directory is TEMP.

**`TZ`**

The time zone. The shell uses the value it inherited, so an assignment in the
shell changes the zone of later programs only.

**`XDG_CONFIG_HOME`**

The base of the user settings directory, as described under [Startup files](#startup-files). A
relative or empty value is ignored.

These Bash variables have no effect. They are HISTCONTROL, HISTFILE,
HISTFILESIZE, HISTIGNORE, HISTSIZE, HISTTIMEFORMAT, histchars, MAIL, MAILCHECK,
MAILPATH, TMOUT, PROMPT_DIRTRIM, FIGNORE, EXECIGNORE, HOSTFILE, INPUTRC,
CHILD_MAX, BASH_COMPAT, BASH_LOADABLES_PATH, GLOBSORT, FUNCNEST,
auto_resume, and POSIXLY_CORRECT.

#### Arrays <a id="arrays"></a>

An indexed array is a list of values with integer indices from 0, and an
associative array maps string keys to values. In the `sh` mood a subscript
assignment creates an element, a compound assignment assigns an empty string,
and unset rejects a subscript.

a=(x y z) assigns an indexed array, a=(\[2\]=c \[0\]=a) assigns explicit
indices, a\[2\]=z assigns an element, and a+=(w) appends. Elements may be
missing, so a\[5\]=z on a two-element array leaves indices 0, 1, and 5.
${a\[i\]} takes an arithmetic subscript, and a negative index counts from the
end.

A scalar variable acts as element 0, and ${a} is ${a\[0\]}. ${a\[@\]} and
${a\[\*\]} list elements, ${#a\[@\]} counts them, and ${!a\[@\]} lists the
indices that are set. "${a\[@\]}" keeps one field per element, "${a\[@\]@A}"
yields the words of the declaration, "${a\[@\]@K}" yields keys and values, and
"${a\[\*\]}" joins with the first byte of IFS. An unquoted ${a\[@\]} or
${a\[\*\]} joins with the first byte of IFS and splits the result.

Under
`set -u`, ${a\[@\]} and ${a\[\*\]} of an unset array expand to nothing, and $a
and ${a\[0\]} are errors. `unset 'a[i]'` removes one element.

`declare -A m` declares an associative array, m\[key\]=v assigns an element,
${m\[key\]} expands it, ${!m\[@\]} lists keys, and `unset 'm[key]'` removes one.
A key is a string, so m\[1+1\] holds the key 1+1. Keys are listed in the order
in which they were first assigned. Bash can list them in another order, so a
script that depends on the order should sort the keys.

`export` and `declare -x` on an array keep the array and record the export
attribute. `declare -p` prints the attribute as -ax or -Ax, and no child
receives the array.

### Expansions <a id="expansions"></a>

The shell expands each word of a command in a fixed order. Brace expansion comes
first, then tilde expansion, parameter expansion, arithmetic expansion, command
substitution, and process substitution from left to right, and then field
splitting, pathname expansion, and quote removal. Only brace expansion, field
splitting, and pathname expansion can change the number of words.

#### Brace expansion <a id="brace-expansion"></a>

A word holding an unquoted {*a*,*b*,...} produces one word per item, each with
the text before and after the braces, as in a{b,c}d giving abd and acd. Items
may be empty and braces nest. A sequence {*x*..*y*} or {*x*..*y*..*step*}
produces integers or single characters from x to y. A leading zero on either
integer pads every result to the same width, a step of 0 counts as 1, and the
sign of the step is ignored.

A character range includes every character between,
as in {Z..a}. A word that is not a valid list or sequence, such as {a}, {a-z},
or {1..a}, stays literal, and so does a sequence that holds an expansion. Brace
expansion does not apply in an assignment value, a here-string, a case pattern,
or a redirection target. It is off in the `sh` mood and while `braceexpand` is
off.

#### Tilde expansion <a id="tilde-expansion"></a>

An unquoted ~ at the start of a word expands to HOME, ~*user* to the home
directory of user, ~+ to PWD, and ~- to OLDPWD. The prefix ends at the first
slash. An unknown user, ~- with OLDPWD unset, and the directory stack forms ~N,
~+N, and ~-N stay literal. An empty HOME expands to an empty word, and an unset
HOME is an error.

A tilde also expands after = and : in an assignment and in the
words of `export`, `declare`, `local`, and `readonly`, and outside the `sh` mood
in any argument that looks like an assignment, such as a=~.

#### Parameter expansion <a id="parameter-expansion"></a>

$*name* and ${*name*} expand to the value of a parameter. The braces are needed
for a positional parameter above 9, an array element, or a name followed by a
character that could continue it. In the forms below, a colon also tests for an
empty value, and without the colon only an unset parameter counts. The word is
expanded only when it is used.

**`${name:-word}`**

The value, or word when the parameter is unset or empty.

**`${name:=word}`**

The value, after assigning word when the parameter is unset or empty. Positional
and special parameters cannot be assigned this way.

**`${name:?word}`**

The value, or an error message holding word when the parameter is unset or
empty. A non-interactive shell exits.

**`${name:+word}`**

Word when the parameter is set and not empty, and nothing otherwise.

**`${#name}`**

The length of the value in characters. ${#\*} and ${#@} give the number of
positional parameters.

**`${name:offset}, ${name:offset:length}`**

A substring. A negative offset counts from the end and needs a leading space, as
in ${v: -3}, since ${v:-3} is the default-value form. A negative length excludes
that many characters from the end. On an indexed array, ${a\[@\]:offset:length}
starts at the first element whose index is at least the offset.

**`${name#pattern}, ${name##pattern}`**

The value with the shortest or the longest prefix matching pattern removed.

**`${name%pattern}, ${name%%pattern}`**

The value with the shortest or the longest suffix matching pattern removed.

**`${name/pattern/string}`**

The value with the first match of pattern replaced by string. // replaces every
match, and /# and /% anchor the match at the start and the end. The
`patsub_replacement` option is on by default. While it is on, an unquoted
ampersand in the replacement, or one from an unquoted expansion, stands for the
match, and a quoted ampersand or \\& is literal. With the option off, every
ampersand is literal.

**`${name^pattern}, ${name^^pattern}, ${name,pattern}, ${name,,pattern}, ${name~pattern}, ${name~~pattern}`**

Convert the first character, or every character, that matches pattern to upper
case, lower case, or the other case. Without a pattern, every character matches.

**`${name@op}`**

Transform the value. Q quotes it for reuse, E expands backslash escapes as
$'...' does, P expands it as a prompt, A gives an assignment or declaration, a
gives the attribute letters, U, L, and u convert to upper case, lower case, or
an upper-case first character, and K and k give the keys and values of an array.
Any other operator on a set parameter is a bad substitution. The `sh` mood has
no transformations.

**`${!name}`**

The value of the variable whose identifier is the value of name. For a name
reference it gives the identifier of the target. An unset or empty name gives an
empty value.

**`${!prefix*}, ${!prefix@}`**

The identifiers of the variables whose identifiers begin with prefix.

**`${!name[@]}, ${!name[*]}`**

The indices or keys of an array.

A value or key with a byte that does not print in the locale, such as a control
byte or invalid UTF-8, takes the $'...' form in ${v@Q}, ${v@A}, ${a\[@\]@K},
printf %q, declare -p, and export -p.

Inside double quotes or a here-document, the word of ${v:-word}, ${v:+word}, and
${v:=word}, with or without the colon, follows double-quote rules, so
"${v:-'a'}" expands to 'a' with its quotes.

#### Command substitution <a id="command-substitution"></a>

$(*command*) and \`*command*\` expand to the output of the command with trailing
newlines removed. Inside backquotes, a backslash keeps its meaning only before
$, \`, or \\. $(\< *file*) expands to the contents of file without running a
command. The Bash and `sh` moods drop null bytes from the output, and the `kosh`
mood keeps them.

The command runs in a subshell, so the shell state, options,
directory stack, working directory, and file creation mask of the shell are
unchanged after it finishes.

The `kosh`, `bash`, and `bash-posix` moods also accept two forms that run in the
shell. ${ *command*; } expands to the output of the command, and ${| *command*;
} expands to the value of REPLY after the command. Variable changes in both
forms remain.

A syntax error inside a substitution is reported at its place in the outer
source, and the script or command string holding it does not run. Nesting deeper
than 64 levels is a syntax error.

#### Arithmetic expansion <a id="arithmetic-expansion"></a>

$((*expression*)) expands to the value of the expression, evaluated as described
under [Arithmetic evaluation](#arithmetic-evaluation). The expression undergoes parameter expansion,
command substitution, and quote removal first. An empty $(( )) yields 0. Outside
the `sh` mood, $\[*expression*\] is an obsolete spelling of the same expansion.

#### Process substitution <a id="process-substitution"></a>

\<(*command*) and \>(*command*) expand to a path from which the output of the
command can be read, or to which its input can be written, with the command in a
separate process. The form expands anywhere in an unquoted word, so
p=\<(command) assigns the path, and every mood accepts it. The path stays open
until the command that expanded it finishes, or until the loop finishes for a
path in the word list of a for loop.

The shell then closes it and waits for the
process. A process substitution sets $! to its process, and `wait` on that
process returns its status. On POSIX the path is /dev/fd/63 or the next free
descriptor above it. On Windows it is a pipe that acts as a FIFO, so each reader
continues where the previous one stopped, and each writer appends to the same
stream.

#### Field splitting <a id="field-splitting"></a>

The results of unquoted parameter expansion, command substitution, and
arithmetic expansion are split into fields at the bytes of IFS. A run of IFS
whitespace collapses and is ignored at the start and the end, and each other IFS
byte delimits a field, and that field may be empty. An empty IFS disables
splitting. An unquoted expansion that yields nothing produces no field, and ""
produces an empty field.

#### Pathname expansion <a id="pathname-expansion"></a>

After field splitting, each unquoted word holding \*, ?, or \[ is a pattern, and
the shell replaces it with the sorted list of matching pathnames, as described
under [Pattern matching](#pattern-matching). A leading dot and a slash must match explicitly, and
. and .. are never results. The Bash moods sort matches by the collation order
of the locale, and the `kosh` and `sh` moods by byte value. `noglob` disables
pathname expansion.

`dotglob` lets \* and ? match a leading dot, `nocaseglob` ignores case, and
`globstar` makes \*\* match any number of directories, with \*\*/ matching
directories only. A pattern that matches nothing is an error in the `kosh` mood.
In the other moods it stays literal, `nullglob` removes it, and `failglob` makes
it an error.

A nonempty `GLOBIGNORE` holds colon-separated patterns. A result matching one is
removed, and a pattern whose results are all removed follows the rules for a
pattern that matches nothing. Assigning a nonempty GLOBIGNORE enables `dotglob`,
and unsetting it disables `dotglob`.

#### Pattern matching <a id="pattern-matching"></a>

Pathname expansion, case, the == and != operators of \[\[ \]\], and the pattern
forms of parameter expansion use these patterns. \* matches any string, ?
matches one character, and \[...\] matches one character from a set. A set may
hold ranges such as a-z and the classes \[:alnum:\], \[:alpha:\], \[:blank:\],
\[:cntrl:\], \[:digit:\], \[:graph:\], \[:lower:\], \[:print:\], \[:punct:\],
\[:space:\], \[:upper:\], and \[:xdigit:\].

A leading ! or ^ negates the set,
and \] first in the set is literal. Equivalence classes and collating symbols
match nothing, and so do reversed ranges. An unclosed \[ is literal. A backslash
or quotes make a character literal. Outside pathname expansion, \* and ? also
match a slash and a leading dot.

While `extglob` is on, ?(*list*), \*(*list*), +(*list*), @(*list*), and
!(*list*) match zero or one, zero or more, one or more, exactly one, or anything
except one of the |-separated patterns. The pattern of \[\[ \]\] always accepts
them. `nocasematch` makes case, \[\[ \]\], and =~ ignore case.

#### Quote removal <a id="quote-removal"></a>

After the other expansions, unquoted backslashes, single quotes, and double
quotes that did not result from an expansion are removed.

### Redirections <a id="redirections"></a>

A redirection opens, duplicates, or closes a descriptor for a command.
Redirections are processed from left to right. A number before the operator
selects the descriptor, and a {*variable*} before it allocates a descriptor
numbered 10 or above and stores the number in the variable. The variable may be
an array element, and the number stays in the variable after the descriptor
closes.

The target word undergoes the same expansions as an argument except
pathname expansion of more than one result. A target that expands to several
words is an error, and so is an unset variable in the `kosh` mood.

A failed redirection fails the command with status 1 in the Bash moods and 2 in
the `kosh` and `sh` moods. On a special builtin or a compound command it ends
the script in the `kosh` mood with status 1 and in the `sh` mood with status 2,
and the Bash moods continue, including `bash-posix`, where Bash exits.

**`[n]<word`**

Open word for reading on n, by default 0.

**`[n]>word`**

Open word for writing on n, by default 1, creating it or truncating it. While
`noclobber` is on, an existing regular file is refused.

**`[n]>|word`**

Open word for writing, even under `noclobber`.

**`[n]>>word`**

Open word for appending. An appending redirection inside a loop body opens its
file once for the whole loop, so writes continue to the original file even after
it is renamed or removed.

**`[n]<>word`**

Open word for reading and writing on n, by default 0.

**`&>word, >&word, &>>word`**

Redirect standard output and standard error to word, or append both. With \>&, a
word that is a number, or -, duplicates or closes standard output, and &\> and
&\>\> always open a file.

**`[n]<&m, [n]>&m`**

Duplicate descriptor m onto n. A target that is not a single open descriptor is
an error.

**`[n]<&-, [n]>&-`**

Close n.

**`[n]<&m-, [n]>&m-`**

Move m onto n and close m.

**`[n]<<word`**

A here-document. The lines after the command, up to a line that is exactly word,
become the input. When any part of word is quoted, the lines are taken
literally. Otherwise they undergo parameter expansion, command substitution, and
arithmetic expansion, a backslash keeps its meaning only before $, \`, \\, or a
newline, and a backslash before a newline joins lines. A command can have
several here-documents, read in order. An unterminated here-document ends at the
end of the input.

**`[n]<<-word`**

A here-document whose lines and terminator lose their leading tabs.

**`[n]<<<word`**

A here-string. The expanded word, followed by a newline, becomes the input.

The `sh` mood rejects &\>, &\>\>, \>&*word*, and \<\<\<. Device paths such as
/dev/tcp are not emulated, so a redirection to one opens the path in the file
system.

In the Bash moods, the redirections of a program, of a command with no command
word, and of a subshell expand in the child, so a side effect such as ${v:=x}
does not reach the shell. Those of a builtin, a function, a group, and `exec`
expand in the shell, as do those of every command in the `kosh` and `sh` moods.

### Aliases <a id="aliases"></a>

An alias replaces the first word of a simple command with a string. The
replacement is expanded when the command runs, so an alias defined earlier on
the same line or in the same compound command applies at once, and an alias in a
function body takes its value at each call. When the replacement ends with a
blank, the next word is also checked for an alias.

A chain of aliases is
followed, and a word that is already being replaced is not replaced again. A
quoted word, a word with a backslash, and a word produced by an expansion are
never aliases, and a reserved word keeps its meaning.

Aliases expand while `expand_aliases` is on. It is on in the `kosh`,
`bash-posix`, and `sh` moods, and in the `bash` mood it is on in an interactive
shell and off in a script. `alias` and `unalias` manage them, and BASH_ALIASES
holds them.

### Functions <a id="functions"></a>

A function runs in the shell, with its own positional parameters. Its variables
are global unless `local` or `declare` makes them local, and a local variable is
visible to the functions it calls. In the `kosh` and `sh` moods, local without a
value keeps the outer value visible, and in the Bash moods the variable starts
unset.

`return` ends the function with a status, and otherwise the status is
that of the last command. FUNCNAME, BASH_SOURCE, BASH_LINENO, and
BASH_COMMAND expose the call stack, and $0 stays the script name. Function
calls nest at most 900 deep and sourced files 400 deep, or less when the stack
runs short, and a deeper call fails with a diagnostic.

A function called from a guarded and-or operand or a negation keeps `errexit`
suppressed throughout its body. In the `kosh` mood the RETURN trap fires at the
end of every function body. The `bash` and `bash-posix` moods fire it for a body
that `functrace` follows, and the `sh` mood rejects the RETURN condition.

`export -f` exports a function to the environment as BASH_FUNC\_*name*%%, which
Bash children import. Koshka does not import exported functions.

### Arithmetic evaluation <a id="arithmetic-evaluation"></a>

Arithmetic expressions appear in $(( )), (( )), let, the C-style for loop, array
subscripts, substring offsets, and assignments to integer variables. The
operators, from the highest precedence to the lowest, are

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

Parentheses group. A comparison or logical operator yields 1 or 0, and && and ||
evaluate their right side only when needed. \*\*= is a Koshka extension.

A number is decimal, octal with a leading 0, hexadecimal with 0x, binary with
0b, which is a Koshka extension, or in base *b* from 2 to 64 as *b*#*digits*,
with digits beyond 9 taken from letters, @, and \_. A variable is replaced by
its value, and the value is evaluated as an expression in turn, so x=1+2 makes
x\*2 equal 6.

An empty value counts as 0. A $ or a backquote inside a variable
value is an error. A subscript taken from a value is expanded again, so with
q='a\[$i\]', $((q)) is the element at i. Outside the `sh` mood, double quotes
inside $(( )), (( )), a C-style for header, a subscript, or a substring offset
are removed first, so $(( "1 + 2" \* 3 )) is 7. The `sh` mood rejects every
quote there.

In the `bash`, `bash-posix`, and `sh` moods, arithmetic uses signed 64-bit
integers that wrap, and an unset variable counts as 0. The
`interpreter.arithmetic_uses_big_numbers` option, held on in the `kosh` mood,
selects arbitrary-precision integers and finite decimal values, so $((2\*\*100))
is exact and $((7.0/2)) is 3.5.

Division of two integers stays integer division.
Shifts, bitwise operators, and the exponent need integers, and a negative shift
count is an error. In the `kosh` mood an unset variable in an expression is an
error.

Division by zero, an invalid digit, a missing operand, and an unexpected token
are errors, as described under [Shell errors](#shell-errors).

### Conditional expressions <a id="conditional-expressions"></a>

The \[\[ \]\] command and the `test` and \[ builtins evaluate conditional
expressions. Inside \[\[ \]\] the words do not undergo field splitting or
pathname expansion. The primaries are

**`-a file, -e file`**

The file exists. \[\[ \]\] accepts -a as a unary operator, and test does not.

**`-b file, -c file, -d file, -f file, -p file, -S file`**

The file is a block device, a character device, a directory, a regular file, a
FIFO, or a socket.

**`-h file, -L file`**

The file is a symbolic link.

**`-g file, -u file, -k file`**

The file has the set-group-id, set-user-id, or sticky bit.

**`-r file, -w file, -x file`**

The file is readable, writable, or executable by the shell.

**`-O file, -G file`**

The file is owned by the effective user or group.

**`-s file`**

The file is a regular file with a nonzero size. A directory is false.

**`-t fd`**

The descriptor is open on a terminal. \[ -t \] with no operand is true.

**`-z string, -n string`**

The string is empty, or not empty. A lone string is true when it is not empty.

**`-v name`**

The variable, array element, or positional parameter is set. Only \[\[ \]\]
accepts -v.

**`-R name`**

The variable is a name reference with a target. The `sh` mood does not accept
-R.

**`-o option`**

The `set -o` option is on. \[\[ \]\] accepts -o in every mood, and test only in
the Bash moods.

**`file1 -ef file2`**

The two files are the same file.

**`file1 -nt file2, file1 -ot file2`**

file1 is newer or older than file2. Both files must exist.

**`s1 == s2, s1 = s2, s1 != s2`**

The strings are equal or differ. In \[\[ \]\] the right side is a pattern unless
it is quoted. test accepts == only in the Bash moods.

**`s1 < s2, s1 > s2`**

s1 sorts before or after s2.

**`s =~ regex`**

In \[\[ \]\] only, s matches the extended regular expression. A quoted part of
the regex is literal. BASH_REMATCH receives the match and its groups, an
unmatched group is empty, and a dot matches a newline. An invalid regex is an
error with status 1, where Bash returns 2.

**`n1 -eq n2, -ne, -lt, -le, -gt, -ge`**

Integer comparisons. In \[\[ \]\] each operand is an arithmetic expression. In
test each operand must be a decimal integer, which may have blanks around it.

`!` *expr* negates, ( ) groups, and && and || combine inside \[\[ \]\]. test
uses -a and -o for and and or, and its outcome depends on the number of
arguments, as POSIX specifies. A malformed \[\[ \]\] fails with status 2.

### Command execution <a id="command-execution"></a>

After a simple command is expanded, the shell looks for the command word in this
order: functions, enabled builtins, cached program paths, PATH, and, when the
mood, the options, or `enable` allow them, bundled utilities. A word holding a
slash runs that path directly. Running a command or `hash` caches its path. A
cached path stays until PATH is assigned, `hash -r` runs, or a directory change
affects a relative PATH entry.

A program runs in a separate process with the exported variables as its
environment. A command that is not found returns 127, and a file that cannot be
run returns 126. A command path ending in a directory separator fails with
status 126 when the target is not a directory. A file without a recognized
executable format runs as a shell script in a new shell, without the variables,
functions, and traps of the caller.

In an interactive shell, a single directory path in command position changes to
that directory as `cd` would, unless a function, builtin, program, or bundled
utility of that name exists. A word of only dots moves up one directory per dot
after the first. The `autocd` option controls this and is on in every mood, and
scripts are never affected.

### Command execution environment <a id="command-execution-environment"></a>

The shell has an execution environment that consists of the open files, the
working directory, the file creation mask, the resource limits, the traps, the
variables and their attributes, the functions, the options, the aliases, the
directory stack, and the job table.

A subshell, a command substitution, a pipeline stage that runs in its own
process, and an asynchronous list receive a copy of this environment, and their
changes do not reach the shell. Traps that the shell catches are reset to their
defaults in them, and traps that the shell ignores stay ignored. A subshell
cannot wait for a job of its parent.

A program receives the open descriptors, the working directory, the creation
mask, the resource limits, the ignored signals, and the exported variables.
Every descriptor the shell opens for itself is closed in the program.

### Environment <a id="environment"></a>

At startup the shell imports the variables of its environment and marks them for
export. A variable inherited this way, and one marked with `export` or
`declare -x`, is passed to every program the shell starts. A prefix assignment
adds a variable for one command. The values that the shell maintains, such as
SECONDS, IFS, SHELLOPTS, and the KOSH variables, are not taken from the
environment.

On Windows variable names are not case-sensitive, and a value holds at most
32,766 UTF-16 units, so a child receives a longer value truncated. Variables
whose names begin with KOSH_INTERNAL\_ are reserved.

### Shell errors <a id="shell-errors"></a>

A syntax error in a script, a `-c` string, an eval, or a sourced file keeps all
of it from running, and the status is 2. The parser reports every syntax error
in one run. In the `kosh` mood, analysis can also reject a script before it
runs, with status 1, as described under [Diagnostics](#diagnostics). An interactive shell
rejects the line and reads the next one.

In the `kosh` mood every arithmetic and expansion error ends the script with
status 1. A usage error of a builtin, such as an unknown option, ends the script
with the status of the error, except for declare, trap, getopts, compopt, and
printf. With valid usage, a failure of bg, cd, dirs, fg, kill, let, local,
mapfile, popd, pushd, read, set, shift, test, ulimit, umask, or z also ends the
script, and a failure of another builtin, such as a missing alias, only sets its
status.

A failed redirection of a special builtin or a compound command ends the
script with status 1, and a write error on standard output, an invalid regular
expression, and a malformed \[\[ \]\] also end it.

In the `bash` and `bash-posix` moods an arithmetic error in $(( )), a subscript,
a substring offset, or an assignment to an integer variable abandons the rest of
the input line, as in Bash. The remaining commands on the line do not run, $? is
1 on the next line, and neither `set -e` nor an ERR trap acts on it.

An eval, a
sourced file, a subshell, and a command substitution count as separate lines. An
error in (( )), `let`, a \[\[ \]\] operand, or a C-style for header fails only
that command with status 1. Outside a here-document body, a $(( )) error ends a
non-interactive shell in the `bash-posix` mood, with status 127 for a `-c`
string and 1 for a script, and in the `sh` mood with status 2. An interactive
shell abandons the line.

Other expansion errors follow Bash 5.3. A glob matching nothing under failglob
abandons the line in the `bash` and `bash-posix` moods. A bad substitution, such
as ${x!y} or ${x:}, and an assignment to a read-only variable abandon the line
in the `bash` mood and end a non-interactive shell in the `bash-posix` and `sh`
moods. An unknown transformation such as ${v@Z}, an unset parameter under
`set -u`, and ${name?word} end the script in every mood.

In the `sh` and `bash-posix` moods, a syntax error in an eval string or a
sourced file ends a non-interactive shell with status 2, and the other moods
fail only that command. In the `sh` mood, an error of a regular builtin, such as
a missing cd directory or an invalid test operand, fails that command with
status 2 and the script goes on.

An error of a special builtin or of local, such
as shift past the last parameter or export of an invalid identifier, ends a
non-interactive shell with status 2 unless `command` runs it. A failed exec ends
a non-interactive shell in every mood.

An error that ends a non-interactive shell passes through an eval and a sourced
file, as in Bash and dash. A subshell, a command substitution, a trap action,
and a startup file contain it, and the command that started them receives the
status.

### Exit status <a id="exit-status"></a>

The status of a command is a number from 0 to 255. Zero means success and any
other value means failure.

**`0`**

Success, and the status of an empty command, of a compound command that ran no
list, and of an asynchronous command.

**`1`**

A general failure, such as a false condition, a failed builtin, or analysis that
rejected a script.

**`2`**

A usage error of a builtin or the shell, or a syntax error.

**`126`**

A command that was found but cannot be run, such as a directory, a file without
execute permission, or a path ending in a separator that is not a directory.

**`127`**

A command that was not found, or a script operand that cannot be opened.

**`128+n`**

A command ended by signal n, such as 130 for SIGINT, 141 for SIGPIPE, and 143
for SIGTERM.

The shell exits with the status of the last command it ran, or with the operand
of `exit`, taken modulo 256. When an expansion error ends a non-interactive
shell, the status is 1 in the `kosh` mood and 2 in the `sh` mood. In the Bash
moods an unset parameter under nounset, ${name:?}, and a bad substitution that
ends the shell return 127 from a `-c` string, as in Bash, and 1 from a script.

`--lint`, `--format`, and `--apply` return 0 when every input succeeds, 1 when
an input cannot be read, parsed, formatted, or replaced or analysis reports an
error, and 2 for an invalid option combination. Warning-only findings return 0.

### Signals <a id="signals"></a>

The shell ignores SIGPIPE, and a program it starts receives the default, so a
producer killed by SIGPIPE returns status 141. An interactive shell also ignores
SIGTERM, SIGQUIT, SIGTSTP, and SIGTTIN. SIGINT interrupts the running command or
the line being edited and reports `Interrupted`, with status 130.

A shell that
does not trap SIGHUP, and a non-interactive shell that does not trap SIGTERM or
SIGQUIT, is terminated by the signal, so its parent sees status 128 plus the
signal number.

When a signal ends a command, the shell prints a line such as
`[Process 2585: Killed, signal 9]` on standard error, once for each killed
pipeline stage, and the status is 128 plus the signal number. SIGPIPE and SIGINT
print no such line.

`trap` sets the action of a signal or of the conditions EXIT, ERR, DEBUG, and
RETURN. A trapped signal runs its action at the next command boundary. The CHLD
trap runs once for every child the shell reaps, and under `set -m` a job that
stops counts too. When a trapped signal arrives while `wait` blocks, `wait`
returns 128 plus the signal number and the job keeps running. On Windows a
background process runs in its own console process group.

As process 1, the shell reaps orphaned children it did not start, before each
command and while it waits. On SIGTERM it sends SIGTERM and then SIGCONT to
every other process, interrupts the running command, waits for every remaining
child, and exits with status 143.

### Job control <a id="job-control"></a>

A job is a pipeline or list that the shell runs in the background, or a
foreground pipeline that was stopped. Every stage of an asynchronous pipeline
belongs to one job, and POSIX stages share a process group. The final stage
determines $!, the status, and the job identity, except that under pipefail the
rightmost failing stage may determine the status.

A stopped stage is recorded
even when it is not the last, and `fg` and `bg` resume every stage. Job control
is on in an interactive shell, where Ctrl-Z stops the foreground job.

A job is referred to by a job spec. %*n* is job n, %+, %%, and % are the current
job, and %- is the previous one. %*string* is the job whose command begins with
string, and %?*string* the job whose command contains it. When several jobs
match, the lowest-numbered one is used. A bare number is also accepted as a job
number. In the `kosh` mood an unquoted %?string is a glob, so it needs quotes.

`jobs` and job notifications print one line per job, as in Bash. The line holds
the job number with a + for the current job or a - for the previous one, the
process with `jobs -l`, the state, and the command. A job ended by a signal
shows its description, such as Terminated, and an exited job shows Done, or Exit
and its status. An interactive shell reports a job that finished or changed
state before the next prompt.

A job reported by `wait`, `jobs`, or a notification leaves the job table, and a
later `wait` on its process returns the remembered status. The shell remembers
the 1024 most recent statuses, and a `wait` without operands discards them. A
subshell, a pipeline stage, an asynchronous command, and a coprocess cannot wait
for a job of their parent.

The shell never sends SIGHUP to its jobs, so running background jobs continue
after it exits, and it exits even while jobs are stopped. `disown` removes a job
from the table.

### History expansion <a id="history-expansion"></a>

In an interactive shell, while the `histexpand` option is on, which is the
default, an unquoted ! in a line starts a history reference. The shell replaces
the reference before it parses the line. A non-interactive shell does not expand
history. A reference has an event, an optional word designator, and optional
modifiers.

The line with its references replaced is printed before it runs.
History expansion takes place inside double quotes, and a single-quoted or
backslash-escaped ! stays literal.

#### Event designators <a id="event-designators"></a>

**`!!`**

The previous event.

**`!n`**

Event number n.

**`!-n`**

The event n before the current one.

**`!string`**

The newest event that begins with string.

**`!?string?`**

The newest event that contains string. The closing ? may be omitted at the end
of the line.

**`!#`**

The current line up to this reference. It takes no word designators or
modifiers.

**`^old^new^`**

At the start of a line, the previous event with the first old replaced by new,
as !!:s/old/new/ does.

#### Word designators <a id="word-designators"></a>

A colon separates a word designator from the event, and it may be omitted before
^, $, and \*. Words are counted from 0, the command.

**`:^, :$, :*`**

The first argument, the last argument, and every argument.

**`:n`**

Word n.

**`:n-m, :-m, :n*, :n-$`**

Words n through m, words 0 through m, words n through the last, and words n
through the last.

**`:n-`**

Words n through the next to last.

!$, !^, and !\* are the last argument, the first argument, and every argument of
the previous event.

#### Modifiers <a id="modifiers"></a>

Each modifier follows a colon.

**`:h, :t, :r, :e`**

Remove the last path component, keep only the last component, remove a trailing
suffix, or keep only the suffix.

**`:s/old/new/`**

Replace the first occurrence of old, as literal text. Another delimiter may
replace the slash, and the last delimiter may be omitted at the end of the line.
An & in new stands for old.

**`:gs, :as`**

Replace every occurrence.

**`:Gs`**

Replace the first occurrence in each word.

**`:&`**

Repeat the last substitution.

**`:q, :x`**

Quote the text as one argument, or each word separately.

**`:p`**

Print the result and record it in the history without running it.

A reference that matches no event, a substitution that finds no match, a missing
substitution text, and an unknown designator are errors, and the command neither
runs nor enters the history.

### Builtins <a id="builtins"></a>

A builtin runs inside the shell. The special builtins are :, ., break, continue,
eval, exec, exit, export, readonly, return, set, shift, times, trap, and unset,
and in the Bash moods source is also special. Assignments before a special
builtin remain in the shell, as described under [Simple commands](#simple-commands), and their
errors follow the rules under [Shell errors](#shell-errors). The `sh` mood hides
let and time.

A builtin given `--help` before its other arguments prints its description,
synopsis, and options. In the compatibility moods echo prints --help and test
ignores it, and \[ --help reports a missing \] in every mood. Unless an entry
states otherwise, a builtin returns 0 on success, 1 on a failure, and 2 for an
unknown option or a wrong number of operands.

#### `:` <a id="builtin-noop"></a>

**`: [argument ...]`**

**`true [argument ...]`**

Do nothing and return 0. The arguments are expanded and ignored, except that
--help prints the help.

#### `.` <a id="builtin-source"></a>

**`. [--mood[=]MOOD] file [argument ...]`**

**`source [--mood[=]MOOD] file [argument ...]`**

Read and run the commands of file in the shell, and return the status of the
last one, or the operand of a `return` in the file. The arguments become the
positional parameters while the file runs, and the `sh` mood ignores them. The
`sourcepath` option is on by default. While it is on, a file without a slash is
searched in PATH and then in the current directory. A missing or unreadable file
returns 1, and in the `sh` mood it ends a non-interactive shell with status 2.

In the `kosh` mood, a Bash startup file (.bashrc, .bash_profile, .bash_login,
.bash_logout, .profile, or /etc/profile) runs in the `bash` mood with its
options, and a file whose shebang selects another shell runs in that mood. The
`kosh` mood returns when the file ends, and the variables, functions, and
aliases it defined stay.

Analysis does not follow such a file, a syntax error in
it fails the command without ending the shell, and the files it sources keep its
mood. A file without a shebang runs in the current mood. `--mood` runs file in
MOOD in place of the mood its shebang or name selects, and restores the previous
mood afterwards.

#### `[` <a id="builtin-test"></a>

**`[ expression ]`**

**`test expression`**

Evaluate a conditional expression, as described under [Conditional expressions](#conditional-expressions), and return 0 when it is true and 1 when it is false. No
expression is false. A malformed expression, a missing \], and an operand of an
integer comparison that is not a decimal integer return 2. In the `kosh` and
`sh` moods == is rejected with a hint to use =.

#### `alias` <a id="builtin-alias"></a>

**`alias [name[=value] ...]`**

Define an alias for each name with a value, and print the definition of each
name without one. Without operands, print every alias as an alias command. An
unknown alias or an empty name returns 1. A name may hold any character except
=.

#### `bg` <a id="builtin-bg"></a>

**`bg [job]`**

Resume a stopped job in the background, by default the current job. A missing
job returns 1, or 2 in the `sh` mood.

#### `bind` <a id="builtin-bind"></a>

**`bind [options] [keyseq:function ...]`**

Accept the readline options -lmrvpPsq, -f, and -u and their operands without
changing key bindings, and return 0. The key bindings are described under [Line editing](#line-editing).

#### `break` <a id="builtin-break"></a>

**`break [n]`**

**`continue [n]`**

Leave the innermost n enclosing loops, by default 1, or start their next
iteration. A count above the nesting depth applies to the outermost loop. A
count of 0 returns 1 and leaves every enclosing loop in the Bash moods, as Bash
does, and the innermost loop in the `kosh` mood. In the `sh` mood it returns 2
and ends a non-interactive shell.

A count that is not a number ends a
non-interactive shell with status 2 in every mood. Outside a loop the commands
do nothing. A loop in a calling function is not affected.

#### `builtin` <a id="builtin-builtin"></a>

**`builtin [name [argument ...]]`**

**`builtin --list`**

Run the builtin name even when a function of that name exists. A name that is
not a builtin returns 1. `--list` prints every builtin, one per line. In the
`kosh` mood, builtin with no operand lists the builtins under POSIX, BASH, and
Koshka headings.

#### `caller` <a id="builtin-caller"></a>

**`caller [n]`**

Print the line, function, and file of call frame n, counting from 0 for the
caller of the current function. When n is past the last frame, caller returns 1.
Without an operand, caller prints the line and file of the caller inside a
function, a sourced file, or a script file, where the top level prints 0 NULL,
and returns 1 in a command string or standard input. An operand that is not a
whole number returns 2, and a second operand is ignored.

#### `cd` <a id="builtin-cd"></a>

**`cd [-L|-P] [dir]`**

Change the working directory to dir, by default HOME, and set PWD and OLDPWD. An
operand of - changes to OLDPWD and prints it. A relative dir that does not begin
with . or .. is searched in each CDPATH directory, and a directory found there
is printed. A relative directory resolves against the logical PWD, and ..
removes its last component, unless `-P` or the `physical` option resolves
symbolic links first.

`-L` keeps the logical path. A missing directory and an
unset HOME or OLDPWD return 1, and 2 in the `sh` mood, and more than one operand
returns 2. In the Bash moods an empty operand returns 1, and the other moods
accept it. A restricted shell refuses cd.

#### `command` <a id="builtin-command"></a>

**`command [-pVv] name [argument ...]`**

Run name as a builtin or program, skipping functions and aliases. A special
builtin run this way loses its special properties. `-p` searches the default
PATH. `-v` prints the path, builtin, keyword, function, or alias definition that
name resolves to, and `-V` describes it in words. With -v or -V the status is 0
when any operand resolves, and 1 when none does, or 127 in the `sh` mood.
Running a command that is not found returns 127, and one that cannot be run 126.

#### `compgen` <a id="builtin-compgen"></a>

**`compgen [options] [word]`**

Print the candidates that the options generate and that begin with word, one per
line. The actions -a (aliases), -b (builtins), -c (commands), -d (directories),
-e (exported variables), -f (files), -g (groups), -j (jobs), -k (reserved
words), -s (services), -u (users), and -v (variables), and `-A `*action* select
sources.

The actions of -A are alias, arrayvar, binding, builtin, command,
directory, disabled, enabled, export, file, function, group, helptopic,
hostname, job, keyword, running, service, setopt, shopt, signal, stopped, user,
and variable. `-W `*wordlist* splits the expanded wordlist, `-G `*glob* adds
pathname matches, and `-X `*pattern* removes the candidates that match pattern,
or keeps only those after a leading !, and an unescaped & in the pattern stands
for the word.

`-P `*prefix* and `-S `*suffix* add text to each candidate after
matching and filtering. `-V `*array* stores the candidates in an indexed array
and prints nothing. -o, -F, and -C are accepted without effect. No candidate
returns 1, and an unknown action returns 2.

#### `complete` <a id="builtin-complete"></a>

**`complete [options] [name ...]`**

Register how the arguments of each command complete. A spec offers the
candidates of its actions, its `-G` glob, its `-W` words, and, on an explicit
Tab, the COMPREPLY of its `-F` function and the output lines of its `-C`
command, in that order. The `-C` command runs as a command substitution with the
command, the word, and the previous word as arguments and COMP_LINE,
COMP_POINT, COMP_KEY, and COMP_TYPE exported.

A completion function receives
the same arguments and the COMP\_ variables described under [Shell variables](#shell-variables).
Trailing spaces are removed from each COMPREPLY entry, the spec's -X, -P, and -S
apply, and with the filenames option a slash is appended to each directory. The
ghost suggestion never runs a function or command. The actions are those of
`compgen`.

`-o default` and `-o bashdefault` fall back to filenames and to the shell
completion when the spec finds nothing, `-o filenames` treats candidates as
filenames, `-o dirnames` adds matching directories when nothing else matched,
and `-o plusdirs` adds them always. A spec quotes inserted candidates only when
it generates filenames, as in Bash.

`-o fullquote` quotes every candidate,
`-o noquote` none, `-o nosort` keeps the generated order, and `-o nospace` adds
no space. An empty result falls through to the next completion source. An
unknown `-o` option or `-A` action returns 2 and registers nothing.

With `progcomp_alias` on, a command without a spec uses the spec of the first
word of its alias, and an explicit Tab follows a PATH command to its symlink
target. A command still without a spec uses the `-D` spec, and when its function
returns 124 completion restarts with the spec it registered.

`-E` serves an
explicit Tab on an empty line and `-I` the command word. Their functions receive
`_EmptycmD_` and `_InitialWorD_` as the command. Of several of `-D`, `-E`, and
`-I`, the first in that order applies. `-p` prints the specs in Bash order, and
`-r` removes the given specs, or all of them. A missing spec returns 1.

#### `compopt` <a id="builtin-compopt"></a>

**`compopt [-o|+o option] [-DEI] [name ...]`**

`-o `*option* enables a `complete -o` option, and `+o `*option* disables it. If
both forms name the same option, it is disabled. It applies to each given
command's spec, or to the first of the `-D`, `-E`, and `-I` specs, or, inside a
completion function with neither, to the current completion.

Without `-o` or
`+o`, it prints one `compopt` line per spec. An unknown option returns 2, and a
missing spec, or a missing target outside a completion function, returns 1.

#### `declare` <a id="builtin-declare"></a>

**`declare [-aAfFgilnprtux] [name[=value] ...]`**

**`typeset [-aAfFgilnprtux] [name[=value] ...]`**

Set variables and their attributes. Inside a function the variables are local
unless `-g` is given. `-a` declares an indexed array, `-A` an associative array,
`-i` an integer, whose assignments are evaluated as arithmetic, `-l` and `-u`
lowercase and uppercase conversion, `-r` a read-only variable, and `-x` an
exported one.

A plus removes an attribute, except that a read-only attribute
stays. `-t` is accepted without effect. `-f` and `-F` print functions with or
without their definitions. `-p` prints declarations, and without operands `-p`
or an attribute option lists every variable with those attributes. Without
options or operands, declare prints nothing. -a with -A returns 2, and a missing
variable or function, or an invalid identifier, returns 1.

`-n` makes a reference to the variable in its value, which may carry a
subscript. Expansion, assignment, append, export, and unset act on the target,
following chains, with the target's read-only, restricted, integer, and case
rules. ${!name} gives the target, `unset -n` and `+n` remove the reference, and
\[\[ -R name \]\] is true for a reference with a value.

A reference without a
value binds at its first assignment. A self-reference is an error at top level.
A circular chain expands as unset without a message, and an assignment through
it is an error. A read-only variable, PATH, IFS, and a dynamic variable other
than RANDOM and SECONDS cannot become a reference.

#### `dirs` <a id="builtin-dirs"></a>

**`dirs [-clpv] [+N|-N]`**

Print the current directory and the saved directory stack on one line, with HOME
shown as ~. `-c` clears the stack, `-l` prints full paths, `-p` prints one entry
per line, and `-v` numbers them from 0. `+`*N* prints the Nth entry from the top
and `-`*N* the Nth from the bottom. An entry past the end returns 1.

#### `disown` <a id="builtin-disown"></a>

**`disown [-ahr] [job ...]`**

Remove each job, by default the current job, from the job table. `-a`, `--all`
removes every job and `-r`, `--running` every running job. `-h`, `--nohup` keeps
the job in the table. A missing job returns 1.

#### `echo` <a id="builtin-echo"></a>

**`echo [-neE] [argument ...]`**

Print the arguments separated by spaces and followed by a newline. `-n` omits
the newline, `-e` interprets backslash escapes, and `-E` does not. The `kosh`
mood interprets escapes by default. The escapes are those of $'...', except that
an octal escape is \\0*nnn* in the Bash moods, and \\c ends the output.

In the
`kosh` and `bash` moods the options may be combined, as in -ne, and any other
argument that begins with a dash is printed. In the `sh` mood, as in dash, only
-n is an option, escapes are interpreted by default, \\e and \\x are not
escapes, and \\u is. `xpg_echo` makes the Bash moods interpret escapes by
default. On Windows, echo in the `kosh` mood leaves backslashes literal unless
`-e` is given. A write error returns 1, or 2 in the `sh` mood.

#### `enable` <a id="builtin-enable"></a>

**`enable [-adnps] [-f file] [name ...]`**

Enable each builtin or bundled utility, or disable it with `-n`. A utility alias
such as `egrep` is rejected and follows its utility. Each builtin word is
separate, so disabling `test` leaves `[` enabled. Lookup skips a disabled
builtin and searches PATH and the bundled utilities, and a disabled bundled
utility never resolves as a command, though the `koshkit` builtin still runs it.
A disabled special builtin loses its special properties. A subshell inherits the
state, and its changes do not reach the parent.

Without an operand, and with `-p`, enable lists the enabled builtins and
reachable utilities as `enable` commands, `-n` the disabled ones, and `-a` both.
`-s` limits a listing to special builtins. A builtin that the mood hides, such
as `let` in the `sh` mood, is neither listed nor accepted. Koshka has no
loadable builtins, so `-f` with an operand and `-d` return 1, and so does a name
that is not a builtin or utility.

#### `eval` <a id="builtin-eval"></a>

**`eval [argument ...]`**

Join the arguments with spaces and run the result as shell input in the current
shell. The status is that of the last command, or 0 for empty input. A syntax
error in the input is reported with an eval: location and returns 2.

#### `exec` <a id="builtin-exec"></a>

**`exec [-cl] [-a name] [command [argument ...]]`**

Replace the shell with command. Without a command, apply the redirections to the
shell itself. `-a` passes name as argument zero, `-l` puts a dash before
argument zero, and `-c` runs the command with an empty environment. A command
that is not found or cannot be run ends a non-interactive shell with status 127
or 126, in every mood, and returns that status in an interactive shell. A
restricted shell refuses exec.

#### `exit` <a id="builtin-exit"></a>

**`exit [n]`**

Run the EXIT trap and exit with status n modulo 256, or with the status of the
last command. A non-numeric operand prints an error and returns 2 without
exiting, except in the `sh` mood, where the shell exits with status 2. More than
one operand exits with status 1.

#### `export` <a id="builtin-export"></a>

**`export [-fnp] [name[=value] ...]`**

Mark each variable for the environment of later commands, after assigning the
value when one is given. `-n` removes the mark, `-f` marks functions, and `-p`
or no operand prints the exported variables. The `kosh` and `sh` moods print
them as export commands and the Bash moods as declare -x commands. An invalid
identifier and a read-only variable return 1.

#### `false` <a id="builtin-false"></a>

**`false [argument ...]`**

Return 1.

#### `fc` <a id="builtin-fc"></a>

**`fc [-e editor] [-lnr] [first [last]]`**

**`fc -s [old=new ...] [command]`**

Edit and run history events. A number selects an event, a negative number is
relative to the current position, and text selects the newest event with that
prefix. The default is the previous event. `-l` lists events, the previous
sixteen by default, `-n` omits numbers, and `-r` reverses the order.

The editor
comes from `-e`, FCEDIT, or EDITOR, and defaults to vi, or ed in the POSIX
moods. An editor of `-` runs the event unedited. `fc -s` applies each literal
replacement to one event, prints it to standard error, runs it, and returns its
status. A text that matches no event returns 1.

#### `fg` <a id="builtin-fg"></a>

**`fg [job]`**

Resume a job in the foreground, by default the current job, and return its
status. A missing job returns 1, or 2 in the `sh` mood.

#### `getopts` <a id="builtin-getopts"></a>

**`getopts optstring name [argument ...]`**

Parse the options in the arguments, or in the positional parameters. Each call
stores the next option letter in name and its argument in OPTARG, and OPTIND
holds the index of the next argument. A letter followed by a colon in optstring
takes an argument. An unknown option sets name to ? and prints a diagnostic.

OPTERR=0 silences the diagnostics. A leading colon in optstring also silences
them, and then an unknown option stores the letter in OPTARG and a missing
argument sets name to :. The status is 0 while options remain and 1 at their
end.

#### `hash` <a id="builtin-hash"></a>

**`hash [-rRt] [-p path] [name ...]`**

Find each name in PATH and remember its location. `-r` forgets every location,
and `-R` also searches PATH again. `-p `*path* records path for each name. `-t`
prints the remembered location of each name. A name that cannot be found returns
1.

#### `help` <a id="builtin-help"></a>

**`help [-dms] [name ...]`**

Print the help of each builtin, as its `--help` flag does. `-d` prints the short
description, `-s` the synopsis, and `-m` a manual page. Without a name, list the
builtins. A name that is not a builtin returns 1.

#### `history` <a id="builtin-history"></a>

**`history [-cnrwaps] [-d selector] [operand]`**

**`history -S|--sync`**

Without a maintenance option, list the events, limited to the newest N with a
numeric operand. `-r` loads the history file, or the file operand, into the
list, and `-n` loads only events not yet loaded. `-w` writes the list to the
history file or replaces the file operand. `-a` appends the events of the
private list that the file lacks.

`-S` replaces the private list with a snapshot
of the shared file. `-c` clears the private list and leaves the file intact.
`-p` prints its operands without storing them, and `-s` stores them as one
event. `-d` deletes an event number, a negative offset from the newest event, or
an inclusive range, and an interactive shell also removes it from the file. An
event out of range returns 1. A restricted shell refuses file operands.

#### `jobs` <a id="builtin-jobs"></a>

**`jobs [-lnprs] [job ...]`**

List the jobs, as described under [Job control](#job-control). `-l` adds the process id,
`-p` prints only process ids, `-r` and `-s` limit the list to running or stopped
jobs, and `-n` to jobs that changed since the last report. A missing job returns
1, or 2 in the `sh` mood.

#### `kill` <a id="builtin-kill"></a>

**`kill [-s signal | -n num | -signal] pid|job ...`**

**`kill -l [signal|status ...]`**

Send a signal, by default SIGTERM, to each process or job. A signal is a number
or a name with or without SIG. `-l` lists the signals, or translates each
number, status above 128, or name. No operand returns 2, and an invalid signal
or a process that cannot be signaled returns 1. In the `sh` mood an invalid
signal returns 2.

#### `let` <a id="builtin-let"></a>

**`let expression ...`**

Evaluate each expression, as described under [Arithmetic evaluation](#arithmetic-evaluation). The
status is 0 when the last value is nonzero and 1 when it is zero. No expression
returns 1. The `sh` mood has no let.

#### `local` <a id="builtin-local"></a>

**`local [-aAilnprux] [name[=value] ...]`**

Make variables local to the running function, with the attributes of the same
options of `declare`. `local -n` passes a variable of the caller by reference,
and a `-` operand saves the `set` options until the function returns. `-p` lists
the local variables. Outside a function local returns 1, and in the `sh` mood it
ends a non-interactive shell with status 2.

#### `logout` <a id="builtin-logout"></a>

**`logout [n]`**

In a login shell, run the EXIT trap and exit with the optional status. In
another shell, report an error and return 1.

#### `mapfile` <a id="builtin-mapfile"></a>

**`mapfile [options] [array]`**

**`readarray [options] [array]`**

Store the lines of standard input in an indexed array, MAPFILE by default. `-t`
strips delimiters, `-n` limits the count, with 0 for every line, `-s` skips
leading lines, `-O` sets the first index, `-d` uses the first byte of its value
as delimiter, NUL when empty, `-u` reads a descriptor, and `-C` runs a callback
with the index and the line every `-c` lines, 5000 by default.

#### `newgrp` <a id="builtin-newgrp"></a>

**`newgrp [group]`**

Replace the shell with the newgrp program found in PATH, which starts a new
shell with the group. A missing program returns 127, and one that cannot run
returns 126. A restricted shell refuses newgrp.

#### `popd` <a id="builtin-popd"></a>

**`popd [-n] [+N|-N]`**

Remove the top entry, or entry N, from the directory stack and change to the new
top. `-n` removes the entry below the current directory without changing
directory. An empty stack returns 1.

#### `printf` <a id="builtin-printf"></a>

**`printf [-v variable] format [argument ...]`**

Print the arguments under the control of format, which is reused while arguments
remain. `-v` assigns the result to a variable or array element. The format
accepts the C conversions d, i, o, u, x, X, e, E, f, F, g, G, c, and s with
flags, width, and precision, \* for a width or precision from an argument, %b
for an argument with backslash escapes, %q for an argument quoted for reuse, and
%(*format*)T for a time in seconds, formatted by strftime, with -1 for the
current time.

An argument that is not a valid number prints 0 and makes the
status 1. A missing format returns 2. A field width or precision over 16777216
is an error, and that conversion prints nothing.

The numeric locale is the first nonempty value of LC_ALL, LC_NUMERIC, and LANG
at each call. Floating operands use its decimal point, and the `'` flag groups
digits with its thousands separator. The C and POSIX locales and every Windows
locale use a dot and no grouping. The `sh` mood rejects -v and prints
%(*format*)T as written.

#### `pushd` <a id="builtin-pushd"></a>

**`pushd [-n] [dir|+N|-N]`**

Push dir onto the directory stack and change to it. Without an operand, swap the
two top entries, and with `+`*N* or `-`*N* rotate the stack so that entry N is
on top. `-n` adds dir below the current directory without changing directory. A
missing directory or an empty stack returns 1.

#### `pwd` <a id="builtin-pwd"></a>

**`pwd [-LPW]`**

Print the working directory. `-L` prints the logical path, the default unless
the `physical` option is on, and `-P` the path with symbolic links resolved.
`-W` prints the physical path in Windows form with forward slashes, as MSYS
does, and acts as `-P` elsewhere.

#### `read` <a id="builtin-read"></a>

**`read [-ers] [-a array] [-d delim] [-n|-N count] [-p prompt] [-t timeout] [-u fd] [-q] [name ...]`**

Read a line from standard input and split it into fields at IFS. Each name
receives one field and the last name receives the rest of the line. Without a
name the line goes to REPLY. `-r` keeps backslashes, and otherwise a trailing
backslash continues the line. `-a` splits into an indexed array, `-d` sets the
delimiter (empty for NUL), `-n` sets the maximum byte count, `-N` reads exactly
that many bytes unsplit, ignoring the delimiter, `-t` sets a timeout in seconds,
which may be fractional, `-p` prints a prompt at a terminal, `-s` disables echo,
`-u` reads a descriptor, and `-q` reads one byte and returns 0 for y or Y.

`-e`
is accepted without effect. The status is 1 at the end of input, even when a
partial line was stored, and 142 after a timeout. The `sh` mood requires a name
and rejects every option except -r and -p.

#### `readonly` <a id="builtin-readonly"></a>

**`readonly [-aAfp] [name[=value] ...]`**

Mark variables read-only, after assigning the value when one is given. `-p` or
no operand lists them reusably, `-a` and `-A` treat operands as arrays, and `-f`
marks functions read-only, or lists them with their bodies when given alone.
Assigning a read-only variable is an error, which ends a non-interactive shell
in the `sh` mood.

#### `return` <a id="builtin-return"></a>

**`return [n]`**

Leave the running function or sourced file with status n modulo 256, or with the
status of the last command. Outside a function or sourced file return reports an
error and returns 2. A non-numeric operand returns 2.

#### `set` <a id="builtin-set"></a>

**`set [-+abefhkmnprtuvxBCEHPTIARNSGW] [-+o option] [-M mood] [-L moods] [--] [argument ...]`**

Change options and positional parameters. A minus enables an option and a plus
disables it. Arguments after the options, or after --, replace the positional
parameters, and - also ends the options. Without arguments, set prints the shell
variables as sorted assignments. `set -o` *option* accepts only Bash spellings.

Without an option, `set -o` prints the states and `set +o` prints reusable
commands. A long option such as `--mood` is rejected with a pointer to
`koshconf`.

The letters are -a (allexport), -b (notify, which reports a finished job at once
while the editor waits), -e (errexit), -f (noglob), -h (hashall, accepted
without effect), -k (keyword, which places assignment arguments in the command
environment), -m (monitor, job control), -n (noexec), -p (privileged), -t
(onecmd, which stops after one top-level command), -u (nounset), -v (verbose),
-x (xtrace), -B (braceexpand), -C (noclobber), -E (errtrace), -H (histexpand),
-P (physical), and -T (functrace). Options without a letter are emacs, vi,
history, ignoreeof, interactive-comments, nolog (accepted without effect),
pipefail, and posix, which selects the `bash-posix` mood, while `set +o posix`
selects `bash` from the `bash-posix` or `sh` mood. The options are described
under [shell settings](kosh_5.md#set-o-options).

The Koshka forms `set -M `*MOOD* and `set -L `*MOOD,...* select the mood and
source the startup files of other moods as the command-line options do. Without
a value they print the mood or the initialized moods. `-I`, `-A`, `-R`, `-N`,
`-S`, and `-G` set interpreter.mimic_shebang, debug.print_syntax_tree,
debug.print_lexed_word_escapes, debug.report_every_exit_code,
debug.print_evaluation_statistics, and debug.print_memory_report_at_exit,
and `-W`, `-WW`, and `-WWW` set optimizer.warning_level.

Every other Koshka
setting belongs to `koshconf`. `-r` turns restricted mode on, and `+r` fails
with status 1. An unknown option returns 1 in the Bash moods, ends a
non-interactive shell with status 1 in the `kosh` mood, and with status 2 in the
`sh` mood. In the `kosh` mood, a value other than the one held for nounset,
pipefail, failglob, or nullglob is an error.

#### `shift` <a id="builtin-shift"></a>

**`shift [n]`**

Discard the first n positional parameters, by default 1. A count above $# or
below 0 returns 1, and in the `sh` mood it ends a non-interactive shell with
status 2.

#### `shopt` <a id="builtin-shopt"></a>

**`shopt [-pqsuo] [option ...]`**

Show or change the `shopt` options, which are described under
[shell settings](kosh_5.md#shopt-options). `-s` enables and `-u` disables each option, or
without one lists the options that are on or off. Without -s or -u, shopt prints the state of each
option, or of all of them, and returns 1 when one is off. `-p` prints reusable
shopt commands, `-o` selects the `set -o` options, and `-q` prints nothing. An
unknown option returns 1 after the valid ones are applied.

#### `suspend` <a id="builtin-suspend"></a>

**`suspend [-f]`**

Stop the shell until it receives SIGCONT. A login shell refuses without `-f`.

#### `time` <a id="builtin-time"></a>

**`time [-p] [-R] pipeline`**

A reserved word, described under [Pipelines](#pipelines).

#### `times` <a id="builtin-times"></a>

**`times`**

Print the user and system times of the shell, then of its children.

#### `trap` <a id="builtin-trap"></a>

**`trap [-lp] [[action] condition ...]`**

Set action to run when the shell receives each signal or meets each condition.
The conditions are the signals by number or by name with or without SIG, EXIT or
0 at exit, ERR after a failing command where errexit would act, DEBUG before
each simple command, and RETURN at the end of a function or sourced file.

An
empty action ignores the signal, and an action of - resets the default. Without
arguments, and with `-p`, trap prints the actions as reusable trap commands, and
`-l` lists the signals. An invalid signal returns 1. A trap action runs with the
status of the command before it, and it does not change that status. The `sh`
mood rejects ERR, RETURN, -l, and -p.

#### `type` <a id="builtin-type"></a>

**`type [-aPptV] name ...`**

Describe how each name is interpreted as a command. `-t` prints one of alias,
keyword, function, builtin, or file, `-p` and `-P` print the path of a program,
`-a` prints every interpretation, and `-V` prints everything the shell holds
about each word, such as a function's definition site and body or the
description of a builtin or utility.

The Bash moods report a function as "is a
function" followed by its body, and the other moods as "is a shell function". A
name that is not found returns 1, and 127 in the `sh` mood.

#### `ulimit` <a id="builtin-ulimit"></a>

**`ulimit [-HSa] [-cdflmnpstuvw] [limit]`**

Show or set a resource limit, by default the file size limit -f. `-H` and `-S`
select the hard or the soft limit, and `-a` prints every limit. The resources
are -c (core size), -d (data size), -f (file size), -l (locked memory), -m
(resident set), -n (open files), -p (pipe size), -s (stack size), -t (CPU
seconds), -u (processes), -v (virtual memory), and -w (file locks). A limit is a
number or unlimited. An invalid limit, or one above the hard limit, returns 1.

#### `umask` <a id="builtin-umask"></a>

**`umask [-pS] [mask]`**

Show or set the file creation mask. A mask is octal or symbolic, as in
u=rwx,g=rx,o=. `-S` prints the mask symbolically, and `-p` as a reusable
command. An invalid mask returns 1.

#### `unalias` <a id="builtin-unalias"></a>

**`unalias [-a] name ...`**

Remove each alias, or every alias with `-a`. An unknown alias returns 1.

#### `unset` <a id="builtin-unset"></a>

**`unset [-fnv] name ...`**

Remove each variable or array element, or each function with `-f`. Without an
option a variable is removed, or a function when no such variable exists. `-n`
removes a name reference itself. A read-only variable returns 1, and an unset
name succeeds.

#### `wait` <a id="builtin-wait"></a>

**`wait [-fn] [-p variable] [pid|job ...]`**

Wait for each process or job and return the status of the last one, or wait for
every child and return 0. `-n` waits for the next job to finish among the
operands, or among all jobs, and returns 127 when none is left. `-p` *variable*
stores the process whose status is returned, or unsets the variable.

`-f` waits
until each job terminates. A stopped job returns 128 plus the stop signal, and
`wait -n` skips it. A process that is not a child of the shell, or a job that
does not exist, returns 127 with a diagnostic, and a missing job number returns
2 in the `sh` mood.

### Standard utilities <a id="standard-utilities"></a>

These utilities run through `koshkit` or their utility names, as described under
[Bundled utilities](#bundled-utilities).

#### `basename` <a id="utility-basename"></a>

**`basename`**

Print the final path component, minus an optional suffix. `-a`, `--multiple`
treats every operand as a path, `-s`, `--suffix` removes a suffix from each and
implies `-a`, and `-z`, `--zero` ends entries with NUL.

#### `bc` <a id="utility-bc"></a>

**`bc`**

Evaluate expressions, assignments, functions, arrays, control flow, input and
output bases, and scale, from the file operands and then standard input. `-l`
adds the functions s (sine), c (cosine), a (arctangent), l (natural logarithm),
and e (exponential) and sets scale to 20, and `-q` suppresses the banner.
Division by zero stops the input with status 1.

#### `cal` <a id="utility-cal"></a>

**`cal`**

Print a Gregorian month or year with weeks starting on Sunday. `-a`, `--today`
starts weeks on Monday and prints the date in words below.

#### `cat` <a id="utility-cat"></a>

**`cat`**

Concatenate files or standard input. `-n` numbers lines across files, `-b`
numbers nonempty lines, `-s` squeezes empty runs, `-v` shows control bytes in ^
notation and high bytes with M-, `-E` marks line ends with $ and a CR before a
newline as ^M, `-T` shows tabs as ^I, and `-e`, `-t`, and `-A` act as `-vE`,
`-vT`, and `-vET`. `-u` is accepted. `--syntax-highlighting` colors shell source
on a terminal.

#### `chgrp` and `chown` <a id="utility-chgrp-and-chown"></a>

**`chgrp, chown`**

Change the group, or the owner and optional group, of each file. `-R` recurses,
`-H`, `-L`, and `-P` select which links are followed, `-h` changes a link
itself, and `-x`, `--one-file-system` keeps recursion on one file system.

#### `chmod` <a id="utility-chmod"></a>

**`chmod`**

Apply symbolic or octal modes. `-R`, `--recursive` recurses without following
links, and `--one-file-system` skips other file systems.

#### `cksum` <a id="utility-cksum"></a>

**`cksum`**

Print the POSIX CRC checksum and byte count of each file or standard input,
followed by the file.

#### `cmp` <a id="utility-cmp"></a>

**`cmp`**

Compare two files byte by byte. A difference prints the first differing byte and line and returns 1, equal files return 0, and an unreadable file returns 2. `-l`, `--verbose` lists every differing byte with both values in octal, and `-s`, `--silent` prints nothing. A file of - is standard input.

#### `comm` <a id="utility-comm"></a>

**`comm`**

Merge two sorted files into columns of lines only in the first, only in the
second, and in both; `-1`, `-2`, and `-3` suppress them. A line out of order
after an unpaired line is reported once per file with status 1; `--check-order`
stops at the first line out of order, and `--nocheck-order` skips the check, the
later one winning. `--output-delimiter` sets the column separator (empty for
NUL), `--total` appends column counts, and `-z` uses NUL line ends.

#### `cp` <a id="utility-cp"></a>

**`cp`**

Copy files and trees with `-r`, `-R`, `-f`, `-i`, `-p`, and `-v`. A recursive
copy keeps links as links; `-L` follows every link, `-H` only links on the
command line, and `-P` none. A link leading back into a directory being copied
is reported as cyclic and skipped with status 1. `-x`, `--one-file-system`
copies a directory on another file system as an empty directory.

#### `csplit` <a id="utility-csplit"></a>

**`csplit`**

Split a file into pieces at each pattern, which is a line number or a /*regex*/,
and print the byte count of each piece. The pieces are xx00, xx01, and so on.
`-f`, `--prefix` replaces xx, `-n`, `--digits` sets the number of digits, `-s`,
`--silent` suppresses the counts, and `-k`, `--keep-files` keeps the pieces
after an error. A line number past the input or a regex that does not match
returns 1.

#### `cut` <a id="utility-cut"></a>

**`cut`**

Select bytes, characters, or fields with `-b`, `-c`, or `-f`. `-d` sets the
delimiter, `-s` skips lines without one, `--complement` inverts the selection,
`--output-delimiter=`*str* joins selections with str, and `-z`,
`--zero-terminated` uses NUL line ends. With `-b`, `-n` leaves out a multibyte
character that the byte range splits. An invalid list or a delimiter longer than
one byte returns 1.

#### `date` <a id="utility-date"></a>

**`date`**

Print the date and time, or format them with a +*format* operand of strftime
conversions. `-u`, `--utc` uses UTC. An operand that would set the clock is
refused.

#### `df` <a id="utility-df"></a>

**`df`**

Report mounted file systems or those of the operands in a right-aligned table.
`-k` uses 1024-byte blocks, `-P` the POSIX header, `-h`, `--human-readable`
powers of 1024, and `-H`, `--si` powers of 1000. Unreadable file systems are
reported before the table.

#### `diff` <a id="utility-diff"></a>

**`diff`**

Compare two files line by line. `-u` selects unified output, `-w` ignores
whitespace, `-a` forces text, and up to two `-L` options set the unified labels.
`--color`\[=*WHEN*\] takes auto (the default), always, or never. The status is 0
for equal files, 1 for different files, and 2 for an error.

#### `dirname` <a id="utility-dirname"></a>

**`dirname`**

Print the directory part of each path. `-z`, `--zero` ends entries with NUL.

#### `du` <a id="utility-du"></a>

**`du`**

Print every file and directory below each path, or the current directory, in
descending size order without following links. `-s` prints totals only, `-h`
human-readable sizes, `-x`, `--one-file-system` skips other file systems,
`--tree` prints a tree, and `-T`, `--top-largest` a tree of the largest entries
and their ancestors. Permission errors are warnings, other read failures are
errors, and either makes the status 1.

#### `env` <a id="utility-env"></a>

**`env`**

Run a command with temporary assignments, or print the environment. `-i` starts
empty, and each `-u` removes a variable. The command is found through its PATH,
or /usr/bin:/bin without one.

#### `expand` and `unexpand` <a id="utility-expand-and-unexpand"></a>

**`expand, unexpand`**

Convert tabs to spaces, or blanks to tabs. `-t` takes ascending tab stops
separated by commas or blanks, or one interval; a last value /N adds a stop at
each multiple of N, and +N one every N columns, after the last explicit stop.
The default is every eight columns. `unexpand -a` also converts blanks after the
first nonblank, and `-t` implies it.

#### `expr` <a id="utility-expr"></a>

**`expr`**

Evaluate an expression whose operators and operands are separate arguments and
print the result. The operators are |, &, =, !=, \<, \<=, \>, \>=, +, -, \*, /,
%, and :, which matches a basic regular expression anchored at the start and
prints the first group or the match length. The status is 0 for a result that is
neither 0 nor empty, 1 otherwise, and 2 for an invalid expression. Integer
overflow and division by zero are errors.

#### `file` <a id="utility-file"></a>

**`file`**

Classify file kinds and contents. `-h`, `--no-dereference` classifies a link and
`-L`, `--dereference` its target, `-i`, `--regular-only` skips content tests,
`-m`, `--magic-file` adds a magic database, and `-M`, `--magic-only` uses only
the given ones. `-d`, `--default-tests` selects the default tests.

The kinds
include text, data, directory, the special files, ELF executable, common image
formats, script text executable, and symbolic link to a target. A file that
cannot be opened returns 1.

#### `find` <a id="utility-find"></a>

**`find`**

Walk sorted trees with `-name`, `-iname`, `-type`, `-maxdepth`, `-mindepth`,
`-print`, and `-print0`. `-xdev` and `-mount` stay on one file system. `-exec`
*command* ... *;* runs once per path with {} replaced, and `-exec` *command* {}
*+* runs on batches of paths.

#### `flock` <a id="utility-flock"></a>

**`flock`**

Run a command while holding a lock on a file or directory; the command inherits
the descriptor. `-n`, `--nonblock` fails with status 1 instead of waiting, and
`--transaction-held-lock` keeps the lock after the launching process exits.

#### `fold` <a id="utility-fold"></a>

**`fold`**

Wrap the lines of each file or standard input at 80 columns, or at the width of
`-w`, `--width`. A tab advances to the next multiple of eight. `-s`, `--spaces`
breaks after the last blank when one fits, and `-b`, `--bytes` counts bytes.

#### `fuser` <a id="utility-fuser"></a>

**`fuser`**

Print the pids that use each file, with `-c`, `-f`, and `-u`.

#### `getconf` <a id="utility-getconf"></a>

**`getconf`**

Print a system or string variable, or a path variable for a path; an unsupported
one prints undefined. `-a`, `--all` prints every variable, and `-v`,
`--specification` selects a POSIX environment.

#### `grep`, `egrep`, and `fgrep` <a id="utility-grep-egrep-and-fgrep"></a>

**`grep, egrep, fgrep`**

Print lines matching basic regular expressions, or extended ones with `-E`
(egrep), or fixed strings with `-F` (fgrep); the two conflict with status 2.
`-e` adds a pattern and `-f` the lines of a file, and with either every operand
is a file; a newline separates patterns. `-i`, `-v`, `-x`, `-n`, `-h`, and `-r`
ignore case, invert, match whole lines, number lines, omit file prefixes, and
recurse, and `--one-file-system` keeps `-r` on one file system.

`-c` counts,
`-l` lists matching files and overrides `-c`, `-q` exits 0 at the first match,
and `-s` hides unreadable-file messages. In a file with a NUL byte, matches
before the line of the first NUL print, the next one shows a single warning, and
later ones print nothing, unless `-a`, `--text` treats it as text.

In a UTF-8
locale a dot and a bracket expression match one character, and one byte
otherwise. The status is 0 when a line is selected, 1 when none is, and 2 on
error.

`--color`\[=*WHEN*\] takes auto (the default), always, or never. GREP_COLORS
holds colon-separated GNU capabilities such as ms=01;31: ms, mt, sl, cx, fn, ln,
se, and rv apply, unknown ones are ignored, and an empty value turns that color
off. GREP_COLOR sets the match color below GREP_COLORS.

#### `head` and `tail` <a id="utility-head-and-tail"></a>

**`head, tail`**

Print the first or last ten lines. `-n` sets a line count and `-c` a byte count,
with an optional suffix: b for 512, k, K, m, M, G, T, P, E, Z, Y, R, or Q for
powers of 1024, and those letters followed by B for powers of 1000 or by iB, as
in 1kB and 1KiB. A first argument such as -5 means `-n 5`.

For head, a leading
dash prints all but the last N; for tail, a leading plus starts at line or byte
N. Headers appear for several files; `-q`, `--quiet`, and `--silent` suppress
them and `-v`, `--verbose` forces them, the last winning. `-z`,
`--zero-terminated` uses NUL line ends.

`tail -f` keeps printing what is appended to each regular file, and `-F` follows
by name, reopening a replaced, removed, or new file, and implies `--retry`.
`--follow=`*descriptor|name* selects the mode, `-s`, `--sleep-interval` the
longest wait between checks (1 second), and `--pid=`*pid* ends following after
that process exits. An interrupted follow exits with status 130.

#### `id` <a id="utility-id"></a>

**`id`**

Print the user and group identities of the current process or of a user. `-u`,
`--user`, `-g`, `--group`, and `-G`, `--groups` print only the user, the group,
or every group, `-n`, `--name` prints names, and `-r`, `--real` uses the real
identities. An unknown user returns 1.

#### `killall` and `pkill` <a id="utility-killall-and-pkill"></a>

**`killall, pkill`**

`killall` signals processes by exact name, and `pkill` those whose short command
contains the pattern. The signal comes from `-s`, `--signal` in killall, and
from `--signal` or `-`*SIGNAL* in pkill; `-l`, `--list` lists signals. pkill
`-s`, `--session` takes comma-separated session ids, 0 for its own, and then the
pattern is optional; Windows has no sessions, so 0 is rejected and nothing
matches another id.

`--inverse` signals what the pattern and sessions do not
select. Process 0, the shell, and the utility itself are never signaled by
killall.

#### `less` <a id="utility-less"></a>

**`less`**

Show files or standard input full screen on the alternate screen. The keys come
from the terminal. Space, f, Ctrl-F, Ctrl-V, and Page Down move forward a page,
b, Ctrl-B, and Page Up back a page, d and Ctrl-D forward half a page, u and
Ctrl-U back half a page, j, e, Enter, Down, Ctrl-E, and Ctrl-N one row down, and
k, y, Up, Ctrl-P, and Ctrl-Y one row up.

The mouse wheel scrolls three rows. g,
\<, and Home go to the top, G, \>, and End to the bottom, and r and Ctrl-L
redraw. `/`*pattern* and `?`*pattern* search forward and backward with an
extended regular expression, and n and N repeat the search. An empty pattern
repeats the previous one, Escape cancels the prompt, and an invalid pattern is
reported. q and Q quit, and an interrupt quits with status 130.

The status row shows the file, the visible lines and their total, the position,
and (END) at the end. Input loads as needed and is kept in memory up to 128 MiB,
counting the line records. Input beyond that is not shown, and the status row
adds (truncated) at the end. `-N`, `--LINE-NUMBERS` numbers the lines, `-S`,
`--chop-long-lines` cuts long lines instead of wrapping them, and `-i`,
`--ignore-case` ignores case in searches. Several files follow headers of
colons. When output is not a terminal, the input is copied unchanged.

#### `link` <a id="utility-link"></a>

**`link`**

Create a hard link file2 to file1. An existing file2 returns 1.

#### `ln` <a id="utility-ln"></a>

**`ln`**

Create hard links, or symbolic links with `-s`. `-f` removes an existing
destination, `-L` follows a link source, and `-P` links the link itself.

#### `locale` <a id="utility-locale"></a>

**`locale`**

Print LANG and the LC\_ variables as the environment sets them. `-a`,
`--all-locales` lists the available locales and `-m`, `--charmaps` the character
maps. A keyword operand such as charmap prints its value, `-k`, `--keyword-name`
adds the keyword, and `-c`, `--category-name` adds its category. An unknown
keyword returns 1.

#### `logger` <a id="utility-logger"></a>

**`logger`**

Send a message, the operands or each line of `-f`, `--file`, to the system log.
`-t`, `--tag` sets the tag, `-p`, `--priority` the facility and severity, as in
user.notice, `-i`, `--id` adds the process id, and `-s`, `--stderr` also writes
the message to standard error. An invalid priority returns 1.

#### `logname` <a id="utility-logname"></a>

**`logname`**

Print the login user, which can differ from the effective user that `whoami`
prints.

#### `ls` <a id="utility-ls"></a>

**`ls`**

List sorted entries with `-a`, `-A`, `-1`, `-l`, and `-h`. `-F` appends type
indicators, `-p` a slash to directories, `-d` lists directories themselves, `-n`
shows numeric ids, and `-g` and `-o` omit the owner or group. `-t`, `-S`, and
`-r` sort by time, by size, and in reverse. `-R` recurses, `--tree` prints a
tree, `-L` limits depth, and `--one-file-system` keeps both on one file system.
`-k` is accepted. Without a terminal, ls prints one entry per line, and `-l`
prints a symbolic link without its target.

#### `make` <a id="utility-make"></a>

**`make`**

Run make rules. `-f`, `--file`, `-C`, `--directory`, `-B` (always rebuild), `-k` (keep going), and the POSIX modes `-e`, `-i`, `-S`, `-n`, `-p`, `-q`, `-r`, `-s`, and `-t` are supported, and `-j` takes an optional job count passed through MAKEFLAGS. Without `-f`, make reads Makefile or makefile, and `-f -` reads standard input. Operands of the form _name_=_value_ set variables, and the variables of the environment are also available.

The functions are abspath, addprefix, addsuffix, and, basename, call, dir,
error, eval, filter, filter-out, findstring, firstword, flavor, foreach, if,
join, lastword, notdir, or, origin, patsubst, realpath, shell, sort, strip,
subst, suffix, value, warning, wildcard, word, wordlist, and words. The
directives are include, ifeq, ifneq, ifdef, ifndef, else, endif, define,
override, export, and unexport, and the special targets are .DEFAULT, .IGNORE,
.PHONY, .POSIX, .PRECIOUS, .SCCS_GET, .SILENT, and .SUFFIXES.

The wildcard function and a prerequisite holding \*, ?, or \[ use shell pathname
expansion, and an unmatched prerequisite pattern stays as written. Recipes are
printed before they run and use the SHELL of the makefile, and MAKE expands to
koshkit make. A target without a rule returns 2, and `-q` returns 1 when a
target is out of date.

#### `man` <a id="utility-man"></a>

**`man`**

Print the source of the manual page of each name, from the section operand or
the first section that has one, searching the directories of MANPATH, by default
/usr/share/man and /usr/local/share/man. `-k`, `--keyword` lists the pages whose
first lines contain the keyword. A missing page returns 1.

#### `mkdir` <a id="utility-mkdir"></a>

**`mkdir`**

Create directories, with parents under `-p`. `-m` takes an octal mode or a
symbolic mode applied to 777.

#### `mkfifo` <a id="utility-mkfifo"></a>

**`mkfifo`**

Create each FIFO, with the mode of `-m`, `--mode` or 0666 less the creation
mask.

#### `mknod` <a id="utility-mknod"></a>

**`mknod`**

Create FIFOs and character or block special files from POSIX type operands or
`--type`, `--fifo`, `--character`, and `--block`. Device nodes take `--major`
and `--minor`, and `-m` sets the mode.

#### `more` <a id="utility-more"></a>

**`more`**

Show files or standard input a screen at a time with a `--More--` prompt, with keys from the terminal: space and f the next page, Enter, j, and Down the next line, d half a page, b, k, Page Up, and Up further back, g and G the top and the bottom, the searches of `less`, and q, or a forward key at the end, to quit. `-n`, `--lines` sets the lines per screen, `-c`, `--clear` clears before each page, `-e`, `--exit` exits after the last line, `-p`, `--command` starts at the first line that matches a pattern, `-t`, `--tag` at a tag of the tags file, `-s`, `--squeeze` collapses repeated blank lines, and `-i`, `--ignore-case` ignores case in searches.

Each of several files follows a header of colons, also when output is not a terminal. When output is not a terminal, the input is copied, changed only by `-p`, `-t`, and `-s`. A pattern of `-p` may begin with a slash. `-t` reads ./tags, whose lines hold a tag, a file, and a line separated by tabs, and takes no file operand.

`-p` and `-t` cannot be combined, and more needs a file when standard input is a terminal. The status row shows --More-- and the position, or (END). Input is kept in memory up to 128 MiB, counting the line records. Input beyond that limit is not shown, and the status row adds (truncated). `-u` is accepted.

#### `mv` <a id="utility-mv"></a>

**`mv`**

Rename with `-f`, `-i`, and `-v`.

#### `nice` <a id="utility-nice"></a>

**`nice`**

Run a utility with its scheduling priority lowered by 10, or by the increment of
`-n`, `--increment`. A utility that is not found returns 127.

#### `nl` <a id="utility-nl"></a>

**`nl`**

Number lines within logical pages. A line of only the section delimiter repeated
three, two, or one times starts a header, body, or footer and prints empty. `-d`
sets the delimiter, \\: by default, adding a colon to a single character; an
empty delimiter disables sections. `-h`, `-b`, and `-f` set the section styles
(default n, t, n): a every line, t nonempty lines, n none, and p*BRE* lines
matching the regular expression.

Each section restarts at `-v` unless `-p` is
given, and `-i` sets the increment; both may be negative. `-l` numbers every Nth
empty line of a run under style a. `-w`, `-n`, and `-s` set the width (at most
65536), the ln, rn, or rz format, and the separator. An overflowing line number
stops with status 1.

#### `nohup` <a id="utility-nohup"></a>

**`nohup`**

Run a utility that ignores SIGHUP. When standard output is a terminal, the
output is appended to nohup.out in the current directory, or in HOME when that
fails, which is created with mode 0600. A utility that is not found returns 127,
and one that cannot run 126.

#### `nproc` <a id="utility-nproc"></a>

**`nproc`**

Print the available processor count; `--all` and `--ignore=`*COUNT* are
supported.

#### `od` <a id="utility-od"></a>

**`od`**

Dump bytes in the formats of `-t`; `-b`, `-c`, `-d`, `-o`, `-s`, and `-x` select o1, c, u2, o2, d2, and x2. A line holds 16 bytes, or `-w`\[_N_\] or `--width`\[=_N_\] bytes, up to 65536 or 32 without N. A width that is not a multiple of the largest unit is replaced by that unit with a warning.

The types of `-t` are c, o, u, d, and x, with an optional size, and several types print one row each. `-A` sets the address radix to d, o, x, or n for none, `-j` skips bytes, `-N` limits the bytes read, and `-v` prints repeated lines that are otherwise shown as \*.

#### `paste` <a id="utility-paste"></a>

**`paste`**

Join corresponding lines with tabs. `-d` sets delimiters used in turn, `-s`
joins each file into one line, and `-z`, `--zero-terminated` uses NUL line ends.

#### `pathchk` <a id="utility-pathchk"></a>

**`pathchk`**

Check paths against the file system limits. `-p` uses the POSIX portable limits
and characters, `-P` rejects an empty path and a component starting with a
hyphen, and `-W`, `--windows` rejects what Windows cannot hold, as Windows
builds always do.

#### `pr` <a id="utility-pr"></a>

**`pr`**

Paginate each file or standard input into 66-line pages with a header of the
date, the file, and the page number. +*first*\[:*last*\] selects pages,
-*columns* prints that many columns, `-a`, `--across` fills them across, `-m`,
`--merge` prints the files side by side, and `-t`, `--omit-header` omits
headers. `-d`, `--double-space`, `-n`, `--number-lines`, `-o`, `--indent`, `-h`,
`--header`, `-D`, `--date-format`, `-l`, `--length`, `-w`, `--width`, and `-s`,
`--separator` set spacing, numbering, the indent, the header text and date
format, the page length and width, and the column separator. `-F` and `-f`
separate pages with form feeds, and `-r`, `--no-file-warnings` suppresses file
warnings.

#### `printenv` <a id="utility-printenv"></a>

**`printenv`**

Print the environment, or the given variables, with status 1 if one is absent.

#### `ps` <a id="utility-ps"></a>

**`ps`**

List processes with `-a`, `-u`, and `-x`; `-w` is accepted.

#### `readlink` <a id="utility-readlink"></a>

**`readlink`**

Print the target of each symbolic link; a non-link is reported with status 1.
`-n` omits the newline for a single file.

#### `realpath` <a id="utility-realpath"></a>

**`realpath`**

Print the canonical absolute path. Every component but the last must exist, all
with `-e`, and none with `-m`. Links resolve as met, or with `-P`, `--physical`;
`-L`, `--logical` resolves .. first, and `-s`, `--no-symlinks`, and `--strip`
leave links alone, the last of the three winning. `--relative-to` prints paths
relative to a directory, and `--relative-base` prints paths below a directory
relative to it and others absolute; with both, paths are relative to the first
when it is below the base. `-q`, `--quiet` omits errors, and `-z`, `--zero` ends
entries with NUL.

#### `renice` <a id="utility-renice"></a>

**`renice`**

Add the increment of the required `-n`, `--increment` to the priority of each
process, or of each process group with `-g`, `--pgrp`, or of each user's
processes with `-u`, `--user`. `-p`, `--pid` selects process ids, the default.

#### `rm` <a id="utility-rm"></a>

**`rm`**

Remove paths. `-r`, `-R`, `--recursive` removes directories and their contents,
`-d`, `--dir` removes empty directories, `-f`, `--force` ignores missing paths
and never asks, `-i` asks before each removal, `-I` asks once before removing
more than three paths or a tree, and `--interactive=`*when* selects never, once,
or always.

`-v`, `--verbose` prints each removed path, and `--dry-run` prints
what would be removed and removes nothing. The operands . and .. are refused,
and so is every spelling of a root directory or a Windows drive root unless
`--no-preserve-root` is given. `--preserve-root=all` also refuses an operand on
another device than its parent. `-x`, `--one-file-system` skips directories on
other file systems with status 1. A missing path returns 1 without `-f`.

#### `rmdir` <a id="utility-rmdir"></a>

**`rmdir`**

Remove empty directories, and with `-p` their empty parents.

#### `sed` <a id="utility-sed"></a>

**`sed`**

Apply an editing script, from the first operand or from each `-e`,
`--expression` and `-f`, `--file`, to the lines of each file or standard input.
The commands are s with the g and p flags, y, d, p, q, =, and a, i, and c, whose
text follows on the same line or on the next lines after a backslash, with line,
$, and regex addresses and ranges.

`-n`, `--quiet` suppresses automatic
printing, and `-E`, `--extended-regexp` selects extended regular expressions.
Blocks, the hold space, branches, and in-place editing are not supported, and an
unsupported command returns 1.

#### `seq` <a id="utility-seq"></a>

**`seq`**

Print numbers with a nonzero step. Integers print exactly, and decimal,
exponent, and hexadecimal operands print with the most fraction digits of the
first operand and the step. `-w`, `--equal-width` pads with zeros, `-s`,
`--separator` sets the separator, and `-f`, `--format` applies one printf %e,
%f, %g, or %a conversion and excludes `-w`.

#### `sleep` <a id="utility-sleep"></a>

**`sleep`**

Pause for the sum of the durations, with optional s, m, h, or d suffixes.

#### `sort` <a id="utility-sort"></a>

**`sort`**

Sort lines by byte order or by `-k` keys of the form _F\[.C\]\[bdfghiMnrV\]\[,F\[.C\]\[bdfghiMnrV\]\]_. `-t` sets the separator. `-b`, `-d`, `-f`, `-i`, `-n`, and `-r` ignore leading blanks, compare only blanks and alphanumerics, fold case, compare only printable characters, compare numbers, and reverse.

`-g` compares floating numbers, including exponents, infinities, and NaN, with text first; `-h` compares K, M, G, T, P, E, Z, Y, R, and Q suffixes; `-M` compares month abbreviations with other text first; and `-V` compares version numbers. At most one of `-g`, `-h`, `-M`, and `-n` applies to a key, and none combines with `-d`, `-i`, or `-V`.

`-z` uses NUL line ends, `-u` keeps the first of equal keys, `-c` reports the first unsorted line and `-C` only the status, `-o` writes to a file, `-s` skips the last-resort whole-line comparison, and `-m` is accepted.

#### `split` <a id="utility-split"></a>

**`split`**

Split a file or standard input into pieces of 1000 lines, the count of `-l`,
`--lines`, or the byte count of `-b`, `--bytes`, called xaa, xab, and so on, or
with the prefix operand. `-a`, `--suffix-length` sets the number of suffix
letters. A count of 0 returns 1.

#### `stat` <a id="utility-stat"></a>

**`stat`**

Print file or file system metadata. `-L`, `-f`, `-t`, `-c`, and `--printf`
follow links, select the file system, select terse output, apply a format, and
apply a format without a trailing newline.

#### `strings` <a id="utility-strings"></a>

**`strings`**

Print each run of at least four printable characters in each file or standard
input. `-n`, `--bytes` sets the minimum length, `-t`, `--radix` prefixes each
string with its offset in d, o, or x, and `-a`, `--all` scans the whole file.

#### `stty` <a id="utility-stty"></a>

**`stty`**

Report or change terminal settings; `-a` reports all, and `-g` prints a state
that restores them later.

#### `sync` <a id="utility-sync"></a>

**`sync`**

Flush file system data. `-d` flushes the data of each operand, and `-f` the file
system of each operand, or every file system without one.

#### `tabs` <a id="utility-tabs"></a>

**`tabs`**

Set the terminal tab stops to every 8 columns, every N columns for -N, or the
listed columns, by writing escape sequences to standard output.

#### `tee` <a id="utility-tee"></a>

**`tee`**

Copy standard input to files, appending with `-a`.

#### `timeout` <a id="utility-timeout"></a>

**`timeout`**

Run a command with a time limit, with an optional duration suffix. `-s`,
`--signal` selects the signal, `-k`, `--kill-after` sends KILL after a further
duration, and `-p`, `--preserve-status` returns the command status.

#### `touch` <a id="utility-touch"></a>

**`touch`**

Update file times. `-a`, `-c`, `-m`, `-r`, and `-t` select the access time, skip
missing files, select the modification time, copy a reference file, and set a
timestamp.

#### `tput` <a id="utility-tput"></a>

**`tput`**

Write the escape sequence of a terminal capability from a fixed table. The capabilities are bel, blink, bold, civis, clear, cnorm, cub1, cud1, cuf1, cuu1, dch1, dl1, ed, el, home, ich1, il1, ind, init, reset, rev, rmso, rmul, sgr0, smso, smul, and cup with a row and a column. cols and lines print the terminal size and need a terminal, and longname prints the description of TERM or of `-T`, `--terminal`. An unknown capability returns 1.

#### `tr` <a id="utility-tr"></a>

**`tr`**

Translate bytes with ranges, classes, \[=c=\] equivalence classes, and \[c\*n\]
repeats. `-d` deletes, `-c` and `-C` complement the first set, and `-s` squeezes
runs of a byte from the last set. A shorter second set is padded with its last
byte unless `-t`, `--truncate-set1` truncates the first.

When translating, the
second set accepts only \[:upper:\] and \[:lower:\], each aligned with an
\[:upper:\] or \[:lower:\] class in the first set, and one \[c\*\] repeat that
fills it to the length of the first; a \[c\*\] in the first set is an error.

#### `tsort` <a id="utility-tsort"></a>

**`tsort`**

Print a total order of the pairs of words in a file or standard input. A cycle
prints a partial order and returns 1.

#### `tty` <a id="utility-tty"></a>

**`tty`**

Print the terminal of standard input, or not a tty with status 1. `-s`,
`--silent` prints nothing.

#### `uname` <a id="utility-uname"></a>

**`uname`**

Print the system name, or the fields of `-m` (machine), `-n` (node), `-r`
(release), `-s` (system), and `-v` (version), or all of them with `-a`.

#### `uniq` <a id="utility-uniq"></a>

**`uniq`**

Collapse adjacent equal lines. `-c` counts, `-d` and `-u` print only repeated or only unique lines, and `-D` every line of a repeated run. `-f` and `-s` skip fields and characters, `-w` compares at most N characters, and `-i` ignores ASCII case. `--all-repeated`\[=_none_|_prepend_|_separate_\] acts as `-D` with empty lines before or between runs, and `--group`\[=_separate_|_prepend_|_append_|_both_\] prints every line with empty lines between, before, or after runs and excludes `-c`, `-d`, `-D`, and `-u`. `-z` uses NUL line ends, and a second operand is the output file.

#### `unlink` <a id="utility-unlink"></a>

**`unlink`**

Remove one non-directory path.

#### `watch` <a id="utility-watch"></a>

**`watch`**

Run a command at an interval. `-n`, `-t`, `-g`, and `-e` set the interval, omit
the title, exit when output changes, and exit after a command error; `-x` is
accepted. On a terminal, q or an interrupt exits with status 0, and the command
reads cooked terminal input.

#### `wc` <a id="utility-wc"></a>

**`wc`**

Count lines, words, characters, and bytes with `-l`, `-w`, `-m`, and `-c`.

#### `which` <a id="utility-which"></a>

**`which`**

Resolve builtins, PATH programs, and bundled utilities, with `-a`, `--all` and
`-q`, `--quiet`.

#### `who` <a id="utility-who"></a>

**`who`**

Print the logged-in users with their terminals and login times. The options -a,
-b, -d, -l, -p, -r, -t, and -T select records and fields as POSIX describes,
`-H` adds headings, `-m` and `am i` select the terminal of standard input, `-q`
prints the users and their count, `-s` the short form, and `-u` idle times.

#### `whoami` <a id="utility-whoami"></a>

**`whoami`**

Print the effective user.

#### `xargs` <a id="utility-xargs"></a>

**`xargs`**

Append arguments from standard input to a command, which reads the null device. `-a`, `--arg-file` reads arguments from a file and leaves the command standard input. `-0` splits on NUL and `-d`, `--delimiter` on one character or escape such as \\n, \\t, \\x41, or \\0; both keep quotes and backslashes literal and disable the end marker.

`-r` skips an empty input, `-E` sets the end marker, and `-e`\[_EOF_\], `--eof`\[=_EOF_\] sets or removes it. `-I` replaces a string with each input line, and `-i`\[_R_\], `--replace`\[=_R_\] does so with R or {}. `-L`, `-n`, and `-s` limit each command to lines, arguments, or bytes; for `-L`, empty lines do not count and a trailing blank continues a line.

`-P`, `--max-procs` runs that many commands at once, or as many as possible for 0, with output that may interleave. `-p` asks on the terminal before each command, `-t` traces, and `-x` fails when a command does not fit. The status is 123 when a command fails, 124 when one exits with 255, 125 when a signal ends one, and 126 or 127 when one cannot run, which under `-P` stops the run after the running commands finish.

#### `yes` <a id="utility-yes"></a>

**`yes`**

Repeat the joined operands, or y, until output closes.

### Locale <a id="locale"></a>

The first nonempty value of LC_ALL, LC_CTYPE, and LANG selects the character
locale. A UTF-8 or utf8 codeset selects UTF-8 when the platform supports it, and
anything else selects bytes. In UTF-8, a ? and each step of a \* consume one
character, a bracket expression compares code points, ${#v} and
${v:offset:length} count characters, and ${v^}, ${v,}, and ${v~} change the case
of multibyte letters.

This applies to pathname expansion, case, \[\[ == \]\],
${v#pat}, ${v%pat}, ${v/pat/rep}, ${v^pat}, and compgen -X. Completion always
matches by UTF-8 sequence.

In the Bash moods pathname expansion sorts by the collation order that LC_ALL
or LC_COLLATE selects. The collation order is read from the environment the
first time a pattern is sorted. The first nonempty value of LC_ALL,
LC_NUMERIC, and LANG selects the numeric locale of `printf`.

### Restricted shell <a id="restricted-shell"></a>

A restricted shell rejects directory changes, including those of pushd, popd, and z, and changes to SHELL, PATH, ENV, and BASH\_ENV, which become read-only. It rejects a directory separator in a command word or a source operand, the redirections \>, \>|, \<\>, \>\>, &\>, &\>\>, and \>&, `exec`, `newgrp`, `bench`, `assimilate`, and `slurp`, `hash -p`, `command -p`, `enable -f`, `enable -d`, `set -M`, `set -I`, `set +I`, `set +r`, `source --mood`, history file operands, every form of `koshkit`, and the `koshconf` forms `create`, `set`, `load`, and `save --persist`. Numeric descriptor duplication stays available, and so does `set -o posix`.

A command word falls back only to a bundled utility that a startup file enabled
with `enable`. After startup, `enable` can disable a builtin but cannot enable
one or change a bundled utility. An enabled utility that starts other programs,
such as `env`, `xargs`, `find`, `nohup`, `timeout`, or `watch`, can start any
program, including another shell, so a startup file for a restricted shell
should enable only utilities that start none.

No setting turns restricted mode off. A shell started restricted by `-r` or as
rbash ignores and removes `KOSHCONF` with a warning, and applies only
presentation and diagnostic options from the settings files. It ignores the
mood, a semantic option, editor.history.file_path, and
editor.history.max_entries there with a warning.

A child shell inherits the
restrictions, and a sourced script keeps them. A shell script found through PATH
and run as a command runs without them.

### Related shells <a id="related-shells"></a>

The `bash`, `dash`, `sh`, `fish`, and `busybox` manuals describe their
respective shells.
