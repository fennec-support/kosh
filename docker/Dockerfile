#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# Run:
#     docker run --rm -it ghcr.io/toiletbril/kosh

FROM alpine:latest

ARG TARGETARCH

COPY linux-${TARGETARCH}/kosh /usr/bin/kosh
COPY kosh.1 /usr/share/man/man1/kosh.1
COPY kosh.5 /usr/share/man/man5/kosh.5
COPY kosh.bash /usr/share/bash-completion/completions/kosh

RUN <<EOF
set -euf

KEEP='
  ash [ [[ test true false echo printf pwd kill
  awk dd install mktemp getopt hostname base64
  md5sum sha256sum sha512sum grep egrep fgrep
  tar gzip gunzip zcat wget vi less
  adduser addgroup deluser delgroup su login passwd nologin
'

is_kept() {
  for KEPT in $KEEP; do
    if [ "$KEPT" = "$1" ]; then
      return 0
    fi
  done

  return 1
}

for APPLET in $(busybox --list-full); do
  if ! is_kept "${APPLET##*/}"; then
    busybox rm -f "/$APPLET"
  fi
done

for UTILITY in $(/usr/bin/kosh -c 'koshkit --list'); do
  if ! is_kept "$UTILITY"; then
    busybox ln -sf kosh "/usr/bin/$UTILITY"
  fi
done

for SHELL_NAME in sh bash dash; do
  busybox ln -sf /usr/bin/kosh "/bin/$SHELL_NAME"
done

busybox ln -sf kosh /usr/bin/shit
EOF

CMD ["/usr/bin/kosh", "-L", "bash", "-Il"]
