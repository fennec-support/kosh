# Switching from bash

## Name

kosh-switching - switch from Bash to Koshka

## Description

Koshka's default `kosh` mood checks shell code before it runs and refuses code
with errors. Try Koshka in a new terminal before changing your login shell.
Keep your current Bash session open while you check your startup files.

## Keep your Bash setup

Start an interactive Koshka session with both startup moods.

```sh
kosh -L kosh,bash
```

Koshka reads its own startup script and then your Bash startup files. In an
interactive session that is not a login shell, the Bash mood reads the system
Bash startup file and `~/.bashrc`. A login shell reads `/etc/profile` and the
first available Bash login profile. That profile must source `~/.bashrc` if
you want its aliases and functions in a login session.

The default `kosh` startup mood reads
`$XDG_CONFIG_HOME/kosh/scripts/rc.kosh`, or
`~/.config/kosh/scripts/rc.kosh` when `XDG_CONFIG_HOME` is unset or is not an
absolute path. On Unix, the superuser skips this user configuration directory.
The default startup mood does not automatically read `~/.bashrc`.

To keep both startup moods for future sessions, run this command inside Koshka.

```sh
koshconf set init_moods kosh,bash --persist
```

If you used `koshconf create kosh`, inspect the generated `rc.kosh` first.
That preset already sources Bash startup files. Choose one method to avoid
running your Bash startup files twice.

## Run Bash scripts

Use the Bash mood when the default strict mood refuses a script that you want
to run with Bash behavior.

```sh
kosh --mood bash script.bash
```

This selects Koshka's Bash compatibility mood. You can also keep Bash installed
and run a script with `bash script.bash`. Existing `#!/bin/bash` shebangs still
select Bash.

## Change your login shell

On a Unix system with `chsh`, first find the installed binary and check that
you can run it.

```sh
command -v kosh
kosh --version
```

Add that absolute path to `/etc/shells` if it is not already listed. Run these
commands from your current Bash session.

```sh
kosh_path="$(command -v kosh)"
grep -Fx "$kosh_path" /etc/shells || printf '%s\n' "$kosh_path" | sudo tee -a /etc/shells
chsh -s "$(command -v kosh)"
```

Sign out and sign in again to use the new login shell. A terminal application
with its own shell command may also need its settings updated. Windows does
not use `/etc/shells` or `chsh`. Set Koshka as a terminal profile there.

## Switch back

Keep Bash installed until you have finished switching. From a Bash session,
check that its absolute path is listed in `/etc/shells`, then select it.

```sh
chsh -s "$(command -v bash)"
```

Sign out and sign in again. Restore any terminal profile that starts Koshka
directly before removing the binary.

## Remove Koshka

If you installed Koshka with a package manager, remove it with that package
manager. For example, a Homebrew installation can be removed with
`brew uninstall kosh`.

The standalone Unix installer normally puts the binary in `~/.local/bin`.
When run as root, it uses `/usr/local/bin`. A custom installation may use a
different directory. Check `command -v kosh` before removing the installed
binary. Remove its entry from `/etc/shells` after selecting another login
shell.

The Windows installer normally puts `kosh.exe` in
`%LOCALAPPDATA%\kosh\bin`. Close Koshka sessions, remove the installed binary,
and remove that directory from your user `PATH` if you added it during setup.
Keep any configuration or history files that you want to reuse.

## See also

The [kosh(1) shell reference](kosh_1.md) covers shell commands and invocation.
The [kosh(5) configuration manual](kosh_5.md) covers options and startup
settings.
