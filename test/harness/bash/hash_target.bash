#!/bin/bash

# hash -t prints the remembered location of each name, with the name and a
# tab ahead of it when several names are given, and fails for a name that is
# not remembered.

hash -r
ls_path=$(command -v ls)
hash -t; echo "none $?"
hash -p "$ls_path" first second
hash -t first; echo "one $?"
hash -t first second; echo "two $?"
hash -t first missing; echo "mixed $?"
hash -t missing; echo "missing $?"
hash -t ./first; echo "slash $?"
hash -rt first; echo "reset $?"
hash -tr; echo "flags $?"
