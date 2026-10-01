ifeq ($(OS), Windows_NT)
SHELL := sh
TEST_ABSOLUTE_DIRECTORY_COMMAND := pwd -W
else
SHELL := /bin/bash
TEST_ABSOLUTE_DIRECTORY_COMMAND := pwd
endif
ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif

ifeq ($(TARGET), Windows_NT)
SKIPPED_TESTS := \
	koshkit_filesystem_posix koshkit_inspect_posix koshkit_system_posix \
	koshkit_text_posix alias_unalias glob_dot_slash_prefix koshkit_getconf \
	pipeline_compound_group read_builtin redirect_high_fd \
	shellcheck_bashisms_sh_shebang trap umask_modes \
	cli_bash_startup_restricted cli_bash_xtracefd cli_bench_behavior \
	cli_cd_suggestion cli_chld_trap_after_return cli_command_edge_case_behavior \
	cli_command_flag_forms cli_command_p_restores_path \
	cli_command_substitution_interrupt cli_command_substitution_strategy \
	cli_crlf_source cli_ctrl_c_interrupts_bash_loop cli_deep_nesting_guard \
	cli_diagnostic_levels cli_disown_removes_jobs cli_disown_running_only \
	cli_env_runs_command_directly cli_exec_behavior cli_export_function \
	cli_export_readonly_integer cli_fg_terminal_handoff cli_granular_arg_caret \
	cli_help_colors cli_history_behavior cli_history_noninteractive \
	cli_interrupt_during_blocking_open cli_job_state_running_then_stopped \
	cli_jobspec_grammar cli_kill_behavior cli_kosh_identity \
	cli_koshkit_error_notes cli_koshkit_evil_regressions \
	cli_koshkit_evilfiles_parity cli_koshkit_evilio_live cli_koshkit_eviliso \
	cli_koshkit_file_magic_batch cli_koshkit_stty cli_mkdir_mode_umask \
	cli_path_cache_behavior cli_signal_noninteractive_term cli_set_option_state \
	cli_spawn_failure_location cli_suspend_behavior \
	cli_trap_during_blocking_builtin cli_trap_startup_ignored \
	cli_unknown_flag_color cli_wait_on_stopped_job \
	cli_koshkit_goodcore_archive cli_koshkit_ls_modes cli_koshkit_misc \
	cli_assimilate \
	cli_koshkit_printenv cli_koshkit_process_tools cli_koshkit_text \
	cli_koshkit_timeout cli_loop_redirect_descriptor \
	cli_pipeline_stage_evaluation_mode cli_shopt_states \
	cli_subshell_replay_trap_suppression cli_subshell_spawn_state \
	cli_trace_policy cli_trailing_separator_execution cli_ulimit_validation \
	completion_ansi_c_quoted_word completion_complete_git_grouped_subcommands \
	completion_complete_go_subcommands completion_complete_help_subcommands \
	completion_editor_append_hot_path completion_executable_filtering_batch \
	completion_help_options_trusted_dir_only completion_help_subcommand_second_level \
	completion_help_subcommand_third_level completion_help_subcommands_no_colon \
	completion_make_targets_fallback completion_completion_glob_quoting \
	completion_path_quoting
