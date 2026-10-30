# make MODE=dbg/rel/tinyrel/prof/cosmo/cosmo_dbg
MODE ?= dbg
TARGET ?= # Linux/Windows_NT/Darwin
ARCH ?= $(shell uname -m)
NO_TOILETLINE ?= 0
NO_KOSHKIT ?= 0
# HARDENED=1 makes a release build trap on a container index past the end.
HARDENED ?= 0
# PGO=generate builds the instrumented release binary ../kosh-pgo-gen, which
# merges its profile into ../pgo/profraw. PGO_PROFILE=file.profdata builds
# the release binary with that profile. PGO=cs-generate with PGO_PROFILE
# builds ../kosh-pgo-csgen, which merges its context-sensitive profile into
# ../pgo/csprofraw.
PGO ?=
PGO_PROFILE ?=

ROUTED_SOURCES := platform/PlatformPosix.cpp platform/PlatformPosixExtra.cpp \
	platform/PlatformPosixFilesystem.cpp \
	platform/PlatformPosixFilesystemExtra.cpp platform/PlatformPosixProcess.cpp \
	platform/PlatformWin32.cpp platform/PlatformWin32Filesystem.cpp \
	platform/PlatformWin32Process.cpp
SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

SOURCE_DIRECTORIES := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)
SOURCES := $(filter-out $(ROUTED_SOURCES),$(SOURCES))

ifeq ($(NO_KOSHKIT), 1)
SOURCES := $(filter-out koshkit/% CLILive.cpp util/UtilsOwnership.cpp,$(SOURCES))
endif

# The host OS, Windows_NT on Windows and the uname elsewhere.
ifeq ($(OS), Windows_NT)
HOST_OPERATING_SYSTEM := Windows_NT
else
HOST_OPERATING_SYSTEM := $(shell uname -s)
endif

IS_ANDROID_HOST := $(if $(findstring Android,$(shell uname -o 2>/dev/null)),1,0)

# Windows_NT/Linux/Darwin
ifeq ($(TARGET),)
TARGET := $(HOST_OPERATING_SYSTEM)
endif

# Validate TARGET
ifneq ($(TARGET), Linux)
ifneq ($(TARGET), Windows_NT)
ifneq ($(TARGET), Darwin)
$(error Unknown target: $(TARGET))
endif
endif
endif

ifeq ($(TARGET), Windows_NT)
REGEX_SOURCES := regcomp regerror regexec tre-mem
endif

# A Linux target is cross-compiled with Zig and musl from another host, from a
# Linux host when ARCH selects a different processor, or whenever LINUX_ZIG=1.
LINUX_ZIG ?= 0
ifeq ($(TARGET), Linux)
HOST_MACHINE := $(shell uname -m)
ifneq ($(filter arm64 aarch64,$(HOST_MACHINE)),)
HOST_MACHINE := aarch64
endif
ifneq ($(filter arm64 aarch64,$(ARCH)),)
LINUX_TARGET_ARCHITECTURE := aarch64
else ifneq ($(filter x86_64 amd64,$(ARCH)),)
LINUX_TARGET_ARCHITECTURE := x86_64
endif
ifneq ($(HOST_OPERATING_SYSTEM), Linux)
IS_LINUX_CROSS_COMPILATION := 1
else ifeq ($(LINUX_ZIG), 1)
IS_LINUX_CROSS_COMPILATION := 1
else ifneq ($(origin ARCH), file)
ifneq ($(LINUX_TARGET_ARCHITECTURE), $(HOST_MACHINE))
IS_LINUX_CROSS_COMPILATION := 1
endif
endif
ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
ifneq ($(MODE), rel)
$(error Linux cross-compilation only supports MODE=rel)
endif
ifeq ($(LINUX_TARGET_ARCHITECTURE),)
$(error Linux cross-compilation does not support ARCH='$(ARCH)')
endif
endif
endif

SHOULD_LINK_STATIC_LINUX := 0
ifeq ($(TARGET), Linux)
ifneq ($(IS_ANDROID_HOST), 1)
SHOULD_LINK_STATIC_LINUX := 1
endif
endif

BUILD_VARIANT_SUFFIX := $(if $(filter 1,$(NO_TOILETLINE)),-no-toiletline,)$(if $(filter 1,$(NO_KOSHKIT)),-no-koshkit,)$(if $(filter 1,$(HARDENED)),-hardened,)$(if $(filter generate,$(PGO)),-pgo-gen,)$(if $(filter cs-generate,$(PGO)),-pgo-csgen,)$(if $(PGO_PROFILE),-pgo,)

