# Migrate from Bash to Koshka

> Migrate an interactive shell environment and login shell from Bash to Koshka.

2026-11-05

Prerequisites - An installed `kosh` binary, an active Bash session, and a familiarity with [kosh(1) invocation](kosh_1.md) and [kosh(5) configuration](kosh_5.md).

Koshka is a command shell that controls execution behavior and startup file loading through operational modes called moods. The default `kosh` mood checks shell code prior to execution and rejects scripts containing errors. The `bash` mood provides Bash compatibility for startup configurations and legacy scripts. Try Koshka in a new terminal before changing the login shell, and keep the current Bash session open in a separate terminal window while testing startup files to prevent terminal lockout.

## Core primitives

| Primitive | Role in migration |
| --- | --- |
| `kosh` mood | Default strict execution mode and native startup sequence |
| `bash` mood | Compatibility mode for Bash scripts and Bash initialization files |
| `rc.kosh` | Native Koshka user initialization script |
| `koshconf` | Configuration utility used to persist startup settings |

## Load existing Bash startup files in Koshka

Launch an interactive Koshka session with both the `kosh` and `bash` startup moods to retain existing Bash aliases, functions, and environment variables.

```sh
kosh -L kosh,bash
```

When `-L kosh,bash` is passed, Koshka executes the `kosh` startup sequence first and the `bash` startup sequence second.

| Session type | Mood | Initialization files read | Constraints and defaults |
| --- | --- | --- | --- |
| Interactive or login | `kosh` (default) | `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh` | Falls back to `~/.config/kosh/scripts/rc.kosh` if `XDG_CONFIG_HOME` is unset, empty, or not an absolute path. Skipped by the Unix superuser. Does not read `~/.bashrc` unless `rc.kosh` sources it. |
| Interactive non-login | `bash` | System Bash startup file, then `~/.bashrc` | Applies when the session mood is `bash` (`--mood bash` or `mood=bash`) or when `-L` or `init_moods` lists `bash`. |
| Interactive login | `bash` | `/etc/profile`, then the first available Bash login profile | The login profile must explicitly source `~/.bashrc` to load aliases and functions. |

Persist the combined startup sequence across future sessions by setting `init_moods` inside an active Koshka session.

```sh
koshconf set init_moods kosh,bash --persist
```

Inspect `rc.kosh` before persisting `init_moods` if the configuration was generated with `koshconf create kosh`. That preset already sources Bash startup files directly inside `rc.kosh`. Enable only one loading mechanism so the Bash startup files are not executed twice.

## Execute Bash scripts that fail strict validation

Run a Bash script under the `bash` mood when the default `kosh` mood rejects its syntax.

```sh
kosh --mood bash script.bash
```

The `--mood bash` flag selects the Bash compatibility mood for that execution. `bash script.bash` uses the first `bash` in `PATH`; use the original Bash executable's absolute path to bypass a Koshka link. A script with a `#!/bin/bash` shebang still runs `/bin/bash`, unless mimicry (`-I`) runs the script inside Koshka.

## Run Koshka under the name bash

A link named `bash` to the `kosh` binary starts Koshka in the `bash` mood with the Bash startup files. Place it in a directory that precedes the installed Bash in `PATH`. `assimilate --link-mood=bash` creates the same link.

```sh
ln -s "$(command -v kosh)" ~/.local/bin/bash
```

A configured `mood` does not override the link name, but `init_moods=kosh,bash` also sources `rc.kosh`. `bash --version` prints the Koshka banner. A shebang with an absolute path, such as `#!/bin/bash`, still runs the installed Bash.

## Use Koshka as a Bash-compatible login shell

On Unix, install Koshka under `/usr/local/bin/kosh` and select `/usr/local/bin/bash` as your login shell to start Koshka in the `bash` mood. Leave the system Bash binary intact. Keep your current Bash session open, and record the absolute path of your current login shell (`printf '%s\n' "$SHELL"` in the original login session) for rollback.

**Safety prerequisite:** Before changing the login shell, verify that you can log in as `root` with its password and restore your account's previous shell. If direct root login is disabled, as on a default macOS installation, verify that you can log in to a separate administrator account and use `sudo` there. Do not proceed if your only recovery path depends on the account whose shell you are changing.

1. Copy the verified executable into `/usr/local/bin`, or link to a stable executable in a directory managed by the system administrator. Create `/usr/local/bin` first if it does not exist. If `/usr/local/bin/kosh` is already a trusted Koshka installation, reuse it. Do not overwrite existing files or links at either destination.

