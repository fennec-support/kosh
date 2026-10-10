#!/bin/bash
# A pattern replacement whose pattern has a bounded length gives the same
# result as bash, the bound only skipping ends a match cannot reach, and
# @U, @L, and @u convert characters outside ASCII as ^^, ,, and ^ do.
export LC_ALL=C.UTF-8
v='héllo wörld [x] a*b ab?c'
echo "${v//ö/o}|${v//[éö]/_}|${v//\[x\]/X}|${v//\*/S}|${v//?/.}|${v//b?/Z}|${v//l*/L}"
shopt -s extglob
echo "${v//+(l)/L}|${v//@(wö|he)/_}"
shopt -u extglob
w='aaa'
echo "${w//aa/X}|${w//a?/Y}|${w/#a?/Z}|${w/%?a/Q}|${w//[[:alpha:]]/c}"
long=""
for ((i = 0; i < 20000; i++)); do long+=ab; done
replaced=${long//ab/c}
echo "long=${#replaced} ${replaced:0:3}"
x='héllo wörld'
y='Éa ÖB'
echo "${x@U}|${y@L}|${x@u}|${y@u}"
set -- éa bé
echo "${@@U}"
