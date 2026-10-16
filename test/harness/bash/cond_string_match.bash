#!/bin/bash
# Bash [[ ]] string comparison and pattern matching, checked byte-for-byte
# against bash. Covers = == != with ASCII operands, the < and > collation order,
# an unquoted right side that matches as a glob, and a quoted right side whose
# operand equals it exactly. A reversed bracket range matches nothing, and a
# hyphen after a range is literal.
[[ apple = apple ]] && echo single-eq
[[ apple == apple ]] && echo double-eq
[[ apple != orange ]] && echo neq
[[ aaa < bbb ]] && echo lt-str
[[ zzz > aaa ]] && echo gt-str
[[ abc == a*c ]] && echo glob-star
[[ abc == a?c ]] && echo glob-question
y="a*c"
[[ $y == "a*c" ]] && echo quoted-literal-match
[[ "a*c" == "a*c" ]] && echo quoted-both
v=hello.txt
[[ $v == *.txt ]] && echo suffix-match


# Bash [[ ]] string unary tests, checked byte-for-byte against bash. Covers -z on
# an empty and a non-empty operand, -n likewise, the -v test on a set and an
# unset variable, and the negation of each.
[[ -z "" ]] && echo z-empty
[[ -z "x" ]] || echo z-nonempty-false
[[ -n "x" ]] && echo n-nonempty
[[ -n "" ]] || echo n-empty-false
[[ ! -z "x" ]] && echo not-z
[[ ! -n "" ]] && echo not-n
unset uv
[[ -v uv ]] || echo v-unset-false
sv=1
[[ -v sv ]] && echo v-set
empty=
[[ -v empty ]] && echo v-set-empty
[[ ! -v uv ]] && echo not-v-unset

[[ abc == "a*c" ]] && echo lit-yes || echo lit-no
[[ "a*c" == a*c ]] && echo gmatch-yes || echo gmatch-no
[[ hello.txt == "*.txt" ]] && echo q-yes || echo q-no
p='a*c'
[[ abc == $p ]] && echo var-yes || echo var-no
[[ abc != "a*c" ]] && echo ne-yes || echo ne-no

# Bash [[ ]] numeric comparison operators, checked byte-for-byte against bash.
# Covers the full set -eq -ne -lt -le -gt -ge and a combination that mixes a
# numeric test with a string test under && and ||.
[[ 5 -eq 5 ]] && echo eq
[[ 5 -ne 6 ]] && echo ne
[[ 3 -lt 4 ]] && echo lt
[[ 4 -le 4 ]] && echo le
[[ 7 -gt 2 ]] && echo gt
[[ 7 -ge 7 ]] && echo ge
[[ 5 -le 4 ]] || echo not-le
[[ 9 -ge 10 ]] || echo not-ge
[[ 0 -eq 0 ]] && [[ -1 -lt 0 ]] && echo zero-and-neg
[[ abc == abc && 1 -eq 1 ]] && echo combine-and
[[ x == y || 2 -gt 1 ]] && echo combine-or
[[ ( 1 -eq 1 || 2 -eq 3 ) && abc == ab? ]] && echo paren-combine

declare -A cap=([france]=paris [japan]=tokyo)
[[ -v cap[france] ]] && echo "france set" || echo "france unset"
[[ -v cap[germany] ]] && echo "germany set" || echo "germany unset"
arr=(a b c)
[[ -v arr[1] ]] && echo "arr1 set" || echo "arr1 unset"
[[ -v arr[5] ]] && echo "arr5 set" || echo "arr5 unset"
[[ -v arr[@] ]] && echo "arr@ set" || echo "arr@ unset"
empty=()
[[ -v empty[@] ]] && echo "empty set" || echo "empty unset"
s=scalar
[[ -v s[0] ]] && echo "s0 set" || echo "s0 unset"

