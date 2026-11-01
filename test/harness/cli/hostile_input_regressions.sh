#!/bin/sh
# Diagnostics go to /dev/null, so the cases compare statuses and resulting files.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir" || exit 1

echo "--- rm -r with a trailing slash leaves the link target ---"
mkdir target holder
echo kept > target/file
ln -s ../target holder/link
"$BIN" -c 'koshkit rm -r holder/link/' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat target/file'

echo "--- mv and cp refuse a second source with the same name ---"
mkdir first second moved copied
echo 1 > first/x
echo 2 > second/x
"$BIN" -c 'koshkit cp first/x second/x copied' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat copied/x'
"$BIN" -c 'koshkit mv first/x second/x moved' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat moved/x second/x'

echo "--- a failed source leaves the next one free ---"
mkdir valid into
echo 3 > valid/y
"$BIN" -c 'koshkit mv missing/y valid/y into' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat into/y'

echo "--- cp -r merges directories with one final component ---"
mkdir -p left/same right/same merged
echo 1 > left/same/one
echo 2 > right/same/two
"$BIN" -c 'koshkit cp -r left/same right/same merged'
echo "status $?"
"$BIN" -c 'koshkit cat merged/same/one merged/same/two'

echo "--- cut -d= takes the equals sign ---"
printf 'a=b\n' | "$BIN" -c 'koshkit cut -d= -f1'

echo "--- sed closes an unterminated line before more output ---"
printf 'a' | "$BIN" -c 'koshkit sed p'
echo
printf 'a\nb' | "$BIN" -c 'koshkit sed -e p -e "\$a\\
x"'
printf 'a' > unterminated
"$BIN" -c 'koshkit sed -i p unterminated'
"$BIN" -c 'koshkit wc -c unterminated'

echo "--- nesting ends in a diagnostic ---"
awk 'BEGIN { for (i = 0; i < 30000; i++) printf "f() "; print "true" }' > functions.sh
"$BIN" -n functions.sh 2>/dev/null
echo "status $?"
awk 'BEGIN { for (i = 0; i < 30000; i++) printf "coproc "; print "true" }' > coprocs.sh
"$BIN" -n coprocs.sh 2>/dev/null
echo "status $?"
awk 'BEGIN { printf "a=(1); echo $(( "; for (i = 0; i < 10000; i++) printf "a["; printf "0"; for (i = 0; i < 10000; i++) printf "]"; print " ))" }' > subscripts.sh
"$BIN" subscripts.sh 2>/dev/null
echo "status $?"

echo "--- bc and make stop deep recursion ---"
echo 'define f(n){if(n==0)return 0; return f(n-1)}; f(20000)' |
  "$BIN" -c 'koshkit bc' 2>/dev/null
echo "status $?"
echo 'define f(n){if(n==0)return 0; return f(n-1)}; f(1000)' |
  "$BIN" -c 'koshkit bc'
awk 'BEGIN { for (i = 1; i <= 10000; i++) print "t" i ": t" i + 1; print "t10001:;@echo end" }' > deep.mk
"$BIN" -c 'koshkit make -f deep.mk t1' 2>/dev/null
echo "status $?"

echo "--- mapfile refuses an origin with no room ---"
printf 'a\nb\n' |
  "$BIN" --mood bash -c 'mapfile -t -O 9223372036854775807 lines' 2>/dev/null
echo "status $?"

echo "--- fg in a forked stage ends with a diagnostic ---"
timeout 10 "$BIN" --mood bash -c 'sleep 0 & fg %1 | true; echo after' \
  2>/dev/null | tail -n 1
timeout 10 "$BIN" --mood bash -c 'sleep 1 & fg %1 | true; echo after' \
  2>/dev/null | tail -n 1

echo "--- an indirect subscript from data runs no command ---"
"$BIN" --mood kosh --no-diagnostics -c \
  'a=(1); v="a[\$(touch marker)0]"; echo "${!v}"' 2>/dev/null
echo "status $?"
ls marker 2>/dev/null
echo "status $?"

echo "--- an indirect value is a name and not an expression ---"
"$BIN" --mood bash --no-diagnostics -c \
  'v="u:-\$(touch injected)"; echo "${!v-d}"' 2>/dev/null
echo "status $?"
ls injected 2>/dev/null
echo "status $?"

