ifndef VERBOSE
MAKEFLAGS += -s
endif

.DEFAULT_GOAL := kosh

CPU_COUNT := $(shell ./scripts/cpu-count.sh)

MAKE_COMMAND_LINE ?= $(shell \
	if test -r /proc/$${PPID}/cmdline; then \
		tr '\000' ' ' < /proc/$${PPID}/cmdline 2>/dev/null; \
	elif command -v ps >/dev/null 2>&1; then \
		ps -p $${PPID} -o command= 2>/dev/null; \
	elif command -v powershell.exe >/dev/null 2>&1; then \
		powershell.exe -NoProfile -Command \
			"(Get-CimInstance Win32_Process -Filter 'ProcessId=$${PPID}').CommandLine" \
			2>/dev/null; \
	fi)
CALLER_JOBS := $(filter -j% --jobs% --jobserver%,$(MAKEFLAGS)) \
	$(filter -j% --jobs%,$(MAKE_COMMAND_LINE)) \
	$(if $(filter command line,$(origin MAKE_COMMAND_LINE)),explicit-command-line,)
AUTO_JOBS = $(if $(strip $(CALLER_JOBS)),,-j$(CPU_COUNT))

MODE ?= dbg
NO_TOILETLINE ?= 0
NO_KOSHKIT ?= 0

ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif

export MODE
export NO_TOILETLINE
export NO_KOSHKIT
export TARGET

TEST_TARGET := $(if $(filter 1,$(NO_TOILETLINE)),cli_history_noninteractive,test)

all: kosh test

kosh:
	echo Creating kosh...
	$(MAKE) $(AUTO_JOBS) -C src kosh

install:
	echo Installing...
	$(MAKE) $(AUTO_JOBS) -C src install

uninstall:
	echo Uninstalling...
	$(MAKE) $(AUTO_JOBS) -C src uninstall

tidy:
	echo Launching '$$'CLANG_TIDY...
	$(MAKE) $(AUTO_JOBS) -C src tidy

fmt:
	echo Launching '$$'CLANG_FMT...
	$(MAKE) $(AUTO_JOBS) -C src fmt

toiletline_test: kosh
	echo Launching editor tests...
	$(MAKE) -C src/toiletline test

test: kosh
	echo Launching tests...
	$(MAKE) $(AUTO_JOBS) -C test $(TEST_TARGET)

bench: kosh
	echo Launching benchmarks...
	$(MAKE) -C test bench

refill_tests: kosh
	echo Refilling tests...
	$(MAKE) $(AUTO_JOBS) -C test refill

completions:
	echo Generating completions...
	$(MAKE) $(AUTO_JOBS) -C src MODE=rel kosh
	./scripts/generate-completions.sh ./kosh completions/kosh.bash \
		> completions/kosh.bash.new
	mv completions/kosh.bash.new completions/kosh.bash

pgo:
	echo Building a profile-guided release...
	rm -f pgo/profraw/*.profraw
	$(MAKE) $(AUTO_JOBS) -C src MODE=rel PGO=generate kosh
	./scripts/pgo-train.sh ./kosh-pgo-gen pgo/work
	./scripts/llvm-tool.sh llvm-profdata merge -o pgo/kosh.profdata \
		pgo/profraw/*.profraw
	$(MAKE) $(AUTO_JOBS) -C src MODE=rel \
		PGO_PROFILE=$(abspath pgo/kosh.profdata) kosh

FUZZ_SECONDS ?= 60

fuzz:
	echo Fuzzing...
	$(MAKE) $(AUTO_JOBS) -C src MODE=fuzz fuzz
	for harness in ./kosh-fuzz-*; do \
		name=$${harness#./kosh-fuzz-}; \
		mkdir -p "fuzz/corpus/$$name" "fuzz/crashes/$$name"; \
		echo "Running $$name for $(FUZZ_SECONDS) seconds..."; \
		"$$harness" -max_total_time=$(FUZZ_SECONDS) -rss_limit_mb=2048 \
			-print_final_stats=1 -artifact_prefix="fuzz/crashes/$$name/" \
			"fuzz/corpus/$$name" || exit 1; \
	done

# docs renders each docs/NAME_SECTION.md into man/NAME.SECTION with pandoc.
PANDOC ?= pandoc

docs:
	echo Generating manual pages...
	mkdir -p man
	for page in docs/*_[0-9].md; do \
		base=$$(basename "$$page" .md); \
		section=$${base##*_}; \
		name=$$(printf '%s' "$${base%_*}" | tr _ -); \
		title=$$(printf '%s' "$$name" | tr a-z A-Z); \
		$(PANDOC) -f gfm-tex_math_dollars-tex_math_gfm -t man -s \
			--lua-filter scripts/md2man.lua -M title="$$title" \
			-M section="$$section" -M name="$$name" \
			-M header="Koshka Manual" -M footer=kosh \
			-o "man/$$name.$$section" "$$page" || exit 1; \
	done

clean:
	echo Cleaning up...
	$(MAKE) $(AUTO_JOBS) -C src clean
	$(MAKE) $(AUTO_JOBS) -C test clean
	$(MAKE) -C src/toiletline clean

.PHONY: all kosh install uninstall tidy fmt toiletline_test test bench \
	refill_tests completions pgo fuzz docs clean
