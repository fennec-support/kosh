#!/bin/bash
time for f in 1 2 3; do echo "$f"; done
time if true; then echo yes; fi
time while false; do echo no; done
time -p echo posix
echo done
! true
echo "neg-true=$?"
! false
echo "neg-false=$?"
! ! false
echo "double-neg-false=$?"
! ! ! true
echo "triple-neg-true=$?"


flag_string=$-
echo "${flag_string//[cI]/}"
set -f
flag_string=$-
echo "${flag_string//[cI]/}"
set +f
set -u
flag_string=$-
echo "${flag_string//[cI]/}"