# Bash [[ str =~ regex ]] match outcome, checked byte-for-byte against bash. The
# regex with grouping or alternation is held in a variable, the bash-recommended
# idiom, so the conditional lexer does not split it. Covers a match, a non-match,
# anchors, a character class, negation, and a combination under && and ||.
[[ foobar =~ oba ]] && echo m-substr
[[ foobar =~ ^foo ]] && echo m-anchor
[[ foobar =~ baz ]] || echo no-match
[[ abc123 =~ [0-9]+ ]] && echo m-class
re="^(a|b)+$"
[[ ababab =~ $re ]] && echo m-alt
[[ abcXYZ =~ $re ]] || echo no-alt
[[ ! abc =~ [0-9] ]] && echo neg-class
[[ hello =~ ell && world =~ orl ]] && echo combine-and
[[ hello =~ zzz || world =~ wor ]] && echo combine-or

[[ abcdef =~ (abc)(def) ]] && echo "m=${BASH_REMATCH[0]} 1=${BASH_REMATCH[1]} 2=${BASH_REMATCH[2]}"
[[ "foo123" =~ ^([a-z]+)([0-9]+)$ ]] && echo "name=${BASH_REMATCH[1]} num=${BASH_REMATCH[2]}"
[[ cat =~ ^(cat|dog)$ ]] && echo "animal=${BASH_REMATCH[1]}"
re='(a+)(b+)'
[[ aaabb =~ $re ]] && echo "var=${BASH_REMATCH[1]}-${BASH_REMATCH[2]}"
[[ "x.y" =~ "x.y" ]] && echo "quoted-literal-ok"
[[ abcdef =~ (abc)(def) ]]; [[ xyz =~ nomatch ]]; echo "after-nomatch: ${#BASH_REMATCH[@]} [${BASH_REMATCH[1]}]"
[[ xQy =~ "x.y" ]] && echo "BAD quoted dot matched" || echo "quoted-dot-literal"
[[ xay =~ x.y ]] && echo "unquoted-dot-live"
[[ "a+b" =~ a"+"b ]] && echo "BAD quoted plus" || echo "quoted-plus-literal"
[[ "a+b" =~ a+b ]] && echo "unquoted-plus-live"
[[ '^$' =~ "^$" ]] && echo "anchors-literal"

# Bash [[ ]] file tests, checked byte-for-byte against bash. Covers -e -r -w on
# /dev/null and the negation of -f and -d there, then -e -f -s -d -r -w -x on a
# regular file, a directory, and an empty file made under a private temporary
# directory that the script removes before exit. The directory path is never
# printed, so the output stays deterministic.
[[ -e /dev/null ]] && echo dev-e
[[ -r /dev/null ]] && echo dev-r
[[ -w /dev/null ]] && echo dev-w
[[ -f /dev/null ]] || echo dev-not-f
[[ -d /dev/null ]] || echo dev-not-d
d=$(mktemp -d)
mkdir -p "$d/sub"
echo content > "$d/file"
: > "$d/empty"
chmod 0644 "$d/file"
[[ -e "$d/file" ]] && echo f-e
[[ -f "$d/file" ]] && echo f-f
[[ -s "$d/file" ]] && echo f-s
[[ -s "$d/empty" ]] || echo empty-not-s
[[ -d "$d/sub" ]] && echo d-d
[[ -f "$d/sub" ]] || echo sub-not-f
[[ -r "$d/file" ]] && echo f-r
[[ -w "$d/file" ]] && echo f-w
[[ -x "$d/file" ]] || echo f-not-x
chmod 0755 "$d/file"
[[ -x "$d/file" ]] && echo f-x
rm -rf "$d"
[[ -e "$d/file" ]] || echo gone

# Bash [[ ]] conditional command, checked byte-for-byte against bash. Covers
# a negated binary test, a quoted operand with spaces, a missing directory, a
# variable operand, a lone operand, and operands that hold arithmetic.
[[ ! a == x ]] && echo 13
[[ -d /nonexistent_dir_xyz ]] || echo 16
[[ "a b c" == "a b c" ]] && echo 17
n=42
[[ $n -eq 42 ]] && echo 19
[[ abc ]] && echo 20
[[ "" ]] || echo 21
[[ 2 -eq 1+1 ]] && echo arith-eq
[[ 5 -gt 2*2 ]] && echo arith-gt
n=7
[[ n -eq 7 ]] && echo bare-var-arith

