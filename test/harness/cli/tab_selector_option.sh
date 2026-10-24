unset KOSH_FLAGS
# The tab selector picks how several completion candidates are presented. The
# session value comes from --tab-selector, then from the plain listing --dumb
# asks for, and a mood change or a subshell keeps it.
echo "== the default session selector:"
"$BIN" -c 'koshconf get completion.menu_style'
echo "== --tab-selector plain:"
"$BIN" --tab-selector plain -c 'koshconf get completion.menu_style'
echo "== --tab-selector external:"
"$BIN" --tab-selector=external -c 'koshconf get completion.menu_style'
echo "== --dumb selects plain:"
"$BIN" --dumb -c 'koshconf get completion.menu_style'
echo "== an explicit value wins over --dumb:"
"$BIN" --dumb --tab-selector interactive -c 'koshconf get completion.menu_style'
echo "== an unknown command line value is rejected:"
"$BIN" --tab-selector bogus -c 'echo unreachable'
echo "status=$?"
echo "== a mood change keeps the selector:"
"$BIN" -c 'koshconf set completion.menu_style plain; set -M bash
koshconf get completion.menu_style'
echo "== the selector survives a subshell:"
"$BIN" -c 'koshconf set completion.menu_style external
(koshconf get completion.menu_style)'
