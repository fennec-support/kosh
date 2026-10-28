#!/bin/bash

# bash completion for the kosh shell.
#
# Do:
# mv kosh.bash /usr/share/bash-completion/completions/kosh
_kosh_compgen ()
{
  local candidate
  COMPREPLY=()

  while IFS= read -r candidate
  do
    COMPREPLY+=("$candidate")
  done < <(compgen "$@")
}

_kosh_complete ()
{
  local current_word previous_word
  current_word=${COMP_WORDS[COMP_CWORD]}
  previous_word=${COMP_WORDS[COMP_CWORD-1]}
  local mood_values="kosh bash sh bash-posix"
  local tab_selector_values="interactive external plain"

  case $previous_word in
    --tab-selector)
      _kosh_compgen -W "$tab_selector_values" -- "$current_word"
      return
    ;;
    -M | --mood)
      _kosh_compgen -W "$mood_values" -- "$current_word"
      return
    ;;
    -L | --init-moods)
      _kosh_compgen -W "$mood_values" -- "$current_word"
      return
    ;;
    --rcfile | --init-file)
      _kosh_compgen -f -- "$current_word"
      return
    ;;
    -o | +o)
      _kosh_compgen -A setopt -- "$current_word"
      return
    ;;
    -O | +O)
      _kosh_compgen -A shopt -- "$current_word"
      return
    ;;
    -c | --command)
      COMPREPLY=()

      return
    ;;
  esac

  local \
    long_flags="--version --short-version --help --interactive --stdin \
--command --error-exit --no-glob --one-command --verbose --xtrace --export-all \
--no-clobber --no-exec --no-unset --login --rcfile --init-file --norc \
--restricted --privileged --no-init-files --no-config --posix --mood \
--init-moods --enable-mimicry --dumb --tab-selector --lint --format --apply --as-language-server --list-diagnostics \
--no-diagnostics --no-annoying-diagnostics --no-init-diagnostics --no-traces --no-completion --no-syntax-highlighting \
--enable-koshkit --enable-extended-arithmetic \
--show-ast \
--show-optimizer-diagnostics --show-exit-code --show-all-exit-codes --show-lexed-words --show-stats --show-memory \
"
  local \
    short_flags="-V -i -s -c -e -f -t -v -x -a -C -n -u -l -r -p -o -O -M -L -I -W -WW -WWW \
-Q -T -A -N -R"
  local plus_flags="+a +C +e +f +n +p +t +u +v +x +o +O"

  if [[ $current_word == --* ]]
  then
    _kosh_compgen -W "$long_flags" -- "$current_word"
  elif [[ $current_word == +* ]]
  then
    _kosh_compgen -W "$plus_flags" -- "$current_word"
  elif [[ $current_word == -* ]]
  then
    _kosh_compgen -W "$short_flags $long_flags" -- "$current_word"
  else
    _kosh_compgen -f -- "$current_word"
  fi
}

complete -o filenames -F _kosh_complete kosh
_kosh_assimilate_complete ()
{
  local current_word=${COMP_WORDS[COMP_CWORD]}
  local previous_word=${COMP_WORDS[COMP_CWORD - 1]}
  local flags="-x --trace --ssh-command --scp-command --link-mood --install-directory -y --yes --help"

  if [[ $previous_word == --link-mood ]]
  then
    _kosh_compgen -W "bash dash sh kosh" -- "$current_word"
    return
  fi

  if [[ $previous_word == --install-directory ]]
  then
    return
  fi

  if [[ $current_word == -* ]]
  then
    _kosh_compgen -W "$flags" -- "$current_word"
    return
  fi

  if declare -F _known_hosts_real >/dev/null
  then
    _known_hosts_real "$current_word"
  fi
}

complete -F _kosh_assimilate_complete assimilate
_kosh_slurp_complete ()
{
  local current_word=${COMP_WORDS[COMP_CWORD]}

  if [[ $current_word == -* ]]
  then
    _kosh_compgen -W "-e -v -V --emacs --vi --vi-normal --help" -- "$current_word"
    return
  fi

  _kosh_compgen -f -- "$current_word"
}

