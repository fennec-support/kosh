#!/bin/bash
# Bash pattern matching under the character locale, checked byte-for-byte
# against bash. A ? and the retry of a * consume one whole character in a UTF-8
# locale and one byte in the C locale. A bracket expression consumes one whole
# character, compares ranges by code point, and classifies a non-ASCII
# character by its Unicode class, and extglob groups split only at character
# boundaries. Covers the first nonempty of LC_ALL, LC_CTYPE, and LANG,
# assignment, unset, and trims, case, [[ ]], substitution, and pathname
# expansion over multibyte values.

v='日本.語.x'

show() {
  printf '[%s|%s]\n' "${v%?.*}" "${v#*??}" | od -An -c
}

LC_ALL=C.UTF-8
show
LC_ALL=C
show
LC_ALL=en_US.UTF-8
show
LC_ALL=en_US.utf8
show
LC_ALL=C
show

LC_ALL=
LC_CTYPE=C.UTF-8
show
LC_CTYPE=C
LANG=C.UTF-8
show
LC_CTYPE=
show
LANG=POSIX
show
unset LC_ALL LC_CTYPE LANG
show

LC_ALL=C.UTF-8
e='é'
for subject in "$e" 'ab' 'é.' '日'; do
  case $subject in
    ?) echo "case-one:$subject" ;;
    ??) echo "case-two:$subject" ;;
    *) echo "case-other:$subject" ;;
  esac
done
[[ $e == ? ]] && echo cond-one-utf8 || echo cond-one-bytes
[[ $e == ?? ]] && echo cond-two-utf8 || echo cond-two-bytes
[[ $e == ?*? ]] && echo cond-star-utf8 || echo cond-star-bytes
[[ $e != ? ]] && echo cond-ne-utf8 || echo cond-ne-bytes
LC_ALL=C
[[ $e == ? ]] && echo cond-one-utf8 || echo cond-one-bytes
[[ $e == ?? ]] && echo cond-two-utf8 || echo cond-two-bytes
[[ $e == ?*? ]] && echo cond-star-utf8 || echo cond-star-bytes
case $e in
  ?) echo case-c-one ;;
  ??) echo case-c-two ;;
esac

LC_ALL=C.UTF-8
w='éa語b'
echo "${w//?/.}"
echo "${w/?/.}"
echo "${w/#?/.}"
echo "${w/%?/.}"
echo "${w##*?}|${w#?}|${w%%?*}|${w%?}"
LC_ALL=C
echo "${w//?/.}" | od -An -c

LC_ALL=C.UTF-8
bad=$'a\xffb\xc3'
printf '%s' "${bad//?/.}" | od -An -c
printf '%s' "${bad#?}" | od -An -c
printf '%s' "${bad%?}" | od -An -c

dir=$(mktemp -d)
: > "$dir/é"
: > "$dir/x"
: > "$dir/語"
LC_ALL=C.UTF-8
for f in "$dir"/?; do echo "utf8:${f##*/}"; done
LC_ALL=C
for f in "$dir"/?; do echo "bytes:${f##*/}"; done
for f in "$dir"/??; do echo "bytes2:${f##*/}"; done
LC_ALL=C.UTF-8 true
for f in "$dir"/?; do echo "bytes-after-prefix:${f##*/}"; done
rm -rf "$dir"

