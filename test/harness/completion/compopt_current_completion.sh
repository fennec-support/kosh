# Inside a running completion function, compopt with no name changes the
# options of the current completion only: -o plusdirs and -o dirnames add the
# directories after the function, +o turns an option of the spec off for this
# completion, and the registered spec keeps its options. With no option it
# prints the current options under the command name. A named compopt inside
# the function changes the registered spec. nosort keeps the order of the
# reply, and the reply is quoted the way Bash quotes it: as file names, with
# fullquote, and never with noquote alone. The probe runs in a temp directory
# holding one directory and two files.
dir=$(mktemp -d)
mkdir "$dir/probe_dir"
: > "$dir/probe_file"
: > "$dir/spaced name"

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
echo "== nosort keeps the reply order and drops only a repeated neighbor:"
complete_in "_f(){ COMPREPLY=(zeta alpha alpha mid zeta); compopt -o nosort; }; complete -F _f cp" 'cp '
echo "== the reply is sorted and deduplicated without nosort:"
complete_in "_f(){ COMPREPLY=(zeta alpha alpha mid zeta); }; complete -F _f cp" 'cp '
echo "== a reply is quoted only as file names or with fullquote:"
complete_in "_f(){ COMPREPLY=('a b'); }; complete -F _f cp" 'cp x'
complete_in "_f(){ COMPREPLY=('a b'); }; complete -o filenames -F _f cp" 'cp x'
complete_in "_f(){ COMPREPLY=('a b'); compopt -o noquote; }; complete -o filenames -F _f cp" 'cp x'
complete_in "_f(){ COMPREPLY=('a b'); }; complete -o fullquote -F _f cp" 'cp x'
complete_in "_f(){ COMPREPLY=('a b'); }; complete -o fullquote -o noquote -F _f cp" 'cp x'
echo "== noquote leaves the fallback file names unquoted:"
complete_in "_f(){ :; }; complete -o default -F _f cp" 'cp spa'
complete_in "_f(){ :; }; complete -o default -o noquote -F _f cp" 'cp spa'
test -n "$dir" && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"