```sh
sudo mkdir -p /usr/local/bin
sudo install -m 0755 /absolute/path/to/kosh /usr/local/bin/kosh
```

To link instead of copying, use this command in place of `install`:

```sh
sudo ln -s /absolute/path/to/kosh /usr/local/bin/kosh
```

2. Create the `bash` link and check its version and interactive login startup before changing your account.

```sh
sudo ln -s /usr/local/bin/kosh /usr/local/bin/bash
/usr/local/bin/bash --version
/usr/local/bin/bash -l -i
```

At the trial shell prompt, run the following commands. The version must end in `-kosh`. Exit the trial shell to continue.

```sh
printf '%s\n' "$BASH_VERSION"
exit
```

3. Add the login shell path to `/etc/shells` if it is not already listed, then change your own login shell. Open a new login session to verify that it starts Koshka before closing your original session.

```sh
(grep -Fxq /usr/local/bin/bash /etc/shells ||
  printf '%s\n' /usr/local/bin/bash | sudo tee -a /etc/shells) &&
  chsh -s /usr/local/bin/bash
```

This changes the login shell for your account. If `/usr/local/bin` also precedes the directory containing the original Bash in `PATH`, it changes what `bash` and `#!/usr/bin/env bash` resolve to. It does not change an absolute shebang such as `#!/bin/bash`.

## Set Koshka as the default login shell

1. Verify that the `kosh` binary is reachable in `PATH` and executes cleanly from the current Bash session.

```sh
command -v kosh
kosh --version
```

2. Register the absolute path of `kosh` in `/etc/shells` and update the user login shell with `chsh` on Unix systems.

```sh
kosh_path="$(command -v kosh)"
grep -Fx "$kosh_path" /etc/shells || printf '%s\n' "$kosh_path" | sudo tee -a /etc/shells
chsh -s "$(command -v kosh)"
```

3. Sign out of the system session and sign back in to activate the new login shell. If a terminal emulator overrides the system login shell with a custom command, update the profile settings inside that terminal application. On Windows, `/etc/shells` and `chsh` are not used. Configure Koshka directly as a terminal profile on Windows.

## Revert the login shell to Bash

1. Keep the original Bash executable installed. From an active session, confirm that the original shell's absolute path is present in `/etc/shells`, then use that path with `chsh`. Do not use `command -v bash` for rollback: it may return the Koshka link. The following example assumes that the original shell was `/bin/bash`.

```sh
chsh -s /bin/bash
```

If the new shell prevents you from logging in, log in as `root` and restore the recorded shell for the affected account. From a separate administrator account, run the same command with `sudo`. Replace `your_username` and `/bin/bash` with the affected account name and the previous shell's absolute path.

```sh
chsh -s /bin/bash your_username
```

2. Open a new login session and verify that it starts the original Bash.
3. If no account or terminal profile uses the Koshka link, remove `/usr/local/bin/bash` and its `/etc/shells` entry if you added them. Remove any direct Koshka startup commands from terminal profiles before deleting the `kosh` binary.

## Uninstall the Koshka binary

1. Revert the system login shell and terminal emulator profiles to another shell before removing Koshka.
2. Remove the binary using the installation method that matches the host environment.

| Installation method | Default binary path | Removal procedure |
| --- | --- | --- |
| Package manager | Managed by package manager | Run the package manager removal command, such as `brew uninstall kosh` for Homebrew. |
| Standalone Unix installer (user) | `~/.local/bin/kosh` unless installed elsewhere | Verify the exact path with `command -v kosh`, delete the binary, and remove its line from `/etc/shells` after selecting another login shell. |
| Standalone Unix installer (root) | `/usr/local/bin/kosh` unless installed elsewhere | Verify the exact path with `command -v kosh`, delete the binary, and remove its line from `/etc/shells` after selecting another login shell. |
| Windows installer | `%LOCALAPPDATA%\kosh\bin\kosh.exe` unless installed elsewhere | Close all Koshka sessions, delete `kosh.exe`, and remove `%LOCALAPPDATA%\kosh\bin` from the user `PATH` if it was added during setup. |

3. Retain configuration and command history files if a future reinstallation is planned.

## Related reference manuals

* Read the [kosh(1) shell reference](kosh_1.md) for command-line flags and invocation syntax.
* Read the [kosh(5) configuration manual](kosh_5.md) for mood options and startup file specifications.
