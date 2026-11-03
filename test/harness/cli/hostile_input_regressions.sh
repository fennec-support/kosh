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

echo "--- export -p leaves out environment entries with invalid keys ---"
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

echo "--- a keyword is a redirection target ---"
"$BIN" --mood bash --no-diagnostics -c 'echo a >fi; echo b >done; koshkit cat fi done'

echo "--- cp -r of a directory with a trailing dot copies its contents ---"
mkdir -p dotted/inner dotted_into
echo 1 > dotted/inner/file
"$BIN" -c 'koshkit cp -r dotted/. dotted_into/'
"$BIN" -c 'koshkit cat dotted_into/inner/file'

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

echo "--- a huge exact result fails with a status ---"
"$BIN" -c 'echo $((1<<(2**33))); echo unreachable' 2>/dev/null
echo "status $?"
"$BIN" -c 'echo $((2**(2**33))); echo unreachable' 2>/dev/null
echo "status $?"

echo "--- a script operand keeps later flags for the script ---"
printf 'echo "[$1]"\n' > operand.sh
"$BIN" operand.sh --version
"$BIN" -c 'echo "[$1]"' name --version

echo "--- the last stage assigns in the kosh mood ---"
printf 'echo 5 | read -r x\necho "$x"\n' > lastpipe.sh
"$BIN" lastpipe.sh
printf 'let "a=1+2"\necho "$a"\n' > let.sh
"$BIN" --no-annoying-diagnostics let.sh

echo "--- a lint run of a bash script keeps the pipeline warning ---"
printf '#!/bin/bash\necho 5 | read -r x\necho "$x"\n' > pipe.bash
"$BIN" --lint pipe.bash 2>&1 | grep -c SC2031
printf '#!/bin/bash\nshopt -s lastpipe\necho 5 | read -r x\necho "$x"\n' > pipe2.bash
"$BIN" --lint pipe2.bash 2>&1 | grep -c SC2031

echo "--- select stays a bash mood loop ---"
"$BIN" -c 'select a in b; do break; done' 2>/dev/null
echo "status $?"

echo "--- --format keeps background compound commands ---"
cat > background.sh <<'EOF'
while true; do :; done &
until true; do :; done &
for i in a; do :; done &
if a; then b; fi &
case x in a) b;; esac &
EOF
"$BIN" --format background.sh > background_out.sh
echo "status $?"
grep -c ' &$' background_out.sh
"$BIN" --format background_out.sh | cmp - background_out.sh

echo "--- --format keeps a case pattern after a continued statement ---"
cat > caseand.sh <<'EOF'
case $i in
  (a)
    x=$(a
 b) && y;;
  (*)
    z;;
esac
y=$(case $i in
  (a) x=1 && y;;
  (*) z;;
esac)
EOF
"$BIN" --format caseand.sh > caseand_out.sh
echo "status $?"
"$BIN" --format caseand_out.sh | cmp - caseand_out.sh
echo "status $?"

echo "--- --format is stable after a brace group continuation ---"
printf 'cc && x=1 &&\n    { echo "$x"; exit; }\necho y\n' > group.sh
"$BIN" --format group.sh > group_out.sh
"$BIN" --format group_out.sh | cmp - group_out.sh
echo "status $?"

echo "--- a Dockerfile comment inside a RUN continuation ---"
printf 'FROM alpine\nRUN apk add a \\\n    # note\n    b\n' > Dockerfile
"$BIN" --format Dockerfile | cmp - Dockerfile
echo "status $?"

echo "--- a missing file substitution reports the file ---"
"$BIN" -c 'x=$(<missing_file); echo "[$x] $?"' 2>&1 | grep -c missing_file

echo "--- an append past the largest index fails ---"
"$BIN" --mood bash -c 'a[9223372036854775807]=y; a+=(z)
echo "status $?"; echo "${#a[@]}"' 2>/dev/null

echo "--- declare -n leaves a populated array alone ---"
"$BIN" --mood bash -c 'a=(1 2); declare -n a=b; echo "status $?"; declare -p a' 2>/dev/null

echo "--- many appends after a gap finish ---"
"$BIN" --mood bash -c 'a=(); a[1]=x; for ((i=0;i<20000;i++)); do a+=(q); done
echo "${#a[@]}"; unset "a[20001]"; a+=(r); echo "${#a[@]} ${a[20001]}"'

