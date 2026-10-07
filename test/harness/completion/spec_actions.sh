# A completion spec offers the candidates of its actions and glob the way
# compgen lists them, then its word list, and applies -X, -P, and -S to the
# whole set. Directory actions, the glob, dirnames, and plusdirs end each
# directory with a slash. complete -p prints the spec back in the bash order,
# with an argument given as an empty string still printed, and the printed
# line registers the same spec again. The probe runs in a temp
# directory holding one directory with a subdirectory and two files.
dir=$(mktemp -d)
mkdir -p "$dir/real_dir/sub"
: > "$dir/readme.txt"
: > "$dir/notes.txt"
echo "== -d offers only directories:"
"$BIN" -c "cd '$dir' || exit; complete -d cp" --debug-complete-at 'cp rea' </dev/null
echo "== -f offers files and marks directories:"
"$BIN" -c "cd '$dir' || exit; complete -f cp" --debug-complete-at 'cp rea' </dev/null
echo "== -A directory descends into a directory:"
"$BIN" -c "cd '$dir' || exit; complete -A directory cp" --debug-complete-at 'cp real_dir/' </dev/null
echo "== -k offers keywords:"
"$BIN" -c "complete -k cp" --debug-complete-at 'cp el' </dev/null
echo "== -b offers builtins:"
"$BIN" -c "complete -b cp" --debug-complete-at 'cp compo' </dev/null
echo "== -v offers shell variables:"
"$BIN" -c "probe_var_one=1 probe_var_two=2; complete -v cp" --debug-complete-at 'cp probe_v' </dev/null
echo "== -G offers the glob matches:"
"$BIN" -c "cd '$dir' || exit; complete -G '*.txt' cp" --debug-complete-at 'cp ' </dev/null
echo "== -X removes matching candidates:"
"$BIN" -c "complete -W 'alpha alpine beta' -X 'alpi*' cp" --debug-complete-at 'cp al' </dev/null
echo "== -X with ! keeps only matching candidates:"
"$BIN" -c "complete -W 'alpha alpine beta' -X '!alpi*' cp" --debug-complete-at 'cp al' </dev/null
echo "== -X with & matches the completion word:"
"$BIN" -c "complete -W 'al alpha' -X '&' cp" --debug-complete-at 'cp al' </dev/null
echo "== -P and -S wrap each candidate:"
"$BIN" -c "complete -W 'alpha alpine' -P pre_ -S _suf cp" --debug-complete-at 'cp al' </dev/null
echo "== -W and -A directory combine:"
"$BIN" -c "cd '$dir' || exit; complete -W 'real_word' -A directory cp" --debug-complete-at 'cp rea' </dev/null
echo "== -o dirnames adds directories when nothing matched:"
"$BIN" -c "cd '$dir' || exit; complete -W 'zzz' -o dirnames cp" --debug-complete-at 'cp rea' </dev/null
echo "== -o dirnames adds nothing beside a match:"
"$BIN" -c "cd '$dir' || exit; complete -W 'real_word' -o dirnames cp" --debug-complete-at 'cp rea' </dev/null
echo "== -o plusdirs adds directories beside a match:"
"$BIN" -c "cd '$dir' || exit; complete -W 'real_word' -o plusdirs cp" --debug-complete-at 'cp rea' </dev/null
echo "== complete -p prints the bash order and round-trips:"
"$BIN" -c "complete -o nospace -o filenames -W 'x y' -d -A function -G '*.c' -P pre -S suf -X '!*.h' -F fn foo
complete -A arrayvar -A setopt -b -A export bar
complete -o dirnames -o plusdirs -A directory -A alias -c -k -v -A signal -A shopt baz
printed=\$(complete -p foo bar baz)
echo \"\$printed\"
eval \"\$printed\"
[ \"\$(complete -p foo bar baz)\" = \"\$printed\" ] && echo round-trip-ok"
echo "== complete -p prints an empty argument and round-trips:"
"$BIN" -c "complete -G '' -W '' -P '' -S '' -X '' -C '' foo
printed=\$(complete -p foo)
echo \"\$printed\"
complete -r foo
eval \"\$printed\"
[ \"\$(complete -p foo)\" = \"\$printed\" ] && echo round-trip-ok"
echo "== complete -p prints an empty function name bare:"
"$BIN" -c "complete -F '' foo; complete -p foo"
rm -rf "$dir"