echo "--- export -p leaves out names from the environment that are not names ---"
env 'bad$(touch exported)=1' "$BIN" --mood bash --no-diagnostics \
  -c 'eval "$(export -p)"' 2>/dev/null
ls exported 2>/dev/null
echo "status $?"

echo "--- deep conditionals end in a diagnostic ---"
awk 'BEGIN { printf "[[ "; for (i = 0; i < 200000; i++) printf "("; printf " a "; for (i = 0; i < 200000; i++) printf ")"; print " ]]" }' > conditional.sh
"$BIN" --no-diagnostics conditional.sh 2>/dev/null
echo "status $?"

echo "--- a self reference through an element ends ---"
"$BIN" --mood bash --no-diagnostics \
  -c 'declare -n r="r[0]"; echo "[$r]"' 2>/dev/null
echo "status $?"

echo "--- loop appends follow the working directory ---"
mkdir into_a into_b
"$BIN" --no-diagnostics \
  -c 'for d in into_a into_b; do cd "$d"; echo "$d" >> out; cd ..; done'
"$BIN" -c 'koshkit cat into_a/out into_b/out'

echo "--- a loop needs a condition ---"
timeout 10 "$BIN" --no-diagnostics -c 'while do :; done' 2>/dev/null
echo "status $?"

echo "--- local - restores an explicit set -u ---"
"$BIN" -c 'f() { local -; set -u; }; f; [ -z "$unset_operand" ]; echo "after $?"'

echo "--- koshconf refuses a configuration that is not a file ---"
mkdir -p config/kosh
mkfifo config/kosh/kosh.conf
XDG_CONFIG_HOME=$PWD/config HOME=$PWD timeout 10 "$BIN" \
  -c 'koshconf set editor.history.max_entries 5 --persist' 2>/dev/null
echo "status $?"

echo "--- --format keeps quoted text inside substitutions ---"
cat > quoted.sh <<'EOF'
echo "$(echo "a   b")"
echo "$(echo "a|b;c&&d")"
echo "${unset_name:-"a   b"}"
if [ "$(echo "a   b")" = "a   b" ]; then echo equal; else echo differ; fi
echo "$( (echo subshell) )"
EOF
cat >> quoted.sh <<'EOF'
if true; then
cat <<E
foo\
E
E
fi
EOF
"$BIN" --format quoted.sh > formatted.sh
"$BIN" --mood bash --no-diagnostics formatted.sh
"$BIN" --format formatted.sh | cmp - formatted.sh
echo "status $?"

echo "--- nice and nohup children receive the default SIGPIPE ---"
timeout 10 "$BIN" -c 'koshkit nice sh -c "while :; do echo x; done" > >(head -1)'
echo "status $?"
timeout 10 "$BIN" -c 'koshkit nohup sh -c "while :; do echo x; done" > >(head -1)' 2>/dev/null
echo "status $?"

echo "--- timeout keeps the status of a child that already exited ---"
"$BIN" -c 'koshkit timeout 1 sh -c "sleep 3 & exit 3"; echo "status $?"'

echo "--- xargs stops an item past the size limit ---"
head -c 3000000 /dev/zero | tr '\0' a | timeout 10 "$BIN" -c 'koshkit xargs -s 100 echo' 2>&1 | grep -c 'exceeds the size limit'

echo "--- xargs -I never runs a truncated item ---"
awk 'BEGIN { for (i = 0; i < 99; i++) printf "a"; print "" }' |
  "$BIN" -c 'koshkit xargs -s 200 -I {} echo {} | koshkit wc -c' 2>/dev/null
awk 'BEGIN { for (i = 0; i < 200; i++) printf "a"; print "" }' |
  "$BIN" -c 'koshkit xargs -s 100 -I {} echo {} | koshkit wc -c' 2>/dev/null

echo "--- a long flag accepts an empty value ---"
printf 'a\nb\n' > comm-left
printf 'b\nc\n' > comm-right
"$BIN" -c 'koshkit comm --output-delimiter= comm-left comm-right' | tr '\0' '@'

echo "--- ps adds repeated -p and -o values ---"
"$BIN" -c 'koshkit ps --no-headers -p $$ -p 1 -o pid -o ppid | wc -l | tr -d " "'
"$BIN" -c 'koshkit ps --no-headers -p $$ -o pid -o ppid | wc -w | tr -d " "'

echo "--- csplit -k writes the rest after a failing pattern ---"
printf '1\n2\n3\n4\n' > csplit-input
"$BIN" -c 'koshkit csplit -k csplit-input 2 "/nomatch/"' >/dev/null 2>&1
echo "status $?"
cat xx00 xx01