echo "--- a function substitution survives descriptors its body duplicates ---"
"$BIN" --no-diagnostics -c 'x=${ exec 2>&1; echo hi; echo e >&2; }; echo "[$x]"; echo gone >&2'
echo "status $?"
"$BIN" --no-diagnostics -c 'x=${ exec 3>&1; echo hi >&3; }; echo "[$x]"; echo gone >&3'
echo "status $?"

echo "--- a coprocess under one name releases the earlier descriptors ---"
"$BIN" --no-diagnostics -c 'i=0; while ((i<300)); do coproc C { echo; }; read -u ${C[0]} l; ((i++)); done; echo done'
echo "status $?"

echo "--- a fatal signal runs the EXIT trap ---"
output=$("$BIN" --no-diagnostics -c 'trap "echo E" EXIT; kill -TERM $$; echo unreachable' 2>/dev/null)
echo "status $? $output"
output=$("$BIN" --no-diagnostics -c 'trap "echo E" EXIT; kill -USR1 $$; echo unreachable' 2>/dev/null)
echo "status $? $output"
"$BIN" --no-diagnostics -c '(trap "echo S" EXIT; kill -TERM $BASHPID; echo unreachable); echo "subshell $?"' 2>/dev/null
output=$("$BIN" --no-diagnostics -c 'trap "echo E" EXIT; trap "echo T" TERM; kill -TERM $$; trap - TERM; kill -TERM $$; echo unreachable' 2>/dev/null)
echo "status $? $output"

echo "--- exec replaces a forked subshell ---"
"$BIN" --no-diagnostics -c '(trap "echo sub" EXIT; exec "$0"  -c "exit 3"); echo "status $?"' "$BIN"
"$BIN" --no-diagnostics -c '(x=$(exec echo inner); echo "$x"; echo more); echo end'

echo "--- an exported function follows its definition ---"
"$BIN" --no-diagnostics -c 'f() { echo one; }; export -f f; f() { echo two; }; koshkit env | koshkit grep -c "BASH_FUNC_f%%=() { echo two; }"; unset -f f; koshkit env | koshkit grep -c BASH_FUNC_f; echo end'

echo "--- a closed descriptor stays closed after the command ---"
"$BIN" --no-diagnostics -c 'f() { exec 3>x3; } 3>&-; f; echo hi >&3; echo "status $?"' 2>/dev/null
"$BIN" --no-diagnostics -c '{ exec 3>x3; } 3>&-; echo hi >&3; echo "status $?"' 2>/dev/null

echo "--- a utility keeps an inherited KOSHCONF ---"
ln -s "$BIN" nice
KOSHCONF=marker ./nice printenv KOSHCONF

echo "--- a user startup script that is not a regular file is skipped ---"
mkdir -p config/kosh/scripts
mkfifo config/kosh/scripts/rc.kosh
HOME="$PWD" XDG_CONFIG_HOME="$PWD/config" "$BIN" --login --no-diagnostics -c 'echo started' 2>/dev/null
echo "status $?"

echo "--- a removed working directory ---"
mkdir holder2 holder2/gone
cat > removed.sh <<'EOF'
cd holder2/gone || exit 1
rmdir ../gone
case $(pwd) in */holder2/gone) echo logical ;; *) echo other ;; esac
pwd -P 2>/dev/null; echo "physical $?"
case $(dirs) in */holder2/gone) echo stack ;; *) echo other ;; esac
koshkit realpath . 2>/dev/null; echo "realpath $?"
koshkit readlink -f . 2>/dev/null; echo "readlink $?"
EOF
"$BIN" --no-diagnostics removed.sh

echo "--- PWD with a dot component is not trusted ---"
"$BIN" --no-diagnostics -c 'cd /; PWD=/./; pwd'

echo "--- make -C returns from a removed directory ---"
mkdir makedir holder3
printf 'all:\n\t@echo built\n' > makedir/Makefile
"$BIN" --no-diagnostics -c 'cd holder3; rmdir ../holder3; koshkit make -C ../makedir; echo "status $?"'

echo "--- history refuses a file that is not regular ---"
"$BIN" --no-diagnostics -c 'history -r /dev/zero; echo "status $?"' 2>/dev/null

echo "--- the listing can be sourced again ---"
"$BIN" --no-diagnostics -c 'alias "x y=z"; alias -- -x=y; alias > listing; unalias -a; . ./listing; alias'

echo "--- formatting keeps a hash in a regular expression operand ---"
printf '#!/bin/bash\n[[ x =~ (x)#y ]]\n[[ x =~ (a #b) ]] # note\n' > regex-hash
"$BIN" --format regex-hash

