#!/bin/bash
# Bash printf numeric conversions with field width and precision, checked
# byte-for-byte against bash. Covers signed and unsigned bases, padding,
# alignment, and a floating conversion.

# The numeric locale cases at the end of the file run here in a shell that
# starts with LOCPATH naming the generated locale.
if [ "${1-}" = numeric-locale ]; then
  printf "%.2f|%'d\n" 3.14 1234567
  LC_ALL=xx_XX printf "%.2f|%.1f|%'d|%'.2f|%'g|%'u\n" \
    3,14 2,5 1234567 1234567,891 1234567,5 1234567
  LC_ALL=xx_XX printf '%.2f\n' 3.14
  echo "point_status=$?"
  printf "%.2f|%'d\n" 3.14 1234567
  export LC_NUMERIC=xx_XX
  printf "%.2f|%'d\n" 3,14 1234567
  printf -v stored '%.3f' 0,125
  echo "stored=$stored"
  echo "arithmetic=$((7 / 2)) $((10 * 3))"
  LC_ALL=C printf '%.2f\n' 3.14
  unset LC_NUMERIC
  LANG=xx_XX
  printf '%.2f\n' 4,5
  LANG=C
  scoped() {
    local LC_ALL=xx_XX
    printf "%'.1f\n" 9876543,25
  }
  scoped
  printf "%'.1f\n" 9876543.25
  LC_ALL=xx_XX
  LC_ALL=C printf "%.2f|%'d\n" 3.14 1234567
  printf "%.2f|%'d\n" 3,14 1234567
  unset LC_ALL
  exit 0
fi

printf '%d\n' 42
printf '%5d\n' 42
printf '%-5d|\n' 42
printf '%05d\n' 42
printf '%x %X\n' 255 255
printf '%o\n' 8
printf '%.3f\n' 3.14159
printf '%8.2f\n' 3.14159
printf '%+d %+d\n' 5 -5
printf '%c\n' A
printf '%e\n' 12345.678


printf '%q\n' "hello world"
printf '%q\n' "it's"
printf '%q\n' ""
printf '%q\n' 'a$b'
printf '%q\n' 'plain_text'
printf '%q\n' 'a"b'
printf '%q\n' 'x;y|z&w'
printf '%q\n' '(paren)'
printf '%q\n' 'a=b:c,d.e/f@g%h+i-j'
printf '%q\n' '~tilde'
printf '%q\n' '#hash'
printf '%q\n' $'tab\there'
printf '%q\n' $'ctrl\x01end'
printf '%q\n' $'esc\x1bend'
printf '%q %q\n' foo bar

# Bash printf reusing the format over extra arguments, checked byte-for-byte
# against bash. The format repeats until the arguments run out, and a format
# with several conversions consumes that many arguments per cycle.
printf '%s\n' one two three
printf '[%d]' 1 2 3 4
echo
printf '%s=%s\n' a 1 b 2 c 3
printf '%d-%d ' 1 2 3 4 5
echo
printf 'x%dy\n' 7

# Bash printf -v assigning the formatted result into a variable, checked
# byte-for-byte against bash. Covers numeric, padded, multi-conversion, reused,
# floating, and star-width forms.
printf -v num "%d" 100
echo "num=$num"
printf -v hex "%04x" 255
echo "hex=$hex"
printf -v multi "%s/%s" a b
echo "multi=$multi"
printf -v reused "[%d]" 1 2 3
echo "reused=$reused"
printf -v floatv "%.2f" 3.14159
echo "floatv=$floatv"
printf -v widthv "%*d" 6 7
echo "widthv=[$widthv]"

echo -e 'tab\there'
echo -e 'a\nb'
echo -e 'esc\e.E\E.'
echo -ne 'no-nl'
echo
echo -E 'kept\tliteral'
echo -e 'stop\cgone'
echo 'no-e-literal\t.'
echo -e 'oct\101'

# Round-five review edge cases, checked byte-for-byte against bash. The bash echo
# reads octal only in the \0NNN form so a bare \NNN stays literal, and assigning
# an element to a scalar-valued name promotes the scalar to element zero.
echo -e '\101 \0101'
echo -e '\0102 end'
echo -e 'lit\1eral'
shopt -u xpg_echo
echo 'xpg-off:\nvalue'
shopt -s xpg_echo
echo 'xpg-on:\nvalue'
echo -E 'xpg-forced-off:\nvalue'
a=5
a[2]=99
echo "${a[0]} ${a[1]} ${a[2]}"
b=hello
b[0]=10
echo "${b[0]}"
c=world
c[1]=second
echo "${c[0]}-${c[1]}-count=${#c[@]}"

