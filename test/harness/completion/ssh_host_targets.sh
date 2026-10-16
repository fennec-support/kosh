# ssh, scp, sftp, and rsync complete hostnames from ~/.ssh/config, known_hosts,
# and /etc/hosts, dropping wildcard patterns. tsh is the Teleport client and
# does not complete ssh hosts. HOME is pointed at a temp config and the prefixes
# are chosen so the /etc/hosts list (which varies per machine) never joins the
# stable candidates.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/.ssh"
cat > "$dir/.ssh/config" <<'CFG'
Host alpha-box beta-box
  HostName 10.0.0.1
Host gamma-wild-*
  User someone
CFG
echo "== ssh completes a config host by prefix:"
HOME="$dir" "$BIN" --debug-complete-at 'ssh alpha' </dev/null
echo "== scp completes the same hosts:"
HOME="$dir" "$BIN" --debug-complete-at 'scp beta' </dev/null
echo "== assimilate completes SSH hosts:"
HOME="$dir" "$BIN" --debug-complete-at 'assimilate alpha' </dev/null
echo "== sftp and rsync complete hosts too:"
HOME="$dir" "$BIN" --debug-complete-at 'sftp alpha' </dev/null
HOME="$dir" "$BIN" --debug-complete-at 'rsync beta' </dev/null
echo "== a wildcard Host pattern is not offered:"
HOME="$dir" "$BIN" --debug-complete-at 'ssh gamma' </dev/null
echo "== tsh does not complete an ssh host:"
HOME="$dir" PATH=/nonexistent "$BIN" --debug-complete-at 'tsh alpha' </dev/null
HOME="$dir" PATH=/nonexistent "$BIN" --debug-complete-at 'tsh ssh alpha' </dev/null
echo "== ssh keeps a login before the host:"
HOME="$dir" "$BIN" --debug-complete-at 'ssh admin@alp' </dev/null
