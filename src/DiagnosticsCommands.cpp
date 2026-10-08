/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements diagnostics selected by a resolved command identifier
 * and literal operands. It checks command-name misuse, command-specific
 * arguments, traps, declaration spelling, and portability constraints. The
 * split keeps command policy separate from generic word and source checks.
 */

#include "DiagnosticsChecksInternal.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"
#include "base/PackedStringKey.hpp"
#include "base/StaticStringMap.hpp"

namespace koshka {

namespace expressions::internal {

namespace {

enum class trap_condition_mode : u8
{
  Default,
  Posix,
};

pure fn signal_name_is_unblockable(StringView bare) wontthrow -> bool
{
  return bare == "KILL" || bare == "STOP";
}

fn check_trap_condition_operands(AnalysisContext &actx,
                                 const ArrayList<const Token *> &args,
                                 trap_condition_mode mode) throws -> void
{
  let const is_posix = mode == trap_condition_mode::Posix;
  for (usize i = 2; i < args.count(); i++) {
    if (args[i]->kind() != Token::Kind::Word) continue;

    let const literal = static_cast<const tokens::WordToken *>(args[i])
                            ->word()
                            .to_literal_string();
    let const view = literal.view();
    if (view.is_empty()) continue;

    if (view == "ERR") {
      if (is_posix) {
        actx.report_diagnostic(diagnostic_id::sc3047,
                               args[i]->source_location());
      }
      continue;
    }

    if (view.is_all_decimal_digits() && view.length <= 3) {
      usize number = 0;
      for (usize position = 0; position < view.length; position++)
        number = number * 10 + static_cast<usize>(view[position] - '0');

      actx.report_diagnostic(diagnostic_id::sc2172, args[i]->source_location(),
                             {view});

      let const name = os::signal_name_from_number(static_cast<i32>(number));
      if (name.has_value() && signal_name_is_unblockable(name->view())) {
        actx.report_diagnostic(diagnostic_id::sc2173,
                               args[i]->source_location(), {view});
      }

      continue;
    }

    let uppercase = String{heap_allocator()};
    let has_lowercase = false;

    for (usize position = 0; position < view.length; position++) {
      let const byte = view[position];
      if (byte >= 'a' && byte <= 'z') {
        has_lowercase = true;
        uppercase.push(static_cast<char>(byte - 'a' + 'A'));
      } else {
        uppercase.push(byte);
      }
    }

    let const has_sig_prefix =
        uppercase.view().starts_with(StringView{"SIG"}) && view.length > 3;
    let const bare =
        has_sig_prefix ? uppercase.view().substring(3) : uppercase.view();
    let const names_a_signal = bare == "EXIT" ||
                               signal_name_is_unblockable(bare) ||
                               os::signal_number_from_name(bare).has_value();

    if (signal_name_is_unblockable(bare)) {
      actx.report_diagnostic(diagnostic_id::sc2173, args[i]->source_location(),
                             {view});
    }

    if (!is_posix) continue;

    if (has_sig_prefix && names_a_signal) {
      actx.report_diagnostic(diagnostic_id::sc3048, args[i]->source_location(),
                             {view});
    }

    if (has_lowercase && names_a_signal) {
      actx.report_diagnostic(diagnostic_id::sc3049, args[i]->source_location(),
                             {view});
    }
  }
}

}

fn check_command_name_lints(AnalysisContext &actx,
                            const command_lint_input &input) throws -> void
{
  let const &args = input.args;
  let const location = input.command_location();

  if (input.is_in_group(COMMAND_GROUP_TEST)) {
    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() != Token::Kind::Word) continue;
      let const &word = static_cast<const tokens::WordToken *>(args[i])->word();
      for (let const &segment : word.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference &&
            segment.is_split_eligible() &&
            !is_single_word_special_parameter(segment.text.view()) &&
            !reference_has_quoted_alternate_word(segment.text.view()))
        {
          let const operand_location = args[i]->source_location();
          actx.report_diagnostic(
              diagnostic_id::sc2086_test,
              expansion_location_with_sigil(
                  actx,
                  segment
                      .get_source_location(operand_location.source_name_index)
                      .value_or(operand_location)));
          break;
        }
      }
    }
  }

  if (input.is_command_shadowed) return;

  let const is_posix = actx.is_posix_sh_shebang;

  switch (input.command_id()) {
  case command_name_id::Read:
    if (!args_have_short_flag(args, 'r'))
      actx.report_diagnostic(diagnostic_id::sc2162,
                             input.command_source_location);
    break;

  case command_name_id::Echo:
    if (is_posix && args.count() >= 2 && args[1]->kind() == Token::Kind::Word) {
      let const flag = static_cast<const tokens::WordToken *>(args[1])
                           ->word()
                           .to_literal_string();
      let const view = flag.view();
      static constexpr PackedStringKey ECHO_FLAG_KEYS[] = {
          SSK("-e"), SSK("-n"), SSK("-E"), SSK("-ne"), SSK("-en")};
      static constexpr StaticStringSet ECHO_FLAGS{ECHO_FLAG_KEYS};
      if (ECHO_FLAGS.contains(view))
        actx.report_diagnostic(diagnostic_id::sc3037,
                               args[1]->source_location(), {view});
    }
    break;

  case command_name_id::Declare:
    if (is_posix)
      actx.report_diagnostic(diagnostic_id::sc3044, location,
                             {input.command_literal});
    break;

  case command_name_id::Typeset:
    if (is_posix) {
      actx.report_diagnostic(diagnostic_id::sc3044, location,
                             {input.command_literal});
    } else {
      actx.report_diagnostic(diagnostic_id::typeset_spelling, location);
    }
    break;

  case command_name_id::Source:
    if (is_posix) actx.report_diagnostic(diagnostic_id::sc3046, location);
    break;

  case command_name_id::Local:
    if (is_posix) actx.report_diagnostic(diagnostic_id::sc3043, location);
    if (actx.function_scope_depth == 0 && !actx.walk.is_command_status_observed)
      actx.report_diagnostic(diagnostic_id::sc2168, location);
    break;

  case command_name_id::Mapfile:
  case command_name_id::Readarray: {
    if (is_posix)
      actx.report_diagnostic(diagnostic_id::sc3030, location,
                             {input.command_literal});

    let filled_name_index = args.count();
    let should_skip_option_operand = false;
    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() != Token::Kind::Word) continue;

      let const literal = static_cast<const tokens::WordToken *>(args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();
      if (should_skip_option_operand) {
        should_skip_option_operand = false;
        continue;
      }
      if (view.length == 2 && view[0] == '-') {
        switch (view[1]) {
        case 'C':
        case 'c':
        case 'd':
        case 'n':
        case 'O':
        case 's':
        case 'u': should_skip_option_operand = true; continue;

        default: continue;
        }
      }
      if (view.length >= 2 && view[0] == '-') {
        continue;
      }
      if (!operand_target_name(view).is_empty()) filled_name_index = i;
    }

    if (filled_name_index == args.count()) {
      actx.add_array_valued_name(StringView{"MAPFILE"});
      break;
    }

    let const filled =
        static_cast<const tokens::WordToken *>(args[filled_name_index])
            ->word()
            .to_literal_string();
    actx.add_array_valued_name(operand_target_name(filled.view()));
    break;
  }

  case command_name_id::Getopts: {
    if (args.count() < 3) break;
    if (args[1]->kind() != Token::Kind::Word) break;
    if (args[2]->kind() != Token::Kind::Word) break;

    let const &optstring_word =
        static_cast<const tokens::WordToken *>(args[1])->word();
    let const &name_word =
        static_cast<const tokens::WordToken *>(args[2])->word();
    if (optstring_word.segments.count() != 1) break;
    if (name_word.segments.count() != 1) break;
    if (!word_is_fully_literal(optstring_word)) break;
    if (!word_is_fully_literal(name_word)) break;

    actx.active_getopts.optstring = optstring_word.segments[0].text.view();
    actx.active_getopts.variable_name = name_word.segments[0].text.view();
    actx.active_getopts.location = args[1]->source_location();
    break;
  }

  case command_name_id::Break:
  case command_name_id::Continue:
    if (actx.loop_body_depth == 0) {
      actx.report_diagnostic(actx.function_scope_depth > 0
                                 ? diagnostic_id::sc2104
                                 : diagnostic_id::sc2105,
                             location, {input.command_literal});
    } else if (actx.walk.is_direct_pipeline_stage) {
      actx.report_diagnostic(diagnostic_id::sc2106, location,
                             {input.command_literal});
    }
    break;

  case command_name_id::Set:
    if (args.count() >= 2) {
      let const literal = args[1]->raw_string();
      let const view = literal.view();
      if (!view.is_empty() && view[0] != '-' && view[0] != '+') {
        let const target = operand_target_name(view);
        if (!target.is_empty() && target.length < view.length &&
            view[target.length] == '=')
        {
          actx.report_diagnostic(diagnostic_id::sc2121,
                                 args[1]->source_location(), {target});
        }
      }
    }
    break;

  case command_name_id::Dot:
    if (is_posix && args.count() > 2) {
      let const operand = args[2]->raw_string();
      actx.report_diagnostic(diagnostic_id::sc2240, args[2]->source_location(),
                             {operand.view()});
    }
    break;

  case command_name_id::Which:
    actx.report_diagnostic(diagnostic_id::sc2230, location);
    break;

  case command_name_id::Egrep:
    actx.report_diagnostic(diagnostic_id::sc2196, location);
    break;

  case command_name_id::Fgrep:
    actx.report_diagnostic(diagnostic_id::sc2197, location);
    break;

  case command_name_id::Expr:
    actx.report_diagnostic(diagnostic_id::sc2003, location);
    break;

  case command_name_id::Trap:
    if (args.count() >= 2 && args[1]->kind() == Token::Kind::Word) {
      let const &action =
          static_cast<const tokens::WordToken *>(args[1])->word();
      let action_expands_now = false;
      for (let const &segment : action.segments)
        if (segment.is_in_double_quotes &&
            (segment.kind == WordSegment::Kind::VariableReference ||
             segment.kind == WordSegment::Kind::CommandSubstitution))
        {
          action_expands_now = true;
          break;
        }
      if (action_expands_now)
        actx.report_diagnostic(diagnostic_id::sc2064,
                               args[1]->source_location());
    }
    check_trap_condition_operands(actx, args,
                                  is_posix ? trap_condition_mode::Posix
                                           : trap_condition_mode::Default);
    break;

  case command_name_id::Exec:
    if (is_posix && args.count() >= 2 && args[1]->kind() == Token::Kind::Word) {
      let const flag = static_cast<const tokens::WordToken *>(args[1])
                           ->word()
                           .to_literal_string();
      let const view = flag.view();
      if (view.length >= 2 && view[0] == '-' && view != "--")
        actx.report_diagnostic(diagnostic_id::sc3038,
                               args[1]->source_location(), {view});
    }
    break;

  case command_name_id::Let:
    if (is_posix) actx.report_diagnostic(diagnostic_id::sc3039, location);

    if (args.count() >= 2)
      actx.report_diagnostic(diagnostic_id::sc2219, location);

    break;

  case command_name_id::Printf: {
    if (is_posix && args.count() >= 2 && args[1]->kind() == Token::Kind::Word &&
        static_cast<const tokens::WordToken *>(args[1])
                ->word()
                .to_literal_string()
                .view() == "-v")
    {
      actx.report_diagnostic(diagnostic_id::sc3045, args[1]->source_location());
    }

    usize format_index = 0;
    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() != Token::Kind::Word) {
        format_index = i;
        break;
      }
      let const literal = static_cast<const tokens::WordToken *>(args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();
      if (view == "--") {
        if (i + 1 < args.count()) format_index = i + 1;
        break;
      }
      if (!(view.length >= 1 && view[0] == '-')) {
        format_index = i;
        break;
      }
    }

    if (format_index != 0 && args[format_index]->kind() == Token::Kind::Word) {
      let const &format =
          static_cast<const tokens::WordToken *>(args[format_index])->word();
      bool has_format_expansion = false;
      for (let const &segment : format.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference ||
            segment.kind == WordSegment::Kind::CommandSubstitution)
        {
          has_format_expansion = true;
          break;
        }
      }
      if (has_format_expansion)
        actx.report_diagnostic(diagnostic_id::sc2059,
                               args[format_index]->source_location());
    }
    break;
  }

  default: break;
  }
}

