unset KOSH_FLAGS KOSH_ANALYSIS
# A set that changes the analysis level exports it as KOSH_ANALYSIS, so a script
# the shell launches lints the way the parent does. A flag on the child wins,
# and a reset in the parent stops the inheritance.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/bin"
ln -s "$BIN" "$dir/bin/bash"
ln -s "$BIN" "$dir/bin/kosh"
printf '%s\n' '#!/usr/bin/env bash' 'x=$1' 'echo $x' 'echo `echo ok`' > "$dir/lint.bash"
printf '%s\n' "#!$dir/bin/kosh" 'x=$1' 'echo $x' 'echo "kosh-shebang-ran"' > "$dir/lint.kosh"
chmod +x "$dir/lint.bash" "$dir/lint.kosh"
PATH="$dir/bin:$PATH"
export PATH

# SC2005 appears with -WWW and not with -W. SC2046 appears with both. The
# unset-variable warning appears only with -WWW.
count() { grep -c "$1" || true; }

echo "plain: $("$BIN" -c "$dir/lint.bash" 2>&1 | count SC2005)"
echo "bash shebang: $("$BIN" -c "set -WWW; $dir/lint.bash" 2>&1 | count SC2005)"
echo "kosh plain: $("$BIN" -c "$dir/lint.kosh" 2>&1 | count "variable '1' is not set")"
echo "kosh shebang: $("$BIN" -c "set -WWW; $dir/lint.kosh" 2>&1 | count "variable '1' is not set")"
echo "operand: $("$BIN" -c "set -WWW; \"$BIN\" $dir/lint.bash" 2>&1 | count SC2005)"
echo "nested -c: $("$BIN" -c "set -WWW; \"$BIN\" -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
explicit_output=$("$BIN" -c "set -WWW; \"$BIN\" -W $dir/lint.bash" 2>&1)
echo "explicit -W: $(printf '%s\n' "$explicit_output" | count SC2005) $(printf '%s\n' "$explicit_output" | count SC2046)"
echo "reset: $("$BIN" -c "set -WWW; set +W; $dir/lint.bash" 2>&1 | count SC2005)"
echo "unset: $("$BIN" -c "set -WWW; set +W; echo \"[\${KOSH_ANALYSIS-}]\"" 2>&1)"
echo "mimicry: $("$BIN" -c "set -I -WWW; \"$BIN\" -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
echo "mimicry off: $("$BIN" -c "set -I -WWW; set +I; \"$BIN\" -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
printf '%s\n' 'echo {1..$n}' > "$dir/brace.sh"
brace_count() { grep -c 'SC2051' || true; }
echo "sourced plain: $("$BIN" --mood bash -c ". $dir/brace.sh" 2>&1 | brace_count)"
echo "sourced -WW: $("$BIN" --mood bash -WW -c ". $dir/brace.sh" 2>&1 | brace_count)"
echo "sourced inherited level: $(KOSH_ANALYSIS='mimicry warnings=2' "$BIN" --mood bash -c ". $dir/brace.sh" 2>&1 | brace_count)"
echo "sourced inherited mimicry only: $(KOSH_ANALYSIS=mimicry "$BIN" --mood bash -c ". $dir/brace.sh" 2>&1 | brace_count)"
echo "script plain: $("$BIN" --mood bash "$dir/brace.sh" 2>&1 | brace_count)"
echo "script -WW: $("$BIN" --mood bash -WW "$dir/brace.sh" 2>&1 | brace_count)"
echo "sourced default mood: $("$BIN" -c ". $dir/brace.sh" 2>&1 | brace_count)"
echo "explicit resync: $("$BIN" -c "set -WWW; \"$BIN\" -W -c 'echo \"\${KOSH_ANALYSIS-}\"'" 2>&1)"