ifneq ($(PGO)$(PGO_PROFILE),)
ifneq ($(MODE), rel)
$(error PGO and PGO_PROFILE only support MODE=rel)
endif
endif
ifeq ($(PGO), cs-generate)
ifeq ($(PGO_PROFILE),)
$(error PGO=cs-generate needs PGO_PROFILE)
endif
endif

ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
OBJ_DIR ?= o/$(TARGET)/$(LINUX_TARGET_ARCHITECTURE)/$(MODE)$(BUILD_VARIANT_SUFFIX)
else
OBJ_DIR ?= o/$(TARGET)/$(MODE)$(BUILD_VARIANT_SUFFIX)
endif
OBJECT_DIRECTORY := $(OBJ_DIR)

REGEX_OBJECTS := $(REGEX_SOURCES:%=$(OBJECT_DIRECTORY)/vendor/regex/%.o)
OBJECTS := $(SOURCES:%.cpp=$(OBJECT_DIRECTORY)/%.o) $(REGEX_OBJECTS)
DEPENDENCY_FILES := $(OBJECTS:.o=.d)

MODE_SUFFIX := $(if $(filter rel,$(MODE)),$(if $(filter generate,$(PGO)),-pgo-gen,)$(if $(filter cs-generate,$(PGO)),-pgo-csgen,),-$(MODE))
ifeq ($(MODE), cosmo)
OUTPUT_PATH := ../kosh$(MODE_SUFFIX).com
else ifeq ($(TARGET), Windows_NT)
OUTPUT_PATH := ../kosh$(MODE_SUFFIX).exe
else
OUTPUT_PATH := ../kosh$(MODE_SUFFIX)
endif

LINKED_OUTPUT_PATH := $(OBJECT_DIRECTORY)/$(notdir $(OUTPUT_PATH))

# Cosmopolitan/Clang.
CXX_VARIABLE_ORIGIN := $(origin CXX)
ifneq ($(filter cosmo cosmo_dbg,$(MODE)),)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
ifeq ($(CXX_VARIABLE_ORIGIN), default)
CXX := clang++
endif
else ifeq ($(CXX_VARIABLE_ORIGIN), default)
CXX := clang++
endif

# If we're cross-compiling, use MingW.
ifeq ($(TARGET), Windows_NT)
ifneq ($(OS), Windows_NT)
CXX := x86_64-w64-mingw32-g++
endif
endif

ifeq ($(TARGET), Windows_NT)
WINDOWS_COMPILER_TARGET := $(shell $(CXX) -dumpmachine 2>/dev/null)
IS_WINDOWS_GNU_ABI := $(or $(findstring mingw,$(WINDOWS_COMPILER_TARGET)),\
	$(findstring windows-gnu,$(WINDOWS_COMPILER_TARGET)))
endif

ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
CXX := zig c++ -target $(LINUX_TARGET_ARCHITECTURE)-linux-musl
OBJCOPY ?= llvm-objcopy
endif
OBJCOPY ?= objcopy

# A Darwin cross build uses osxcross for arm64. An environment CXX serves host
# builds, so only a command-line CXX replaces oa64-clang++, and the compiler
# must target apple-darwin before any object is built.
ifeq ($(TARGET), Darwin)
ifneq ($(HOST_OPERATING_SYSTEM), Darwin)
ifeq ($(filter arm64 aarch64,$(ARCH)),)
$(error A Darwin cross build supports only arm64, so run make TARGET=Darwin ARCH=arm64 with osxcross)
endif
ifneq ($(CXX_VARIABLE_ORIGIN), command line)
CXX := oa64-clang++
endif
DARWIN_COMPILER_TARGET := $(shell $(CXX) -dumpmachine 2>/dev/null)
ifeq ($(DARWIN_COMPILER_TARGET),)
$(error TARGET=Darwin needs osxcross, but '$(CXX)' was not found, so add the osxcross bin directory to PATH or pass CXX=)
else ifeq ($(findstring apple-darwin,$(DARWIN_COMPILER_TARGET)),)
$(error TARGET=Darwin needs an apple-darwin compiler such as osxcross oa64-clang++, but '$(CXX)' targets '$(DARWIN_COMPILER_TARGET)')
endif
endif
endif

