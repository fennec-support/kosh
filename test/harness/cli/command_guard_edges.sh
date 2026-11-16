#!/bin/bash

run_probe()
{
  "$BIN" -c "$2" >/dev/null 2>&1
  printf '%s=%s\n' "$1" "$?"
}

run_probe sequence 'command -v guard_missing; guard_missing'
run_probe negated_and '! command -v guard_missing && guard_missing'
run_probe negated_if 'if ! command -v guard_missing; then guard_missing; fi'
run_probe shadowed_command 'command(){ false; }; command -v guard_missing && guard_missing'
run_probe shadowed_type 'type(){ false; }; type guard_missing && guard_missing'
run_probe shadowed_hash 'hash(){ false; }; hash guard_missing && guard_missing'
run_probe until_failure 'until command -v guard_missing; do guard_missing; break; done'
run_probe valid_or '! command -v printf || printf okay'
run_probe valid_until 'until ! command -v printf; do printf okay; break; done'
run_probe zsh_guard 'if test -n "${ZSH_VERSION+set}" && (emulate sh) >"$TEST_NULL_DEVICE" 2>&1; then emulate sh; setopt NO_GLOB_SUBST; zmodload zsh/parameter; fi'
run_probe zsh_bracket_guard 'if [ -n "${ZSH_VERSION+set}" ] && (emulate sh) >"$TEST_NULL_DEVICE" 2>&1; then emulate sh; setopt NO_GLOB_SUBST; zmodload zsh/parameter; fi'
run_probe zsh_semicolon 'test -n "${ZSH_VERSION+set}"; emulate sh'
run_probe zsh_else 'if test -n "${ZSH_VERSION+set}"; then :; else emulate sh; fi'
run_probe zsh_negated 'if ! test -n "${ZSH_VERSION+set}"; then setopt NO_GLOB_SUBST; fi'
run_probe zsh_other_variable 'if test -n "${BASH_VERSION+set}"; then zmodload zsh/parameter; fi'
run_probe zsh_shadowed_test 'test(){ true; }; if test -n "${ZSH_VERSION+set}"; then emulate sh; fi'
run_probe zsh_shadowed_bracket 'function [ { true; }; if [ -n "${ZSH_VERSION+set}" ]; then emulate sh; fi'

unset KOSH_FLAGS
directory=$(mktemp -d) || exit 1
trap '"$BIN_DIR/invoke-koshkit" rm -rf -- "$directory"' EXIT

handler_source='
command_not_found_handle() {
  printf "handler=<%s>" "$1"
  shift
  printf " <%s>" "$@"
  printf "\n"
  handler_changed=yes
  return 23
}
handler_changed=no
kosh_missing_handler_command "two words" "" "\$(literal)" "a;b" "quote'"'"'value"
printf "handler-status=%s state=%s\n" "$?" "$handler_changed"
command kosh_missing_handler_command wrapped
printf "command-status=%s\n" "$?"
kosh_missing_handler_command piped | koshkit cat
printf "pipeline-status=%s\n" "${PIPESTATUS[0]}"
kosh_missing_handler_command redirected > "$1/handler-output"
printf "redirect-status=%s\n" "$?"
koshkit cat "$1/handler-output"
command_not_found_handle() { read -r value; printf "handler-input=<%s>\n" "$value"; }
kosh_missing_handler_command <<EOF
handler heredoc
EOF
command_not_found_handle() { return 23; }
kosh_missing_handler_command &
wait "$!"
printf "background-status=%s\n" "$?"
command -v kosh_missing_handler_command
printf "query-status=%s\n" "$?"
unset -f command_not_found_handle
kosh_missing_handler_command
printf "unset-status=%s\n" "$?"
'
KOSH_WELCOME= KOSH_FAREWELL= "$BIN" --no-init-files --mood bash -i \
  -c "$handler_source" handler-test "$directory" 2> "$directory/error"
printf 'handler-default-diagnostic=%s\n' \
  "$(grep -c "was not found" "$directory/error")"

for invocation in noninteractive exec restricted; do
  case "$invocation" in
    noninteractive) options=; command=kosh_missing_handler_command ;;
    exec) options=-i; command='exec kosh_missing_handler_command' ;;
    restricted) options='-i -r'; command=./kosh_missing_handler_command ;;
  esac
  output=$(KOSH_WELCOME= KOSH_FAREWELL= "$BIN" --no-init-files --mood bash \
    $options -c "command_not_found_handle() { echo handler-called; return 23; }; $command" \
    2> "$directory/error")
  status=$?
  case "$output" in
    *handler-called*) result=broken ;;
    *) result=ok ;;
  esac
  printf 'handler-%s=%s status=%s\n' "$invocation" "$result" "$status"
done

handler_source='
command_not_found_handle() {
  printf "value=%s new=%s\n" "$value" "${new_value-unset}"
  printf "%s" "$2" | koshkit od -An -t x1
}
value=old
printf -v argument "a\r\nb"
value=new new_value=temporary kosh_missing_handler_command "$argument"
printf "after=%s new=%s\n" "$value" "${new_value-unset}"
command kosh_missing_handler_command "$argument"
'
KOSH_WELCOME= KOSH_FAREWELL= "$BIN" --no-init-files --mood bash -i \
  -c "$handler_source" 2> "$directory/error"

handler_source='
command_not_found_handle() { :; }
shopt -u dotglob
GLOBIGNORE=x kosh_missing_handler_command
shopt -q dotglob; printf "handler-dotglob=%s\n" "$?"
SECONDS=0
SECONDS=1000000 kosh_missing_handler_command
if (( SECONDS < 100 )); then echo handler-clock=preserved; fi
'
KOSH_WELCOME= KOSH_FAREWELL= "$BIN" --no-init-files --mood bash -i \
  -c "$handler_source" 2> "$directory/error"
