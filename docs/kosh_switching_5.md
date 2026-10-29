# Migrate from Bash to Koshka

> Migrate an interactive shell environment and login shell from Bash to Koshka[cite: 1].

2026-10-28[cite: 1]

Prerequisites - An installed `kosh` binary, an active Bash session, and a familiarity with [kosh(1) invocation](kosh_1.md) and [kosh(5) configuration](kosh_5.md)[cite: 1].

Koshka is a command shell that controls execution behavior and startup file loading through operational modes called moods[cite: 1]. The default `kosh` mood validates shell syntax prior to execution and rejects scripts containing errors[cite: 1]. The `bash` mood provides Bash compatibility for startup configurations and legacy scripts[cite: 1]. Keep the current Bash session open in a separate terminal window while testing startup files to prevent terminal lockout[cite: 1].

## Core primitives

| Primitive | Role in migration |
| --- | --- |
| `kosh` mood | Default strict execution mode and native startup sequence[cite: 1] |
| `bash` mood | Compatibility mode for Bash scripts and Bash initialization files[cite: 1] |
| `rc.kosh` | Native Koshka user initialization script[cite: 1] |
| `koshconf` | Configuration utility used to persist startup settings[cite: 1] |

## Load existing Bash startup files in Koshka

Launch an interactive Koshka session with both the `kosh` and `bash` startup moods to retain existing Bash aliases, functions, and environment variables[cite: 1].

```sh
kosh -L kosh,bash
```

When `-L kosh,bash` is passed, Koshka executes the `kosh` startup sequence first and the `bash` startup sequence second[cite: 1].

| Session type | Mood | Initialization files read | Constraints and defaults |
| --- | --- | --- | --- |
| Interactive | `kosh` (default) | `$XDG_CONFIG_HOME/kosh/scripts/rc.kosh`[cite: 1] | Falls back to `~/.config/kosh/scripts/rc.kosh` if `XDG_CONFIG_HOME` is unset or relative[cite: 1]. Skipped by the Unix superuser[cite: 1]. Does not read `~/.bashrc`[cite: 1]. |
| Interactive non-login | `bash` | System Bash startup file, then `~/.bashrc`[cite: 1] | Requires `-L bash` or `init_moods` configuration[cite: 1]. |
| Interactive login | `bash` | `/etc/profile`, then the first available Bash login profile[cite: 1] | The login profile must explicitly source `~/.bashrc` to load aliases and functions[cite: 1]. |

Persist the combined startup sequence across future sessions by setting `init_moods` inside an active Koshka session[cite: 1].

```sh
koshconf set init_moods kosh,bash --persist
```

Inspect `rc.kosh` before persisting `init_moods` if the configuration was generated with `koshconf create kosh`[cite: 1]. That preset already sources Bash startup files directly inside `rc.kosh`[cite: 1]. Enable only one loading mechanism so the Bash startup files are not executed twice[cite: 1].

## Execute Bash scripts that fail strict validation

Run a Bash script under the `bash` mood when the default `kosh` mood rejects its syntax[cite: 1].

```sh
kosh --mood bash script.bash
```

The `--mood bash` flag switches the parser to Bash compatibility mode for that execution[cite: 1]. If the `bash` binary remains installed on the host, `bash script.bash` and scripts with a `#!/bin/bash` shebang continue to execute through the system Bash interpreter[cite: 1].

## Set Koshka as the default login shell

1. Verify that the `kosh` binary is reachable in `PATH` and executes cleanly from the current Bash session[cite: 1].

```sh
command -v kosh
kosh --version
```

2. Register the absolute path of `kosh` in `/etc/shells` and update the user login shell with `chsh` on Unix systems[cite: 1].

```sh
kosh_path="$(command -v kosh)"
grep -Fx "$kosh_path" /etc/shells || printf '%s\n' "$kosh_path" | sudo tee -a /etc/shells
chsh -s "$(command -v kosh)"
```

3. Sign out of the system session and sign back in to activate the new login shell[cite: 1]. If a terminal emulator overrides the system login shell with a custom command, update the profile settings inside that terminal application[cite: 1]. On Windows, `/etc/shells` and `chsh` are not used[cite: 1]. Configure Koshka directly as a terminal profile on Windows[cite: 1].

## Revert the login shell to Bash

1. Keep the `bash` binary installed on the system until migration testing is complete[cite: 1].
2. Confirm from an active Bash session that the absolute path of `bash` is present in `/etc/shells`, then assign `bash` as the login shell[cite: 1].

```sh
chsh -s "$(command -v bash)"
```

3. Sign out and sign back in to restore Bash as the active login shell[cite: 1].
4. Remove any direct Koshka startup commands from terminal emulator profiles before deleting the `kosh` binary[cite: 1].

## Uninstall the Koshka binary

1. Revert the system login shell and terminal emulator profiles to another shell before removing Koshka[cite: 1].
2. Remove the binary using the installation method that matches the host environment[cite: 1].

| Installation method | Default binary path | Removal procedure |
| --- | --- | --- |
| Package manager | Managed by package manager | Run the package manager removal command, such as `brew uninstall kosh` for Homebrew[cite: 1]. |
| Standalone Unix installer (user) | `~/.local/bin/kosh`[cite: 1] | Verify the exact path with `command -v kosh`, delete the binary, and remove its line from `/etc/shells`[cite: 1]. |
| Standalone Unix installer (root) | `/usr/local/bin/kosh`[cite: 1] | Verify the exact path with `command -v kosh`, delete the binary, and remove its line from `/etc/shells`[cite: 1]. |
| Windows installer | `%LOCALAPPDATA%\kosh\bin\kosh.exe`[cite: 1] | Close all Koshka sessions, delete `kosh.exe`, and remove `%LOCALAPPDATA%\kosh\bin` from the user `PATH`[cite: 1]. |

3. Retain configuration and command history files if a future reinstallation is planned[cite: 1].

## Related reference manuals

* Read the [kosh(1) shell reference](kosh_1.md) for command-line flags and invocation syntax[cite: 1].
* Read the [kosh(5) configuration manual](kosh_5.md) for mood options and startup file specifications[cite: 1].
