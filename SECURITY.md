# Security policy

## Supported versions

Only the `master` branch and the latest release receive security fixes, unless
explicitly states otherwise.

## Reporting a vulnerability

Report vulnerabilities privately through the GitHub security advisory form at
<https://github.com/fennec-support/kosh/security/advisories/new>, and do not
open a public issue.

Reports will be acknowledged. When a release includes a fix, the fix and its
advisory will be published together, and the reporter will be credited unless
they ask not to be.

## In scope

- Memory safety, segmentation faults, address sanitizer alerts.
- Code execution during static or interactive inspection.
- Command or code injection from untrusted data.
- Restricted shell bypasses.
- Privilege retention and startup bypass failures.
- System file, atomic write, traversal, and IPC flaws.
- Terminal escape and bidirectional text injection.

## Out of scope

- Explicitly executed shell code.
- Caller-controlled user configuration or environment.
- Documented behavior.