ifeq ($(MODE), cosmo)
OS_INFO := "unknown-cosmo"
LIBC_INFO := cosmopolitan libc
else ifeq ($(MODE), cosmo_dbg)
OS_INFO := "unknown-cosmo-dbg"
LIBC_INFO := cosmopolitan libc
else ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
OS_INFO := Linux $(LINUX_TARGET_ARCHITECTURE) cross
else
OS_INFO := $(shell test -f /etc/os-release && . /etc/os-release 2>/dev/null; printf '%s%s' "$${PRETTY_NAME:+$$PRETTY_NAME }" "$$(uname -rvom 2>/dev/null || echo '<no uname>')")
endif
OPERATING_SYSTEM_INFORMATION := $(OS_INFO)

ifndef LIBC_INFO
ifeq ($(TARGET), Windows_NT)
ifneq ($(IS_WINDOWS_GNU_ABI),)
LIBC_INFO := mingw msvcrt
else
LIBC_INFO := msvc
endif
else ifeq ($(TARGET), Darwin)
LIBC_INFO := Apple libSystem
else ifeq ($(IS_ANDROID_HOST), 1)
LIBC_INFO := Android bionic
else ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
LIBC_INFO := zig musl libc++
else ifneq ($(TARGET), $(HOST_OPERATING_SYSTEM))
LIBC_INFO := zig musl libc++
else
LIBC_INFO := $(shell getconf GNU_LIBC_VERSION 2>/dev/null || { command -v ldd >/dev/null 2>&1 && ldd --version 2>&1 | head -n 1; } || echo 'unknown libc')
endif
endif
LIBC_INFORMATION := $(LIBC_INFO)

COMMIT_HASH := $(shell git rev-parse HEAD || echo "<no git>")

# Remember CXXFLAGS passed to us from CLI. Make exports an environment
# variable with its final value, so a nested make receives the user flags
# through KOSH_USER_CXXFLAGS instead of the complete CXXFLAGS below.
ifeq ($(KOSH_HAS_USER_CXXFLAGS), 1)
ENVCXXFLAGS := $(KOSH_USER_CXXFLAGS)
else
ENVCXXFLAGS := $(CXXFLAGS)
endif
export KOSH_USER_CXXFLAGS := $(ENVCXXFLAGS)
export KOSH_HAS_USER_CXXFLAGS := 1
unexport CXXFLAGS
USER_CXXFLAGS := $(ENVCXXFLAGS)
# A distribution that sets its own fortify level, as Arch's makepkg does with
# -Wp,-D_FORTIFY_SOURCE=3, keeps it. Otherwise release builds use level 3
# against glibc and Darwin and level 2 elsewhere.
ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
FORTIFY_LEVEL := 2
else ifneq ($(filter Linux Darwin,$(TARGET)),)
FORTIFY_LEVEL := 3
else
FORTIFY_LEVEL := 2
endif
FORTIFY_FLAGS := $(if $(findstring _FORTIFY_SOURCE,$(USER_CXXFLAGS)),,-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=$(FORTIFY_LEVEL))

# Release hardening: stack protection everywhere, stack clash probes and
# control-flow protection where the target supports them, and a read-only,
# eagerly bound, non-executable-stack image with packed relative relocations
# on Linux.
TARGET_MACHINE := $(or $(LINUX_TARGET_ARCHITECTURE),$(HOST_MACHINE))
# zig ignores -static-pie and spells a static position-independent executable
# as a static PIE link.
ZIG_STATIC_PIE_FLAGS := -static -fPIE -pie
HARDENING_FLAGS := -fstack-protector-strong
HARDENING_LINK_FLAGS := # nothing
ifeq ($(TARGET), Linux)
HARDENING_FLAGS += -fstack-clash-protection
HARDENING_LINK_FLAGS += -Wl,-z,relro,-z,now,-z,noexecstack
ifneq ($(IS_LINUX_CROSS_COMPILATION), 1)
HARDENING_LINK_FLAGS += -Wl,-z,pack-relative-relocs
endif
ifeq ($(TARGET_MACHINE), x86_64)
HARDENING_FLAGS += -fcf-protection=full
else ifeq ($(TARGET_MACHINE), aarch64)
HARDENING_FLAGS += -mbranch-protection=standard
endif
endif
ifeq ($(TARGET), Windows_NT)
ifneq ($(IS_WINDOWS_GNU_ABI),)
HARDENING_LINK_FLAGS += -lssp
endif
# control flow guard and a System32-only search for dependent DLLs, when the
# compiler and linker accept them. GCC for mingw supports neither.
WINDOWS_PROBE_LINK = $(shell probe="$${TMPDIR:-/tmp}/kosh-probe-$$$$.exe"; \
	printf 'int main(){}\n' | $(CXX) -x c++ - $(1) -o "$$probe" \
	>/dev/null 2>&1 && echo yes; rm -f "$$probe")
