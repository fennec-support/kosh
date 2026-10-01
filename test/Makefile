ifeq ($(OS), Windows_NT)
SHELL := sh
TEST_ABSOLUTE_DIRECTORY_COMMAND := pwd -W
else
SHELL := /bin/bash
TEST_ABSOLUTE_DIRECTORY_COMMAND := pwd
endif

DIFF_FLAGS := -u
MODE ?= dbg
ARCH ?= $(shell uname -m)
NO_TOILETLINE ?= 0
IS_NONDEBUG_BUILD := 1
ifeq ($(MODE),dbg)
IS_NONDEBUG_BUILD := 0
else ifeq ($(MODE),cosmo_dbg)
IS_NONDEBUG_BUILD := 0
endif
export IS_NONDEBUG_BUILD
BUILD_VARIANT_SUFFIX := $(if $(filter 1,$(NO_TOILETLINE)),-no-toiletline,)
ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif
DIFF_BOOTSTRAP := verify_koshkit_diff

ifeq ($(TARGET), Windows_NT)
export TEST_PATH_SEPARATOR := ;
export TEST_NULL_DEVICE := NUL
export TEST_PATH_ENVIRONMENT_NAME := Path
export MSYS2_ENV_CONV_EXCL := Path
export HOME := $(USERPROFILE)
else
export TEST_PATH_SEPARATOR := :
export TEST_NULL_DEVICE := /dev/null
export TEST_PATH_ENVIRONMENT_NAME := PATH
endif
TEST_JOBS ?= $(or $(shell ../scripts/cpu-count.sh),1)
export TEST_HOST_PATH_SEPARATOR := :
export TEST_TEMP_DIRECTORY := .test-work
export TEST_MKTEMP_DIRECTORY := $(shell $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/.test-work
export TEST_KOSHKIT := $(CURDIR)/bin/test-koshkit

ifeq ($(MODE),rel)
BIN_NAME := kosh
else
BIN_NAME := kosh-$(MODE)
endif
ifeq ($(MODE),cosmo)
BIN_EXTENSION := .com
else ifeq ($(TARGET),Windows_NT)
BIN_EXTENSION := .exe
endif
BIN ?= ../$(BIN_NAME)$(BIN_EXTENSION)
ifeq ($(TARGET),Windows_NT)
DIFF_FLAGS += -w -a # CRLF on Windows SMH
endif

# The test drivers run with the current directory set to test, so a BIN given
# relative to the repo root or as a bare name is resolved to an absolute path
# here. A path that exists as given wins, then the repo-root sibling, then a
# PATH lookup, so the target binary is found no matter where make was launched.
RESOLVED_BIN := $(strip $(shell \
	if [ -x "$(BIN)" ]; then \
		echo "$$(cd "$$(dirname "$(BIN)")" && $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$$(basename "$(BIN)")"; \
	elif [ -x "$(CURDIR)/../$(BIN)" ]; then \
		candidate="$(CURDIR)/../$(BIN)"; \
		echo "$$(cd "$$(dirname "$$candidate")" && $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$$(basename "$$candidate")"; \
	elif command -v "$(BIN)" >/dev/null 2>&1; then \
		found=$$(command -v "$(BIN)"); \
		echo "$$(cd "$$(dirname "$$found")" && $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$$(basename "$$found")"; \
	else echo "$(BIN)"; fi))
# A directory probe that produces nothing leaves the original path in place.
override BIN := $(if $(RESOLVED_BIN),$(RESOLVED_BIN),$(BIN))
export BIN
TEST_TIMEOUT = $(TEST_KOSHKIT) timeout -k 2s
ifeq ($(TARGET), Linux)
export TEST_EVILISO_LOOPBACK := $(abspath \
	../src/o/$(TARGET)/$(MODE)$(BUILD_VARIANT_SUFFIX)/eviliso-loop)
EVILISO_LOOPBACK_DEPENDENCY := eviliso_loopback_helper
endif
BIN_FLAGS := -AR
FAILED_LIST := failed.diff
export BIN_FLAGS DIFF_FLAGS FAILED_LIST TEST_JOBS
export TEST_SHELL := $(SHELL)

export KOSH_HISTORY_FILE := $(CURDIR)/.test_history
export KOSH_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history
ifeq ($(TARGET), Windows_NT)
export TEST_SYSTEM_PATH := .test-work/system-path
export TEST_UNAME_DIRECTORY := .test-work/system-path
else
export TEST_SYSTEM_PATH := $(shell \
	shell_path=$$(command -v sh); printf '%s' "$${shell_path%/*}")
export TEST_UNAME_DIRECTORY := $(shell \
	uname_path=$$(command -v uname); printf '%s' "$${uname_path%/*}")
endif
export PATH := $(CURDIR)/bin$(TEST_HOST_PATH_SEPARATOR)$(PATH)

%: kosh/%.kosh expected/%.out $(BIN) $(DIFF_BOOTSTRAP)
	@$(TEST_KOSHKIT) rm -f '$(FAILED_LIST)' && $(TEST_KOSHKIT) rm -rf '$(FAILED_LIST).d'
	@BIN='$(BIN)' BIN_FLAGS='$(BIN_FLAGS)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' $(SHELL) run-kosh-test.sh $*

clean:
	@rm -f $(FAILED_LIST) $(KOSH_HISTORY_FILE) $(KOSH_DIRECTORY_HISTORY)
	@rm -rf $(FAILED_LIST).d
	@rm -rf "$(CURDIR)/.test-work"

test: $(EVILISO_LOOPBACK_DEPENDENCY) $(BIN) $(DIFF_BOOTSTRAP)
	@$(SHELL) run-test-suite.sh all

eviliso_loopback_helper:
	@$(MAKE) -C ../src MODE='$(MODE)' TARGET='$(TARGET)' ARCH='$(ARCH)' \
		NO_TOILETLINE='$(NO_TOILETLINE)' eviliso_loopback

verify_koshkit_diff: $(BIN)
	@if test ! -x '$(BIN)'; then \
		echo "The shell '$(BIN)' is missing or is not executable." >&2; \
		exit 1; \
	fi
	@$(TEST_TIMEOUT) 30 sh verify-koshkit-diff.sh

kosh_tests: $(BIN) $(DIFF_BOOTSTRAP)
	@$(SHELL) run-test-suite.sh kosh

cli_%: expected/%.out $(BIN) $(DIFF_BOOTSTRAP)
	@$(TEST_KOSHKIT) rm -f '$(FAILED_LIST)' && $(TEST_KOSHKIT) rm -rf '$(FAILED_LIST).d'
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-cli-test.sh '$(SHELL)' cli/$*.sh

pty_live_probes: $(BIN)
	@BIN='$(BIN)' TARGET='$(TARGET)' \
		$(TEST_TIMEOUT) 30 python3 cli/koshkit_live_pty.py

cli_koshkit_eviliso_loopback: $(EVILISO_LOOPBACK_DEPENDENCY)

cli_tests: $(EVILISO_LOOPBACK_DEPENDENCY) $(BIN) $(DIFF_BOOTSTRAP)
	@$(SHELL) run-test-suite.sh cli

ifneq ($(TARGET), Linux)
NON_LINUX_SKIPPED_CLI_NAMES := \
	cli_koshkit_eviliso_loopback \
	cli_koshkit_mknod

$(NON_LINUX_SKIPPED_CLI_NAMES):
	@printf "\t%-64s skipped, requires Linux\n" \
		"cli/$(patsubst cli_%,%,$@).sh"
endif

completion_%: expected/%.out $(BIN) $(DIFF_BOOTSTRAP)
	@$(TEST_KOSHKIT) rm -f '$(FAILED_LIST)' && $(TEST_KOSHKIT) rm -rf '$(FAILED_LIST).d'
	@$(SHELL) run-editor-test.sh completion '$(SHELL)' completion/$*.sh

ifeq ($(TARGET), Windows_NT)
WINDOWS_SKIPPED_TEST_NAMES := \
	koshkit_filesystem_posix

$(WINDOWS_SKIPPED_TEST_NAMES):
	@printf "	%-64s skipped, unsupported Windows backend feature\n" "$@.kosh"

cli_assimilate:
	@printf "	%-64s skipped, remote transaction requires POSIX\n" "cli/assimilate.sh"

WINDOWS_SKIPPED_CLI_NAMES := \
	cli_bash_startup_restricted \
	cli_bash_xtracefd \
	cli_bench_behavior \
	cli_cd_suggestion \
	cli_chld_trap_after_return \
	cli_command_edge_case_behavior \
	cli_command_flag_forms \
	cli_command_p_restores_path \
	cli_command_substitution_interrupt \
	cli_command_substitution_strategy \
	cli_crlf_source \
	cli_ctrl_c_interrupts_bash_loop \
	cli_deep_nesting_guard \
	cli_diagnostic_levels \
	cli_disown_removes_jobs \
	cli_disown_running_only \
	cli_env_runs_command_directly \
	cli_exec_behavior \
	cli_export_function \
	cli_export_readonly_integer \
	cli_fg_terminal_handoff \
	cli_granular_arg_caret \
	cli_help_colors \
	cli_history_behavior \
	cli_history_noninteractive \
	cli_interrupt_during_blocking_open \
	cli_job_state_running_then_stopped \
	cli_jobspec_grammar \
	cli_kill_behavior \
	cli_kosh_identity \
	cli_koshkit_error_notes \
	cli_koshkit_evil_regressions \
	cli_koshkit_evilfiles_parity \
	cli_koshkit_evilio_live \
	cli_koshkit_eviliso \
	cli_koshkit_file_magic_batch \
	cli_koshkit_stty \
	cli_mkdir_mode_umask \
	cli_path_cache_behavior \
	cli_signal_noninteractive_term \
	cli_set_option_state \
	cli_spawn_failure_location \
	cli_stopped_job_survives_poll \
	cli_suspend_behavior \
	cli_trap_during_blocking_builtin \
	cli_trap_startup_ignored \
	cli_unknown_flag_color \
	cli_wait_on_stopped_job

$(WINDOWS_SKIPPED_CLI_NAMES):
	@printf "	%-64s skipped, unsupported Windows backend feature\n" \
		"cli/$(patsubst cli_%,%,$@).sh"

WINDOWS_SKIPPED_COMPLETION_NAMES := \
	completion_ansi_c_quoted_word \
	completion_complete_git_grouped_subcommands \
	completion_complete_go_subcommands \
	completion_complete_help_subcommands \
	completion_editor_append_hot_path \
	completion_executable_filtering_batch \
	completion_help_options_trusted_dir_only \
	completion_help_subcommand_second_level \
	completion_help_subcommand_third_level \
	completion_help_subcommands_no_colon \
	completion_make_targets_fallback

$(WINDOWS_SKIPPED_COMPLETION_NAMES):
	@printf "	%-64s skipped, needs a POSIX helper executable\n" "$@"

WINDOWS_UNREPRESENTABLE_COMPLETION_NAMES := \
	completion_completion_glob_quoting \
	completion_path_quoting

$(WINDOWS_UNREPRESENTABLE_COMPLETION_NAMES):
	@printf "	%-64s skipped, golden test filenames are unsupported on Windows\n" \
		"$(patsubst completion_%,completion/%,$@).sh"
endif

completion_tests: $(BIN) $(DIFF_BOOTSTRAP)
	@$(SHELL) run-test-suite.sh completion


highlight_%: expected/%.out $(BIN) $(DIFF_BOOTSTRAP)
	@$(TEST_KOSHKIT) rm -f '$(FAILED_LIST)' && $(TEST_KOSHKIT) rm -rf '$(FAILED_LIST).d'
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-editor-test.sh highlight '$(SHELL)' highlight/$*.sh

highlight_tests: $(BIN) $(DIFF_BOOTSTRAP)
	@$(SHELL) run-test-suite.sh highlight

SKIPPED_TEST_NAMES := $(WINDOWS_SKIPPED_TEST_NAMES)
SKIPPED_CLI_INPUT := $(patsubst cli_%,cli/%.sh,\
	$(WINDOWS_SKIPPED_CLI_NAMES) $(NON_LINUX_SKIPPED_CLI_NAMES))
SKIPPED_COMPLETION_INPUT := \
	$(patsubst completion_%,completion/%.sh,$(WINDOWS_SKIPPED_COMPLETION_NAMES))
UNREPRESENTABLE_COMPLETION_INPUT := \
	$(patsubst completion_%,completion/%.sh,$(WINDOWS_UNREPRESENTABLE_COMPLETION_NAMES))
ifeq ($(TARGET), Windows_NT)
SKIP_CLI_ASSIMILATE := 1
endif

export SKIPPED_TEST_NAMES SKIPPED_CLI_INPUT
export WINDOWS_SKIPPED_CLI_NAMES NON_LINUX_SKIPPED_CLI_NAMES
export SKIPPED_COMPLETION_INPUT UNREPRESENTABLE_COMPLETION_INPUT
export SKIP_CLI_ASSIMILATE

REFILL ?=

refill: $(EVILISO_LOOPBACK_DEPENDENCY) $(BIN)
	@REFILL='$(REFILL)' \
		$(SHELL) run-refill.sh '$(SHELL)'


ZSH ?= /bin/zsh
ASH ?= /bin/busybox
YASH ?= $(shell command -v yash)
PYTHON ?= python3
DASH ?= $(shell command -v dash)

ifeq ($(origin BASHP), undefined)
MODERN_BASH := $(shell ../scripts/find-modern-bash.sh)
ifneq ($(MODERN_BASH),)
BASHP := $(MODERN_BASH)
endif
endif
BASHP ?= /bin/bash

export BASHP DASH

compatdiff: $(BIN) $(DIFF_BOOTSTRAP)
	@$(SHELL) run-test-suite.sh compat

dashdiff bashdiff mimicrydiff: compatdiff


BENCH := bench/configure.sh
BENCH_BASH := bench/configure.bash
BENCH_KOSH := bench/configure.kosh
PRIMES := bench/primes.bash
PRIMES_PY := bench/primes.py
PRIMES_LIMIT ?= 100000
SCALE ?= 100
BENCH_WARMUP_COUNT ?= 0
BENCH_SAMPLE_COUNT ?= 1
DASH_SPEEDUP_REQUIRED ?= 1
BASH_SPEEDUP_REQUIRED ?= 3

bench: $(BIN) $(DIFF_BOOTSTRAP)
	@SCALE='$(SCALE)' BIN='$(BIN)' DASH='$(DASH)' BASHP='$(BASHP)' ZSH='$(ZSH)' \
		ASH='$(ASH)' YASH='$(YASH)' BENCH='$(BENCH)' \
		BENCH_BASH='$(BENCH_BASH)' BENCH_KOSH='$(BENCH_KOSH)' PRIMES='$(PRIMES)' \
		PRIMES_PY='$(PRIMES_PY)' PRIMES_LIMIT='$(PRIMES_LIMIT)' PYTHON='$(PYTHON)' \
		BENCH_WARMUP_COUNT='$(BENCH_WARMUP_COUNT)' \
		BENCH_SAMPLE_COUNT='$(BENCH_SAMPLE_COUNT)' \
		DASH_SPEEDUP_REQUIRED='$(DASH_SPEEDUP_REQUIRED)' \
		BASH_SPEEDUP_REQUIRED='$(BASH_SPEEDUP_REQUIRED)' \
		$(SHELL) run-bench-test.sh

.PHONY: test clean eviliso_loopback_helper \
	verify_koshkit_diff kosh_tests refill \
		compatdiff dashdiff bashdiff mimicrydiff bench completion_tests \
		cli_tests highlight_tests
