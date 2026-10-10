unset KOSH_FLAGS KOSHCONF
# Options that change how a script runs are semantic, so KOSHCONF never carries
# them to a child shell, and autocd changes the directory only in an
# interactive shell, as in bash.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir -p "$work/sub" "$work/config/kosh"
cd "$work" || exit 1
export HOME="$work" XDG_CONFIG_HOME="$work/config"

echo "== a script does not change into a directory named as a command:"
printf 'sub\necho "status=$?"\n' >script.sh
"$BIN" --no-diagnostics script.sh 2>&1 | grep -v '^ '
"$BIN" --mood bash -c 'sub; echo "status=$?"; pwd' 2>&1 |
  grep -v '^ ' | sed "s|$work|WORK|"

echo "== an interactive shell does:"
printf 'sub\npwd\n' >.bashrc
printf 'mood=bash\n' >config/kosh/kosh.conf
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | sed "s|$work|WORK|"
"$BIN_DIR/invoke-koshkit" rm -- .bashrc config/kosh/kosh.conf

echo "== KOSHCONF carries no semantic option:"
printf '#!/bin/sh\necho "${KOSH_VERSION:-another shell}"\n' >shebang.sh
chmod +x shebang.sh
"$BIN" -c 'for name in interpreter.mimic_shebang \
  debug.report_every_exit_code interpreter.resolve_koshkit_applets_as_commands \
  editor.auto_close_brackets_and_quotes; do
  koshconf set "$name" on
done
koshconf set optimizer.analyze_before_running off
env -u KOSH_ANALYSIS KOSHCONF="$KOSHCONF" "$1" -c "koshconf list |
  grep -e ^interpreter.mimic -e ^debug.report \
    -e ^optimizer.analyze -e ^interpreter.resolve_koshkit \
    -e ^editor.auto_close
./shebang.sh" 2>&1' _ "$BIN" 2>/dev/null