echo "--- bounds in sequence stay valid and nested bounds do not ---"
"$BIN" -c "koshkit grep -cE 'a{30000}b{30000}c{30000}' /dev/null"
echo "status $?"
"$BIN" -c "koshkit grep -cE '(a{30000}b{30000}c{30000}){2}' /dev/null" 2>/dev/null
echo "status $?"

term_after_ready() {
  rm -f ready
  "$BIN" --no-diagnostics -c "$1" &
  target=$!
  attempt=0
  while [ ! -e ready ] && [ "$attempt" -lt 500 ]; do
    sleep 0.01
    attempt=$((attempt + 1))
  done
  sleep 0.5
  started=$(date +%s)
  kill -TERM "$target"
  wait "$target"
  term_status=$?
  elapsed=$(($(date +%s) - started))
  [ "$elapsed" -lt 3 ] && term_speed=prompt || term_speed=late
  echo "status $term_status, $term_speed"
}

echo "--- a fatal signal ends a wait and runs the EXIT trap ---"
term_after_ready 'trap "echo foreground" EXIT; : > ready; sleep 4 > /dev/null 2>&1'
term_after_ready 'trap "echo wait" EXIT; sleep 4 > /dev/null 2>&1 & : > ready; wait'
term_after_ready 'trap "echo substitution" EXIT; : > ready; x=$(sleep 4 2>&1)'
term_after_ready 'trap "echo function" EXIT; : > ready; x=${ sleep 4 2>&1; }'
mkfifo reader_pipe
sleep 4 > reader_pipe &
term_after_ready 'trap "echo read" EXIT; : > ready; read -r x < reader_pipe'

echo "--- clearing the EXIT trap restores the default signal action ---"
term_after_ready 'trap "echo never" EXIT; trap - EXIT; : > ready; sleep 4 > /dev/null 2>&1'

cat > exit_trap_body.sh <<'EOF'
trap 'x=$(echo captured); echo "got=$x"
sh -c "echo child"
sleep 0.01 & wait $!; echo "waited=$?"
mkdir trap_dir; rmdir trap_dir; [ -e trap_dir ] || echo removed
y=${ echo inner; }; echo "y=$y"
(echo subshell; exit 3); echo "status=$?"
echo stage | tr a-z A-Z' EXIT
EOF

echo "--- the EXIT trap action runs in order after a signal ---"
for name in TERM USR1 HUP; do
  output=$("$BIN" --no-diagnostics -c ". ./exit_trap_body.sh; kill -$name \$\$; echo unreachable" 2>/dev/null)
  term_status=$?
  printf '%s\n' "$output"
  echo "status $term_status"
done
term_after_ready '. ./exit_trap_body.sh; : > ready; sleep 4 > /dev/null 2>&1'
term_after_ready '. ./exit_trap_body.sh; sleep 4 > /dev/null 2>&1 & : > ready; wait'
sleep 4 > reader_pipe &
term_after_ready '. ./exit_trap_body.sh; : > ready; read -r line < reader_pipe'

echo "--- a fatal signal during the EXIT trap action ends the shell ---"
term_after_ready 'trap ": > ready; sleep 4 > /dev/null 2>&1; echo never" EXIT; exit 0'

echo "--- date keeps the local year across a year boundary ---"
TZ=EST5 "$BIN" -c "koshkit date -d @-5364644639 '+%F %T %z|%m|%Y'"

echo "--- a descriptor open for both directions ends with the capture ---"
"$BIN" --no-diagnostics -c 'y=${ exec 4<>/dev/stdout; echo b; }; echo "[$y]"'

echo "--- a capture works under a low descriptor limit ---"
"$BIN" --no-diagnostics -c 'ulimit -n 12; x=${ echo a; }; y=$(echo b); echo "$x $y"'
"$BIN" --no-diagnostics -c 'ulimit -n 8; x=${ echo a; }; y=$(echo b); echo "$x $y"'

echo "--- a long extglob subject keeps the stack ---"
"$BIN" --mood=bash --no-diagnostics -c 'shopt -s extglob; p=$(printf "a%.0s" {1..20000}); [[ $p == "$p"@(x|) ]]; echo "status $?"'
"$BIN" --mood=bash --no-diagnostics -c 'shopt -s extglob; p=$(printf "a%.0s" {1..20000}); q=$(printf "?%.0s" {1..20000}); [[ $p == $q@(x|) ]]; echo "status $?"' 2>/dev/null

echo "--- a loop reuses an absolute target across directories ---"
mkdir loop_a loop_b
"$BIN" --no-diagnostics -c 'abs=$PWD/loop_file; for d in loop_a loop_b; do cd "$d"; echo "$d" >> "$abs"; [ "$d" = loop_a ] && mv "$abs" "$abs.moved"; cd ..; done; cat "$abs.moved"'

