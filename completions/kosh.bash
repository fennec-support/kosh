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
    option_names="allexport export-all notify errexit error-exit noglob no-glob \
hashall keyword monitor noexec no-exec nounset no-unset verbose xtrace braceexpand \
histexpand history history-prefix-search ignoreeof interactive-hints interactive-diagnostics auto-pair interactive-comments nolog space-after-completion transient-prompt extended-keys \
noclobber no-clobber errtrace physical functrace onecmd pipefail failglob koshkit vi emacs \
posix show-ast show-lexed-words show-exit-code show-all-exit-codes mimicry extended-arithmetic annoying-diagnostics \
show-stats no-diagnostics show-memory login rcfile"
  local tab_selectors="interactive external plain"
  local \
    switches="--help --options --mood --init-moods --tab-selector -o +o -M -L \
-a -b -e -f -h -k -m -n -t -u -v -x -B -C -E -H -N -P -T -A -R -W -WW -WWW -I -S -G \
+a +b +e +f +h +k +m +n +t +u +v +x +B +C +E +H +N +P +T +A +R +W +WW +WWW +I +S +G"

  case $previous_word in
    -o | +o)
      _kosh_compgen -W "$option_names" -- "$current_word"
      return
    ;;
    -M | --mood)
      _kosh_compgen -W "$moods" -- "$current_word"
      return
    ;;
    -L | --init-moods)
      _kosh_compgen -W "$moods" -- "$current_word"
      return
    ;;
    --tab-selector)
      _kosh_compgen -W "$tab_selectors" -- "$current_word"
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
    option_names="mood editor.tab_selector editor.hints editor.diagnostics \
editor.auto_pair editor.transient_prompt editor.extended_keys \
history.prefix_search history.file history.size completion.space_after \
diagnostics.level diagnostics.annoying diagnostics.analysis koshkit.commands \
arithmetic.extended compat.mimicry debug.show_ast debug.show_lexed_words \
debug.show_exit_code debug.show_all_exit_codes debug.show_stats \
debug.show_memory legacy.export_all legacy.notify_jobs legacy.exit_on_error \
legacy.no_glob legacy.hash_commands legacy.keyword_assignments \
legacy.job_control legacy.no_exec legacy.one_command legacy.privileged \
legacy.unset_is_error legacy.echo_input legacy.trace_commands \
legacy.brace_expansion legacy.no_clobber legacy.err_trap_inherit \
legacy.history_expansion legacy.physical_paths legacy.debug_trap_inherit \
legacy.pipe_fail legacy.history legacy.ignore_eof legacy.no_log \
legacy.vi_editing legacy.emacs_editing legacy.posix legacy.bare_dir_is_cd \
legacy.assoc_expand_once legacy.cd_to_variable legacy.cd_spelling \
legacy.check_hash legacy.check_jobs_on_exit legacy.check_window_size \
legacy.complete_full_quote legacy.dir_expand legacy.dir_spelling \
legacy.glob_dotfiles legacy.exec_failure_continues legacy.expand_aliases \
legacy.extended_debug legacy.extended_glob legacy.extended_quote \
legacy.empty_glob_is_error legacy.force_fignore legacy.glob_ascii_ranges \
legacy.glob_skip_dots legacy.glob_star legacy.gnu_error_format \
legacy.history_reedit legacy.history_verify legacy.host_completion \
legacy.hup_on_exit legacy.inherit_exit_on_error legacy.interactive_comments \
legacy.last_pipe_in_shell legacy.local_inherits_value legacy.local_unset \
legacy.login_shell legacy.mail_warn legacy.no_empty_command_completion \
legacy.glob_ignore_case legacy.match_ignore_case legacy.empty_glob_is_removed \
legacy.patsub_replacement legacy.programmable_completion \
legacy.programmable_completion_alias legacy.prompt_expansion \
legacy.restricted_shell legacy.shift_verbose legacy.source_uses_path \
legacy.varredir_close legacy.echo_escapes"

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
          editor.tab_selector)
            _kosh_compgen -W "interactive external plain" -- "$current_word"
          ;;
          diagnostics.level) _kosh_compgen -W "0 1 2 3" -- "$current_word" ;;
          history.file | history.size) ;;
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
