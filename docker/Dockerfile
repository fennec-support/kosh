#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# This is the official kosh image. It is built by the release workflow from the
# released static binaries, the manual pages, and the Bash completion.
#
# Every koshkit utility is a link to kosh in /usr/bin, except grep. Busybox
# keeps grep and only the applets that koshkit does not provide and that
# package scripts and basic administration need.
#
# Run:
#     docker run --rm -it ghcr.io/toiletbril/kosh

FROM alpine:latest

ARG TARGETARCH

COPY linux-${TARGETARCH}/kosh /usr/bin/kosh
COPY kosh.1 /usr/share/man/man1/kosh.1
COPY kosh.5 /usr/share/man/man5/kosh.5
COPY kosh.bash /usr/share/bash-completion/completions/kosh

RUN set -eu; \
    KEEP=' sh ash [ [[ test true false echo printf pwd kill awk dd install mktemp getopt hostname base64 md5sum sha256sum sha512sum grep egrep fgrep tar gzip gunzip zcat wget vi less adduser addgroup deluser delgroup su login passwd nologin '; \
    for APPLET in $(busybox --list-full); do \
      case "$KEEP" in \
        *" ${APPLET##*/} "*) ;; \
        *) busybox rm -f "/$APPLET" ;; \
      esac; \
    done; \
    for UTILITY in $(/usr/bin/kosh -c 'koshkit --list'); do \
      case "$KEEP" in \
        *" $UTILITY "*) ;; \
        *) busybox ln -sf kosh "/usr/bin/$UTILITY" ;; \
      esac; \
    done

CMD ["/usr/bin/kosh", "-L", "bash", "-Il"]
