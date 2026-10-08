# A bad substitution and an assignment to a readonly variable end the shell
# with status 2, through an eval as well, as dash does. An expansion error in
# a here-document body fails only the command it feeds, with status 2, unless
# that command is a special builtin.
x=abc
readonly r=1
( echo ${x!y}; echo same ); echo "bad=$?"
( eval 'echo ${x:}'; echo same ); echo "bad-eval=$?"
( r=2; echo same ); echo "readonly=$?"
( eval 'r=2'; echo same ); echo "readonly-eval=$?"
( for r in 1; do :; done; echo same ); echo "readonly-for=$?"
( eval 'for r in 1; do :; done'; echo same ); echo "readonly-for-eval=$?"
y=$(r=2; echo same); echo "substitution=[$y] $?"
zero=0
cat <<E
${unset_name?gone}
E
echo "heredoc-question=$?"
cat <<E; echo "heredoc-arithmetic=$?"
$((1 / zero))
E
cat <<E
${x!y}
E
echo "heredoc-bad=$?"
(
  set -u
  cat <<E
$unset_name
E
  echo "heredoc-nounset=$?"
)
cat <<E | cat
${unset_name?gone}
E
echo "heredoc-stage=$?"
heredoc_reader() { cat; }
heredoc_reader <<E
${unset_name?gone}
E
echo "heredoc-function=$?"
(
  : <<E
${unset_name?gone}
E
  echo same
)
echo "heredoc-special=$?"
echo end
r=2
echo not reached
