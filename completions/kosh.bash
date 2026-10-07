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
    -c | --command)
      COMPREPLY=()

      return
    ;;
  esac

  local \
    long_flags="--version --short-version --help --interactive --stdin \
--command --error-exit --no-glob --one-command --verbose --xtrace --export-all \
--no-clobber --no-exec --no-unset --login --rcfile --init-file --norc \
--restricted --privileged --no-init-files --posix --mood \
--init-moods --enable-mimicry --dumb --tab-selector --lint --format --apply --as-language-server --list-diagnostics \
--no-diagnostics --no-annoying-diagnostics --no-init-diagnostics --no-traces --no-completion --no-syntax-highlighting \
--enable-koshkit --enable-extended-arithmetic \
--show-ast \
--show-optimizer-diagnostics --show-exit-code --show-all-exit-codes --show-lexed-words --show-stats --show-memory \
"
  local \
    short_flags="-V -i -s -c -e -f -t -v -x -a -C -n -u -l -r -p -M -L -I -W -WW -WWW \
-Q -T -A -N -R"

  if [[ $current_word == --* ]]
  then
    _kosh_compgen -W "$long_flags" -- "$current_word"
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
  local flags="-x --trace --ssh-command --scp-command --link-mood --help"

  if [[ $previous_word == --link-mood ]]
  then
    _kosh_compgen -W "bash dash sh kosh" -- "$current_word"
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
    option_names="mood editor.completion_menu_style \
editor.show_command_synopsis editor.show_live_diagnostics \
editor.auto_close_brackets_and_quotes editor.transient_prompt_after_submit \
editor.request_extended_key_reports history.arrow_keys_search_by_typed_prefix \
history.file_path history.max_entries \
completion.add_space_after_completed_word diagnostics.warning_level \
diagnostics.show_annoying_tier diagnostics.analyze_before_running \
koshkit.run_utilities_as_plain_commands \
arithmetic.use_big_integers_and_decimals compat.mimic_shell_named_by_shebang \
debug.print_syntax_tree debug.print_lexed_word_escapes \
debug.report_nonzero_exit_codes debug.report_every_exit_code \
debug.print_evaluation_statistics debug.print_memory_report_at_exit \
legacy.export_every_assigned_variable legacy.jobs_report_status_immediately \
legacy.exit_on_command_failure legacy.glob_disabled \
legacy.assignments_anywhere_in_command \
legacy.job_control legacy.parse_without_executing \
legacy.exit_after_one_command legacy.privileged_mode \
legacy.unset_variable_is_error legacy.trace_print_input_lines \
legacy.trace_print_expanded_commands legacy.brace_expansion \
legacy.redirect_refuses_to_overwrite_files \
legacy.trap_err_inherited_by_functions legacy.history_bang_expansion \
legacy.cd_resolves_symlinks \
legacy.trap_debug_and_return_inherited_by_functions \
legacy.pipeline_fails_on_any_stage legacy.history_recording \
legacy.ctrl_d_does_not_exit \
legacy.base_editor_mode legacy.posix_mode legacy.cd_by_typing_directory_name \
legacy.array_subscripts_expand_once legacy.cd_to_variable_value \
legacy.cd_fix_typos legacy.verify_remembered_command_paths \
legacy.jobs_check_before_exit legacy.update_columns_and_lines \
legacy.completion_quote_all_special_characters \
legacy.completion_expand_directory_names \
legacy.completion_fix_directory_typos legacy.glob_includes_dotfiles \
legacy.exec_failure_keeps_shell legacy.aliases_expand \
legacy.debugger_support legacy.glob_extended_patterns \
legacy.quote_dollar_strings_in_parameter_expansion \
legacy.glob_no_match_is_error legacy.completion_always_apply_fignore \
legacy.glob_ranges_use_ascii_order legacy.glob_never_matches_dot_and_dotdot \
legacy.glob_double_star_recurses legacy.errors_use_gnu_format \
legacy.history_reedit_failed_substitution \
legacy.history_verify_expansion_before_running \
legacy.completion_hostnames_after_at legacy.jobs_hangup_on_exit \
legacy.command_substitution_inherits_exit_on_failure \
legacy.interactive_comments legacy.pipeline_last_stage_runs_in_shell \
legacy.local_inherits_outer_value legacy.local_unset_hides_outer \
legacy.login_shell legacy.mail_warn_when_read legacy.completion_skip_empty_line \
legacy.glob_ignores_case legacy.match_ignores_case \
legacy.glob_no_match_expands_to_nothing \
legacy.pattern_substitution_ampersand_is_match legacy.completion_programmable \
legacy.completion_programmable_follows_aliases \
legacy.prompt_expands_parameters legacy.restricted_shell \
legacy.shift_reports_overflow legacy.source_searches_path \
legacy.redirect_variable_fd_closed_after_command \
legacy.echo_interprets_backslash_escapes"

  if [[ $current_word == -* ]]
  then
    _kosh_compgen -W "--help --persist --force" -- "$current_word"
    return
  fi

  case ${#operands[@]} in
    0)
      _kosh_compgen -W "create get list load set" -- "$current_word"
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
          mood) _kosh_compgen -W "kosh sh bash bash-posix" -- "$current_word" ;;
          editor.completion_menu_style)
            _kosh_compgen -W "interactive external plain" -- "$current_word"
          ;;
          diagnostics.warning_level)
            _kosh_compgen -W "0 1 2 3" -- "$current_word"
          ;;
          legacy.base_editor_mode)
            _kosh_compgen -W "emacs vi" -- "$current_word"
          ;;
          history.file_path | history.max_entries) ;;
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
complete -W '-r -R -p --help' hash
complete -c -W '--help --posix -p -R' time
_koshkit_utils="basename bc cal calc cat chgrp chmod chown cksum cmp comm cp csplit cut date df diff \
dirname du env evil evildisk evilfiles evilfs evilio evillogs evilnet evilps evilss expand expr file find flock \
fold fuser getconf goodcore goodfsw goodnode goodstat eviliso grep head id killall link ln locale logger logname ls make man mkdir mkfifo mknod more \
mv nice nl nohup nproc od paste pathchk pkill pr printenv ps readlink realpath renice retry rm rmdir sed seq \
sleep sort split stat strings stty sync tabs tail tee timeout touch tput tr tsort tty uname unexpand uniq \
unlink watch wc which who whoami xargs yes"

