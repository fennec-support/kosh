#!/bin/bash
# Bash BASH_ALIASES behavior, checked byte-for-byte against bash. This covers
# live lookups, element and scalar writes, element and whole-array unsets, alias
# removal, alias preservation after whole-array unset, and an ordinary
# associative array created by redeclaring the disconnected name. An alias body
# is read as shell source.
alias first='echo one'
alias second='echo two'
printf 'first=%s second=%s count=%s\n' \
  "${BASH_ALIASES[first]}" "${BASH_ALIASES[second]}" \
  "${#BASH_ALIASES[@]}"

BASH_ALIASES[third]='echo three'
alias third >/dev/null
printf 'third=%s alias-status=%s\n' "${BASH_ALIASES[third]}" "$?"
BASH_ALIASES[first]='echo changed'
printf 'changed=%s\n' "${BASH_ALIASES[first]}"

unset 'BASH_ALIASES[first]'
printf 'element-unset=%s\n' "${BASH_ALIASES[first]}"
BASH_ALIASES=()
printf 'empty-assignment-count=%s\n' "${#BASH_ALIASES[@]}"
unalias second
printf 'unalias=%s count=%s\n' "${BASH_ALIASES[second]-missing}" \
  "${#BASH_ALIASES[@]}"
BASH_ALIASES=zero
printf 'scalar=%s\n' "${BASH_ALIASES[0]}"

unset BASH_ALIASES
printf 'whole-unset=%s\n' "${BASH_ALIASES[third]-missing}"
alias third >/dev/null
printf 'alias-survives=%s\n' "$?"
declare -A BASH_ALIASES
BASH_ALIASES[ordinary]='echo ordinary'
alias ordinary >/dev/null 2>&1
printf 'ordinary-alias-status=%s\n' "$?"
alias live='echo live'
printf 'ordinary-live=%s ordinary-value=%s\n' \
  "${BASH_ALIASES[live]-missing}" "${BASH_ALIASES[ordinary]}"

# An alias body is read as shell source: a pipeline, a list, quotes, and
# expansions in it work, an alias does not expand in its own body, and a body
# that ends in a blank also replaces the next word.
shopt -s expand_aliases
alias piped_alias='echo a | tr a b'
alias listed_alias='echo first; echo second'
alias quoted_alias='echo "two  spaces"'
alias expanded_alias='echo "$ALIAS_VALUE"'
alias self_alias='echo self'
alias chained_alias=self_alias
alias blank_alias='echo '
alias word_alias=replaced
piped_alias
listed_alias
quoted_alias
ALIAS_VALUE=value
expanded_alias
chained_alias
blank_alias word_alias word_alias
piped_alias | tr b B
alias chain_head_alias='chain_tail_alias '
alias chain_tail_alias=echo
alias cycle_one_alias=cycle_two_alias
alias cycle_two_alias=cycle_one_alias
blank_alias chain_head_alias chain_head_alias
blank_alias cycle_one_alias
