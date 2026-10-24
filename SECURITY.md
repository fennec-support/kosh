# Security policy

## Supported versions

Security fixes go to the `master` branch and to the latest release. Older
releases do not receive fixes.

## Reporting a vulnerability

Report a vulnerability privately through the repository's GitHub security
advisory form at
<https://github.com/fennec-support/kosh/security/advisories/new>. Do not open a
public issue for it.

A report should state the affected version or commit (`kosh --version`), the
platform, the steps or input that trigger the problem, and the impact you
observed. A small script or input file that reproduces it helps most.

A report receives an acknowledgement, and the fix is published together with
an advisory once a release carries it. The reporter is credited unless they ask
not to be.

## Scope

The following are in scope:

- memory safety errors in the shell, the line editor, and the bundled
  utilities, such as out-of-bounds access, use after free, or a crash on
  crafted input;
- escapes from a restricted shell (`kosh -r` or `rbash`);
- privilege retention after a setuid or setgid start, or after a start with
  file capabilities;
- unsafe handling of startup files, `kosh.conf`, `KOSHCONF`, history files, and
  completion caches, such as following a link that another user controls or
  reading a file another user can write;
- terminal escape injection through file names, completion candidates,
  diagnostics, or prompts;
- a bundled utility that acts on files outside the operands it receives.

The following are out of scope:

- behavior that a shell script requests explicitly, such as running a command
  or overwriting a file it can write;
- denial of service through unbounded input that a script chooses to process,
  unless a small input causes it;
- problems that need a modified binary, a modified build environment, or
  control of the user's own startup files.
