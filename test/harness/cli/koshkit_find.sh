# The find utility walks a fixed temporary tree, so the relative paths it prints
# stay the same on every machine. The children of a directory are listed in
# sorted order, so the whole walk is deterministic.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

"$BIN" -c 'koshkit mkdir -p a/b/c'
"$BIN" -c 'koshkit touch a/one.txt'
"$BIN" -c 'koshkit touch a/MiXeD.TxT'
"$BIN" -c 'koshkit touch a/b/two.log'
"$BIN" -c 'koshkit touch a/b/c/three.txt'
"$BIN" -c 'koshkit ln -sf missing broken'
"$BIN" -c 'koshkit mkdir -p long/abcdefghijklmnopqrstuvwxyz/segment'
"$BIN" -c 'koshkit touch long/abcdefghijklmnopqrstuvwxyz/segment/leaf'
"$BIN" -c 'koshkit ln -s a/b/c/three.txt file-link'

echo "--- find all ---"
"$BIN" -c 'koshkit find .'
echo "--- find -name *.txt ---"
"$BIN" -c 'koshkit find . -name "*.txt"'
echo "--- find -iname *.TXT ---"
"$BIN" -c 'koshkit find . -iname "*.TXT"'
"$BIN" -c 'koshkit touch "a/literal[bracket].txt"'
echo "--- find escaped metacharacter ---"
"$BIN" -c 'koshkit find . -name "literal\[bracket\].txt"'
echo "--- find slash-containing no match ---"
"$BIN" -c 'koshkit find . -iname "a/*.TXT"; printf "status=%s\\n" "$?"'
echo "--- find no match ---"
"$BIN" -c 'koshkit find . -iname "*.does-not-exist"; printf "status=%s\\n" "$?"'
echo "--- find -type d ---"
"$BIN" -c 'koshkit find . -type d'
echo "--- find -maxdepth 1 ---"
"$BIN" -c 'koshkit find . -maxdepth 1'
echo "--- find -maxdepth 2 -name *.txt ---"
"$BIN" -c 'koshkit find . -maxdepth 2 -name "*.txt"'
echo "--- find -maxdepth 1 -type d ---"
"$BIN" -c 'koshkit find . -maxdepth 1 -type d'
echo "--- find -mindepth 3 -type f ---"
"$BIN" -c 'koshkit find . -mindepth 3 -type f'
echo "--- find a named root ---"
"$BIN" -c 'koshkit find a/b'
echo "--- find multiple roots ---"
"$BIN" -c 'koshkit find a/one.txt a/b -maxdepth 0'
echo "--- find a dangling symlink root ---"
"$BIN" -c 'koshkit find broken -type l -maxdepth 0'
echo "--- find an explicit file symlink root ---"
"$BIN" -c 'koshkit find file-link -type l -maxdepth 0'
echo "--- find a long path ---"
"$BIN" -c 'koshkit find long -name leaf'
echo "--- find unknown predicate ---"
"$BIN" -c 'koshkit find . -bogus' 2>&1
echo "--- find missing -name argument ---"
"$BIN" -c 'koshkit find . -name' 2>&1
echo "--- find invalid -type argument ---"
"$BIN" -c 'koshkit find . -type x' 2>&1
echo "--- find missing -type argument ---"
"$BIN" -c 'koshkit find . -type' 2>&1
echo "--- find negative -maxdepth argument ---"
"$BIN" -c 'koshkit find . -maxdepth -1' 2>&1
echo "--- find out of range -maxdepth argument ---"
"$BIN" -c 'koshkit find . -maxdepth 99999999999999999999' 2>&1
echo "status=$?"
"$BIN" -c 'koshkit find . -mindepth 99999999999999999999' 2>&1
echo "status=$?"
echo "--- find invalid -mindepth argument ---"
"$BIN" -c 'koshkit find . -mindepth many' 2>&1
echo "--- find missing -maxdepth argument ---"
"$BIN" -c 'koshkit find . -maxdepth' 2>&1
echo "--- find valid -maxdepth argument ---"
"$BIN" -c 'koshkit find . -maxdepth 0'
echo "--- find unreadable nested path ---"
mkdir -p a/private
: > a/private/entry
chmod 000 a/private
if ls a/private >/dev/null 2>&1; then
  readable_output=$("$BIN" -c 'koshkit find a/private' 2>&1)
  readable_status=$?
  chmod 700 a/private
  case $readable_output in
    *a/private/entry*)
      if [ "$readable_status" -eq 0 ]; then
        echo "find-unreadable=checked"
      else
        echo "find-unreadable=failed"
      fi
      ;;
    *) echo "find-unreadable=failed" ;;
  esac
