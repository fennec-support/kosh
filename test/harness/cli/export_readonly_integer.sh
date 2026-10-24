unset KOSH_FLAGS
# export of a read-only name fails, and a bare export of a read-only name
# publishes its value to a child.
echo "== export of a read-only name fails:"
"$BIN" -c 'readonly x=old; export x=new'; echo "rc=$?"
echo "== a bare export publishes a read-only value:"
"$BIN" -c 'readonly x=old; export x; koshkit env | grep "^x=old$"'
