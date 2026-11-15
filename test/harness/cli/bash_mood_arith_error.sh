unset KOSH_FLAGS
echo "== bash mood reports an arithmetic error and goes on to the next line, matching bash:"
"$BIN" --mood bash -c 'echo before
echo $((1/0))
echo after'
echo "rc=$?"
echo "== the strict default mood aborts the run on an arithmetic error:"
"$BIN" -c 'echo $((1/0)); echo after'
echo "rc=$?"
echo "== the strict default mood aborts on an unset arithmetic operand:"
"$BIN" --mood kosh -c 'echo before; echo $((nope + 1)); echo not-reached'
echo "rc=$?"
echo "== bash mood reports a stray token after an operand before evaluating:"
"$BIN" --mood bash -c 'n=src/Koshkit.cpp; echo $((n-2))' 2>&1 | grep -c "Unexpected '.cpp' after the expression"
echo "== bash mood soft errors keep running:"
echo "-- a soft builtin error carries a located caret"
"$BIN" --mood bash -c 'builtin nosuchbuiltin'
echo "rc=$?"
echo "-- the run keeps going past the soft error"
"$BIN" --mood bash -c 'builtin nosuchbuiltin; echo after'
echo "-- a thrown builtin error is located and soft too"
"$BIN" --mood bash -c 'read -Z; echo after'
echo "== a wrapped zero operand keeps its source expression:"
"$BIN" --mood bash -c 'echo $((12 / 18446744073709551616))
echo after'
echo "rc=$?"
echo "== expanded arithmetic keeps the outer source location:"
"$BIN" --mood bash -c 'expression="12 / 0"; echo $(( $expression ))
echo after'
echo "rc=$?"

# Runtime failure must preserve preceding output; static rejection runs nothing.
directory=$(mktemp -d) || exit 1
trap '"$BIN_DIR/invoke-koshkit" rm -rf -- "$directory"' EXIT
runtime_source='echo before; value=0; echo "$((1 / value))"; echo after'
prerun_source='echo before; x=1; echo $x; echo after'
for input in command file; do
  for phase in runtime prerun; do
    case "$phase" in
      runtime) source=$runtime_source; diagnostic='Division by zero' ;;
      prerun) source=$prerun_source; diagnostic='SC2086' ;;
    esac
    printf '%s\n' '#!/bin/env kosh' "$source" > "$directory/source.kosh"
    for output in pipe file; do
      if [ "$output" = pipe ]; then
        (
          if [ "$input" = command ]; then
            "$BIN" -c "$source"
          else
            "$BIN" "$directory/source.kosh"
          fi
          printf '%s\n' "$?" > "$directory/status"
        ) 2> "$directory/error" | cat > "$directory/output"
      else
        if [ "$input" = command ]; then
          "$BIN" -c "$source" > "$directory/output" 2> "$directory/error"
        else
          "$BIN" "$directory/source.kosh" > "$directory/output" 2> "$directory/error"
        fi
        printf '%s\n' "$?" > "$directory/status"
      fi
      result=broken
      if [ "$(cat "$directory/status")" = 1 ] &&
          grep -q "$diagnostic" "$directory/error"; then
        if [ "$phase" = runtime ] &&
            [ "$(cat "$directory/output")" = before ]; then
          result=ok
        elif [ "$phase" = prerun ] && [ ! -s "$directory/output" ]; then
          result=ok
        fi
      fi
      printf 'fatal-%s-%s-%s=%s\n' "$phase" "$input" "$output" "$result"
    done
  done
done