complete -o filenames -F _kosh_slurp_complete slurp
_kosh_source_complete ()
{
  local current_word=${COMP_WORDS[COMP_CWORD]}
  local previous_word=${COMP_WORDS[COMP_CWORD - 1]}

  if [[ $previous_word == --mood && $COMP_CWORD -eq 2 ]]
  then
    _kosh_compgen -W "kosh bash sh bash-posix" -- "$current_word"
    return
  fi

  if [[ $current_word == -* && $COMP_CWORD -eq 1 ]]
  then
    _kosh_compgen -W "--mood" -- "$current_word"
    return
  fi

  _kosh_compgen -f -- "$current_word"
}

complete -o filenames -F _kosh_source_complete source .
_kosh_set_complete ()
{
  local current_word=${COMP_WORDS[COMP_CWORD]}
  local previous_word=${COMP_WORDS[COMP_CWORD - 1]}
  local moods="kosh bash sh bash-posix"
  local \
    option_names="allexport braceexpand emacs errexit errtrace functrace \
hashall histexpand history ignoreeof interactive-comments keyword monitor \
noclobber noexec noglob nolog notify nounset onecmd physical pipefail posix \
privileged verbose vi xtrace"
  local \
    switches="--help -o +o -M -L \
-a -b -e -f -h -k -m -n -t -u -v -x -B -C -E -H -N -P -T -A -R -W -WW -WWW -I -S -G \
+a +b +e +f +h +k +m +n +t +u +v +x +B +C +E +H +N +P +T +A +R +W +WW +WWW +I +S +G"

  case $previous_word in
    -o | +o)
      _kosh_compgen -W "$option_names" -- "$current_word"
      return
    ;;
    -M | -L)
      _kosh_compgen -W "$moods" -- "$current_word"
      return
    ;;
  esac

  _kosh_compgen -W "$switches" -- "$current_word"
}

complete -F _kosh_set_complete set

