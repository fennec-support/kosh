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
"$BIN" --debug-complete-at 'cp rea' -c "cd '$dir' || exit; complete -d cp" </dev/null
echo "== -f offers files and marks directories:"
"$BIN" --debug-complete-at 'cp rea' -c "cd '$dir' || exit; complete -f cp" </dev/null
echo "== -A directory descends into a directory:"
"$BIN" --debug-complete-at 'cp real_dir/' -c "cd '$dir' || exit; complete -A directory cp" </dev/null
echo "== -k offers keywords:"
"$BIN" --debug-complete-at 'cp el' -c "complete -k cp" </dev/null
echo "== -b offers builtins:"
"$BIN" --debug-complete-at 'cp compo' -c "complete -b cp" </dev/null
echo "== -v offers shell variables:"
"$BIN" --debug-complete-at 'cp probe_v' -c "probe_var_one=1 probe_var_two=2; complete -v cp" </dev/null
echo "== -G offers the glob matches:"
"$BIN" --debug-complete-at 'cp ' -c "cd '$dir' || exit; complete -G '*.txt' cp" </dev/null
echo "== -X removes matching candidates:"
"$BIN" --debug-complete-at 'cp al' -c "complete -W 'alpha alpine beta' -X 'alpi*' cp" </dev/null
echo "== -X with ! keeps only matching candidates:"
"$BIN" --debug-complete-at 'cp al' -c "complete -W 'alpha alpine beta' -X '!alpi*' cp" </dev/null
echo "== -X with & matches the completion word:"
"$BIN" --debug-complete-at 'cp al' -c "complete -W 'al alpha' -X '&' cp" </dev/null
echo "== -P and -S wrap each candidate:"
"$BIN" --debug-complete-at 'cp al' -c "complete -W 'alpha alpine' -P pre_ -S _suf cp" </dev/null
echo "== -W and -A directory combine:"
"$BIN" --debug-complete-at 'cp rea' -c "cd '$dir' || exit; complete -W 'real_word' -A directory cp" </dev/null
echo "== -o dirnames adds directories when nothing matched:"
"$BIN" --debug-complete-at 'cp rea' -c "cd '$dir' || exit; complete -W 'zzz' -o dirnames cp" </dev/null
echo "== -o dirnames adds nothing beside a match:"
"$BIN" --debug-complete-at 'cp rea' -c "cd '$dir' || exit; complete -W 'real_word' -o dirnames cp" </dev/null
echo "== -o plusdirs adds directories beside a match:"
"$BIN" --debug-complete-at 'cp rea' -c "cd '$dir' || exit; complete -W 'real_word' -o plusdirs cp" </dev/null
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
