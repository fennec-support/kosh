/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares literal classification, constant arithmetic folding,
 * propagated values, static command verdicts, and syntax-tree optimization
 * entry points. Optimizer.cpp owns the rewrite rules and traversal.
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

fn try_fold_constant_arithmetic(StringView expression) wontthrow -> Maybe<i64>;

fn simple_command_static_verdict(const ArrayList<const Token *> &args,
                                 const AnalysisContext &actx) throws
    -> Maybe<bool>;

pure fn word_segment_has_glob_metacharacter(
    const WordSegment &segment) wontthrow -> bool;

pure fn classify_plain_literal(const Word &word) wontthrow
    -> Word::PlainLiteral;

fn literal_word_value(const Word &word) throws -> Maybe<String>;
fn literal_word_value(const Token *token) throws -> Maybe<String>;

fn plain_variable_reference_name(const Token *token) wontthrow
    -> Maybe<StringView>;

fn propagated_literal_word_value(const Token *token,
                                 const AnalysisContext &actx) throws
    -> Maybe<String>;

fn try_fold_arithmetic_with_constants(StringView expression,
                                      const AnalysisContext &actx) wontthrow
    -> Maybe<String>;

fn optimize_node(const Expression *node, AnalysisContext &actx) throws -> void;

} /* namespace optimizer */

} /* namespace koshka */
