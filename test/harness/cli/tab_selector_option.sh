unset KOSH_FLAGS
# The tab selector picks how several completion candidates are presented. The
# session value comes from --tab-selector, then from the plain listing --dumb
# asks for, and koshconf reports and changes it at runtime.
echo "== the default session selector:"
"$BIN" -c 'koshconf get editor.tab_selector'
echo "== --tab-selector plain:"
"$BIN" --tab-selector plain -c 'koshconf get editor.tab_selector'
echo "== --tab-selector external:"
"$BIN" --tab-selector=external -c 'koshconf get editor.tab_selector'
echo "== --dumb selects plain:"
"$BIN" --dumb -c 'koshconf get editor.tab_selector'
echo "== an explicit value wins over --dumb:"
"$BIN" --dumb --tab-selector interactive -c 'koshconf get editor.tab_selector'
echo "== an unknown command line value is rejected:"
"$BIN" --tab-selector bogus -c 'echo unreachable'
echo "status=$?"
echo "== koshconf changes it at runtime:"
"$BIN" -c 'koshconf set editor.tab_selector external
koshconf get editor.tab_selector'
echo "== a mood change keeps the selector:"
"$BIN" -c 'koshconf set editor.tab_selector plain; set -M bash
koshconf get editor.tab_selector'
echo "== an unknown koshconf value is rejected:"
"$BIN" --no-annoying-diagnostics -c 'koshconf set editor.tab_selector bogus'
echo "status=$?"
echo "== set no longer accepts the long form:"
"$BIN" --no-annoying-diagnostics -c 'set --tab-selector plain'
echo "status=$?"
echo "== the selector survives a subshell:"
"$BIN" -c 'koshconf set editor.tab_selector external
(koshconf get editor.tab_selector)'
