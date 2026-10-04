#!/bin/bash

# The words of an asynchronous function call are expanded exactly once. A
# command substitution in an argument appends one line to a counter file each
# time it runs, so the line count shows how many times the call expanded it.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

show() {
  echo "args=$# first=$1 second=$2"
}

echo substitution-argument
show "$(echo hit >> "$dir/count"; echo value)" tail &
wait "$!"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo substitution-with-redirection
: > "$dir/count"
show "$(echo hit >> "$dir/count"; echo value)" tail > "$dir/out" &
wait "$!"
cat "$dir/out"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo prefix-assignment
: > "$dir/count"
mark() {
  echo "mark=$MARK first=$1"
}
MARK=set mark "$(echo hit >> "$dir/count"; echo value)" &
wait "$!"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo expanded-function-name
: > "$dir/count"
fn=show
$fn "$(echo hit >> "$dir/count"; echo value)" tail &
wait "$!"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo expansion-done