fn check_command_value_lints(AnalysisContext &actx,
                             const command_lint_input &input) throws -> void
{
  if (input.is_command_shadowed) return;

  let const &args = input.args;

  if (input.is_in_group(COMMAND_GROUP_ASSIGNMENT_BUILTIN)) {
    let has_reported_substitution_value = false;
    let has_array_declaration = false;
    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() == Token::Kind::Assignment) {
        let const *assignment =
            static_cast<const tokens::Assignment *>(args[i]);
        if (has_array_declaration)
          actx.add_array_valued_name(assignment->key().view());

        if (has_reported_substitution_value) continue;

        let value_has_substitution = false;
        for (let const &segment : assignment->value_word().segments)
          if (segment.kind == WordSegment::Kind::CommandSubstitution) {
            value_has_substitution = true;
            break;
          }
        if (!value_has_substitution) continue;

        actx.report_diagnostic(diagnostic_id::sc2155,
                               args[i]->source_location());
        has_reported_substitution_value = true;
        continue;
      }

      if (args[i]->kind() != Token::Kind::Word) continue;

      let const literal = static_cast<const tokens::WordToken *>(args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();

      if (view.length >= 2 && view[0] == '-') {
        if (view.find_character('a').has_value() ||
            view.find_character('A').has_value())
        {
          has_array_declaration = true;
        }

        continue;
      }

      if (!has_array_declaration) continue;

      let const target = operand_target_name(view);
      if (!target.is_empty()) actx.add_array_valued_name(target);
    }
  }

  switch (input.command_id()) {
  case command_name_id::Rm:
    if (!args_have_short_flag(args, 'r')) break;

    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() != Token::Kind::Word) continue;
      let const &word = static_cast<const tokens::WordToken *>(args[i])->word();
      if (word.segments.count() >= 2 &&
          word.segments[0].kind == WordSegment::Kind::VariableReference &&
          !word.segments[0].text.view().find_character(':').has_value() &&
          !word.segments[1].text.is_empty() && word.segments[1].text[0] == '/')
      {
        actx.report_diagnostic(diagnostic_id::sc2115,
                               args[i]->source_location(),
                               {word.segments[0].text.view()});
      }
      if (word_is_fully_literal(word)) {
        let const literal = word.to_literal_string();
        if (is_system_directory(literal.view()))
          actx.report_diagnostic(diagnostic_id::sc2114,
                                 args[i]->source_location(), {literal.view()});
      }
    }
    break;

  case command_name_id::Grep:
  case command_name_id::Egrep:
  case command_name_id::Fgrep: {
    let is_fixed_string_mode = input.command_id() == command_name_id::Fgrep;
    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() != Token::Kind::Word) continue;
      let const &word = static_cast<const tokens::WordToken *>(args[i])->word();
      let const literal = word.to_literal_string();
      let const view = literal.view();
      if (view.length >= 1 && view[0] == '-') {
        if (view.find_character('F').has_value() ||
            view.starts_with(StringView{"--fixed-strings"}))
        {
          is_fixed_string_mode = true;
        }
        continue;
      }
      if (word.segments.count() == 1 &&
          word.segments[0].kind == WordSegment::Kind::UnquotedText &&
          word.segments[0].has_glob_metacharacter())
      {
        actx.report_diagnostic(diagnostic_id::sc2062,
                               args[i]->source_location());
      } else if (!view.is_empty() && view[0] == '*') {
        actx.report_diagnostic(diagnostic_id::sc2063,
                               args[i]->source_location());
      } else if (!is_fixed_string_mode && view_is_glob_shaped_pattern(view)) {
        actx.report_diagnostic(diagnostic_id::sc2022,
                               args[i]->source_location(), {view});
      }
      break;
    }
    break;
  }

  case command_name_id::Mkdir:
    if (args_have_short_flag(args, 'p') && args_have_short_flag(args, 'm'))
      actx.report_diagnostic(diagnostic_id::sc2174, input.command_location());
    break;

  case command_name_id::Exit:
  case command_name_id::Return: {
    let const is_return = input.command_id() == command_name_id::Return;

    if (args.count() > 2) {
      actx.report_diagnostic(is_return ? diagnostic_id::sc2151
                                       : diagnostic_id::sc2241,
                             args[2]->source_location());
    }

    if (args.count() >= 2 && args[1]->kind() == Token::Kind::Word) {
      let const &operand =
          static_cast<const tokens::WordToken *>(args[1])->word();
      if (word_is_fully_literal(operand)) {
        let const literal = operand.to_literal_string();
        let const view = literal.view();
        let is_in_range = view_is_integer_literal(view) && view[0] != '-';
        if (is_in_range) {
          let const parsed_code = view.to<i64>();
          is_in_range = !parsed_code.is_error() && parsed_code.value() <= 255;
        }
        if (!is_in_range)
          actx.report_diagnostic(diagnostic_id::sc2242,
                                 args[1]->source_location(),
                                 {view, input.command_literal});
      } else if (is_return && operand.segments.count() == 1 &&
                 operand.segments[0].kind ==
                     WordSegment::Kind::CommandSubstitution)
      {
        actx.report_diagnostic(diagnostic_id::sc2152,
                               args[1]->source_location());
      }
    }
    break;
  }

  case command_name_id::Cp:
  case command_name_id::Ln:
  case command_name_id::Mv: {
    let const operand = single_literal_file_operand(args);
    if (!operand.has_value()) break;

    let missing_destination = diagnostic_id::sc2224;
    switch (input.command_id()) {
    case command_name_id::Cp:
      missing_destination = diagnostic_id::sc2225;
      break;
    case command_name_id::Ln:
      missing_destination = diagnostic_id::sc2226;
      break;
    default: break;
    }

    actx.report_diagnostic(missing_destination, (*operand)->source_location(),
                           {(*operand)->raw_string().view()});
    break;
  }

  case command_name_id::Xargs:
    if (args_have_short_flag(args, 'i'))
      actx.report_diagnostic(diagnostic_id::sc2267, input.command_location());

    for (usize i = 1; i < args.count(); i++) {
      if (args[i]->kind() != Token::Kind::Word) continue;

      let const literal = static_cast<const tokens::WordToken *>(args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();
      if (!view.is_empty() && view[0] == '-') {
        continue;
      }

      if (actx.functions.defined.contains(view)) {
        actx.report_diagnostic(diagnostic_id::sc2033,
                               args[i]->source_location(), {view});
      }
      break;
    }
    break;

  default: break;
  }
}

}

}