shopt -s extglob
check_patterns() {
  local entry subject pattern
  for entry in \
    'é|[é]' 'é|[à-ü]' 'é|[ü-à]' 'é|[é-é]' 'é|[a-é]' 'é|[é-z]' 'ý|[é-ü]' \
    'à|[é-ü]' '語|[一-龥]' 'é|[!語]' '語|[!語]' 'é|[!a]' 'é|[^a]' 'é|[a-z]' \
    'é|[]é]' 'é|[!]é]' 'é|[\é]' 'é|["é"]' 'é|[é]*' 'éa|[é]a' 'éa|[é]?' \
    'éé|[é][é]' 'éé|[é]' 'ab|[é]b' \
    'é|[[:alpha:]]' 'é|[[:alnum:]]' 'é|[[:lower:]]' 'é|[[:upper:]]' \
    'É|[[:upper:]]' 'É|[[:alpha:]]' 'é|[[:print:]]' 'é|[[:graph:]]' \
    'é|[[:punct:]]' 'é|[[:space:]]' 'é|[[:cntrl:]]' 'é|[![:alpha:]]' \
    '語|[[:alpha:]]' '語|[[:print:]]' 'a|[[:alpha:]]' ' |[[:space:]]' \
    'é|@(é)' 'é|?(é)' 'éé|*(é)' 'éé|+(é)' 'é|!(é)' 'é|!(a)' 'éa|!(é)a' \
    'éa|@(é|語)a' 'éa|?(é)a' '日本|*(日|本)' '日本|+(日|本)' '日|@(?)' \
    'é|@(?)' 'é|@(??)' 'é|!(?)' 'é|!(??)' 'éa|*(?)' 'éa|*(??)' 'éa|+(?)' \
    'éa|?(?)a' 'éa|?(??)a' 'éb|@([é]|x)b' 'éb|@([!a])b' 'éb|@([a-z])b' \
    'éb|@([à-ü])b' 'éb|*([é])b' 'éb|!(?)b' 'éb|!(??)b' 'éé|!(?)' \
    'éé|!(??)' 'éé|!(????)' \
    'ab|@([[:alpha:]])b' 'éb|@([[:alpha:]])b' '1b|@([[:alpha:]])b' \
    'ab|*([[:alpha:]])b' 'éb|*([[:alpha:]])b' '1b|*([[:alpha:]])b' \
    'ab|+([[:alpha:]])b' 'éb|+([[:alpha:]])b' '1b|+([[:alpha:]])b' \
    'ab|?([[:alpha:]])b' 'éb|?([[:alpha:]])b' '1b|?([[:alpha:]])b' \
    'ab|!([[:alpha:]])b' 'éb|!([[:alpha:]])b' '1b|!([[:alpha:]])b' \
    'ab|@([[:digit:]])b' 'éb|@([[:digit:]])b' '1b|@([[:digit:]])b' \
    'ab|@([![:alpha:]])b' 'éb|@([![:alpha:]])b' '1b|@([![:alpha:]])b' \
    'ab|@([a[:digit:]])b' 'éb|@([a[:digit:]])b' '1b|@([a[:digit:]])b' \
    'ab|@([[:alpha:]]|x)b' 'éb|@([[:alpha:]]|x)b' '1b|@([[:alpha:]]|x)b' \
    'ab|@([]a])b' 'éb|@([]a])b' 'ab|@([[:alpha:])b' 'éb|@([[:foo:]])b' \
    'ab|@([[:alpha:][:digit:]])b' 'éb|@([[:alpha:][:digit:]])b' \
    '1b|@([[:alpha:][:digit:]])b' '-b|@([![:alpha:]])b'; do
    subject=${entry%%|*}
    pattern=${entry#*|}
    case $subject in
      $pattern) printf 'Y %s\n' "$entry" ;;
      *) printf 'N %s\n' "$entry" ;;
    esac
  done
}

LC_ALL=C.UTF-8
echo utf8-brackets
check_patterns
LC_ALL=C
echo c-brackets
check_patterns

check_class_forms() {
  local subject pattern trimmed
  for subject in ab éb 1b; do
    for pattern in '@([[:alpha:]])b' '*([[:alpha:]])' '+([![:alpha:]])b' \
      '?([a[:digit:]])b'; do
      [[ $subject == $pattern ]] && echo "cond-Y $subject $pattern" ||
        echo "cond-N $subject $pattern"
      trimmed=${subject##$pattern}
      echo "trim:$trimmed"
      trimmed=${subject%%$pattern}
      echo "trim-suffix:$trimmed"
    done
  done
}

LC_ALL=C.UTF-8
echo utf8-class-forms
check_class_forms
LC_ALL=C
echo c-class-forms
check_class_forms

check_quoted_and_escaped_operators() {
  local s=abc q='*(a)b' p='\a' t=ab
  echo "quoted:${s#'*(a)b'}|${s/"$q"/X}|${s#"$q"}|${s%%"$q"}"
  echo "unquoted:${s#$q}|${s/$q/X}"
  echo "escaped:${t#$p}|${t/$p/X}|${t#"$p"}|${t/"$p"/X}"
  [[ a == $p ]] && echo cond-escaped-y || echo cond-escaped-n
  [[ ab == $p ]] && echo cond-escaped-two-y || echo cond-escaped-two-n
  [[ $t == $p* ]] && echo cond-escaped-star-y || echo cond-escaped-star-n
  [[ $s == "$q" ]] && echo cond-quoted-y || echo cond-quoted-n
  case a in $p) echo case-escaped-y ;; *) echo case-escaped-n ;; esac
  case $t in $p*) echo case-escaped-star-y ;; *) echo case-escaped-star-n ;; esac
  case $q in "$q") echo case-quoted-y ;; *) echo case-quoted-n ;; esac
  case aab in $q) echo case-ext-y ;; *) echo case-ext-n ;; esac
  case aab in "$q") echo case-extq-y ;; *) echo case-extq-n ;; esac
}

shopt -s extglob
LC_ALL=C.UTF-8
echo utf8-operators
check_quoted_and_escaped_operators
LC_ALL=C
echo c-operators
check_quoted_and_escaped_operators