WINDOWS_PROBE_COMMA := ,
ifneq ($(call WINDOWS_PROBE_LINK,-mguard=cf),)
HARDENING_FLAGS += -mguard=cf
HARDENING_LINK_FLAGS += -mguard=cf
endif
ifneq ($(call WINDOWS_PROBE_LINK,-Wl$(WINDOWS_PROBE_COMMA)--dependent-load-flag=0x800),)
HARDENING_LINK_FLAGS += -Wl,--dependent-load-flag=0x800
endif
endif
CXXFLAGS := # nothing

encode_build_metadata = $(1)
ifneq ($(filter cosmo cosmo_dbg,$(MODE)),)
EMPTY_STRING :=
SPACE := $(EMPTY_STRING) $(EMPTY_STRING)
encode_build_metadata = $(subst $(SPACE),\\040,$(1))
endif

BUILD_DEFINITIONS := \
	-DKOSH_COMPILER_COMMAND="\"$(call encode_build_metadata,$(CXX))\"" \
	-DKOSH_LIBC="\"$(call encode_build_metadata,$(LIBC_INFORMATION))\"" \
	-DKOSH_OS_INFO="\"$(call encode_build_metadata,$(OPERATING_SYSTEM_INFORMATION))\"" \
	-DKOSH_COMMIT_HASH="\"$(call encode_build_metadata,$(COMMIT_HASH))\"" \
	-DKOSH_BUILD_MODE="\"$(call encode_build_metadata,$(MODE))\"" \
	-DKOSH_ENVCXXFLAGS="\"$(call encode_build_metadata,$(USER_CXXFLAGS))\"" \
	$(if $(filter tinyrel,$(MODE)),-DKOSH_TINYREL,) \
	$(if $(filter 1,$(NO_TOILETLINE)),-DKOSH_NO_TOILETLINE,) \
	$(if $(filter 1,$(NO_KOSHKIT)),-DKOSH_NO_KOSHKIT,) \
	$(if $(filter 1,$(HARDENED)),-DKOSH_HARDENED,)

CXXFLAGS := \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wformat \
	-Wformat-security \
	-Werror \
	-Wno-unused-command-line-argument \
	-Wno-deprecated \
	-std=c++23 \
	-Wno-unknown-warning-option \
	-Wno-error=maybe-uninitialized \
	-pipe \
	-fvisibility=hidden \
	-fno-rtti \
	-fno-semantic-interposition \
	-Wno-unknown-pragmas \
	-fno-strict-aliasing \
	$(BUILD_DEFINITIONS) \
	$(USER_CXXFLAGS)
ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
CXXFLAGS += -Wno-date-time
endif

LINK_LIBRARIES := # nothing

ifeq ($(TARGET), Linux)
LINK_LIBRARIES += -lm
endif

ifeq ($(TARGET), Windows_NT)
ifeq ($(IS_WINDOWS_GNU_ABI),)
LINK_LIBRARIES += --rtlib=compiler-rt
endif
endif