_kosh_koshconf_complete ()
{
  local current_word=${COMP_WORDS[COMP_CWORD]}
  local -a operands=()
  local word index
  for ((index = 1; index < COMP_CWORD; index++))
  do
    word=${COMP_WORDS[index]}
    [[ $word == -* ]] || operands+=("$word")
  done
  local \
    option_names="bash.completion.programmable \
bash.completion.programmable_follows_aliases \
bash.debug.debugger_support \
bash.editor.cd_by_typing_directory_name \
bash.editor.history.bang_expansion \
bash.editor.history.records_commands \
bash.editor.interactive_comments \
bash.interpreter.aliases_expand \
bash.interpreter.assignments_anywhere_in_command \
bash.interpreter.brace_expansion \
bash.interpreter.cd_resolves_symlinks \
bash.interpreter.cd_to_variable_value \
bash.interpreter.command_substitution_inherits_exit_on_failure \
bash.interpreter.echo_interprets_backslash_escapes \
bash.interpreter.exit_after_one_command \
bash.interpreter.glob_double_star_recurses \
bash.interpreter.glob_extended_patterns \
bash.interpreter.glob_ignores_case \
bash.interpreter.glob_includes_dotfiles \
bash.interpreter.glob_never_matches_dot_and_dotdot \
bash.interpreter.glob_no_match_expands_to_nothing \
bash.interpreter.glob_no_match_is_error \
bash.interpreter.local_inherits_outer_value \
bash.interpreter.local_unset_hides_outer \
bash.interpreter.login_shell \
bash.interpreter.match_ignores_case \
bash.interpreter.pattern_substitution_ampersand_is_match \
bash.interpreter.pipeline_last_stage_runs_in_shell \
bash.interpreter.posixly_correct \
bash.interpreter.privileged_mode \
bash.interpreter.restricted_shell \
bash.interpreter.shift_reports_overflow \
bash.interpreter.source_searches_path \
bash.interpreter.trap_debug_and_return_inherited_by_functions \
bash.interpreter.trap_err_inherited_by_functions \
bash.interpreter.verify_remembered_command_paths \
bash.legacy.assoc_expand_once \
bash.legacy.bash_source_fullpath \
bash.legacy.cdspell \
bash.legacy.checkjobs \
bash.legacy.checkwinsize \
bash.legacy.cmdhist \
bash.legacy.compat31 \
bash.legacy.compat32 \
bash.legacy.compat40 \
bash.legacy.compat41 \
bash.legacy.compat42 \
bash.legacy.compat43 \
bash.legacy.compat44 \
bash.legacy.complete_fullquote \
bash.legacy.direxpand \
bash.legacy.dirspell \
bash.legacy.execfail \
bash.legacy.extquote \
bash.legacy.force_fignore \
bash.legacy.globasciiranges \
bash.legacy.gnu_errfmt \
bash.legacy.histappend \
bash.legacy.histreedit \
bash.legacy.histverify \
bash.legacy.hostcomplete \
bash.legacy.huponexit \
bash.legacy.lithist \
bash.legacy.mailwarn \
bash.legacy.no_empty_cmd_completion \
bash.legacy.noexpand_translation \
bash.legacy.promptvars \
bash.legacy.varredir_close \
compatibility.dev_null \
compatibility.slash_tmp \
completion.add_space_after_completed_word \
completion.menu_style \
completion.on_tab \
debug.print_evaluation_statistics \
debug.print_lexed_word_escapes \
debug.print_memory_report_at_exit \
debug.print_syntax_tree \
debug.report_every_exit_code \
debug.report_nonzero_exit_codes \
editor.auto_close_brackets_and_quotes \
editor.base_mode \
editor.highlight_syntax_and_show_ghost_text \
editor.hints.show_command_synopsis \
editor.hints.show_live_diagnostics \
editor.history.arrow_keys_search_by_typed_prefix \
editor.history.file_path \
editor.history.max_entries \
editor.request_extended_key_reports \
editor.wrap_width \
init_moods \
interpreter.arithmetic_uses_big_numbers \
interpreter.mimic_shebang \
interpreter.resolve_koshkit_applets_as_commands \
mood \
optimizer.analyze_before_running \
optimizer.show_annoying_tier \
optimizer.show_source_traces \
optimizer.warning_level \
posix.debug.trace_expanded_commands \
posix.debug.trace_input_lines \
posix.editor.ctrl_d_does_not_exit \
posix.editor.jobs_report_status_immediately \
posix.interpreter.exit_on_command_failure \
posix.interpreter.export_every_assigned_variable \
posix.interpreter.glob_disabled \
posix.interpreter.job_control \
posix.interpreter.parse_without_executing \
posix.interpreter.pipeline_fails_on_any_stage \
posix.interpreter.redirect_refuses_to_overwrite_files \
posix.interpreter.unset_variable_is_error"

  if [[ $current_word == -* ]]
  then
    _kosh_compgen -W "--help -a --all --persist --force" -- "$current_word"
    return
  fi

  case ${#operands[@]} in
    0)
      _kosh_compgen -W "create get list load save set" -- "$current_word"
    ;;
    1)
      case ${operands[0]} in
        create) _kosh_compgen -W "bash kosh sh" -- "$current_word" ;;
        set | get) _kosh_compgen -W "$option_names" -- "$current_word" ;;
      esac
    ;;
    2)
      if [[ ${operands[0]} == set ]]
      then
        case ${operands[1]} in
          mood | init_moods)
            _kosh_compgen -W "kosh sh bash bash-posix" -- "$current_word"
          ;;
          completion.menu_style)
            _kosh_compgen -W "interactive external plain" -- "$current_word"
          ;;
          optimizer.warning_level)
            _kosh_compgen -W "0 1 2 3" -- "$current_word"
          ;;
          editor.base_mode)
            _kosh_compgen -W "emacs vi" -- "$current_word"
          ;;
          completion.add_space_after_completed_word)
            _kosh_compgen -W "off on on-excluding-trailing-slash" -- \
              "$current_word"
          ;;
          editor.history.file_path | editor.history.max_entries) ;;
          *) _kosh_compgen -W "on off" -- "$current_word" ;;
        esac
      fi
    ;;
  esac
}

