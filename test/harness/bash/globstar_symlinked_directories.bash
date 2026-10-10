#!/bin/bash
# With no prefix, a later pattern after ** does not look inside a symlinked
# directory, while a literal prefix lets it, and ** itself still matches the
# symlink, all as in bash.
work_dir=$(mktemp -d)
cd "$work_dir" || exit 1
mkdir -p d/real/sub target
touch d/real/x d/real/sub/y target/inside
ln -s ../target d/l
ln -s ../../target d/real/l2
shopt -s globstar
echo **/*
echo d/**/*
echo d/real/**/*
echo **/
echo ./**/*
cd / || exit 1
rm -r "$work_dir"