AVAILABLE_LLD := $(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null)
ifeq ($(TARGET), Windows_NT)
ifneq ($(AVAILABLE_LLD),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif
else ifeq ($(TARGET), Darwin)
# mold is ELF-only and lld-as-Mach-O is rare on macOS, so the system ld64
# links directly with no -fuse-ld flag.
USE_LD :=
else ifneq ($(shell command -v mold 2>/dev/null),)
USE_LD := -fuse-ld=mold
else ifneq ($(AVAILABLE_LLD),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif
ifeq ($(TARGET), Linux)
ifeq ($(SHOULD_LINK_STATIC_LINUX), 1)
ifneq ($(filter rel tinyrel prof cov,$(MODE)),)
# static links use mold when it is installed and the default linker
# otherwise. mold folds identical functions in release builds.
ifneq ($(shell command -v mold 2>/dev/null),)
USE_LD := -fuse-ld=mold
ifneq ($(filter rel tinyrel,$(MODE)),)
USE_LD += -Wl,--icf=all
endif
else
USE_LD :=
endif
endif
endif
endif
LINKER_SELECTION_FLAG := $(USE_LD)

# MODE=rel
ifeq ($(MODE), rel)
CXXFLAGS += \
	-DNDEBUG \
	-O3 \
	-funroll-loops \
	-ffunction-sections \
	-fdata-sections \
	$(HARDENING_FLAGS) \
	$(FORTIFY_FLAGS)
LINK_LIBRARIES += $(HARDENING_LINK_FLAGS)
ifeq ($(TARGET), Darwin)
LINK_LIBRARIES += -Wl,-dead_strip
else
LINK_LIBRARIES += -Wl,--gc-sections
endif
ifneq ($(TARGET), Windows_NT)
ifneq ($(TARGET), Darwin)
CXXFLAGS += -flto
else ifeq ($(HOST_OPERATING_SYSTEM), Darwin)
CXXFLAGS += -flto
endif
endif
ifeq ($(TARGET), Linux)
SHOULD_SPLIT_DEBUG_INFO := 1
ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
CXXFLAGS += $(ZIG_STATIC_PIE_FLAGS)
else ifeq ($(SHOULD_LINK_STATIC_LINUX), 1)
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBRARIES += -lsupc++
endif
endif
ifeq ($(PGO), generate)
PGO_PROFILE_DIRECTORY := $(abspath ../pgo/profraw)
CXXFLAGS += "-fprofile-generate=$(PGO_PROFILE_DIRECTORY)"
else ifneq ($(PGO_PROFILE),)
CXXFLAGS += \
	"-fprofile-use=$(abspath $(PGO_PROFILE))" \
	-Wno-profile-instr-unprofiled \
	-Wno-profile-instr-out-of-date \
	-Wno-backend-plugin
ifeq ($(PGO), cs-generate)
PGO_PROFILE_DIRECTORY := $(abspath ../pgo/csprofraw)
CXXFLAGS += "-fcs-profile-generate=$(PGO_PROFILE_DIRECTORY)"
endif
endif
# MODE=tinyrel
else ifeq ($(MODE), tinyrel)
CXXFLAGS += \
	-DNDEBUG \
	-Oz \
	-fno-unroll-loops \
	-ffunction-sections \
	-fdata-sections \
	-fno-asynchronous-unwind-tables \
	-fmerge-all-constants \
	$(HARDENING_FLAGS) \
	$(FORTIFY_FLAGS)
LINK_LIBRARIES += $(HARDENING_LINK_FLAGS)
ifneq ($(TARGET), Windows_NT)
ifneq ($(TARGET), Darwin)
CXXFLAGS += -flto
else ifeq ($(HOST_OPERATING_SYSTEM), Darwin)
CXXFLAGS += -flto
endif
endif
ifeq ($(TARGET), Darwin)
LINK_LIBRARIES += -Wl,-dead_strip
else
LINK_LIBRARIES += -Wl,--gc-sections -s
endif
ifeq ($(TARGET), Linux)
ifeq ($(IS_LINUX_CROSS_COMPILATION), 1)
CXXFLAGS += $(ZIG_STATIC_PIE_FLAGS)
else ifeq ($(SHOULD_LINK_STATIC_LINUX), 1)
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBRARIES += -lsupc++
endif
endif
# MODE=dbg
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-DKOSH_DEBUG_SYSTEM_KOSHCONF_OVERRIDE \
	-fno-omit-frame-pointer \
	-g3 \
	-O0
ifneq ($(TARGET), Windows_NT)
ifneq ($(MODE), cosmo)
CXXFLAGS += \
	-fsanitize=address \
	-fsanitize=undefined
endif
endif
# MODE=cosmo
else ifeq ($(MODE), cosmo)
CXXFLAGS += \
	-mcosmo \
	-mtiny \
	-fexceptions \
	-g \
	-DNDEBUG \
	-O3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo)
endif
# MODE=cosmo_dbg
else ifeq ($(MODE), cosmo_dbg)
CXXFLAGS += \
	-mcosmo \
	-mdbg \
	-fexceptions \
	-g3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo_dbg)
