/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares literal classification, static command verdicts, and the
 * syntax-tree optimization entry points. Optimizer.cpp owns the rewrite rules,
 * constant folding, and traversal.
 */

#pragma once

#include "Tokens.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/Maybe.hpp"

namespace koshka {

class Token;
class Expression;
class AnalysisContext;

namespace optimizer {

fn
simple_command_static_verdict(const ArrayList<const Token *> &args,
                              const AnalysisContext          &actx) throws
    -> Maybe<bool>;

pure fn
word_segment_has_glob_metacharacter(const WordSegment &segment)
    wontthrow -> bool;

pure fn
classify_plain_literal(const Word &word) wontthrow -> Word::PlainLiteral;

fn
literal_word_value(const Word &word) throws -> Maybe<String>;
fn
literal_word_value(const Token *token) throws -> Maybe<String>;

fn
plain_variable_reference_name(const Token *token)
    wontthrow -> Maybe<StringView>;

fn
optimize_node(const Expression *node, AnalysisContext &actx) throws
    -> void;

} /* namespace optimizer */

} /* namespace koshka */
