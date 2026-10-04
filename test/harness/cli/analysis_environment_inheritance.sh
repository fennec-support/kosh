unset KOSH_FLAGS KOSH_ANALYSIS
# A set that changes the analysis level exports it as KOSH_ANALYSIS, so a script
# the shell launches lints the way the parent does. A flag on the child wins,
# and a reset in the parent stops the inheritance.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/bin"
ln -s "$BIN" "$dir/bin/bash"
printf '%s\n' '#!/usr/bin/env bash' 'x=$1' 'echo $x' 'echo `echo ok`' > "$dir/lint.bash"
printf '%s\n' "#!$BIN" 'x=$1' 'echo $x' 'echo "kosh-shebang-ran"' > "$dir/lint.kosh"
chmod +x "$dir/lint.bash" "$dir/lint.kosh"
PATH="$dir/bin:$PATH"
export PATH

count() { grep -c 'warning' || true; }

echo "plain: $("$BIN" -c "$dir/lint.bash" 2>&1 | count)"
echo "bash shebang: $("$BIN" -c "set -WWW; $dir/lint.bash" 2>&1 | count)"
echo "kosh shebang: $("$BIN" -c "set -WWW; $dir/lint.kosh" 2>&1 | count)"
echo "operand: $("$BIN" -c "set -WWW; \"$BIN\" $dir/lint.bash" 2>&1 | count)"
echo "nested -c: $("$BIN" -c "set -WWW; \"$BIN\" -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
echo "explicit -W: $("$BIN" -c "set -WWW; \"$BIN\" -W $dir/lint.bash" 2>&1 | count)"
echo "reset: $("$BIN" -c "set -WWW; set +W; $dir/lint.bash" 2>&1 | count)"
echo "unset: $("$BIN" -c "set -WWW; set +W; echo \"[\${KOSH_ANALYSIS-}]\"" 2>&1)"
echo "mimicry: $("$BIN" -c "set -I -WWW; \"$BIN\" -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
echo "mimicry off: $("$BIN" -c "set -I -WWW; set +I; \"$BIN\" -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
echo "explicit resync: $("$BIN" -c "set -WWW; \"$BIN\" -W -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