endif
# MODE=prof
else ifeq ($(MODE), prof)
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-flto \
	-funroll-loops \
	$(HARDENING_FLAGS)
LINK_LIBRARIES += $(HARDENING_LINK_FLAGS)
ifneq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-O3 \
	$(FORTIFY_FLAGS)
ifeq ($(SHOULD_LINK_STATIC_LINUX), 1)
CXXFLAGS += -static-pie
endif
else
CXXFLAGS += \
	-O2
endif
ifeq ($(TARGET), Linux)
CXXFLAGS += -finstrument-functions
ifeq ($(SHOULD_LINK_STATIC_LINUX), 1)
CXXFLAGS += -nostdlib++
LINK_LIBRARIES += -lsupc++
endif
endif
else ifeq ($(MODE), cov)
COVERAGE_PROFILE_DIRECTORY := $(abspath ../coverage/profraw)
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-O0 \
	"-fprofile-instr-generate=$(COVERAGE_PROFILE_DIRECTORY)/kosh-%p.profraw" \
	-fcoverage-mapping
ifeq ($(TARGET), Linux)
ifeq ($(SHOULD_LINK_STATIC_LINUX), 1)
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBRARIES += -lsupc++
endif
endif
# MODE=fuzz
else ifeq ($(MODE), fuzz)
CXXFLAGS += \
	-DKOSH_FUZZ \
	-fno-omit-frame-pointer \
	-g \
	-O1 \
	-fsanitize=fuzzer-no-link,address,undefined
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "tinyrel", "prof", "cov", "fuzz", "cosmo", "cosmo_dbg")
endif

ifneq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-pthread
ifneq ($(IS_ANDROID_HOST), 1)
LINK_LIBRARIES += -lpthread
endif
endif

ifeq ($(TARGET), Darwin)
LINK_LIBRARIES += \
	-framework CoreFoundation \
	-framework IOKit
endif

# Missing Windows libraries.
ifeq ($(TARGET), Windows_NT)
LINK_LIBRARIES += \
	-luserenv \
	-ladvapi32 \
	-lshell32 \
	-lole32 \
	-luuid \
	-liphlpapi \
	-lrstrtmgr \
	-lws2_32
endif

ifeq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-Wno-unknown-pragmas
ifneq ($(IS_WINDOWS_GNU_ABI),)
CXXFLAGS += \
		-mconsole \
		-static \
		-static-libgcc \
		-static-libstdc++
LINK_LIBRARIES += -municode -Wl,--stack,8388608
else
LINK_LIBRARIES += -Wl,/entry:wmainCRTStartup -Wl,/STACK:8388608
endif
endif

REGEX_COMPILER_FLAGS := \
	$(filter-out -std=c++23 -fno-rtti,$(CXXFLAGS)) -std=c11 -I../vendor/regex

.DEFAULT_GOAL := kosh

COMMIT_HASH_STAMP := $(OBJECT_DIRECTORY)/.commit-hash
COMMIT_HASH_OBJECTS := \
	$(OBJECT_DIRECTORY)/CLI.o \
	$(OBJECT_DIRECTORY)/Main.o
STORED_COMMIT_HASH := $(shell cat $(COMMIT_HASH_STAMP) 2>/dev/null)

force_commit_hash:

force_commit_hash_objects:

ifneq ($(STORED_COMMIT_HASH),$(COMMIT_HASH))
$(COMMIT_HASH_OBJECTS): force_commit_hash_objects
endif

$(COMMIT_HASH_STAMP): force_commit_hash | dirs
	current_commit_hash='$(COMMIT_HASH)'; \
	if test ! -f $@ || test "$$(cat $@)" != "$$current_commit_hash"; then \
		printf '%s\n' "$$current_commit_hash" > $@; \
	fi

$(COMMIT_HASH_OBJECTS): $(COMMIT_HASH_STAMP)

$(OBJECTS): | dirs

ifneq ($(PGO_PROFILE),)
$(OBJECTS) $(LINKED_OUTPUT_PATH): $(abspath $(PGO_PROFILE))
endif

