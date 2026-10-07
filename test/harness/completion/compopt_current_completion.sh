# Inside a running completion function, compopt with no name changes the
# options of the current completion only: -o plusdirs and -o dirnames add the
# directories after the function, +o turns an option of the spec off for this
# completion, and the registered spec keeps its options. With no option it
# prints the current options under the command name. A named compopt inside
# the function changes the registered spec. The probe runs in a temp directory
# holding one directory and one file.
dir=$(mktemp -d)
mkdir "$dir/probe_dir"
: > "$dir/probe_file"

complete_in()
{
  "$BIN" -c "cd '$dir' || exit; $1" --debug-complete-at "$2" </dev/null
}

echo "== compopt -o plusdirs adds the directories after the reply:"
complete_in "_f(){ COMPREPLY=(probe_word); compopt -o plusdirs; }; complete -F _f cp" 'cp pro'
echo "== compopt -o dirnames adds the directories to an empty reply:"
complete_in "_f(){ compopt -o dirnames; }; complete -F _f cp" 'cp pro'
echo "== compopt +o plusdirs drops the directories of the spec:"
complete_in "_f(){ COMPREPLY=(probe_word); compopt +o plusdirs; }; complete -o plusdirs -F _f cp" 'cp pro'
echo "== compopt prints the current options under the command name:"
complete_in "_f(){ compopt -o nospace; compopt +o filenames; compopt > opts; }; complete -o filenames -F _f cp" 'cp pro'
cat "$dir/opts"
echo "== the registered spec keeps its options:"
complete_in "_f(){ compopt -o nospace; compopt +o filenames; compopt cp > opts; }; complete -o filenames -F _f cp" 'cp pro'
cat "$dir/opts"
echo "== a named compopt inside the function changes the spec:"
complete_in "_f(){ compopt -o nospace cp; compopt cp > opts; }; complete -F _f cp" 'cp pro'
cat "$dir/opts"
test -n "$dir" && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"
