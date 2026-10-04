unset KOSH_FLAGS
# Runtime errors report the file, line, column, source line, and caret of the
# code that failed, and each level of indirection between the top level and the
# failing command adds one trace frame at the construct that entered it. The
# fixtures use relative names so the printed paths are the typed ones.
d=$(mktemp -d)
trap '[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
cd "$d" || exit 1

run()
{
    label=$1
    shift
    printf '== %s\n' "$label"
    out=$("$@" 2>&1)
    status=$?
    printf '%s\n' "$out" | sed "s|$d|DIR|g"
    printf 'rc=%s\n' "$status"
}

cat > top.sh <<'EOF'
echo start
missing_top
echo end
EOF
run "no indirection, no trace" "$BIN" --mood bash top.sh

cat > lib.sh <<'EOF'
echo lib-loaded
missing_lib
echo lib-done
EOF
cat > use_lib.sh <<'EOF'
echo before
. ./lib.sh
echo after
EOF
run "command not found at the top of a sourced file" \
    "$BIN" --mood bash use_lib.sh

cat > helper_lib.sh <<'EOF'
# helper library
build()
{
  echo building
  missing_tool --flag
  echo built
}

other()
{
  echo other
}
EOF
cat > call_helper.sh <<'EOF'
#!/bin/bash

. ./helper_lib.sh

echo hello

build
EOF
run "function defined in a sourced file, called by the sourcer" \
    "$BIN" call_helper.sh

cat > viavar.sh <<'EOF'
lib=./helper_lib.sh
. "$lib"
build
EOF
run "file sourced through a variable" "$BIN" --mood bash viavar.sh

cat > level_c.sh <<'EOF'
echo c-start
missing_c
EOF
cat > level_b.sh <<'EOF'
echo b-start
missing_b
. ./level_c.sh
EOF
cat > level_a.sh <<'EOF'
echo a-start
. ./level_b.sh
missing_a
EOF
run "nested source with an error at each level" "$BIN" --mood bash level_a.sh

cat > args_lib.sh <<'EOF'
echo "arg=$1"
if [ "$1" = stop ]; then
  return 3
fi
missing_args
EOF
cat > use_args.sh <<'EOF'
. ./args_lib.sh stop
echo "status=$?"
. ./args_lib.sh go
echo "status=$?"
EOF
run "source with arguments and a return" "$BIN" --mood bash use_args.sh

cat > twice.sh <<'EOF'
. ./lib.sh
. ./lib.sh
EOF
run "the same file sourced twice" "$BIN" --mood bash twice.sh

cat > redefine.sh <<'EOF'
cat > swap_lib.sh <<'LIB'
swapped()
{
  first_version_missing
}
LIB
. ./swap_lib.sh
swapped
cat > swap_lib.sh <<'LIB'
# a longer header
# moves the body down

swapped()
{
  echo changed
  second_version_missing
}
LIB
. ./swap_lib.sh
swapped
EOF
run "a sourced file modified and sourced again" "$BIN" --mood bash redefine.sh

cat > dot_and_source.sh <<'EOF'
. ./lib.sh
source ./lib.sh
EOF
run "dot and source" "$BIN" --mood bash dot_and_source.sh

mkdir sub
cat > sub/inner.sh <<'EOF'
missing_inner
EOF
cat > paths.sh <<'EOF'
. sub/inner.sh
. ./sub/inner.sh
. "$PWD/sub/inner.sh"
PATH=$PWD/sub:$PATH
. inner.sh
EOF
run "relative, dot slash, absolute, and PATH lookup" \
    "$BIN" --mood bash paths.sh

cat > heredoc.sh <<'EOF'
echo before
cat <<EOT
first line
value $((1/0))
EOT
echo after
EOF
run "arithmetic error in a here-document" "$BIN" --mood bash heredoc.sh

cat > multiline.sh <<'EOF'
echo one \
  two \
  $(missing_multiline) \
  three
echo next
EOF
run "failure on a continuation line" "$BIN" --mood bash multiline.sh

cat > after_body.sh <<'EOF'
f()
{
  echo 1
  echo 2
  echo 3
  echo 4
  echo 5
}
f > /dev/null
missing_after_body
EOF
run "line after a long function body" "$BIN" --mood bash after_body.sh

printf 'echo crlf\r\nmissing_crlf\r\nf()\r\n{\r\n  missing_in_crlf\r\n}\r\n' \
    > crlf_lib.sh
printf '. ./crlf_lib.sh\r\nf\r\n' > crlf_use.sh
run "source file with CRLF line endings" "$BIN" --mood bash crlf_use.sh

cat > trap_lib.sh <<'EOF'
# trap library
cleanup()
{
  echo cleaning
  missing_cleanup
}
trap cleanup EXIT
EOF
cat > use_trap_lib.sh <<'EOF'
. ./trap_lib.sh
echo working
EOF
run "trap set in a sourced file fires at exit" \
    "$BIN" --mood bash use_trap_lib.sh

