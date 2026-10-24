/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file fuzzes the kosh.conf text reader and the KOSHCONF blob decoder
 * with the same input.
 */

#include "FuzzCommon.hpp"
#include "Koshconf.hpp"

extern "C" int
LLVMFuzzerTestOneInput(const u8 *data, usize size)
{
  return koshka::fuzz::run_input(data, size, [](koshka::StringView input) {
    koshka::koshconf_reading text_reading{};
    koshka::koshconf_reading blob_reading{};

    koshka::read_koshconf_text(input, "fuzz", text_reading);
    unused(koshka::read_koshconf_blob(input, blob_reading));
  });
}
