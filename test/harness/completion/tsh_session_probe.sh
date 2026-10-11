home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT
mkdir -p "$home/bin"
cat > "$home/bin/tsh" <<'EOF'
#!/bin/sh
case $1 in
status)
  [ -n "$FAKE_TSH_LOGGED_IN" ] && exit 0
  echo "ERROR: Active profile expired." >&2
  exit 1
  ;;
ls)
  : > "$FAKE_TSH_MARKER"
  printf 'Node Name Address\n--------- -------\nnode-bravo 10.0.0.2\nnode-brick 10.0.0.3\n'
  ;;
esac
EOF
chmod +x "$home/bin/tsh"
echo "== an expired Teleport session never reaches tsh ls, which would log in:"
HOME="$home" PATH="$home/bin:/usr/bin:/bin" FAKE_TSH_MARKER="$home/listed" \
  "$BIN" --debug-complete-at 'tsh ssh root@node-br' </dev/null
if [ -e "$home/listed" ]; then echo "tsh ls ran"; else echo "tsh ls skipped"; fi
echo "== a valid session lists the nodes:"
HOME="$home" PATH="$home/bin:/usr/bin:/bin" FAKE_TSH_MARKER="$home/listed" \
  FAKE_TSH_LOGGED_IN=1 "$BIN" --debug-complete-at 'tsh ssh node-br' </dev/null
if [ -e "$home/listed" ]; then echo "tsh ls ran"; else echo "tsh ls skipped"; fi
echo "== a login before the node is kept:"
HOME="$home" PATH="$home/bin:/usr/bin:/bin" FAKE_TSH_MARKER="$home/listed" \
  FAKE_TSH_LOGGED_IN=1 "$BIN" --debug-complete-at 'tsh ssh root@node-bri' </dev/null
