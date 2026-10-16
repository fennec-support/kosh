#!/bin/bash

# An empty ${} reports a bad substitution and the script goes on. An empty $''
# keeps one field.

echo before
echo ${}
echo after
f() { echo "fields=$#"; }
f $''
