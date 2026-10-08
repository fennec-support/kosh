/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shell option and mood changes, option listings,
 * positional parameter replacement, and variable listings for the set
 * builtin.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Options.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/StaticStringMap.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abefhkmnruvxBCEPTARWISG] [+abefhkmnuvxBCEPTARWISG] "
                   "[-o name] [+o name] [-M mood] [-L mood,...] [--] "
                   "[arg ...]");

HELP_DESCRIPTION_DECL(
    "The set builtin sets the Bash shell options, the Koshka options that "
    "have a letter, and the positional parameters. koshconf owns every other "
    "Koshka setting.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(MOOD, String, 'M', "",
     "Set the runtime mood to kosh, bash, sh, or bash-posix, or print it with "
     "no value.");
FLAG(INIT_MOODS, ManyStrings, 'L', "",
     "Source the startup files for the listed moods, or print the loaded ones "
     "with no value.");

REGISTER_BUILTIN_FLAGS(Set);

namespace koshka {

namespace {

fn is_set_option(const option_descriptor &option) wontthrow -> bool
{
  return !option.set_name.is_empty() ||
         (option.letter != '\0' && option.storage != option_storage::Mood);
}

fn is_restricted(const EvalContext &cxt) wontthrow -> bool
{
  return cxt.runtime_state().option_is_enabled(shell_option_id::Restricted);
}

fn option_is_on(const EvalContext &cxt, const option_descriptor &option) throws
    -> bool
{
  if (!option_is_available(cxt, option)) return false;
  if (option.storage == option_storage::WarningLevel)
    return cxt.runtime_state().get_warning_level() > 0;
  return read_option_number(cxt, option) != 0;
}

fn apply_or_reject_option(EvalContext &cxt, const option_descriptor &option,
                          bool enable,
                          bool should_step_warning_level = false) throws -> void
{
  if (!option_is_available(cxt, option))
    throw Error{StringView{"Unknown option '"} + option.set_name + "'"};
  if (option.storage == option_storage::WarningLevel) {
    if (should_step_warning_level)
      step_warning_level(cxt, enable);
    else
      write_option_number(cxt, option, enable ? 1 : 0, option_origin::Set);
    return;
  }
  write_option_number(cxt, option, enable ? 1 : 0, option_origin::Set);
}

enum class retired_option_owner : u8
{
  Koshconf,
  KoshconfInverted,
  Set,
  Shopt,
};

struct retired_option
{
  StringView replacement;
  retired_option_owner owner;
};

fn find_retired_option_replacement(StringView name, bool enable) throws
    -> Maybe<String>
{
  using enum retired_option_owner;
  static constexpr static_string_entry<retired_option> RETIRED_ENTRIES[] = {
      {SSK("annoying-diagnostics"),
       {"diagnostics.show_annoying_tier", Koshconf}                                         },
      {SSK("auto-pair"),               {"editor.auto_close_brackets_and_quotes", Koshconf}  },
      {SSK("error-exit"),              {"errexit", Set}                                     },
      {SSK("export-all"),              {"allexport", Set}                                   },
      {SSK("extended-arithmetic"),
       {"arithmetic.use_big_integers_and_decimals", Koshconf}                               },
      {SSK("extended-keys"),           {"editor.request_extended_key_reports", Koshconf}    },
      {SSK("failglob"),                {"failglob", Shopt}                                  },
      {SSK("history-prefix-search"),
       {"history.arrow_keys_search_by_typed_prefix", Koshconf}                              },
      {SSK("interactive-diagnostics"),
       {"editor.show_live_diagnostics", Koshconf}                                           },
      {SSK("interactive-hints"),       {"editor.show_command_synopsis", Koshconf}           },
      {SSK("koshkit"),                 {"koshkit.run_utilities_as_plain_commands", Koshconf}},
      {SSK("mimicry"),                 {"compat.mimic_shell_named_by_shebang", Koshconf}    },
      {SSK("no-clobber"),              {"noclobber", Set}                                   },
      {SSK("no-diagnostics"),
       {"diagnostics.analyze_before_running", KoshconfInverted}                             },
      {SSK("no-exec"),                 {"noexec", Set}                                      },
      {SSK("no-glob"),                 {"noglob", Set}                                      },
      {SSK("no-unset"),                {"nounset", Set}                                     },
      {SSK("show-all-exit-codes"),     {"debug.report_every_exit_code", Koshconf}           },
      {SSK("show-ast"),                {"debug.print_syntax_tree", Koshconf}                },
      {SSK("show-exit-code"),          {"debug.report_nonzero_exit_codes", Koshconf}        },
      {SSK("show-lexed-words"),        {"debug.print_lexed_word_escapes", Koshconf}         },
      {SSK("show-memory"),             {"debug.print_memory_report_at_exit", Koshconf}      },
      {SSK("show-stats"),              {"debug.print_evaluation_statistics", Koshconf}      },
      {SSK("space-after-completion"),
       {"completion.add_space_after_completed_word", Koshconf}                              },
      {SSK("transient-prompt"),
       {"editor.transient_prompt_after_submit", Koshconf}                                   },
  };
  static constexpr StaticStringMap RETIRED{RETIRED_ENTRIES};

  let const retired = RETIRED.find(name);
  if (!retired.has_value()) return None;

  let replacement = String{heap_allocator()};
  switch (retired->owner) {
  case Koshconf:
  case KoshconfInverted: {
    let const is_on = enable == (retired->owner == Koshconf);
    replacement += "koshconf set ";
    replacement += retired->replacement;
    replacement += is_on ? " on" : " off";
  } break;
  case Set:
    replacement += enable ? "set -o " : "set +o ";
    replacement += retired->replacement;
    break;
  case Shopt:
    replacement += enable ? "shopt -s " : "shopt -u ";
    replacement += retired->replacement;
    break;
  }

  return replacement;
}

fn list_options(const EvalContext &cxt) throws -> String
{
  let out = String{heap_allocator()};
  for (let const &option : get_set_listing_order()) {
    if (!option.is_listed_by_set || !option_is_available(cxt, option)) {
      continue;
    }
    out += option_is_on(cxt, option) ? "set -o " : "set +o ";
    out += option.set_name;
    out += '\n';
  }
  return out;
}

fn list_options_columnar(const EvalContext &cxt) throws -> String
{
  const usize name_field_width = 15;
  let out = String{heap_allocator()};
  for (let const &option : get_set_listing_order()) {
    if (!option.is_listed_by_set || !option_is_available(cxt, option)) {
      continue;
    }
    out += option.set_name;
    out.append_repeated(' ', option.set_name.length < name_field_width
                                 ? name_field_width - option.set_name.length
                                 : 0);
    out.push('\t');
    out += option_is_on(cxt, option) ? "on" : "off";
    out.push('\n');
  }
  return out;
}

fn apply_long_option_by_name(const ExecContext &ec, EvalContext &cxt,
                             const ArrayList<String> &args, usize &i,
                             bool enable) throws -> void
{
  if (i + 1 >= args.count()) {
    ec.print_to_stdout(enable ? list_options_columnar(cxt) : list_options(cxt));
    return;
  }
  let const &name = args[++i];
  let const *option = find_option_by_set_name(name);
  if (option == nullptr) {
    let const message = StringView{"Unknown -o option '"} + name + "'";
    if (let const replacement =
            find_retired_option_replacement(name.view(), enable);
        replacement.has_value())
    {
      throw make_error_for_arg(ec, i, message.view(),
                               StringView{"The option was retired; use `"} +
                                   *replacement + "` instead");
    }
    throw make_error_for_arg(ec, i, message.view());
  }
  if (!option_is_available(cxt, *option)) {
    let error = make_error_for_arg(
        ec, i, StringView{"Unknown -o option '"} + name + "'");
    error.set_command_status(2);
    throw error;
  }
  apply_or_reject_option(cxt, *option, enable);
}

fn format_option_table() throws -> String
{
  const usize name_field_width = 30;
  let out = String{heap_allocator()};
  for (let const &option : get_option_registry()) {
    if (!is_set_option(option)) continue;
    out += "  ";
    if (option.letter != '\0') {
      out.push('-');
      out.push(option.letter);
      out += "  ";
    } else {
      out += "    ";
    }
    let const name_cell = option.set_name.is_empty()
                              ? StringView{option.koshconf_name}
                              : StringView{option.set_name};
    out += name_cell;
    out.append_repeated(' ', name_cell.count() < name_field_width
                                 ? name_field_width - name_cell.count()
                                 : 0);
    out += option.help;
    out.push('\n');
  }
  return out;
}

fn format_option_switches_help() throws -> String
{
  let section = String{"OPTION SWITCHES\n"};
  section += "  A letter after a minus enables the option and after a plus "
             "disables it.\n  -o NAME and +o NAME do the same by Bash "
             "name. A Koshka letter is shown with its koshconf name.\n\n";
  section += format_option_table();
  section += "\n  The -o long names:\n";
  let listed_names = String{heap_allocator()};
  for (let const &option : get_set_listing_order()) {
    if (!listed_names.is_empty()) listed_names += ", ";
    listed_names += option.set_name;
  }
  section += wrap_text(listed_names.view(), 4, HELP_WRAP_WIDTH);
  section += '\n';
  return section;
}

} /* namespace */

fn query_shell_option(const EvalContext &cxt, StringView name) throws
    -> Maybe<bool>
{
  let const *option = find_option_by_set_name(name);
  if (option == nullptr) return None;
  return option_is_on(cxt, *option);
}

fn shell_option_names() throws -> const ArrayList<StringView> &
{
  static ArrayList<StringView> names = [] throws {
    let collected = ArrayList<StringView>{heap_allocator()};
    for (let const &option : get_set_listing_order())
      collected.push(option.set_name);
    return collected;
  }();
  return names;
}

fn shell_option_letters() throws -> const String &
{
  static String letters = [] throws {
    let collected = String{heap_allocator()};
    let const order = get_shell_flag_letter_order();
    for (usize position = 0; position < order.count(); position++) {
      let const letter = order[position];
      collected.push(letter);
      if (letter == 'h') collected.push('r');
    }
    return collected;
  }();
  return letters;
}

fn enabled_shell_option_names(const EvalContext &cxt) throws -> String
{
  let joined = String{heap_allocator()};
  for (let const &option : get_set_listing_order()) {
    if (!option.is_listed_by_set || !option.is_legacy() ||
        !option_is_on(cxt, option))
    {
      continue;
    }
    if (!joined.is_empty()) joined.push(':');
    joined.append(option.set_name);
  }
  return joined;
}

fn enabled_shell_option_letters(const EvalContext &cxt) throws -> String
{
  let letters = String{heap_allocator()};
  let const order = get_shell_flag_letter_order();
  for (usize position = 0; position < order.count(); position++) {
    let const letter = order[position];
    let const *option = find_option_by_letter(letter);
    ASSERT(option != nullptr);
    if (letter == 'h') {
      if (option_is_on(cxt, *option)) letters.push('h');
      if (cxt.runtime_state().option_is_enabled(shell_option_id::Restricted))
        letters.push('r');
      if (cxt.execution_store().shell_is_interactive()) letters.push('i');
      continue;
    }
    if (option->storage == option_storage::WarningLevel) {
      for (u8 warning_level = 0;
           warning_level < cxt.runtime_state().get_warning_level();
           warning_level++)
        letters.push('W');
      continue;
    }
    if (option_is_on(cxt, *option)) letters.push(letter);
  }
  if (cxt.execution_store().has_execution_string()) letters.push('c');
  return letters;
}

fn apply_shell_option(EvalContext &cxt, StringView name, bool enable) throws
    -> bool
{
  let const *option = find_option_by_set_name(name);
  if (option == nullptr || !option_is_available(cxt, *option)) return false;
  apply_or_reject_option(cxt, *option, enable);
  return true;
}

fn Set::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") {
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(ec,
                                       format_option_switches_help().view());
  }

  if (args.count() == 1) {
    let out = String{cxt.scratch_allocator()};
    for (let const &assignment : cxt.sorted_variable_assignments()) {
      out += assignment.view();
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  let operands = ArrayList<String>{heap_allocator()};
  bool is_collecting_operands = false;
  bool should_rebind = false;

  usize i = 1;
  let const do_read_option_value =
      [&](const String &option_arg) -> Maybe<StringView> {
    if (let const eq = option_arg.view().find_character('='); eq.has_value())
      return option_arg.view().substring(*eq + 1);
    if (i + 1 < args.count() && args[i + 1] != "--") return args[++i].view();
    return None;
  };

  for (; i < args.count(); i++) {
    let const &arg = args[i];

    if (is_collecting_operands) {
      operands.push_managed(arg);
      continue;
    }

    if (arg == "--") {
      is_collecting_operands = true;
      should_rebind = true;
      continue;
    }

    if (arg == "-M" || arg.view().starts_with(StringView{"-M="})) {
      let const value = do_read_option_value(arg);
      if (!value.has_value()) {
        ec.print_to_stdout(String{cxt.scratch_allocator(),
                                  mood_name(cxt.runtime_state().get_mood())} +
                           "\n");
        continue;
      }
      let const parsed = parse_mood_name(*value);
      if (!parsed.has_value())
        throw make_error_for_arg(
            ec, i,
            String{cxt.scratch_allocator(), "Unknown -M value '"} + *value +
                "', expected 'kosh', 'bash', 'sh', or 'bash-posix'");
      if (is_restricted(cxt)) {
        throw make_error_for_arg(
            ec, i, "Changing the mood is forbidden in a restricted shell");
      }
      let const *mood_option = find_option_by_letter('M');
      ASSERT(mood_option != nullptr);
      write_option_number(cxt, *mood_option, static_cast<u32>(*parsed),
                          option_origin::Set);
      continue;
    }

    if (arg == "-L" || arg.view().starts_with(StringView{"-L="})) {
      let const value = do_read_option_value(arg);
      if (!value.has_value()) {
        let out = String{cxt.scratch_allocator()};
        for (mimic_mood listed : {mimic_mood::Default, mimic_mood::Posix,
                                  mimic_mood::Bash, mimic_mood::BashPosix})
        {
          if (!cxt.runtime_control_store().mood_initialized(listed)) continue;
          if (!out.is_empty()) out += " ";
          out += mood_name(listed);
        }
        out += "\n";
        ec.print_to_stdout(out);
        continue;
      }
      let moods = ArrayList<mimic_mood>{cxt.scratch_allocator()};
      usize name_start = 0;
      for (usize j = 0; j <= value->length; j++) {
        if (j != value->length && (*value)[j] != ',') {
          continue;
        }
        let const name = value->substring_of_length(name_start, j - name_start);
        name_start = j + 1;
        if (name.is_empty()) continue;
        let const parsed = parse_mood_name(name);
        if (!parsed.has_value())
          throw make_error_for_arg(
              ec, i,
              String{cxt.scratch_allocator(), "Unknown -L value '"} + name +
                  "', expected 'kosh', 'bash', 'sh', or 'bash-posix'");
        moods.push(*parsed);
      }
      let const previous_mood = cxt.runtime_state().get_mood();
      source_init_moods(cxt, moods, cxt.startup_store().is_login_shell(),
                        cxt.execution_store().shell_is_interactive());
      cxt.select_mood(previous_mood);
      continue;
    }

    if (arg.view().starts_with(StringView{"--"})) {
      throw make_error_for_arg(ec, i,
                               StringView{"Unknown option '"} + arg + "'",
                               "Use koshconf for Koshka settings without a "
                               "letter, such as `koshconf set mood bash`");
    }

    if (arg == "-o" || arg == "+o") {
      apply_long_option_by_name(ec, cxt, args, i, arg[0] == '-');
      continue;
    }

    if (arg == "-" || arg == "+") {
      if (arg[0] == '-') {
        let const *xtrace = find_option_by_letter('x');
        let const *verbose = find_option_by_letter('v');
        ASSERT(xtrace != nullptr && verbose != nullptr);
        apply_or_reject_option(cxt, *xtrace, false);
        apply_or_reject_option(cxt, *verbose, false);
      }

      is_collecting_operands = true;
      should_rebind = i + 1 < args.count();
      continue;
    }

    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
      let const enable = arg[0] == '-';
      for (usize c = 1; c < arg.length(); c++) {
        let const letter = arg[c];

        if (letter == 'o') {
          apply_long_option_by_name(ec, cxt, args, i, enable);
          break;
        }
        if (letter == 'r') {
          if (!enable && cxt.runtime_state().option_is_enabled(
                             shell_option_id::Restricted))
          {
            throw make_error_for_arg(ec, i,
                                     "Restricted mode cannot be disabled");
          }
          if (enable)
            cxt.runtime_state().set_option(shell_option_id::Restricted, true);
          continue;
        }

        let const *option = find_option_by_letter(letter);
        let const is_letter_flag =
            option != nullptr &&
            (option->type == option_type::Boolean ||
             option->storage == option_storage::WarningLevel);
        if (!is_letter_flag || !option_is_available(cxt, *option)) {
          let invalid_option = String{heap_allocator()};
          invalid_option += arg[0];
          invalid_option += arg.view().substring_of_length(
              c, utils::decode_utf8(arg.view(), c, 0).length);
          if (!is_letter_flag)
            throw make_error_for_arg(
                ec, i, StringView{"Unknown option '"} + invalid_option + "'");
          let unavailable_error = make_error_for_arg(
              ec, i, StringView{"Unknown option '"} + invalid_option + "'");
          unavailable_error.set_command_status(2);
          throw unavailable_error;
        }
        let const is_mimicry = option->storage == option_storage::ShellOption &&
                               option->shell_option == shell_option_id::Mimicry;
        if (is_mimicry && is_restricted(cxt)) {
          throw make_error_for_arg(
              ec, i, "Changing mimicry is forbidden in a restricted shell");
        }
        apply_or_reject_option(cxt, *option, enable, true);
      }
      continue;
    }

    is_collecting_operands = true;
    should_rebind = true;
    operands.push_managed(arg);
  }

  if (should_rebind) {
    LOG(Debug, "set rebinding %zu positional parameters", operands.count());
    cxt.variable_store().positional_params() = steal(operands);
  }

  cxt.sync_analysis_environment();

  return 0;
}

} /* namespace koshka */
