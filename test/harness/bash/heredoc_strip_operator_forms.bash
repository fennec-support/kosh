#!/bin/bash
# The tab-stripping operator takes its delimiter after a blank as well as
# attached, a delimiter starting with a dash after << or <<- is an ordinary
# delimiter whether it is quoted or not, <<- with no delimiter is a syntax
# error, a lone backslash at the end of the input is literal, and
# BASH_COMMAND keeps the operator and the delimiter of both <<- forms.
work=$(mktemp -d)
cd "$work" || exit 1
cat <<- EOF
	spaced
	EOF
cat <<-EOF
	attached
	EOF
cat <<-'Q'
	$quoted
	Q
cat << -
dash-delimited
-
cat <<- -
	stripped-dash
	-
cat <<'-EOF'
quoted-dash
-EOF
cat <<-'-EOF'
	quoted-stripped-dash
	-EOF
printf 'cat <<-\nx\n' > missing-delimiter.sh
"$BASH" -n missing-delimiter.sh 2> /dev/null
echo "missing-delimiter=$?"
eval 'echo trailing\'
printf 'echo file-end \\' > trailing-backslash.sh
. ./trailing-backslash.sh
cat <<$'ansi end'
ansi-body
ansi end
cat <<"quoted end"
quoted-body
quoted end
trap 'printf "[%s]\n" "$BASH_COMMAND"' DEBUG
cat <<- EOF > /dev/null
	traced-spaced
	EOF
cat <<-EOF > /dev/null
	traced-attached
	EOF
trap - DEBUG
cd / && rm -rf -- "$work"
