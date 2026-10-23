/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the history constants and the atomic history file
 * replacement shared by both editor configurations.
 */

#pragma once

#include "Platform.hpp"
#include "base/ArrayList.hpp"
#include "base/Common.hpp"
#include "base/Path.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace toiletline {

inline constexpr usize UNWRITTEN_HISTORY_RECORD_BYTE_OFFSET =
    static_cast<usize>(-1);

inline constexpr int HISTORY_RACE_ATTEMPT_COUNT = 3;

inline constexpr usize HISTORY_RECORD_MAX_DECODED_BYTE_COUNT =
    koshka::os::HISTORY_RECORD_MAX_DECODED_BYTE_COUNT;

/* the longest command the history keeps, with and without the editor. */
inline constexpr usize HISTORY_ENTRY_MAX_BYTE_COUNT = 2048;

/* replaces path with contents through a temporary file renamed over it, and
   keeps the identity of that file in written when written is not null. */
fn
write_history_file_atomically(
    const koshka::Path      &path,
    koshka::StringView       name_prefix,
    koshka::StringView       contents,
    koshka::os::file_status *written = nullptr) throws -> bool;

struct history_record_span
{
  usize start_byte_offset;
  usize end_byte_offset;
};

fn
splice_history_spans(koshka::String                               &output,
                     koshka::StringView                            contents,
                     const koshka::ArrayList<history_record_span> &spans,
                     koshka::StringView inserted) throws -> usize;

} /* namespace toiletline */
