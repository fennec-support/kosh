unset KOSH_FLAGS

starting_directory=$PWD
d=$(mktemp -d) || exit 1
trap 'cd "$starting_directory" && [ -n "$d" ] && "$TEST_KOSHKIT" rm -rf "$d"' EXIT

printf '#!/bin/sh\nprintf "launched\\n"\n' > "$d/program"
chmod +x "$d/program"
ln -s program "$d/link"
mkdir "$d/directory"
cd "$d" || exit 1

run_rejected_file() {
    label=$1
    source=$2
    output=$("$BIN" -c "$source" 2>&1)
    status=$?
    echo "--- $label ---"
    printf '%s\n' "$output" | grep -F 'This file is not a directory.'
    case "$output" in
    *launched*) echo launched ;;
    *) echo refused ;;
    esac
    echo "rc=$status"
}

run_rejected_file plain './program/'
run_rejected_file exec 'exec ./program/'
run_rejected_file timeout 'koshkit timeout 1 ./program/'
run_rejected_file timeout-pipeline \
    'set -o pipefail; koshkit timeout 1 ./program/ | koshkit cat'
run_rejected_file command 'command ./program/'
run_rejected_file env 'koshkit env ./program/'
run_rejected_file symlink './link/'
run_rejected_file pipeline 'set -o pipefail; ./program/ | koshkit cat'

echo '--- directory path changes directory ---'
output=$("$BIN" --mood sh -c './directory/; status=$?; printf "directory=%s status=%s\n" "${PWD##*/}" "$status"' 2>&1)
case "$output" in
*'directory=directory status=0'*) echo directory-changed ;;
*) echo directory-error ;;
esac

echo '--- missing remains missing ---'
"$BIN" --mood sh -c './missing/' >/dev/null 2>&1
echo "rc=$?"