echo "--- flock -n on a FIFO does not wait for a writer ---"
mkfifo flock-fifo
timeout 10 "$BIN" -c 'koshkit flock -n flock-fifo true'
echo "status $?"

echo "--- compgen -W with a trailing backslash ---"
"$BIN" -c "compgen -W 'a \\' -- a" 2>&1 | grep -v '^ \|^note\|SC1003'

echo "--- make stops a pattern rule that feeds itself ---"
unset MAKEFLAGS MFLAGS MAKELEVEL MODE
mkdir make_cases
cd make_cases || exit 1
printf '%%: %%.c\n\t@echo built\nall: foo\n' > Makefile
"$BIN" -c 'koshkit make all' 2>/dev/null
echo "status $?"

echo "--- make reads quoted conditional operands ---"
cat > Makefile <<'EOF'
ifeq "a" "a"
Y = 1
endif
ifneq 'a' "b"
Z = 1
endif
all:
	@echo [$(Y)][$(Z)]
EOF
"$BIN" -c 'koshkit make'

echo "--- make takes references with colons in a rule ---"
cat > Makefile <<'EOF'
X = a b
all: $(X:=.t) $(X:%=%.u)
	@echo [$^]
a.t b.t a.u b.u:
	@:
EOF
"$BIN" -c 'koshkit make'

echo "--- make applies a recipe prefix from a variable ---"
cat > Makefile <<'EOF'
Q = @
M = -
all:
	$(Q)echo quiet
	$(M)false
	$(Q)$(M)false
EOF
"$BIN" -c 'koshkit make'
echo "status $?"

echo "--- make runs static pattern rules ---"
cat > Makefile <<'EOF'
OBJS = a.o b.o
all: $(OBJS)
$(OBJS): %.o: %.c
	@echo $@ $< $*
a.c b.c:
	@:
EOF
"$BIN" -c 'koshkit make'

echo "--- make runs every double-colon rule ---"
cat > Makefile <<'EOF'
t:: a
	@echo one
t:: b
	@echo two
a b:
	@:
EOF
"$BIN" -c 'koshkit make'
printf 't: a\nt:: b\n' > Makefile
"$BIN" -c 'koshkit make' 2>/dev/null
echo "status $?"

echo "--- make keeps automatic variables of an expanded target ---"
cat > Makefile <<'EOF'
T = foo
$(T): ; @echo [$@]
EOF
"$BIN" -c 'koshkit make'

echo "--- make override beats the command line ---"
cat > Makefile <<'EOF'
override X = file
override Y += more
all:
	@echo [$(X)][$(Y)][$(origin X)]
EOF
"$BIN" -c 'koshkit make X=cmd Y=cmd'

echo "--- make predefines its session variables ---"
cat > Makefile <<'EOF'
all: a | b c
	@echo [$(notdir $(realpath .))] [$|] [$(MAKEFILE_LIST)]
	@echo [$(MAKELEVEL)] $$MAKELEVEL [$(.DEFAULT_GOAL)] [$(MAKECMDGOALS)]
	@echo [$(or )][$(and )]
a b c:
	@:
EOF
"$BIN" -c 'koshkit make all'

echo "--- make reads a blank inside a target ---"
: > 'a b'
cat > Makefile <<'EOF'
all: a\ b c\ d
	@echo [$^]
c\ d:
	@echo [$@]
EOF
"$BIN" -c 'koshkit make'

echo "--- a later make recipe replaces an earlier one ---"
printf 'all:\n\t@echo one\nall:\n\t@echo two\n' > Makefile
"$BIN" -c 'koshkit make' 2>/dev/null
cd .. || exit 1

echo "--- nested regex repeats are refused ---"
for pattern in '(((a{100}){100}){100}){100}' '[[:alpha:]]{1,500}{1,500}'; do
  "$BIN" -c "koshkit grep -E '$pattern' /dev/null" 2>/dev/null
  echo "status $?"
done
"$BIN" -c "echo a | koshkit sed -E 's/((a{100}){100}){100}/x/'" 2>/dev/null
echo "status $?"
"$BIN" -c "koshkit grep -cE '(a{255}){255}' /dev/null"
"$BIN" -c "echo aaa | koshkit grep -c 'a\{1,3\}'"