echo "--- only the top shell can suspend ---"
"$BIN" --no-diagnostics -c '( suspend ); echo "subshell $?"; x=$(suspend); echo "substitution $?"; suspend & wait $!; echo "background $?"; suspend | cat; echo "stage ${PIPESTATUS[0]}"' 2>/dev/null

echo "--- xargs keeps its flags after a nested utility ---"
printf 'a/b\nc/d\n' | "$BIN" --no-diagnostics -c 'koshkit xargs -t -n 1 koshkit basename' 2>&1

echo "--- touch keeps a fractional date ---"
"$BIN" --no-diagnostics -c 'touch -d @1.5 fractional; koshkit stat -c %y fractional' | cut -d. -f2 | cut -c1-9

echo "--- csplit -k writes the rest only for a pattern past the input ---"
mkdir csplit_dir
printf 'a\nb\n' > csplit_dir/in
"$BIN" --no-diagnostics -c 'cd csplit_dir; koshkit csplit -k -s in bad' 2>/dev/null
echo "status $?"
ls csplit_dir
"$BIN" --no-diagnostics -c 'cd csplit_dir; koshkit csplit -k -s in 9' 2>/dev/null
echo "status $?"
ls csplit_dir

echo "--- cp merges a source that ends in a separator and a dot ---"
mkdir cp_source cp_target
echo merged > cp_source/file
"$BIN" --no-diagnostics -c 'koshkit cp -r cp_source/. cp_target; ls cp_target'

echo "--- cp refuses a second source for a created target ---"
mkdir cp_one cp_two cp_into
echo one > cp_one/same
echo two > cp_two/same
"$BIN" --no-diagnostics -c 'koshkit cp cp_one/same cp_two/same cp_into; echo "status $?"; cat cp_into/same' 2>/dev/null

echo "--- the git branch lookup skips special files ---"
mkdir git_fifo git_zero git_head git_head/.git
mkfifo git_fifo/.git git_head/.git/HEAD
ln -s /dev/zero git_zero/.git
for repo in git_fifo git_zero git_head; do
  (cd "$repo" && GIT_CEILING_DIRECTORIES=$dir timeout 10 "$BIN" -c 'echo "[$KOSH_GIT_BRANCH]"')
  echo "status $?"
done

echo "--- man skips a FIFO page ---"
mkdir -p man_fifo/man1
mkfifo man_fifo/man1/fifo.1
printf 'plain \\- a page\n' > man_fifo/man1/plain.1
MANPATH=$dir/man_fifo timeout 10 "$BIN" -c 'koshkit man -k page'
echo "status $?"
MANPATH=$dir/man_fifo timeout 10 "$BIN" -c 'koshkit man fifo' 2>/dev/null
echo "status $?"

echo "--- z drops stored ranks and times out of range ---"
mkdir z_low z_high
printf '%s\t5\t-9223372036854775808\n%s\t9223372036854775807\t1\n' "$dir/z_low" "$dir/z_high" > z_store
KOSH_DIRECTORY_HISTORY=$dir/z_store timeout 10 "$BIN" -c 'z z_high' >/dev/null 2>&1
echo "status $?"
cut -f1,2 z_store | sed "s|^$dir/||"

echo "--- history refuses a FIFO history file ---"
mkfifo history_fifo
for operation in -r -w -a; do
  KOSH_HISTORY_FILE=$dir/history_fifo timeout 10 "$BIN" -c "history $operation" < /dev/null 2>/dev/null
  echo "status $?"
done

echo "--- a here-string ignores a TMPDIR that is not a directory ---"
TMPDIR=$dir/missing "$BIN" -c 'cat <<<here; echo "status $?"'

echo "--- help reports a failed write of a builtin help ---"
"$BIN" --mood bash -c 'help cd >&-; echo "status $?"' 2>/dev/null
"$BIN" --mood bash -c 'help false [ >/dev/null; echo "status $?"'

echo "--- -o posix applies under the bash mood ---"
"$BIN" --mood=bash -o posix -c 'shopt -qo posix && echo on'
"$BIN" --mood=bash --posix -c 'shopt -qo posix && echo on'
"$BIN" --mood=bash -o posix +o posix -c 'shopt -qo posix || echo off'

echo "--- a multicall timeout passes --version to its command ---"
ln -s "$BIN" timeout
./timeout 1 sh -c 'echo "[$1]"' _ --version
./timeout --version | grep -c Koshka
