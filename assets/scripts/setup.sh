#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Prepares the scratch environment for the demo recordings in
# KOSH_DEMO_DIRECTORY: a home directory with kosh installed from this checkout,
# a clone of the repository, the Open MPI sources for the lint demo, a Bash
# startup file that loads Homebrew and bash-completion, the default kosh
# settings, and a history file for the prompts. Neovim uses the configuration
# and plugins of the real home directory.
#
# Usage: setup.sh
# Needs: asciinema, agg, tmux, curl, nvim, and Homebrew in /opt/homebrew with
# cowsay installed.

set -eu

ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
DEMO_DIRECTORY=${KOSH_DEMO_DIRECTORY:-${TMPDIR:-/tmp}/kosh-demo}
DEMO_HOME=$DEMO_DIRECTORY/home
OPEN_MPI=openmpi-4.1.4

mkdir -p "$DEMO_HOME/.config" "$DEMO_HOME/.local/share" "$DEMO_HOME/Downloads" \
  "$DEMO_HOME/External" "$DEMO_HOME/Projects"

"${MAKE:-make}" -C "$ROOT" install PREFIX="$DEMO_HOME/.local" > /dev/null

if [ ! -d "$DEMO_HOME/Projects/kosh" ]; then
  git clone -q --shared "$ROOT" "$DEMO_HOME/Projects/kosh"
fi

if [ ! -d "$DEMO_HOME/External/$OPEN_MPI" ]; then
  curl -fsSL -o "$DEMO_DIRECTORY/$OPEN_MPI.tar.bz2" \
    "https://download.open-mpi.org/release/open-mpi/v4.1/$OPEN_MPI.tar.bz2"
  tar -xjf "$DEMO_DIRECTORY/$OPEN_MPI.tar.bz2" -C "$DEMO_HOME/External"
fi

for directory in .config/nvim .local/share/nvim; do
  if [ ! -e "$DEMO_HOME/$directory" ]; then
    ln -s "$HOME/$directory" "$DEMO_HOME/$directory"
  fi
done

cat > "$DEMO_HOME/.bashrc" << 'EOF'
# ~/.bashrc
eval "$(/opt/homebrew/bin/brew shellenv)"
[ -r /usr/share/bash-completion/bash_completion ] && . /usr/share/bash-completion/bash_completion
[ -r /opt/homebrew/etc/bash_completion.d/brew ] && . /opt/homebrew/etc/bash_completion.d/brew
EOF

cat > "$DEMO_DIRECTORY/tmux.conf" << 'EOF'
set -g status off
set -g escape-time 0
set -g default-terminal xterm-256color
EOF

cat > "$DEMO_DIRECTORY/env.sh" << EOF
#!/usr/bin/bash
# Runs a command in the demo environment.
exec env -i HOME='$DEMO_HOME' USER="\${USER:-$USER}" LOGNAME="\${USER:-$USER}" \\
  TERM=xterm-256color LANG=en_US.UTF-8 HOMEBREW_NO_ANALYTICS=1 \\
  HOMEBREW_NO_ENV_HINTS=1 \\
  PATH='$DEMO_HOME/.local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/bin:/bin' \\
  KOSH_HISTORY_FILE="\${KOSH_HISTORY_FILE:-$DEMO_DIRECTORY/history-probe}" "\$@"
EOF
chmod +x "$DEMO_DIRECTORY/env.sh"

cat > "$DEMO_DIRECTORY/history" << 'EOF'
git status
make MODE=rel
cd ~/Projects/kosh
nvim src/Main.cpp
kosh --lint ./configure
kosh --format --apply script.bash
brew update
brew upgrade
ssh lith
htop
docker build -t kosh .
docker run --rm -it kosh
curl -fsSL https://fennec.support/kosh/install | sh
tar -xzf release.tar.gz
grep -rn TODO src
du -sh ~/Downloads
git push
EOF

if [ ! -f "$DEMO_HOME/.config/kosh/kosh.conf" ]; then
  "$DEMO_DIRECTORY/env.sh" kosh -c 'koshconf create kosh' > /dev/null
fi

# the formula list of the brew completion comes from the downloaded API.
"$DEMO_DIRECTORY/env.sh" /opt/homebrew/bin/brew update > /dev/null 2>&1 || :

echo "The demo environment is ready in $DEMO_DIRECTORY."
