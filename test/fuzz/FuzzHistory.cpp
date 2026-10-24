/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file fuzzes the history file validation and the history record
 * encoder with the same input.
 */

#include "FuzzCommon.hpp"
#include "Toiletline.hpp"
#include "base/String.hpp"

extern "C" int
LLVMFuzzerTestOneInput(const u8 *data, usize size)
{
  return koshka::fuzz::run_input(data, size, [](koshka::StringView input) {
    unused(toiletline::is_history_contents_valid(input));

    if (input.length > toiletline::HISTORY_ENTRY_MAX_BYTE_COUNT) return;

    let encoded = koshka::String{koshka::heap_allocator()};

    toiletline::encode_history_record(encoded, input);
  });
}