echo "--- cp refuses a file conflict below an operand ---"
mkdir -p conflict/a/x conflict/b/x conflict/d
echo first > conflict/a/x/f
echo second > conflict/b/x/f
echo other > conflict/b/x/g
"$BIN" -c 'koshkit cp -r conflict/a/x conflict/b/x conflict/d' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat conflict/d/x/f conflict/d/x/g'

echo "--- mv puts a directory with a slash inside the destination ---"
mkdir slash_source slash_destination
"$BIN" -c 'koshkit mv slash_source/ slash_destination'
echo "status $?"
[ -d slash_destination/slash_source ] && echo moved

echo "--- grep -w takes an empty match ---"
"$BIN" -c "printf '\n' | koshkit grep -cw '^\$'"
"$BIN" -c "printf 'abc\n' | koshkit grep -cw 'x*'"
"$BIN" -c "printf 'a b\n \nab\n' | koshkit grep -cw 'x*'"

echo "--- sed opens a line range after its start was skipped ---"
"$BIN" -c "koshkit seq 1 5 | koshkit sed -n -e 3d -e '3,\$p'"
"$BIN" -c "koshkit seq 1 5 | koshkit sed -n -e 2,3d -e '3,4p'"

echo "--- sed replacements change case and read hex ---"
"$BIN" -c "echo 'hello World' | koshkit sed 's/.*/\\U&/'"
"$BIN" -c "echo 'HELLO WORLD' | koshkit sed -E 's/(\\w+) (\\w+)/\\l\\1 \\U\\2\\E x/'"
"$BIN" -c "echo ab | koshkit sed 's/a/\\x41/'"
"$BIN" -c "printf 'a\tb c\n' | koshkit sed 's/[ \\t]/X/g'"
"$BIN" -c "echo a | koshkit sed 's/a/\\q/'" 2>/dev/null
echo "status $?"

echo "--- sort reads an empty general numeric key ---"
printf 'a b 1\nc d\n' > keys
"$BIN" -c 'koshkit sort -g -k3,1 keys'

echo "--- a key modifier replaces the global options ---"
printf 'x  2\ny 10\nz 3\n' > keys
"$BIN" -c 'koshkit sort -n -k2,2b keys'

echo "--- expr anchors a pattern once ---"
"$BIN" -c "koshkit expr abc : '^a'"
"$BIN" -c "koshkit expr '*' : '\\(*\\)'"
"$BIN" -c "koshkit expr aaa : 'a**'"

echo "--- bc keeps the scale in nested functions ---"
"$BIN" -c "echo 'scale=3; sqrt(sqrt(2))' | koshkit bc -l"
"$BIN" -c "echo 'scale=30; l(e(1))' | koshkit bc -l"

echo "--- bc reads last, else, and print ---"
"$BIN" -c "printf '2+3\nlast\n.*2\n' | koshkit bc -l"
"$BIN" -c "printf 'if (1 > 2) 3 else 4\nif (1) { 5 } else { 6 }\n' | koshkit bc -l"
"$BIN" -c "printf 'if (0) 5\nelse 6\n' | koshkit bc -l"
cat > print.bc <<'EOF'
print "a", 1+1, "b\n"
EOF
"$BIN" -c 'koshkit bc print.bc < /dev/null'
"$BIN" -c "printf 'obase=16; .5\n' | koshkit bc"

echo "--- calc keeps digits for large arguments ---"
"$BIN" -c "koshkit calc 'exp(100)'"
"$BIN" -c "koshkit calc 'sin(10**20)'"

echo "--- date reads signs and fractions and writes more conversions ---"
"$BIN" -c "koshkit date -u -d @-1 '+%F %T'"
"$BIN" -c "koshkit date -u -d @1.5 '+%s %N'"
"$BIN" -c "koshkit date -u -d @-1.5 '+%s %N'"
"$BIN" -c "koshkit date -u -d 0099-03-04 '+%F %Y'"
"$BIN" -c "koshkit date -u -d @0 '+%:z %::z %:::z %q %Z'"
"$BIN" -c "koshkit date -u -d @67768036191676799 +%Y"

echo "--- printf %q in the sh mood reads back in the sh mood ---"
cat > quoted_sh.sh <<'EOF'
value="a
b'c	d e"
quoted=$(printf '%q' "$value")
eval "copy=$quoted"
[ "$copy" = "$value" ] && echo same
EOF
"$BIN" --mood sh quoted_sh.sh
