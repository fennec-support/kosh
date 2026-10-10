unset KOSH_FLAGS KOSHCONF ENV BASH_ENV
export SHELL=kept-shell
# A restricted shell refuses every write that reaches a restricted variable
# through a name reference, and refuses to make a restricted name a reference.
# Each form runs in its own shell, and PATH, SHELL, and ENV keep their values.
# MSYS turns SHELL into an absolute Windows path for a native child, so only
# the part after the last separator is compared.
for form in 'declare -n r=PATH; r=/tmp' 'declare -n r=PATH; r+=:/tmp' \
  'declare -n r=PATH; read -r r <<<"/tmp"' \
  'declare -n r=PATH; printf -v r %s /tmp' 'declare -n r=PATH; unset r' \
  'declare -n r=PATH; export r=/tmp' 'declare -n r=SHELL; r[0]=/tmp' \
  'declare -n r=ENV; r=(/tmp)' 'declare -n r=BASH_ENV; r=/tmp' \
  'f() { local -n r=PATH; r=/tmp; }; f' 'f() { declare -gn r=SHELL; }; f; r=/tmp' \
  'declare -n r; r=PATH; r=/tmp' 'declare -n PATH=other' \
  'f() { local -n PATH=other; }; f' 'declare -n SHELL' \
  'declare -n RANDOM=PATH; RANDOM=/tmp' \
  'declare -n SECONDS=PATH; read -r SECONDS <<<"/tmp"' \
  'f() { local -n SECONDS=PATH; SECONDS=/tmp; }; f' \
  'f() { local -n PATH=PATH; PATH=/tmp; }; f' \
  'declare -n KOSHCONF=PATH' 'declare -n KOSH_IDENTITY=PATH'
do
  echo "== kosh -r --mood bash: $form"
  PATH_BEFORE=$PATH "$BIN" -r --mood bash -c "$form
echo \"after status=\$?\"
[ \"\$PATH\" = \"\$PATH_BEFORE\" ] && echo PATH kept
echo \"SHELL=\${SHELL##*[/\\\\]} ENV=\${ENV-unset} BASH_ENV=\${BASH_ENV-unset}\"" 2>&1 |
    grep -e '^after' -e 'kept' -e '^SHELL' -e 'read only' -e 'read-only'
done
