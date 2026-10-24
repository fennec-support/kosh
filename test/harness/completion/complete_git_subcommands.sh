# The git subcommand index reads the git-<command> pages in the section-1
# directories of $MANPATH, so git subcommands complete from the installed
# manpages. A hermetic fake man tree under $MANPATH keeps the candidates
# stable across machines.
dir=$(mktemp -d)
mandir="$dir/man/man1"
mkdir -p "$mandir"
cat > "$mandir/git.1" <<'EOF'
.TH GIT 1
.SH SYNOPSIS
\fBgit\fR [\fB\-v\fR]
.SH "GIT COMMANDS"
.sp
\fBgit\fR \fBfoo\fR
\fBgit\fR \fBbar\fR
EOF
cat > "$mandir/git-foo.1" <<'EOF'
.TH GIT-FOO 1
.SH SYNOPSIS
\fBgit\fR \fBfoo\fR
EOF
cat > "$mandir/git-bar.1" <<'EOF'
.TH GIT-BAR 1
.SH SYNOPSIS
\fBgit\fR \fBbar\fR
EOF
echo "== subcommands with no prefix:"
MANPATH="$dir/man" "$BIN" --debug-complete-at 'git ' </dev/null
echo "== subcommands with a prefix:"
MANPATH="$dir/man" "$BIN" --debug-complete-at 'git ba' </dev/null
rm -rf "$dir"