_koshkit_util_flags ()
{
  case $1 in
    bc)
      echo "-l --mathlib -q --quiet"
    ;;
    cal)
      echo "-a --today"
    ;;
    calc)
      echo "-i --interactive -p --pipe"
    ;;
    cp)
      echo "-r -R -f -H -i -L -P -p -v -x --one-file-system"
    ;;
    cut)
      echo \
        "-b --bytes -c --characters -f --fields -d --delimiter -n --no-split -s --only-delimited"
    ;;
    diff)
      echo "-u --unified -w --ignore-all-space -a --text -L --label --color"
    ;;
    file)
      echo \
        "-d --default-tests -h --no-dereference -i --regular-only -L --dereference -m --magic-file -M --magic-only"
    ;;
    getconf)
      echo "-a --all -v --specification"
    ;;
    evil)
      echo "-a --all -s --short -u --users"
    ;;
    goodcore)
      echo "-p --pid -b --binary -o --output -q --quiet --no-compress"
    ;;
    eviliso)
      echo \
        "-a --all -n --namespaces -c --cgroups -s --sessions -r --remote -k --runtime --kubernetes --container --containers"
    ;;
    evildisk)
      echo "-a --all"
    ;;
    evilfiles)
      echo "-t --terse -p --pid -u --user -c --command -i --network -w --wide"
    ;;
    evilfs)
      echo "-a --all"
    ;;
    evilio)
      echo \
        "-a --all -h --human-readable -C --cumulative -l --live --ps -n --count -p --pid --sort"
    ;;
    evillogs)
      echo "--cores --logs"
    ;;
    evilnet)
      echo \
        "-a --all -t --traffic -l --live -C --cumulative -f --failures --sort"
    ;;
    goodnode)
      echo "-i --inode -r --root --verify"
    ;;
    evilps)
      echo \
        "-p --show-pids -n --numeric-sort -a --all -A --arguments -c --cpu -U --show-owner -M --memory -w --wide --sort -l --live -C --cumulative"
    ;;
    goodstat)
      echo "-L --dereference -c --checksum -f --filesystem"
    ;;
    goodfsw)
      echo \
        "-h --human-readable -m --machine-readable -r --recursive -t --timestamp -x --one-file-system --event-flags -1 --one-event -l --latency -e --exclude --timezone --precision"
    ;;
    ls)
      echo \
        "-a -A -1 -d -g -k -l -h -n -o -p -F -t -S -r -R -L --classify --recursive --level --one-file-system --tree"
    ;;
    nproc)
      echo "--all --ignore="
    ;;
    ln)
      echo "-s -f -L -P"
    ;;
    locale)
      echo \
        "-a --all-locales -m --charmaps -c --category-name -k --keyword-name"
    ;;
    man)
      echo "-k --keyword"
    ;;
    more)
      echo \
        "-c --clear -e --exit -i --ignore-case -s --squeeze -u --plain -n --lines -p --command -t --tag"
    ;;
    rm)
      echo "-r -R -f -i -x --one-file-system --dry-run"
    ;;
    rmdir)
      echo "-p"
    ;;
    mkdir)
      echo "-p -m"
    ;;
    mknod)
      echo "-m --mode --type --fifo --character --block --major --minor"
    ;;
    mv)
      echo "-f -i -v"
    ;;
    od)
      echo \
        "-A --address-radix -b -c -d -j --skip-bytes -N --read-bytes -o -s -t --format -v --output-duplicates -x"
    ;;
    env)
      echo "-i --ignore-environment -u --unset"
    ;;
    xargs)
      echo \
        "-0 --null -r --no-run-if-empty -E --eof -I --replace -L --max-lines -n --max-args -p --prompt -s --max-size -t --trace -x --exit"
    ;;
    pr)
      echo \
        "-a --across -d --double-space -F --form-feed -f --form-feed-alias -h --header -l --length -m --merge -n --number-lines -o --indent -r --no-file-warnings -t --omit-header -s --separator -w --width"
    ;;
    stty)
      echo "-a --all -g --save"
    ;;
    cat)
      echo "-n -u --syntax-highlighting"
    ;;
    tee)
      echo "-a"
    ;;
    touch)
      echo "-a -c -m -r -t"
    ;;
    tput)
      echo "-T --terminal"
    ;;
    who)
      echo \
        "-a --all -b --boot -d --dead -H --heading -l --login -m --current -p --process -q --quick -r --runlevel -s --short -t --time -T --terminal-state -u --idle"
    ;;
    du)
      echo "-s -h -x -T --one-file-system --tree --top-largest"
    ;;
    head)
      echo "-n -c"
    ;;
    tail)
      echo "-n -c -f -F -q -v -s --follow --retry --pid --sleep-interval --quiet --silent --verbose"
    ;;
    wc)
      echo "-l -w -c -m"
    ;;
    tr)
      echo "-c -C -d -s"
    ;;
    grep)
      echo "-i -v -r -n -h -E -F -e -f -c -l -q -s -x -rnh --recursive --line-number --no-filename --extended-regexp --fixed-strings --regexp --file --count --files-with-matches --quiet --no-messages --line-regexp --one-file-system --color"
    ;;
    sort)
      echo "-b -c -C -d -f -i -k -m -n -o -r -s -t -u"
    ;;
    uniq)
      echo "-c -d -f -s -u"
    ;;
    timeout)
      echo "-s --signal -k --kill-after -p --preserve-status"
    ;;
    pkill | killall)
      echo "-s --signal -l --list"
    ;;
    make)
      echo \
        "-f --file -C --directory -B --always-make -k --keep-going -e --environment-overrides -i --ignore-errors -S --stop -n --just-print -j --jobs -p --print-data-base -q --question -r --no-builtin-rules -s --silent -t --touch"
    ;;
    find)
      echo "-name -iname -type -maxdepth -mindepth -xdev -mount -print -print0 -exec"
    ;;
    flock)
      echo "--transaction-held-lock"
    ;;
    fuser)
      echo "-c -f -u"
    ;;
    readlink)
      echo "-n --no-newline"
    ;;
    chgrp | chown)
      echo "-H --dereference-arguments -L --dereference -P --physical -R --recursive -h --no-dereference -x --one-file-system"
    ;;
    chmod)
      echo "-R --recursive --one-file-system"
    ;;
    cmp)
      echo "-l --verbose -s --silent"
    ;;
    comm)
      echo "-1 --hide-first -2 --hide-second -3 --hide-common"
    ;;
    csplit)
      echo "-f --prefix -k --keep-files -n --digits -s --silent"
    ;;
    date)
      echo "-u --utc"
    ;;
    df)
      echo "-k --kilobytes -P --portability -h --human-readable -H --si"
    ;;
    expand | unexpand)
      echo "-a --all -t --tabs"
    ;;
    fold)
      echo "-b --bytes -s --spaces -w --width"
    ;;
    id)
      echo "-G --groups -g --group -n --name -r --real -u --user"
    ;;
    logger)
      echo "-f --file -i --id -p --priority -s --stderr -t --tag"
    ;;
    mkfifo)
      echo "-m --mode"
    ;;
    nice)
      echo "-n --increment"
    ;;
    nl)
      echo \
        "-b --body-numbering -d --section-delimiter -f --footer-numbering -h --header-numbering -i --line-increment -l --join-blank-lines -n --number-format -p --no-renumber -s --number-separator -v --starting-line-number -w --number-width"
    ;;
    paste)
      echo "-d --delimiters -s --serial"
    ;;
    pathchk)
      echo "-P --leading-hyphen -p --portable"
    ;;
    renice)
      echo "-g --pgrp -n --increment -p --pid -u --user"
    ;;
    sed)
      echo "-E --extended-regexp -e --expression -f --file -n --quiet"
    ;;
    split)
      echo "-a --suffix-length -b --bytes -l --lines"
    ;;
    strings)
      echo "-a --all -n --bytes -t --radix"
    ;;
    tty)
      echo "-s --silent"
    ;;
    uname)
      echo \
        "-a --all -m --machine -n --nodename -r --release -s --kernel-name -v --kernel-version"
    ;;
    ps)
      echo "-a -u -x -w"
    ;;
    retry)
      echo "-n --attempts -d --delay -b --backoff -m --max-delay -q --quiet"
    ;;
    evilss)
      echo \
        "-4 --ipv4 -6 --ipv6 -a --all -H --no-header -l --listening --live -n --numeric -p --processes -t --tcp -u --udp -x --unix"
    ;;
    stat)
      echo "-L --dereference -f --file-system -t --terse -c --format --printf"
    ;;
    sync)
      echo "-d --data -f --file-system"
    ;;
    watch)
      echo "-n --interval -t --no-title -g --chgexit -e --errexit -x --exec"
    ;;
    which)
      echo "-a --all -q --quiet"
    ;;
    *)
      echo ""
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
                  -W "$_koshkit_utils --list --assimilate --help" \
                  -- "$current_word"
    return
  fi

  local util=${COMP_WORDS[1]}

  if [[ $current_word == -* ]]
  then
    _kosh_compgen -W "$(_koshkit_util_flags "$util") --help" -- \
      "$current_word"
  else
    _kosh_compgen -f -- "$current_word"
  fi
}

complete -o filenames -F _koshkit_complete koshkit
