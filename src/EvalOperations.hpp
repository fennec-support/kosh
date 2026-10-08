/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares evaluator operations shared with parser, optimizer, and
 * startup code, including constant arithmetic, source moods, glob checks, and
 * assignment helpers. It avoids pulling the full evaluator into those owners.
 */

#pragma once

#include "MimicMood.hpp"
#include "base/Arena.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/Maybe.hpp"

namespace koshka {

class EvalContext;

fn evaluate_constant_arithmetic(StringView expression) throws -> i64;
fn evaluate_constant_arithmetic_nonzero(StringView expression,
                                        bool is_exact) throws -> bool;
fn evaluate_constant_arithmetic_text(StringView expression,
                                     Allocator allocator) throws -> String;
pure fn obvious_xor_power_operator_position(StringView expression) wontthrow
    -> Maybe<usize>;

fn find_substring_length_separator(StringView body) wontthrow -> usize;

wontreturn fn throw_script_fatal(StringView message,
                                 StringView note = {}) throws -> void;

fn source_init_moods(EvalContext &context, const ArrayList<mimic_mood> &moods,
                     bool is_login_shell, bool should_be_interactive) throws
    -> void;

}