# Bash echo -e/-E/-n and printf -v, checked byte-for-byte against bash. The bash
# echo leaves escapes literal unless -e, and printf -v stores into a variable.
echo -e "a\tb\tc"
echo -e "line1\nline2"
echo "no\tescapes here"
echo -ne "x\ty\n"
echo -E "stays\tliteral"
echo plain words
echo -e "bell\aafter" | od -An -c | tr -s ' '
printf -v x "%d-%d" 3 5
echo "x is $x"
printf -v greeting "Hello, %s!" world
echo "$greeting"
printf -v padded "%05d" 42
echo "$padded"
printf "%s %s\n" direct output

# printf parses the leading numeric prefix of a malformed integer argument the
# way bash does, including the diagnostic stream and final status.
printf '%d\n' 12abc
printf '%d\n' 9z
printf '%d\n' '  42  '
printf '%d\n' 010
printf '%d\n' 0x1f
printf '%d\n' ' 7'
printf '[%d]\n' ''
printf '%d\n' abc

# The format string and a %b argument both decode the \NNN octal and \xHH hex
# escapes, and a \x without a hex digit stays literal on stdout.
printf 'a\0b\n' | od -An -tx1 | tr -s ' '
printf '\101\102\060\n' | od -An -tx1 | tr -s ' '
printf '\x41\x42\n' | od -An -tx1 | tr -s ' '
printf '%b' '\x41\n' | od -An -tx1 | tr -s ' '
printf '%b' '\101\n' | od -An -tx1 | tr -s ' '
printf '%b' '\0101\n' | od -An -tx1 | tr -s ' '
printf '%b' '\x4\n' | od -An -tx1 | tr -s ' '
printf '%b' '\x\n' 2>/dev/null | od -An -tx1 | tr -s ' '
printf '%b' 'a\0b\n' | od -An -tx1 | tr -s ' '

# The printf %(fmt)T time conversion, checked against bash. A fixed epoch
# renders a fixed time so the result is stable, and both shells read the same
# zone from the environment.
printf '%(%Y-%m-%d)T\n' 1700000000
printf '[%(%H:%M:%S)T]\n' 0
printf '%(%Y)T\n' 1000000000
printf 'year=%(%Y)T month=%(%m)T\n' 1700000000 1700000000

# The printf format is recycled over the extra arguments. A numeric conversion
# with no remaining argument substitutes zero and the status stays zero, the
# same as bash. An explicit empty argument is still an invalid number and the
# status is one.
printf '%d %d %d\n' 1 2 3 4 5
echo "recycle_status=$?"
printf '%d\n' '' 2>/dev/null
echo "empty_status=$?"

# The ' flag groups thousands only where the numeric locale defines a
# separator, so the C locale prints the digits plainly and a conversion without
# grouping ignores the flag. A floating operand must be consumed whole, a
# leading quote supplies the code point of the next character, and %F prints
# like %f.
LC_ALL=C printf "%'d|%'.2f|%'i|%'u|%'s|%'x|%F\n" \
  1234567 1234567.5 -1234567 1234567 abc 255 1.5
LC_ALL=C printf '%.2f|' 3x ' 3.5' "'a" 1e3 .5
echo "float_status=$?"
printf '[%.1f]\n' '' 2>/dev/null
echo "empty_float_status=$?"

# printf reads the numeric locale from the first nonempty of LC_ALL,
# LC_NUMERIC, and LANG for the decimal point of a floating operand and for the
# decimal point and grouping of its output. Arithmetic expansion stays in the C
# locale. localedef builds a comma locale in a temporary directory, and a shell
# started with LOCPATH runs the cases at the top of the file. The -c run covers
# the default kosh mood. A host without localedef takes the same skip branch in
# both shells.
locale_dir=$(mktemp -d)
if command -v localedef > /dev/null 2>&1; then
  printf 'LC_NUMERIC\ndecimal_point ","\nthousands_sep "."\ngrouping 3;3\nEND LC_NUMERIC\n' \
    > "$locale_dir/source"
  localedef -c -i "$locale_dir/source" "$locale_dir/xx_XX" > /dev/null 2>&1
fi
if [ -f "$locale_dir/xx_XX/LC_NUMERIC" ]; then
  LOCPATH=$locale_dir LANG=C LC_ALL= LC_NUMERIC= "$BASH" "$0" numeric-locale
  echo "numeric_locale_status=$?"
  LOCPATH=$locale_dir LANG=C LC_ALL=xx_XX LC_NUMERIC= \
    "$BASH" -c 'printf "%.2f\n" 2,5'
else
  echo numeric-locale-unavailable
fi
rm -rf "$locale_dir"
