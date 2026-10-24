# An allowlisted command whose subcommand chain runs three deep. The top-level
# --help lists subcommands, the second level lists its own sub-subcommands, and
# the third level lists that sub-subcommand's options. Completion forks each
# level when the settled words name a parsed subcommand chain, capped at the
# depth limit. A fake binary called an allowlisted command keeps the
# candidates stable across machines. The help parser accepts a bare SUBCOMMANDS
# header with no colon (tailscale), a flags header at the margin that ends the
# section, left-margin group headers that keep a section open (go shaped like
# git), a "The commands are:" header followed by a blank line (go), and a
# Commands header (cargo).
dir=$(mktemp -d) || exit 1
trap 'test -n "$dir" && rm -rf "$dir"' EXIT
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/docker" <<'SH'
#!/bin/sh
if [ "$1" = "compose" ] && [ "$2" = "config" ]; then
  echo "OPTIONS"
  echo "  --services   List the services"
  echo "  --volumes    List the volumes"
elif [ "$1" = "compose" ] && [ "$2" = "bogus" ]; then
  echo "OPTIONS"
  echo "  --forked   Printed only when the unknown word forks"
elif [ "$1" = "compose" ]; then
  echo "Commands:"
  echo "  up        Start the services"
  echo "  config    Show the resolved config"
else
  echo "Commands:"
  echo "  compose   Run a compose file"
  echo "  run       Run a command"
fi
SH
chmod +x "$dir/docker"
mkdir -p "$dir/man/man1"
cat > "$dir/man/man1/docker.1" <<'EOF'
.TH DOCKER 1
.SH SYNOPSIS
\fBdocker\fR [OPTIONS] COMMAND
EOF
echo "== first-level subcommands:"
MANPATH="$dir/man" PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'docker ' </dev/null
echo "== help lists a subcommand that the man page lacks:"
MANPATH="$dir/man" PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'docker r' </dev/null
echo "== second-level sub-subcommands from 'docker compose --help':"
MANPATH="$dir/man" PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'docker compose ' </dev/null
echo "== third-level options from 'docker compose config --help':"
MANPATH="$dir/man" PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'docker compose config --' </dev/null
echo "== an unknown deeper word does not fork:"
MANPATH="$dir/man" PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'docker compose bogus --' </dev/null
mkdir -p "$dir/grouped" "$dir/plain"
chmod 755 "$dir/grouped" "$dir/plain"
cat > "$dir/grouped/go" <<'SH'
#!/bin/sh
printf '%s\n' \
    'usage: go [--version] <command> [<args>]' \
    '' \
    'These are common Go commands used in various situations:' \
    '' \
    'start a working area (see also: go help tutorial)' \
    '   clone      Clone a repository into a new directory' \
    '   init       Create an empty Go repository' \
    '' \
    'work on the current change (see also: go help everyday)' \
    '   add        Add file contents to the index' \
    '   mv         Move or rename a file, a directory, or a symlink' \
    '   restore    Restore working tree files' \
    '' \
    'examine the history and state (see also: go help revisions)' \
    '   diff       Show changes between commits' \
    '   log        Show commit logs' \
    '   status     Show the working tree status'
SH
chmod +x "$dir/grouped/go"
cat > "$dir/plain/go" <<'SH'
#!/bin/sh
cat <<'HELP'
Go is a tool for managing Go source code.

Usage:

	go <command> [arguments]

The commands are:

	bug         start a bug report
	build       compile packages and dependencies
	clean       remove object files and cached files
	doc         show documentation for package or symbol
	env         print Go environment information
HELP
SH
chmod +x "$dir/plain/go"
cat > "$dir/cargo" <<'SH'
#!/bin/sh
cat <<'HELP'
A fake multi-tool

Usage: cargo [OPTIONS] <COMMAND>

Options:
      --verbose   Be loud
  -h, --help      Print help

Commands:
    build, b    Compile the thing
    check       Check the thing for errors
    clean       Remove the output directory
HELP
SH
chmod +x "$dir/cargo"
cat > "$dir/tailscale" <<'SH'
#!/bin/sh
if [ "$1" = "set" ]; then
  cat <<'HELP'
USAGE
  tailscale set [flags]

FLAGS
  --hostname     Set the device hostname
  --shields-up   Block incoming connections
HELP
elif [ "$1" = "debug" ]; then
  cat <<'HELP'
SUBCOMMANDS
  derp-map     Print the DERP map
  prefs        Print the prefs
HELP
elif [ "$1" = "bogus" ]; then
  printf '%s\n' 'FLAGS' '  --forked   Printed only when the unknown word forks'
else
  printf '%s\n' \
    'The easiest way to fake WireGuard.' \
    '' \
    'USAGE' \
    '  tailscale [flags] <subcommand> [command flags]' \
    '' \
    'SUBCOMMANDS' \
    '  up           Connect to the thing' \
    '  down         Disconnect from the thing' \
    '  set          Change a preference' \
    '  exit-node    Show exit nodes' \
    '  debug        Debugging commands' \
    '' \
    'FLAGS' \
    '  --help       Show help'
fi
SH
chmod +x "$dir/tailscale"
echo "== subcommands under a colon-less header:"
attempt_count=0
while [ "$attempt_count" -lt 3 ]; do
  subcommands=$(PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale ' </dev/null)
  case $subcommands in
    *down*) break ;;
  esac
  attempt_count=$((attempt_count + 1))
done
printf '%s\n' "$subcommands"
echo "== a flags header at the margin ends the section:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale -' </dev/null
echo "== second-level options from 'tailscale set --help':"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale set --' </dev/null
echo "== second-level sub-subcommands from 'tailscale debug --help':"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale debug ' </dev/null
echo "== an unknown second word does not fork:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale bogus --' </dev/null
echo "== grouped subcommands with no prefix:"
PATH="$dir/grouped${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go ' </dev/null
echo "== grouped subcommands with a prefix:"
PATH="$dir/grouped${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go di' </dev/null
echo "== go subcommands in subcommand position:"
PATH="$dir/plain${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go bu' </dev/null
echo "== go subcommands with no prefix:"
PATH="$dir/plain${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go ' </dev/null
echo "== cargo subcommands in subcommand position:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'cargo c' </dev/null
echo "== cargo option after a dash:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'cargo --v' </dev/null
