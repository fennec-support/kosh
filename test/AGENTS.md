# Test suite guidance

## Goal

The test suite preserves observable behavior with the fewest source files and
target process launches that can express the contract. Every behavior test
launches Kosh or a reference shell and asserts its output, status, diagnostics,
side effects, or terminal interaction. A new regression normally extends an
existing owner. A new test file is the last choice.

For every test, identify why it exists, what behavior it protects, and why each
expected output line is correct. Recheck that contract before adding, changing,
or refilling the test.

The debug test step should finish within 180 seconds and must finish within 300
seconds. A change that adds measurable runtime should remove equivalent cost or
explain why the process boundary is required.

## Harness selection

Place a test in the cheapest harness that expresses its contract.

- `harness/kosh` owns native shell behavior and uses a matching golden.
- `harness/cli` owns executable and process boundary behavior and uses a
  matching golden.
- `harness/completion` owns completion output and uses a matching golden.
- `harness/highlight` owns debug highlighting output and uses a matching golden.
- `harness/sh` compares one test case with dash.
- `harness/bash` compares one test case with Bash 5.3 or newer.
- `bench` owns performance workloads.

Make discovers each harness with a wildcard and creates one direct target for
each source path. A pattern recipe passes that path to `test/bin/run-test`.
The runner prints the status, invokes the process model runner, and appends
diagnostics to `failed.diff`.

`run-harness-kosh` launches the built executable with `-WWW`, compares the
complete output with `expected/<name>.out`, and removes the temporary output.

`run-harness-script` serves CLI, completion, and highlight tests. The
compatibility runners launch the reference shell and Kosh, compare output and
status, and compare stderr presence unless the source requests exact stderr.

The final `test` recipe fails when `failed.diff` is nonempty. Test preparation
creates that file and clears the temporary directory. Koshkit provides the
timeout, file, and cleanup commands used by the runners.

## Refill

`make -C test refill` runs `test/bin/run-refill`, which visits Kosh, CLI,
completion, and highlight sources. `REFILL` limits the visit to the given
stems. Any captured Kosh output will replace the matching golden. When a case
times out, refill removes its temporary output and leaves the existing golden
unchanged.

## Focused validation

Resolve the source path and run its direct target.

```sh
make -C test harness/kosh/name.kosh
make -C test harness/cli/name.sh
make -C test harness/completion/name.sh
make -C test harness/highlight/name.sh
```

Inspect `failed.diff` before treating a suite as successful.

## Ownership

Search all harnesses before adding a case. Extend the existing owner when the
new case exercises the same behavior. Keep a case separate when process exit,
fatal parsing, signals, jobs, terminal ownership, or shell state would change
after concatenation.

A compatibility matrix keeps the cases Kosh does not yet pass in a sibling
`*_pending` test case. The matrix sources that file for its keys and skips
them, the pending test case runs only them, and `SKIPPED_TESTS` lists every
pending test case. A fix removes its keys, and an empty pending test case leaves
the skip list.

Every golden-backed source has one matching file directly under `expected`.
Keep output deterministic and use the shared test environment variables for
temporary paths and platform values.