# Cold sources compile for size in MODE=rel. Throughput utilities such as grep,
# sort, wc, cat, tr, head, tail, cut, find, ls, and du stay at -O3.
COLD_SIZE_OBJECTS := \
	$(OBJECT_DIRECTORY)/koshkit/Evil%.o \
	$(OBJECT_DIRECTORY)/koshkit/Good%.o \
	$(OBJECT_DIRECTORY)/koshkit/Make.o \
	$(OBJECT_DIRECTORY)/koshkit/File.o \
	$(OBJECT_DIRECTORY)/koshkit/Pr.o \
	$(OBJECT_DIRECTORY)/koshkit/Od.o \
	$(OBJECT_DIRECTORY)/koshkit/Man.o \
	$(OBJECT_DIRECTORY)/koshkit/Meow.o \
	$(OBJECT_DIRECTORY)/builtins/Bench.o \
	$(OBJECT_DIRECTORY)/LanguageServer.o \
	$(OBJECT_DIRECTORY)/DiagnosticsCatalog.o
ifeq ($(MODE), rel)
$(COLD_SIZE_OBJECTS): CXXFLAGS += -Os -fno-unroll-loops
endif

$(OBJECT_DIRECTORY)/%.o: %.cpp Makefile
	printf "\tCompiling %-12s\033[K\r" $@
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJECT_DIRECTORY)/vendor/regex/%.o: ../vendor/regex/%.c Makefile
	printf "\tCompiling %-12s\033[K\r" $@
	$(CXX) $(REGEX_COMPILER_FLAGS) -x c -MMD -MP -c $< -o $@

# Linking.
$(LINKED_OUTPUT_PATH): $(OBJECTS)
	printf "\tLinking $(LINKED_OUTPUT_PATH)... %-64s\033[K\r" ' '
ifeq ($(TARGET), Windows_NT)
	cd $(OBJECT_DIRECTORY) && $(CXX) $(CXXFLAGS) $(LINKER_SELECTION_FLAG) \
		$(patsubst $(OBJECT_DIRECTORY)/%,%,$(OBJECTS)) $(LINK_LIBRARIES) \
		-o $(abspath $@)
else
	$(CXX) $(CXXFLAGS) $(LINKER_SELECTION_FLAG) $(OBJECTS) $(LINK_LIBRARIES) -o $@
endif
ifeq ($(SHOULD_SPLIT_DEBUG_INFO), 1)
	$(OBJCOPY) --only-keep-debug $@ $@.debug
	$(OBJCOPY) --strip-all --add-gnu-debuglink=$@.debug $@
endif

dirs: $(SOURCE_DIRECTORIES)
	mkdir -p $(OBJECT_DIRECTORY)/
	for source_directory in $(SOURCE_DIRECTORIES); do \
		mkdir -p "$(OBJECT_DIRECTORY)/$$source_directory"; \
	done
ifneq ($(REGEX_SOURCES),)
	mkdir -p $(OBJECT_DIRECTORY)/vendor/regex
endif
ifeq ($(MODE), cov)
	mkdir -p "$(COVERAGE_PROFILE_DIRECTORY)"
endif
ifneq ($(PGO_PROFILE_DIRECTORY),)
	mkdir -p "$(PGO_PROFILE_DIRECTORY)"
endif

kosh: dirs
ifneq ($(NO_TOILETLINE), 1)
	@test -f toiletline/toiletline.h || { \
		printf '%s\n' 'The Toiletline submodule is missing. Run git submodule update --init --recursive from the repository root.' >&2; \
		exit 1; \
	}
endif
	$(MAKE) $(LINKED_OUTPUT_PATH)
ifneq ($(LINKED_OUTPUT_PATH), $(OUTPUT_PATH))
	cp $(LINKED_OUTPUT_PATH) $(OUTPUT_PATH)
endif

# MODE=fuzz links each harness in ../test/fuzz with the shell objects into
# ../kosh-fuzz-NAME.
FUZZ_HARNESSES := $(wildcard ../test/fuzz/Fuzz*.cpp)
FUZZ_OUTPUTS := $(patsubst ../test/fuzz/Fuzz%.cpp,../kosh-fuzz-%,$(FUZZ_HARNESSES))

# the shell objects carry no main in MODE=fuzz.
../kosh-fuzz-%: ../test/fuzz/Fuzz%.cpp $(OBJECTS)
	printf "\tLinking $@... %-64s\033[K\r" ' '
	$(CXX) $(CXXFLAGS) -fsanitize=fuzzer -I. $< $(OBJECTS) \
		$(LINK_LIBRARIES) -o $@

