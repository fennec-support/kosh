/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file fuzzes the lexer, the parser, and the formatter through
 * format_shell_source. The first input byte selects the mood and a file
 * name, so the embedded format parsers also receive input.
 */

#include "Formatter.hpp"
#include "FuzzCommon.hpp"
#include "base/Arena.hpp"

static constexpr koshka::StringView FUZZ_FILE_NAMES[] = {
    "script.sh",       "script.bash",         "Makefile",
    "Dockerfile",      ".gitlab-ci.yml",      "package.json",
    "justfile",        "Taskfile.yml",        "compose.yaml",
    "playbook.yml",    "README.md",           "kosh.spec",
    "template.sh.j2",  ".github/workflows/ci.yml",
};

extern "C" int
LLVMFuzzerTestOneInput(const u8 *data, usize size)
{
  return koshka::fuzz::run_input(data, size, [](koshka::StringView input) {
    if (input.length == 0) return;

    let const selector  = static_cast<u8>(input[0]);
    let const mood      = static_cast<koshka::mimic_mood>(selector % 4);
    let const file_name = FUZZ_FILE_NAMES[(selector / 4) %
                                          countof(FUZZ_FILE_NAMES)];
    let arena           = koshka::BumpArena{};
    let errors = koshka::ArrayList<koshka::String>{koshka::heap_allocator()};

    unused(koshka::format_shell_source(input.substring(1), arena, errors,
                                       nullptr, nullptr, mood, file_name));
  });
}
