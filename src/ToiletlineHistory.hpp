/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the history constants and the atomic history file
 * replacement shared by both editor configurations. It stays apart from
 * Toiletline.hpp because Toiletline.cpp defines the vendored editor
 * configuration macros itself and cannot include that header.
 */

#pragma once

#include "Platform.hpp"
#include "base/Common.hpp"
#include "base/Path.hpp"
#include "base/StringView.hpp"

namespace toiletline {

inline constexpr usize UNWRITTEN_HISTORY_RECORD_BYTE_OFFSET =
    static_cast<usize>(-1);

inline constexpr int HISTORY_RACE_ATTEMPT_COUNT = 3;

inline constexpr usize HISTORY_RECORD_MAX_DECODED_BYTE_COUNT =
    koshka::os::HISTORY_RECORD_MAX_DECODED_BYTE_COUNT;

fn write_history_file_atomically(const koshka::Path &path,
                                 koshka::StringView name_prefix,
                                 koshka::StringView contents) throws -> bool;

}