else
  unreadable_output=$("$BIN" -c 'koshkit find a/private' 2>&1)
  unreadable_status=$?
  chmod 700 a/private
  case $unreadable_output in
    *entry*) echo "find-unreadable=failed" ;;
    *)
      if [ "$unreadable_status" -ne 0 ]; then
        echo "find-unreadable=checked"
      else
        echo "find-unreadable=failed"
      fi
      ;;
  esac
fi
unset KOSH_FLAGS

echo "--- find -name ? follows the locale codeset ---"
"$BIN" -c 'koshkit mkdir -p wide; koshkit touch wide/x wide/é'
"$BIN" -c 'LC_ALL=C.UTF-8; export LC_ALL; koshkit find wide -name "?"'
"$BIN" -c 'LC_ALL=C; export LC_ALL; koshkit find wide -name "?"'
echo "--- find -name bracket expressions ---"
"$BIN" -c 'koshkit mkdir -p br; koshkit touch br/a br/b br/c br/E br/d br/1 br/2 br/ab "br/!" br/- br/é'
"$BIN" -c 'koshkit find br -maxdepth 1 -name "[a-c]"'
"$BIN" -c 'koshkit find br -maxdepth 1 -name "[E]"'
"$BIN" -c 'koshkit find br -maxdepth 1 -name "[!a]*"'
"$BIN" -c 'LC_ALL=C.UTF-8; export LC_ALL; koshkit find br -maxdepth 1 -name "[^a-c]"'
"$BIN" -c 'koshkit find br -maxdepth 1 -name "[[:digit:]]"'
"$BIN" -c 'koshkit find br -maxdepth 1 -name "[-a]"'
"$BIN" -c 'koshkit find br -maxdepth 1 -name "[]a]"'
echo "--- find -name multibyte bracket follows the locale codeset ---"
"$BIN" -c 'LC_ALL=C.UTF-8; export LC_ALL; koshkit find br -maxdepth 1 -name "[é]*"'
"$BIN" -c 'LC_ALL=C; export LC_ALL; koshkit find br -maxdepth 1 -name "[é]*"'
echo "--- find roots with trailing separators ---"
"$BIN" -c 'koshkit find a/ -maxdepth 0 -name a'
"$BIN" -c 'koshkit find a// -maxdepth 0 -name a'
"$BIN" -c 'koshkit find a/b/ -name b'
"$BIN" -c 'koshkit find a/ -maxdepth 0 -name b; printf "status=%s\\n" "$?"'
"$BIN" -c 'koshkit find / -maxdepth 0 -name ""; printf "status=%s\\n" "$?"'
echo "--- find -exec runs a command for each entry ---"
"$BIN" -c 'koshkit find a -name "*.txt" -exec echo got {} \;'
echo "--- find -exec batches entries after plus ---"
"$BIN" -c 'koshkit find a -name "*.txt" -exec echo batch {} +'
echo "--- find -exec failure skips the later print ---"
"$BIN" -c 'koshkit find a -name "*.txt" -exec false \; -print'
"$BIN" -c 'koshkit find a -name "*.txt" -exec true \; -print'
echo "--- find -print0 separates paths with null bytes ---"
"$BIN" -c 'koshkit find a -name "*.txt" -print0 | koshkit xargs -0 echo'
echo "--- find -exec without a terminator ---"
"$BIN" -c 'koshkit find a -exec echo {}; printf "status=%s\\n" "$?"' 2>&1
"$BIN" -c 'koshkit find a -exec echo \+; printf "status=%s\\n" "$?"' 2>&1
