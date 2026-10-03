#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# This is the official kosh image. It is built by the release workflow from the
# released static binaries, the manual pages, and the Bash completion.
#
# Run:
#     docker run --rm -it ghcr.io/toiletbril/kosh

FROM alpine:latest

ARG TARGETARCH

COPY linux-${TARGETARCH}/kosh /usr/local/bin/kosh
COPY kosh.1 /usr/local/share/man/man1/kosh.1
COPY kosh.5 /usr/local/share/man/man5/kosh.5
COPY kosh.bash /usr/local/share/bash-completion/completions/kosh

CMD ["/usr/local/bin/kosh"]
