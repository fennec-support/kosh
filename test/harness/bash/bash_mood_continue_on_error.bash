#!/bin/bash
# bash fails the command and goes on after a readonly assignment, while a
# set -u read and a ${name:?} abort the run, here fenced in subshells so the
# statuses surface, the behaviors varenv exercises.
readonly xx=1
(xx=2) 2>/dev/null
echo "readonly_assign=$?"
(set -u; echo "$not_set_at_all"; echo not_reached) 2>/dev/null
echo "nounset_abort=$?"
# A set -u length of an unset array, a declared array without a value, or a
# scalar with a subscript aborts. An empty array and a missing element of a
# set array have length 0, and "${u[@]}" of an unset array expands to nothing.
(set -u; unset u; echo "${#u[@]}"; echo not_reached) 2>/dev/null
echo "nounset_unset_length=$?"
(set -u; declare -a u; echo "${#u[*]}"; echo not_reached) 2>/dev/null
echo "nounset_valueless_length=$?"
(set -u; f() { local -A u; echo "${#u[@]}"; }; f; echo not_reached) 2>/dev/null
echo "nounset_local_length=$?"
(set -u; u=x; echo "${#u[@]}"; echo not_reached) 2>/dev/null
echo "nounset_scalar_length=$?"
(set -u; unset u; echo "${#u[0]}"; echo not_reached) 2>/dev/null
echo "nounset_unset_element_length=$?"
(set -u; unset u; echo "[${u[@]}]" "[${!u[@]}]"; u=(); v=(x)
echo "${#u[@]} ${#u[1]} ${#v[1]}") 2>/dev/null
echo "nounset_empty_length=$?"
(echo "${also_missing:?gone}"; echo not_reached) 2>/dev/null
echo "report_abort=$?"
echo survived


local x 2>/dev/null && echo "set" || echo "notset rc=$?"
cd /nonexistent_dir_xyz 2>/dev/null
echo "after cd rc=$?"
unset -- 2>/dev/null
echo "continued"

# Bash defines the output of the following forms exactly. The
# PIPESTATUS array reports each stage by position, and a negative array index
# counts back from the highest set index across the sparse elements rather than
# the dense length.
true | false | true
echo "${PIPESTATUS[@]}"
a=(A B)
a[5]=z
echo "[${a[-1]}][${a[-2]}]"
b=(x y z)
echo "${b[-1]} ${b[-2]} ${b[-3]}"

true | false | true
printf 'pipeline=%s\n' "${PIPESTATUS[*]}"
true
printf 'single=%s count=%s\n' "${PIPESTATUS[*]}" "${#PIPESTATUS[@]}"
PIPESTATUS=scalar
true
printf 'scalar-reset=%s count=%s direct=%s\n' "${PIPESTATUS[*]}" \
    "${#PIPESTATUS[@]}" "$PIPESTATUS"
PIPESTATUS[8]=sparse
false
printf 'sparse=%s count=%s\n' "${PIPESTATUS[*]}" "${#PIPESTATUS[@]}"
pipe_status_function() { return 7; }
pipe_status_function
printf 'function=%s\n' "${PIPESTATUS[*]}"
substitution_result=$(printf captured)
printf 'substitution=%s result=%s\n' "${PIPESTATUS[*]}" "$substitution_result"
false
assignment=ok
printf 'assignment=%s\n' "${PIPESTATUS[*]}"
false
array_assignment=(one)
printf 'array-assignment=%s\n' "${PIPESTATUS[*]}"
(( 0 ))
printf 'arithmetic=%s\n' "${PIPESTATUS[*]}"
[[ no = yes ]]
printf 'conditional=%s\n' "${PIPESTATUS[*]}"
(exit 7)
printf 'subshell=%s\n' "${PIPESTATUS[*]}"
if false; then
    :
fi
observed=("$?" "${PIPESTATUS[*]}")
printf 'empty-if=%s pipeline=%s\n' "${observed[@]}"
while false; do
    :
done
observed=("$?" "${PIPESTATUS[*]}")
printf 'empty-while=%s pipeline=%s\n' "${observed[@]}"
for ((; 0;)); do
    :
done
observed=("$?" "${PIPESTATUS[*]}")
printf 'empty-cstyle=%s pipeline=%s\n' "${observed[@]}"
for value in; do
    :
done
observed=("$?" "${PIPESTATUS[*]}")
printf 'empty-for=%s pipeline=%s\n' "${observed[@]}"
pipe_status_definition() { :; }
observed=("$?" "${PIPESTATUS[*]}")
printf 'definition=%s pipeline=%s\n' "${observed[@]}"

case b in
    ["a"-c]) printf 'quoted-lower-range=match\n' ;;
    *) printf 'quoted-lower-range=miss\n' ;;
esac
case ^ in
    []-a]) printf 'leading-close-range=match\n' ;;
    *) printf 'leading-close-range=miss\n' ;;
esac

# unset of a read-only variable fails with status one, the same as bash, and
# the shell keeps running.
readonly x=5
unset x 2>/dev/null
echo "status=$?"
echo after