else ifneq ($(TARGET), Linux)
SKIPPED_TESTS := koshkit_mknod
else
SKIPPED_TESTS :=
endif
export SKIPPED_TESTS

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

ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif
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
export TEST_HOST_PATH_SEPARATOR := :
export TEST_TEMP_DIRECTORY := .test-work
export TEST_MKTEMP_DIRECTORY := $(shell $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$(TEST_TEMP_DIRECTORY)
export TEST_KOSHKIT := $(CURDIR)/aux/invoke-koshkit

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
export DIFF_FLAGS := -u -w -a # CRLF on Windows SMH
else
export DIFF_FLAGS := -u
endif

ifeq ($(NO_TOILETLINE), 1)
BIN_EXTENSION += -no-toiletline
endif

ifeq ($(MODE),rel)
export BIN ?= ../kosh$(BIN_EXTENSION)
else
export BIN ?= ../kosh-$(MODE)$(BIN_EXTENSION)
endif

TEST_TIMEOUT = $(TEST_KOSHKIT) timeout -k 2s
FAILED_LIST := failed.diff
export FAILED_LIST

export TEST_SHELL := $(SHELL)

export KOSH_HISTORY_FILE := $(CURDIR)/.test_history
export KOSH_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history

ifeq ($(TARGET), Windows_NT)
export TEST_SYSTEM_PATH := $(TEST_TEMP_DIRECTORY)/system-path
else
export TEST_SYSTEM_PATH := $(dir $(shell command -v sh))
endif

export PATH := $(CURDIR)/aux$(TEST_HOST_PATH_SEPARATOR)$(PATH)
export ASAN_OPTIONS := symbolize=0

.PHONY: test kosh cli completion highlight sh bash test_prepare FORCE

test: kosh cli completion highlight sh bash | test_prepare

KOSH_TESTS := $(wildcard harness/kosh/*.kosh)
kosh: $(KOSH_TESTS)

CLI_TESTS := $(wildcard harness/cli/*.sh)
cli: $(CLI_TESTS)

COMPLETION_TESTS := $(wildcard harness/completion/*.sh)
completion: $(COMPLETION_TESTS)

HIGHLIGHT_TESTS := $(wildcard harness/highlight/*.sh)
highlight: $(HIGHLIGHT_TESTS)

SH_TESTS := $(wildcard harness/sh/*.sh)
sh: $(SH_TESTS)

BASH_TESTS := $(wildcard harness/bash/*.bash)
bash: $(BASH_TESTS)

harness/kosh/%.kosh: FORCE $(BIN) | test_prepare
	@$(SHELL) aux/run-harness-kosh '$@'

harness/cli/%.sh: FORCE $(BIN) | test_prepare
	@$(SHELL) aux/run-harness-cli '$@'

harness/completion/%.sh: FORCE $(BIN) | test_prepare
	@$(SHELL) aux/run-harness-completion '$@'

harness/highlight/%.sh: FORCE $(BIN) | test_prepare
	@$(SHELL) aux/run-harness-highlight '$@'

harness/sh/%.sh: FORCE $(BIN) | test_prepare
	@$(SHELL) aux/run-harness-sh '$@'

harness/bash/%.bash: FORCE $(BIN) | test_prepare
	@$(SHELL) aux/run-harness-bash '$@'

FORCE:

clean:
	@rm -f $(FAILED_LIST) $(KOSH_HISTORY_FILE) $(KOSH_DIRECTORY_HISTORY)
	@rm -rf "$(CURDIR)/$(TEST_TEMP_DIRECTORY)"

test_prepare:
	@$(TEST_KOSHKIT) rm -f '$(FAILED_LIST)'
	@$(TEST_KOSHKIT) touch '$(FAILED_LIST)'
	@$(TEST_KOSHKIT) rm -rf '$(CURDIR)/$(TEST_TEMP_DIRECTORY)'

test: | test_prepare
	@if test -s '$(FAILED_LIST)'; then \
		cat -n '$(FAILED_LIST)'; \
		exit 1; \
	fi

REFILL ?=

refill: $(BIN)
	@REFILL='$(REFILL)' \
		$(SHELL) aux/run-refill '$(SHELL)'

export ZSH ?= /bin/zsh
export ASH ?= /bin/busybox
export YASH ?= $(shell command -v yash)
export DASH ?= $(shell command -v dash)
export BASHP ?= $(shell ../scripts/find-modern-bash.sh)
export PYTHON ?= python3

export PRIMES_LIMIT ?= 100000
export SCALE ?= 100
export BENCH_WARMUP_COUNT ?= 0
export BENCH_SAMPLE_COUNT ?= 1
export DASH_SPEEDUP_REQUIRED ?= 1
export BASH_SPEEDUP_REQUIRED ?= 3

bench: $(BIN)
	@$(SHELL) aux/run-bench

.PHONY: test clean refill bench