cat > trap_exit.sh <<'EOF'

trap 'missing_exit_action' EXIT

echo working
EOF
run "trap on EXIT" "$BIN" --mood bash trap_exit.sh

cat > trap_signal.sh <<'EOF'
trap 'missing_signal_action' USR1
kill -USR1 $$
echo after-signal
EOF
run "trap on a signal" "$BIN" --mood bash trap_signal.sh

cat > trap_err.sh <<'EOF'
trap 'missing_err_action' ERR
false
echo after-err
EOF
run "trap on ERR" "$BIN" --mood bash trap_err.sh

cat > trap_debug.sh <<'EOF'
trap 'missing_debug_action' DEBUG
echo debugged
trap - DEBUG
EOF
run "trap on DEBUG" "$BIN" --mood bash trap_debug.sh

cat > trap_return.sh <<'EOF'
f()
{
  trap 'missing_return_action' RETURN
  echo in-f
}
f
trap - RETURN
EOF
run "trap on RETURN" "$BIN" --mood bash trap_return.sh

cat > trap_function.sh <<'EOF'
inner_failure()
{
  missing_in_function
}
trap inner_failure EXIT
echo working
EOF
run "trap action calling a function that fails" \
    "$BIN" --mood bash trap_function.sh

cat > trap_in_function.sh <<'EOF'
install()
{
  trap 'missing_installed' EXIT
}
install
echo working
EOF
run "trap defined inside a function" "$BIN" --mood bash trap_in_function.sh

cat > recursion.sh <<'EOF'
down()
{
  if [ "$1" -gt 0 ]; then
    down $(($1 - 1))
  else
    missing_bottom
  fi
}
down 4
EOF
run "recursion collapses repeated frames" "$BIN" --mood bash recursion.sh

cat > ping.sh <<'EOF'
ping()
{
  pong "$@"
}
EOF
cat > alternate.sh <<'EOF'
pong()
{
  ping_end "$@"
}
ping_end()
{
  missing_alternate
}
. ./ping.sh
ping 1
EOF
run "frames alternate between two files" "$BIN" --mood bash alternate.sh

cat > constructs.sh <<'EOF'
value=$(missing_substitution)
cat <(missing_process)
eval 'missing_eval'
eval 'eval "missing_nested_eval"'
EOF
run "substitution, process substitution, and eval" \
    "$BIN" --mood bash constructs.sh

cat > combined.sh <<'EOF'
. ./helper_lib.sh
trap build EXIT
echo working
EOF
run "function from a sourced file called by a trap" \
    "$BIN" --mood bash combined.sh

cat > nested_eval.sh <<'EOF'
inner='eval "echo \$((1/0))"'
eval "$inner"
EOF
run "nested eval, bash mood" "$BIN" --mood bash nested_eval.sh
run "nested eval, sh mood" "$BIN" --mood sh nested_eval.sh
run "nested eval, kosh mood" "$BIN" --mood kosh nested_eval.sh

printf 'one=$(echo $((1/0)))\ntwo=`echo $((2/0))`\nthree=$(echo $(echo $((3/0))))\n' \
    > substitutions.sh
run "arithmetic errors in every substitution form" \
    "$BIN" --mood bash substitutions.sh

cat > warnings.sh <<'EOF'
#!/bin/bash
files=$(echo *.txt)
cat <(echo *.nope)
x=`echo *.back`
cat <<EOT
$(echo *.zip)
EOT
EOF
run "glob warnings from inside substitutions and here-documents" \
    "$BIN" -WWW warnings.sh

cat > subshell_function.sh <<'EOF'
c()
{
  echo $((1/0))
}
( c )
EOF
run "function called from a subshell reports once" \
    "$BIN" --mood bash subshell_function.sh

cat > subshell_nested.sh <<'EOF'
. ./helper_lib.sh
trap '( build )' EXIT
echo working
EOF
run "sourced function called by a trap inside a subshell" \
    "$BIN" --mood bash subshell_nested.sh

cat > pipeline.sh <<'EOF'
c()
{
  echo $((1/0))
}
echo one | c | cat
EOF
run "function in a pipeline stage" "$BIN" --mood bash pipeline.sh

cat > bash_c_string.sh <<'EOF'
eval "$(printf 'echo first\necho $((1/0))\n')"
EOF
run "multi-line eval text" "$BIN" --mood bash bash_c_string.sh

printf 'echo first\nmissing_first\n' > first.sh
printf '== a script with arguments carries no command line frame\n'
out=$("$BIN" --mood bash first.sh second.sh extra 2>&1)
printf '%s\n' "$out" | sed "s|$d|DIR|g" | "$BIN_DIR/invoke-normalize-trace" "$BIN"

printf '== two command strings carry the command line frame\n'
out=$("$BIN" --mood bash -c 'echo $((1/0))' -c 'echo $((2/0))' 2>&1)
printf '%s\n' "$out" | sed "s|$d|DIR|g" | "$BIN_DIR/invoke-normalize-trace" "$BIN"
