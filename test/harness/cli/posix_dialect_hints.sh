unset KOSH_FLAGS
# POSIX mode reports the owning dialect when a bashism trips its parse, and
# the exit status is the usage status.
for source in 'true |& cat' \
  'for ((i=0;i<2;i++)); do echo $i; done' \
  'read -r x <<< hi' \
  'echo ${ echo body; }'; do
  out=$("$BIN" --mood sh -c "$source" 2>&1)
  rc=$?
  printf '%s\n' "$out" | head -1
  echo "rc=$rc"
done
# The default mood accepts the here-string.
"$BIN" -c 'read -r x <<< "hello words"; echo "default=$x"'
