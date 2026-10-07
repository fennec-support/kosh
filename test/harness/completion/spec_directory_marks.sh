# A completion spec registered with -o filenames gives every reply that names a
# directory a trailing slash, for a -F function and a -W word list alike, the
# way bash does. Without the option a reply stays as the spec gave it, and
# compopt +o filenames inside the function turns the slash off again, also for
# the -d, -f, and -G candidates listed before it. The dirnames and plusdirs
# directories come after the function and keep their slash. The probe runs in a
# temp directory holding one directory and one file.
dir=$(mktemp -d)
mkdir "$dir/probe_dir"
: > "$dir/probe_file"
echo "== a plain function reply keeps its text:"
"$BIN" -c "cd '$dir' || exit; _f(){ COMPREPLY=(probe_dir probe_file); }; complete -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== -o filenames marks the directory reply:"
"$BIN" -c "cd '$dir' || exit; _f(){ COMPREPLY=(probe_dir probe_file); }; complete -o filenames -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== compopt +o filenames turns the mark off:"
"$BIN" -c "cd '$dir' || exit; _f(){ COMPREPLY=(probe_dir); compopt +o filenames; }; complete -o filenames -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== compopt +o filenames turns the -d mark off:"
"$BIN" -c "cd '$dir' || exit; _f(){ compopt +o filenames; }; complete -d -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== compopt +o filenames turns the -f mark off:"
"$BIN" -c "cd '$dir' || exit; _f(){ compopt +o filenames; }; complete -f -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== compopt +o filenames turns the -G mark off:"
"$BIN" -c "cd '$dir' || exit; _f(){ compopt +o filenames; }; complete -G 'probe_*' -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== -o dirnames marks the directories it adds after the function:"
"$BIN" -c "cd '$dir' || exit; _f(){ compopt +o filenames; }; complete -o dirnames -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== -o plusdirs marks the directories it adds after the function:"
"$BIN" -c "cd '$dir' || exit; _f(){ compopt +o filenames; }; complete -o plusdirs -F _f cp" --debug-complete-at 'cp pro' </dev/null
echo "== -o filenames marks a word list directory:"
"$BIN" -c "cd '$dir' || exit; complete -o filenames -W 'probe_dir probe_file' cp" --debug-complete-at 'cp pro' </dev/null
echo "== complete -p prints the option:"
"$BIN" -c "complete -o filenames -W 'probe_dir' cp; complete -p cp"
rm -rf "$dir"
