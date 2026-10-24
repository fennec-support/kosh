#!/bin/bash
# The patsub_replacement shopt, checked byte-for-byte against bash. It is on by
# default. With it on, an unquoted & in the replacement of ${v/pat/rep} inserts
# the matched text. A quoted or backslash-escaped & stays literal, a & that
# comes from an unquoted expansion is active, and a backslash that comes from
# an unquoted expansion quotes a following & or backslash. With the option off
# every & is literal. An empty pattern is a no-op, and the anchored # and %
# forms still splice.

v=abc
shopt patsub_replacement
echo "plain:${v/b/[&]}:${v//b/&&}:${v/#a/&-}:${v/%c/-&}:${v/b/x&y&z}"
echo "quoted:${v/b/\&}:${v/b/"&"}:${v/b/'&'}:${v/b/$'&'}:${v/b/"[&]"}"
echo "backslash:${v/b/\\&}:${v/b/\\\&}:${v/b/\\}:${v/b/a\&b}:${v/b/\n}"
r='&'
echo "variable:${v/b/$r}:${v/b/"$r"}:${v/b/<$r>}"
r='\&'
echo "escaped-variable:${v/b/$r}:${v/b/"$r"}"
r='\\&'
echo "double-escaped-variable:${v/b/$r}"
echo "substitution:${v/b/$(echo '&')}:${v/b/`echo '&'`}"
echo "empty-match:${v/#/&-}:${v/%/&-}:${v//b/}"
a=(abc bbb)
echo "array:${a[@]/b/<&>}"
set -- abc bbb
echo "positional:${@/b/<&>}:${*//b/<&>}"
w=a/b
echo "slash:${w/\//[&]}:${w//\//&&}"
amp=hello
echo "match:${amp/l/[&]}:${amp//l/<&>}:${amp/e/\&}"
dotted=a.b.c
echo "dot:${dotted//./[&]}"

shopt -u patsub_replacement
shopt patsub_replacement
echo "off:${v/b/[&]}:${v/b/\&}:${v//b/&&}"
r='&'
echo "off-variable:${v/b/$r}"
shopt -s patsub_replacement
echo "on-again:${v/b/[&]}"
shopt -p patsub_replacement

v=ab; e=
printf 'replace-all=[%s]\n' "${v//$e/-}"
printf 'replace-one=[%s]\n' "${v/$e/-}"
w=hi
printf 'prefix=[%s]\n' "${w/#/X}"
printf 'suffix=[%s]\n' "${w/%/Z}"

# A slash right after the // operator belongs to the pattern.
s=a/b/c
printf 'slash-pattern=[%s]\n' "${s////|}"
printf 'slash-delete=[%s]\n' "${s///}"
printf 'slash-pattern-delete=[%s]\n' "${s///x/y}"
printf 'slash-escaped=[%s]\n' "${s//\//|}"
printf 'slash-empty-pattern=[%s]\n' "${s//}"
