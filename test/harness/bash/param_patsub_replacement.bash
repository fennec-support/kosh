#!/bin/bash
# The patsub_replacement shopt, checked byte-for-byte against bash. It is on by
# default. With it on, an unquoted & in the replacement of ${v/pat/rep} inserts
# the matched text. A quoted or backslash-escaped & stays literal, a & that
# comes from an unquoted expansion is active, and a backslash that comes from
# an unquoted expansion quotes a following & or backslash. With the option off
# every & is literal.

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

shopt -u patsub_replacement
shopt patsub_replacement
echo "off:${v/b/[&]}:${v/b/\&}:${v//b/&&}"
r='&'
echo "off-variable:${v/b/$r}"
shopt -s patsub_replacement
echo "on-again:${v/b/[&]}"
shopt -p patsub_replacement