complete -F _kosh_koshconf_complete koshconf
_kosh_fc_complete ()
{
  local current_word=${COMP_WORDS[COMP_CWORD]}
  local previous_word=${COMP_WORDS[COMP_CWORD - 1]}
  local switches="--help -e -l -n -r -s"

  if [[ $previous_word == -e ]]
  then
    _kosh_compgen -c -- "$current_word"
    return
  fi

  _kosh_compgen -W "$switches" -- "$current_word"
}

complete -F _kosh_fc_complete fc
complete -W '-c -d -n -r -a -w -p -s -S --sync --help' history
complete -W '-r -R -t -p --help' hash
complete -c -W '--help --posix -p -R' time
# The block below is rebuilt by make completions.
# BEGIN GENERATED KOSHKIT FLAGS
_koshkit_utils="basename bc cal calc cat chgrp chmod chown cksum cmp comm cp \
csplit cut date df diff dirname du egrep emacs env evil evildisk evilfiles \
evilfs evilio eviliso evillogs evilnet evilps evilss expand expr fgrep file \
find flock fold fuser getconf goodcore goodfsw goodnode goodstat grep head id \
killall less link ln locale logger logname ls make man meow mkdir mkfifo mknod \
more mv nice nl nohup nproc od paste pathchk pkill pr printenv ps readlink \
realpath renice retry rm rmdir sed seq sleep slurp sort split stat strings \
stty sync tabs tail tee timeout touch tput tr tsort tty uname unexpand uniq \
unlink vi watch wc which who whoami xargs yes"