fuzz: dirs
ifneq ($(MODE), fuzz)
	$(error The fuzz target needs MODE=fuzz)
endif
	$(MAKE) $(FUZZ_OUTPUTS)

# INSTALL_MOOD_SYMLINKS="sh bash dash".
PREFIX ?= /usr/local
DESTDIR ?=
bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man/man1
man5dir ?= $(PREFIX)/share/man/man5
bashcompdir ?= $(PREFIX)/share/bash-completion/completions
INSTALL_MOOD_SYMLINKS ?=

RELEASE_BINARY := ../kosh
COMMAND_MANPAGE := ../man/kosh.1
CONFIGURATION_MANPAGE := ../man/kosh.5
SWITCHING_MANPAGE := ../man/kosh-switching.5
BASH_COMPLETION_FILE := ../completions/kosh.bash

install:
ifeq ($(TARGET), Windows_NT)
	$(error Unable to figure out install directory for Windows. \
			Please put '$(realpath ..)' in your PATH manually)
else
	$(MAKE) MODE=rel kosh
	mkdir -p "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)" \
		"$(DESTDIR)$(man5dir)" "$(DESTDIR)$(bashcompdir)"
	install -m 755 $(RELEASE_BINARY) "$(DESTDIR)$(bindir)/kosh"
	install -m 644 $(COMMAND_MANPAGE) "$(DESTDIR)$(mandir)/kosh.1"
	install -m 644 $(CONFIGURATION_MANPAGE) "$(DESTDIR)$(man5dir)/kosh.5"
	install -m 644 $(SWITCHING_MANPAGE) \
		"$(DESTDIR)$(man5dir)/kosh-switching.5"
	install -m 644 $(BASH_COMPLETION_FILE) "$(DESTDIR)$(bashcompdir)/kosh"
	ln -sf kosh "$(DESTDIR)$(bindir)/koshkit"
	for mood in $(INSTALL_MOOD_SYMLINKS); do \
		ln -sf kosh "$(DESTDIR)$(bindir)/$$mood"; \
	done
	echo "Installed kosh, the koshkit link, its manual pages, and its bash completion under '$(DESTDIR)$(PREFIX)'."
endif

uninstall:
	rm -f "$(DESTDIR)$(bindir)/kosh"
	link="$(DESTDIR)$(bindir)/koshkit"; \
	case $$(readlink "$$link" 2>/dev/null) in \
	kosh | "$(bindir)/kosh") rm -f "$$link" ;; \
	*) if [ -e "$$link" ] || [ -L "$$link" ]; then \
		echo "Kept '$$link', which is not a link to kosh."; \
	fi ;; \
	esac
	rm -f "$(DESTDIR)$(mandir)/kosh.1"
	rm -f "$(DESTDIR)$(man5dir)/kosh.5"
	rm -f "$(DESTDIR)$(man5dir)/kosh-switching.5"
	rm -f "$(DESTDIR)$(bashcompdir)/kosh"
	for mood in $(INSTALL_MOOD_SYMLINKS); do \
		rm -f "$(DESTDIR)$(bindir)/$$mood"; \
	done
	echo "Removed kosh, its manual pages, and its bash completion from '$(DESTDIR)$(PREFIX)'."

CLANG_TIDY ?= clang-tidy

tidy:
	echo "	CLANG_TIDY $(SOURCES)"
	$(CLANG_TIDY) $(SOURCES) --header-filter='.*[.](cpp|hpp)$$' -- $(CXXFLAGS)

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG_FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(ROUTED_SOURCES) $(HEADERS)

clean:
	rm -rf ./o ../kosh ../kosh-$(MODE) ../kosh.exe ../kosh-$(MODE).exe
	rm -rf ../*.ilk ../*.pdb ../*.lib # Windows
	rm -rf ../kosh.com ../kosh-cosmo.com ../kosh.com.dbg ../kosh-cosmo.com.dbg ../kosh.aarch64.elf ../kosh-cosmo.aarch64.elf # Cosmopolitan
	rm -rf ../perf*
	rm -rf ../zig-out
	rm -rf ../*.profraw ../*.profdata ../coverage # Coverage

.PHONY: dirs force_commit_hash force_commit_hash_objects kosh fuzz install \
	uninstall tidy fmt clean

-include $(DEPENDENCY_FILES)
