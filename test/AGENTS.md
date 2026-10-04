# Test suite guidance

## Goal

The test suite preserves observable behavior with the fewest source files and
target process launches that can express the contract. Every behavior test
launches Kosh or a reference shell and asserts its output, status, diagnostics,
side effects, or terminal interaction. A new regression normally extends an
existing owner. A new test file is the last choice.

For every test, identify why the test exists, what behavior it protects, and
why each expected output line is correct. Recheck that contract before adding,
changing, or refilling the test.

The debug test step should finish within 180 seconds. It must finish within 300
seconds. A change that adds measurable runtime should remove equivalent cost or
explain why the process boundary is required.

## Harness selection

Place a test in the cheapest harness that expresses its contract.

- `harness/kosh` owns native shell behavior and uses a matching golden.
- `harness/cli` owns executable and process boundary behavior and uses a
  matching golden.
- `harness/completion` owns completion output and uses a matching golden.
- `harness/highlight` owns debug highlighting output and uses a matching golden.
- `harness/sh` compares one fixture with dash.
- `harness/bash` compares one fixture with Bash 5.3 or newer.
- `unit` owns the contract of one base type or pure helper that no shell input
  reaches precisely, such as container growth, string state, static tables,
  path text operations, and the live counter window. It has no golden.
- `bench` owns performance workloads.

A unit test is `unit/<module>_test.cpp`. It includes `unit/Unit.hpp`, defines
`kosh_main` with one `RUN_TEST` per case, and returns `unit::finish`. A failed
`CHECK` or `CHECK_EQUAL` prints `file:line: expected X, got Y` to the error
stream, and the binary exits nonzero. `test/bin/run-unit` builds the test
through `src/Makefile`, runs it, and shows its output only on failure. A unit
test targeting one platform checks the platform inside `kosh_main` and still
compiles everywhere.

Make discovers each harness with a wildcard and creates one direct target for
each source path. A pattern recipe passes that path to `test/bin/run-test`.
The runner prints the status, invokes the process model runner, and appends
diagnostics to `failed.diff`.

`run-harness-kosh` launches the built executable with `-WWW`, captures the
complete output, compares it with `expected/<name>.out`, and removes the
temporary output. It does not suppress annoying diagnostics.

`run-harness-script` serves CLI, completion, and highlight fixtures. The
compatibility runners launch the reference shell and Kosh, compare output and
status, and compare stderr presence unless the source requests exact stderr.

The final `test` recipe fails when `failed.diff` is nonempty. Test preparation
creates that file and clears the temporary directory. Koshkit owns timeout,
file, and cleanup commands used by the runners.

## Refill

`make -C test refill` runs `test/bin/run-refill`. The refill runner visits Kosh,
CLI, completion, and highlight sources. `REFILL` limits the visit to named
stems. Any captured Kosh output replaces the matching golden. A timeout removes
its temporary output and leaves the existing golden unchanged.

Refill records process output. It does not validate behavior. Every changed
golden requires absolute validation against its fixture. Inspect the complete
output and verify statuses, diagnostics, side effects, and active platform
branches before accepting the golden.

POSIX and Bash fixtures do not use refill or repository goldens. Their runners
compare each reference shell result with the Kosh result directly.

## Focused validation

Resolve the source path and run its direct target.

```sh
make -C test harness/kosh/name.kosh
make -C test harness/cli/name.sh
make -C test harness/completion/name.sh
make -C test harness/highlight/name.sh
```

Pass `MODE`, `BIN`, and `TARGET` when the fixture must use a specific root
build. Run `make test` from the repository root for the complete suite. Read
every changed golden and inspect `failed.diff` before treating a suite as
successful.

## Ownership

Search all harnesses before adding a case. Extend the existing owner when the
new case exercises the same behavior. Keep a case separate when process exit,
fatal parsing, signals, jobs, terminal ownership, or shell state would change
after concatenation.

Every golden backed source has one matching file directly under `expected`.
Compatibility sources have no repository golden. Keep output deterministic and
use the shared test environment variables for temporary paths and platform
values.