_koshkit_util_flags ()
{
  case $1 in
    basename)
      printf "%s\n" "-a --multiple -s --suffix -z --zero"
    ;;
    bc)
      printf "%s\n" "-l --mathlib -q --quiet"
    ;;
    cal)
      printf "%s\n" "-a --today"
    ;;
    calc)
      printf "%s\n" "-i --interactive -p --pipe"
    ;;
    cat)
      printf "%s\n" \
        "-A --show-all -b --number-nonblank -e -E --show-ends -n --number -s --squeeze-blank -t -T --show-tabs -v --show-nonprinting -u --syntax-highlighting"
    ;;
    chgrp)
      printf "%s\n" \
        "-h --no-dereference -R --recursive -H --dereference-arguments -L --dereference -P --physical -x --one-file-system"
    ;;
    chmod)
      printf "%s\n" "-R --recursive --one-file-system"
    ;;
    chown)
      printf "%s\n" \
        "-h --no-dereference -R --recursive -H --dereference-arguments -L --dereference -P --physical -x --one-file-system"
    ;;
    cmp)
      printf "%s\n" "-l --verbose -s --silent"
    ;;
    comm)
      printf "%s\n" \
        "-1 --hide-first -2 --hide-second -3 --hide-common --check-order --nocheck-order --output-delimiter --total -z --zero-terminated"
    ;;
    cp)
      printf "%s\n" "-r -R -f -i -p -v -H -L -P -x --one-file-system"
    ;;
    csplit)
      printf "%s\n" "-k --keep-files -f --prefix -n --digits -s --silent"
    ;;
    cut)
      printf "%s\n" \
        "-b --bytes -c --characters -f --fields -d --delimiter -n --no-split -s --only-delimited --complement --output-delimiter -z --zero-terminated"
    ;;
    date)
      printf "%s\n" "-u --utc"
    ;;
    df)
      printf "%s\n" \
        "-k --kilobytes -P --portability -h --human-readable -H --si"
    ;;
    diff)
      printf "%s\n" \
        "-u --unified -w --ignore-all-space -a --text -L --label --color"
    ;;
    dirname)
      printf "%s\n" "-z --zero"
    ;;
    du)
      printf "%s\n" "-s -h --tree -x --one-file-system -T --top-largest"
    ;;
    egrep)
      printf "%s\n" \
        "-E --extended-regexp -F --fixed-strings -a --text -e --regexp -f --file -c --count -l --files-with-matches -q --quiet -s --no-messages -x --line-regexp -i -v -r --recursive --one-file-system -n --line-number -h --no-filename --color"
    ;;
    emacs)
      printf "%s\n" "-e --emacs -v --vi -V --vi-normal"
    ;;
    env)
      printf "%s\n" "-i --ignore-environment -u --unset"
    ;;
    evil)
      printf "%s\n" "-a --all -s --short -u --users"
    ;;
    evildisk)
      printf "%s\n" "-a --all"
    ;;
    evilfiles)
      printf "%s\n" \
        "-t --terse -p --pid -u --user -c --command -i --network -w --wide"
    ;;
    evilfs)
      printf "%s\n" "-a --all"
    ;;
    evilio)
      printf "%s\n" \
        "-a --all -h --human-readable -C --cumulative --ps -l --live -n --count -p --pid --sort"
    ;;
    eviliso)
      printf "%s\n" \
        "-a --all -n --namespaces -c --cgroups -s --sessions -r --remote -k --runtime --kubernetes --container --containers"
    ;;
    evillogs)
      printf "%s\n" "--cores --logs"
    ;;
    evilnet)
      printf "%s\n" \
        "-a --all -t --traffic --sort -l --live -C --cumulative -f --failures"
    ;;
    evilps)
      printf "%s\n" \
        "-p --show-pids -n --numeric-sort -a --all -A --arguments -U --show-owner -c --cpu -M --memory -w --wide --sort -l --live -C --cumulative"
    ;;
    evilss)
      printf "%s\n" \
        "-l --listening -a --all -t --tcp -u --udp -x --unix -p --processes -n --numeric -4 --ipv4 -6 --ipv6 -H --no-header --live"
    ;;
    expand)
      printf "%s\n" "-t --tabs"
    ;;
    fgrep)
      printf "%s\n" \
        "-E --extended-regexp -F --fixed-strings -a --text -e --regexp -f --file -c --count -l --files-with-matches -q --quiet -s --no-messages -x --line-regexp -i -v -r --recursive --one-file-system -n --line-number -h --no-filename --color"
    ;;
    file)
      printf "%s\n" \
        "-d --default-tests -h --no-dereference -i --regular-only -L --dereference -m --magic-file -M --magic-only"
    ;;
    flock)
      printf "%s\n" "-n --nonblock --transaction-held-lock"
    ;;
    fold)
      printf "%s\n" "-b --bytes -s --spaces -w --width"
    ;;
    fuser)
      printf "%s\n" "-c -f -u"
    ;;
    getconf)
      printf "%s\n" "-v --specification -a --all"
    ;;
    goodcore)
      printf "%s\n" "-p --pid -b --binary -o --output -q --quiet --no-compress"
    ;;
    goodfsw)
      printf "%s\n" \
        "-r --recursive -h --human-readable -m --machine-readable --event-flags -1 --one-event -x --one-file-system -l --latency -e --exclude --timezone --precision"
    ;;
    goodnode)
      printf "%s\n" "-i --inode -r --root --verify"
    ;;
    goodstat)
      printf "%s\n" "-L --dereference -c --checksum -f --filesystem"
    ;;
    grep)
      printf "%s\n" \
        "-E --extended-regexp -F --fixed-strings -a --text -e --regexp -f --file -c --count -l --files-with-matches -q --quiet -s --no-messages -x --line-regexp -i -v -r --recursive --one-file-system -n --line-number -h --no-filename --color"
    ;;
    head)
      printf "%s\n" \
        "-n -c -q --quiet --silent -v --verbose -z --zero-terminated"
    ;;
    id)
      printf "%s\n" "-G --groups -g --group -n --name -r --real -u --user"
    ;;
    killall)
      printf "%s\n" "-s --signal -l --list"
    ;;
    less)
      printf "%s\n" "-i --ignore-case -N --LINE-NUMBERS -S --chop-long-lines"
    ;;
    ln)
      printf "%s\n" "-s -f -L -P"
    ;;
    locale)
      printf "%s\n" \
        "-a --all-locales -m --charmaps -c --category-name -k --keyword-name"
    ;;
    logger)
      printf "%s\n" "-i --id -f --file -p --priority -s --stderr -t --tag"
    ;;
    ls)
      printf "%s\n" \
        "-a -A -1 -l -h -F --classify -d -p -n -g -o -k -t -S -r -R --recursive --tree -L --level --one-file-system"
    ;;
    make)
      printf "%s\n" \
        "-f --file -C --directory -B --always-make -k --keep-going -e --environment-overrides -i --ignore-errors -S --stop -n --just-print -p --print-data-base -q --question -r --no-builtin-rules -s --silent -t --touch -j --jobs"
    ;;
    man)
      printf "%s\n" "-k --keyword"
    ;;
    meow)
      printf "%s\n" "-f --fps -s --speed -n --cats"
    ;;
    mkdir)
      printf "%s\n" "-p -m"
    ;;
    mkfifo)
      printf "%s\n" "-m --mode"
    ;;
    mknod)
      printf "%s\n" \
        "-m --mode --type --fifo --character --block --major --minor"
    ;;
    more)
      printf "%s\n" \
        "-c --clear -e --exit -i --ignore-case -s --squeeze -u --plain -n --lines -p --command -t --tag"
    ;;
    mv)
      printf "%s\n" "-f -i -v"
    ;;
    nice)
      printf "%s\n" "-n --increment"
    ;;
    nl)
      printf "%s\n" \
        "-b --body-numbering -d --section-delimiter -f --footer-numbering -h --header-numbering -i --line-increment -l --join-blank-lines -n --number-format -p --no-renumber -s --number-separator -v --starting-line-number -w --number-width"
    ;;
    nproc)
      printf "%s\n" "--all --ignore"
    ;;
    od)
      printf "%s\n" \
        "-A --address-radix -j --skip-bytes -N --read-bytes -t --format -v --output-duplicates -b -c -d -o -s -x -w --width"
    ;;
    paste)
      printf "%s\n" "-d --delimiters -s --serial -z --zero-terminated"
    ;;
    pathchk)
      printf "%s\n" "-p --portable -P --leading-hyphen -W --windows"
    ;;
    pkill)
      printf "%s\n" "--signal -s --session --inverse -l --list"
    ;;
    pr)
      printf "%s\n" \
        "-a --across -d --double-space -D --date-format -F --form-feed -f --form-feed-alias -h --header -l --length -m --merge -n --number-lines -o --indent -r --no-file-warnings -t --omit-header -s --separator -w --width"
    ;;
    ps)
      printf "%s\n" "-a -u -x -w"
    ;;
    readlink)
      printf "%s\n" \
        "-f --canonicalize -e --canonicalize-existing -m --canonicalize-missing -n --no-newline -q --quiet -s --silent -z --zero"
    ;;
    realpath)
      printf "%s\n" \
        "-e --canonicalize-existing -m --canonicalize-missing -L --logical -P --physical -s --no-symlinks --strip -q --quiet -z --zero --relative-to --relative-base"
    ;;
    renice)
      printf "%s\n" "-g --pgrp -p --pid -u --user -n --increment"
    ;;
    retry)
      printf "%s\n" \
        "-n --attempts -d --delay -b --backoff -m --max-delay -q --quiet"
    ;;
    rm)
      printf "%s\n" \
        "-r --recursive -R -d --dir -f --force -i -I --interactive -v --verbose -x --one-file-system --preserve-root --no-preserve-root --dry-run"
    ;;
    rmdir)
      printf "%s\n" "-p"
    ;;
    sed)
      printf "%s\n" "-E --extended-regexp -e --expression -f --file -n --quiet"
    ;;
    seq)
      printf "%s\n" "-f --format -s --separator -w --equal-width"
    ;;
    slurp)
      printf "%s\n" "-e --emacs -v --vi -V --vi-normal"
    ;;
    sort)
      printf "%s\n" \
        "-r -n -g --general-numeric-sort -h --human-numeric-sort -M --month-sort -V --version-sort -z --zero-terminated -u -b -d -f -i -c -C -m -s -o -t -k"
    ;;
    split)
      printf "%s\n" "-a --suffix-length -b --bytes -l --lines"
    ;;
    stat)
      printf "%s\n" \
        "-L --dereference -f --file-system -t --terse -c --format --printf"
    ;;
    strings)
      printf "%s\n" "-a --all -n --bytes -t --radix"
    ;;
    stty)
      printf "%s\n" "-a --all -g --save"
    ;;
    sync)
      printf "%s\n" "-d --data -f --file-system"
    ;;
    tail)
      printf "%s\n" \
        "-n -c -f -F --follow --retry -s --sleep-interval --pid -q --quiet --silent -v --verbose -z --zero-terminated"
    ;;
    tee)
      printf "%s\n" "-a"
    ;;
    timeout)
      printf "%s\n" "-s --signal -k --kill-after -p --preserve-status"
    ;;
    touch)
      printf "%s\n" "-a -c -m -r -t"
    ;;
    tput)
      printf "%s\n" "-T --terminal"
    ;;
    tr)
      printf "%s\n" "-d -c -C -s -t --truncate-set1"
    ;;
    tty)
      printf "%s\n" "-s --silent"
    ;;
    uname)
      printf "%s\n" \
        "-a --all -m --machine -n --nodename -r --release -s --kernel-name -v --kernel-version"
    ;;
    unexpand)
      printf "%s\n" "-a --all -t --tabs"
    ;;
    uniq)
      printf "%s\n" \
        "-c -d -D --all-repeated -u --group -i --ignore-case -w --check-chars -z --zero-terminated -f -s"
    ;;
    vi)
      printf "%s\n" "-e --emacs -v --vi -V --vi-normal"
    ;;
    watch)
      printf "%s\n" \
        "-n --interval -t --no-title -g --chgexit -e --errexit -x --exec"
    ;;
    wc)
      printf "%s\n" "-l -w -c -m"
    ;;
    which)
      printf "%s\n" "-a --all -q --quiet"
    ;;
    who)
      printf "%s\n" \
        "-a --all -b --boot -d --dead -H --heading -l --login -m --current -p --process -q --quick -r --runlevel -s --short -t --time -T --terminal-state -u --idle"
    ;;
    xargs)
      printf "%s\n" \
        "-a --arg-file -d --delimiter -E -e --eof -I -i --replace -P --max-procs -L --max-lines -n --max-args -p --prompt -s --max-size -t --trace -x --exit -0 --null -r --no-run-if-empty"
    ;;
  esac
}
# END GENERATED KOSHKIT FLAGS

# Words that a utility reads before its flags are parsed.
_koshkit_util_extra_flags ()
{
  case $1 in
    find)
      printf "%s\n" \
        "-exec -iname -maxdepth -mindepth -mount -name -print -print0 -type -xdev"
    ;;
    pr)
      printf "%s\n" "--columns --pages"
    ;;
  esac
}

_koshkit_complete ()
{
  local current_word
  current_word=${COMP_WORDS[COMP_CWORD]}

  if [[ $COMP_CWORD -eq 1 ]]
  then
    _kosh_compgen \
                  -W "$_koshkit_utils --list --list-flags --color --help" \
                  -- "$current_word"
    return
  fi

  local util=${COMP_WORDS[1]}

  if [[ $current_word == -* ]]
  then
    _kosh_compgen -W "$(_koshkit_util_flags "$util") \
$(_koshkit_util_extra_flags "$util") --help" -- "$current_word"
  else
    _kosh_compgen -f -- "$current_word"
  fi
}

complete -o filenames -F _koshkit_complete koshkit
