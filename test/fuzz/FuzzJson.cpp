/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file fuzzes the JSON parser of the language server.
 */

#include "FuzzCommon.hpp"

/* the header keeps the protocol helpers internal to one source file, and
   this harness uses only the parser. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunneeded-internal-declaration"
#include "LanguageServerProtocol.hpp"
#pragma GCC diagnostic pop

extern "C" int
LLVMFuzzerTestOneInput(const u8 *data, usize size)
{
  return koshka::fuzz::run_input(data, size, [](koshka::StringView input) {
    let parser = koshka::language_server::JsonParser{input};

    unused(parser.parse());
  });
}
