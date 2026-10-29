# Migrate from Bash to Koshka

> Migrate an interactive shell environment and login shell from Bash to Koshka.

2026-10-28

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

The `--mood bash` flag selects the Bash compatibility mood for that execution. If the `bash` binary remains installed on the host, `bash script.bash` and a script started as a command with a `#!/bin/bash` shebang run under the installed Bash, unless mimicry (`-I`) runs the script inside Koshka.

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

1. Keep the `bash` binary installed on the system until migration testing is complete.
2. Confirm from an active Bash session that the absolute path of `bash` is present in `/etc/shells`, then assign `bash` as the login shell.

```sh
chsh -s "$(command -v bash)"
```

3. Sign out and sign back in to restore Bash as the active login shell.
4. Remove any direct Koshka startup commands from terminal emulator profiles before deleting the `kosh` binary.

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
