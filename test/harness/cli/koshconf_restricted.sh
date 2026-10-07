unset KOSH_FLAGS KOSHCONF
# A restricted shell refuses every koshconf form that changes a setting or
# writes a file, keeps get and list, and ignores an inherited KOSHCONF blob.
config=$(mktemp -d)
trap '[ -n "$config" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$config"' EXIT
export XDG_CONFIG_HOME="$config"

for form in 'koshconf create kosh' 'koshconf create --force bash' \
  'koshconf set editor.auto_pair on --persist' \
  'koshconf set editor.auto_pair on' 'koshconf set mood bash' \
  'koshconf load BQEB'
do
  echo "== kosh -r: $form"
  "$BIN" -r -c "$form; koshconf get editor.auto_pair"
  echo "rc=$?"
done
echo "== set -r refuses a later change:"
"$BIN" -c 'set -r; koshconf set editor.auto_pair on'
echo "rc=$?"
echo "== get and list stay available:"
"$BIN" -r -c 'koshconf get mood; koshconf list | grep -c .' | sed 's/^[0-9][0-9]*$/COUNT/'
echo "== no configuration file was written:"
[ -e "$config/kosh" ] && echo written || echo absent
echo "== an inherited KOSHCONF is ignored and removed:"
KOSHCONF=BQEB "$BIN" -r -c 'koshconf get editor.auto_pair; echo "[${KOSHCONF-unset}]"'
echo "rc=$?"