# Bash [[ str =~ regex ]] matching, checked byte-for-byte against bash. Covers
# a variable subject, hyphens in a bare regex, an escaped dot, and a regex
# held in a variable.
v=2024-01-15
[[ $v =~ ^[0-9]+-[0-9]+-[0-9]+$ ]] && echo 6
[[ foo.bar =~ \. ]] && echo 9
ip=192.168.1.1
ipre="^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$"
[[ $ip =~ $ipre ]] && echo 11

# A [[ =~ ]] right side that joins a variable expansion with a regex group, an
# alternation, or a bracket must expand the variable and keep the metacharacters
# live, the construct sdkman's sdk use performs.
d=/usr/local
p=/usr/local/bin/foo
if [[ $p =~ ${d}/([^/]+) ]]; then echo "g1=${BASH_REMATCH[1]}"; fi
[[ cats =~ ^(cat|dog)s$ ]] && echo "alt=${BASH_REMATCH[1]}"
v=x
[[ xy =~ ${v}(y) ]] && echo "grp=${BASH_REMATCH[1]}"
[[ axb =~ a"."b ]] && echo "quoted-dot-literal-matched" || echo "quoted-dot-literal-no-match"
[[ a.b =~ a"."b ]] && echo "quoted-dot-exact-matched"
PROMPT_COMMAND='alpha; shell_session_history_check ; omega'
if [[ $PROMPT_COMMAND =~ (.*)(; *shell_session_history_check *| *shell_session_history_check *; *)(.*) ]]; then
    printf 'apple=%s|%s|%s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"
fi
[[ 'a b' =~ (a b|c) ]] && echo 'spaced-group'
[[ ab =~ a(b) && ab == ab ]] && echo 'and-boundary'
[[ ab == ab && ( ab =~ a(b) ) ]] && echo 'paren-boundary'

# Edge cases found by code review, checked byte-for-byte against bash. The [[ ]]
# operators short-circuit so a bad dead branch does not error.
[[ 0 -eq 0 || x -eq y ]] && echo or-shortcircuit
[[ 1 -eq 2 && x -eq y ]] || echo and-shortcircuit
[[ -n "" && bad =~ "(" ]] || echo regex-dead-branch

# bash accepts == as a synonym for = in test and [, so the bash mood does too.
[ foo == foo ] && echo bracket-eq
[ foo == bar ] || echo bracket-neq
test foo == foo && echo test-eq
test "$HOME" == "$HOME" && echo var-eq
[ abc != xyz ] && echo neq-still-works

[[ "!" ]] && echo quoted-not-is-an-operand
[[ "-n" ]] && echo quoted-unary-is-an-operand
eval '[[ x "==" x ]]'
echo "quoted-binary-status=$?"

# The [[ -v ]] subscript of an expanded operand is not expanded again: an
# associative array takes it as a literal key, and an indexed array errors.
declare -A km=(["\$key"]=1 [a]=2); key=a
[[ -v "km[\$key]" ]] && echo literal-key-set || echo literal-key-unset
declare -A kn=([a]=2)
[[ -v "kn[\$key]" ]] && echo expanded-key-set || echo expanded-key-unset
ka=(x y); ki=1
eval '[[ -v "ka[\$ki]" ]]' 2>/dev/null
echo "indexed-dollar-status=$?"
[[ -v ka[ki] ]] && echo indexed-arith-set

case c in
    [c-a]) echo reversed-range-match ;;
    *) echo reversed-range-miss ;;
esac
case - in
    [a-b-c]) echo hyphen-match ;;
    *) echo hyphen-miss ;;
esac
