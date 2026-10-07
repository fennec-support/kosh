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
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abefhkmnruvxBCEPTARWISG] [+abefhkmnuvxBCEPTARWISG] "
                   "[-o name] [+o name] [--options] [-M mood] "
                   "[-L mood,...] [--tab-selector mode] [--] [arg ...]");

HELP_DESCRIPTION_DECL(
    "The set builtin sets the shell options and the positional parameters.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(OPTIONS, Bool, '\0', "options",
     "Display every option with its current state and description.");
/* The mood flags are parsed by hand in execute(), so these declarations only
   join the set builtin's flag list for completion and the help listing. */
FLAG(MOOD, String, 'M', "mood",
     "Set the runtime mood to kosh, bash, or sh, or print it with no value.");
FLAG(INIT_MOODS, ManyStrings, 'L', "init-moods",
     "Source the startup files for the listed moods, or print the loaded ones "
     "with no value.");
FLAG(TAB_SELECTOR, String, '\0', "tab-selector",
     "Present several completion candidates as interactive, external, or "
     "plain, or print the active one with no value.");

REGISTER_BUILTIN_FLAGS(Set);

namespace koshka {

namespace {

fn is_set_option(const option_descriptor &option) wontthrow -> bool
{
  return !option.set_name.is_empty() ||
         option.storage == option_storage::WarningLevel;
}

fn option_is_on(const EvalContext &cxt, const option_descriptor &option) throws
    -> bool
{
  if (!option_is_available(cxt, option)) return false;
  if (option.storage == option_storage::WarningLevel)
    return cxt.runtime_state().get_warning_level() > 0;
  return (read_option_number(cxt, option) != 0) != option.is_set_name_inverted;
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
  write_option_number(cxt, option, enable != option.is_set_name_inverted,
                      option_origin::Set);
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
  if (option == nullptr)
    throw make_error_for_arg(ec, i,
                             StringView{"Unknown -o option '"} + name + "'");
  if (!option_is_available(cxt, *option)) {
    let error = make_error_for_arg(
        ec, i, StringView{"Unknown -o option '"} + name + "'");
    error.set_command_status(2);
    throw error;
  }
  apply_or_reject_option(cxt, *option, enable);
}

fn format_option_table(const EvalContext *cxt,
                       bool include_alias_spellings) throws -> String
{
  const usize name_field_width = include_alias_spellings ? 30 : 18;
  let out = String{heap_allocator()};
  for (let const &option : get_option_registry()) {
    if (!is_set_option(option)) continue;
    if (cxt != nullptr && !option_is_available(*cxt, option)) {
      continue;
    }
    out += "  ";
    if (option.letter != '\0') {
      out.push('-');
      out.push(option.letter);
      out += "  ";
    } else {
      out += "    ";
    }
    let name_cell = String{StringView{option.set_name}};
    if (include_alias_spellings && !option.set_alias.is_empty()) {
      name_cell += ", ";
      name_cell += option.set_alias;
    }
    out += name_cell.view();
    out.append_repeated(' ', name_cell.count() < name_field_width
                                 ? name_field_width - name_cell.count()
                                 : 0);
    if (cxt != nullptr) out += option_is_on(*cxt, option) ? "[on]  " : "[off] ";
    out += option.help;
    out.push('\n');
  }
  return out;
}

fn format_option_switches_help() throws -> String
{
  let section = String{"OPTION SWITCHES\n"};
  section += "  A letter after a minus enables the option and after a plus "
             "disables it.\n  -o NAME and +o NAME do the same by long "
             "name.\n\n";
  section += format_option_table(nullptr, true);
  section += "\n  The -o long names:\n";
  let listed_names = String{heap_allocator()};
  for (let const &option : get_option_registry()) {
    if (option.set_name.is_empty()) continue;
    if (!listed_names.is_empty()) listed_names += ", ";
    listed_names += option.set_name;
    if (!option.set_alias.is_empty()) {
      listed_names += ", ";
      listed_names += option.set_alias;
    }
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

fn shell_option_names(bool include_alias_spellings) throws
    -> const ArrayList<StringView> &
{
  static ArrayList<StringView> canonical = [] throws {
    let names = ArrayList<StringView>{heap_allocator()};
    for (let const &option : get_option_registry())
      if (!option.set_name.is_empty()) names.push(option.set_name);
    return names;
  }();
  static ArrayList<StringView> with_aliases = [] throws {
    let names = ArrayList<StringView>{heap_allocator()};
    for (let const &option : get_option_registry()) {
      if (!option.set_name.is_empty()) names.push(option.set_name);
      if (!option.set_alias.is_empty()) names.push(option.set_alias);
    }
    return names;
  }();
  return include_alias_spellings ? with_aliases : canonical;
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

Set::Set() = default;

pure fn Set::kind() const wontthrow -> Builtin::Kind { return Kind::Set; }

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

    if (arg == "--mood" || arg == "-M" ||
        arg.view().starts_with(StringView{"--mood="}) ||
        arg.view().starts_with(StringView{"-M="}))
    {
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
            String{cxt.scratch_allocator(), "Unknown --mood value '"} + *value +
                "', expected 'kosh', 'bash', 'sh', or 'bash-posix'");
      let const *mood_option = find_option_by_letter('M');
      ASSERT(mood_option != nullptr);
      write_option_number(cxt, *mood_option, static_cast<u32>(*parsed),
                          option_origin::Set);
      continue;
    }

    if (arg == "--tab-selector" ||
        arg.view().starts_with(StringView{"--tab-selector="}))
    {
      let const value = do_read_option_value(arg);
      if (!value.has_value()) {
        ec.print_to_stdout(
            String{cxt.scratch_allocator(),
                   tab_selector_name(cxt.runtime_state().get_tab_selector())} +
            "\n");
        continue;
      }
      let const parsed = parse_tab_selector_name(*value);
      if (!parsed.has_value()) {
        throw make_error_for_arg(
            ec, i,
            String{cxt.scratch_allocator(), "Unknown --tab-selector value '"} +
                *value + "', expected 'interactive', 'external', or 'plain'");
      }
      cxt.runtime_state().set_tab_selector(*parsed);
      continue;
    }

    if (arg == "--init-moods" || arg == "-L" ||
        arg.view().starts_with(StringView{"--init-moods="}) ||
        arg.view().starts_with(StringView{"-L="}))
    {
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
              String{cxt.scratch_allocator(), "Unknown --init-moods value '"} +
                  name + "', expected 'kosh', 'bash', 'sh', or 'bash-posix'");
        moods.push(*parsed);
      }
      let const previous_mood = cxt.runtime_state().get_mood();
      source_init_moods(cxt, moods, cxt.startup_store().is_login_shell(),
                        cxt.execution_store().shell_is_interactive());
      cxt.select_mood(previous_mood);
      continue;
    }

    if (arg == "-o" || arg == "+o") {
      apply_long_option_by_name(ec, cxt, args, i, arg[0] == '-');
      continue;
    }

    if (arg == "--options") {
      ec.print_to_stdout(format_option_table(&cxt, false));
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

        /* The o letter ends the bundle and names one option from the next
           argument, the way bash accepts set -euo pipefail. */
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
