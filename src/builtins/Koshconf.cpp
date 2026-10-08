/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the koshconf builtin, the only interface to the
 * settings Koshka owns. It creates preset files, reads, changes, and persists
 * single options, lists the registry, and loads a binary KOSHCONF form.
 */

#include "../Koshconf.hpp"

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Options.hpp"
#include "../base/StaticStringMap.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("create <bash|sh|kosh> [--force]",
                   "set <option> <value> [--persist]", "get <option>",
                   "list | load <base64>");

HELP_DESCRIPTION_DECL(
    "The koshconf builtin reads and changes the settings Koshka owns, writes "
    "the configuration file, and loads the binary form of KOSHCONF.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(PERSIST, Bool, '\0', "persist",
     "With set, also write the option to the user configuration file.");
FLAG(FORCE, Bool, '\0', "force",
     "With create, replace an existing configuration file.");

REGISTER_BUILTIN_FLAGS(Koshconf);

namespace koshka {

namespace {

enum class koshconf_command : u8
{
  Create,
  Set,
  Get,
  List,
  Load,
};

constexpr static_string_entry<koshconf_command> KOSHCONF_COMMAND_ENTRIES[] = {
    {SSK("create"), koshconf_command::Create},
    {SSK("set"),    koshconf_command::Set   },
    {SSK("get"),    koshconf_command::Get   },
    {SSK("list"),   koshconf_command::List  },
    {SSK("load"),   koshconf_command::Load  },
};
constexpr StaticStringMap KOSHCONF_COMMANDS{KOSHCONF_COMMAND_ENTRIES};

constexpr static_string_entry<mimic_mood> KOSHCONF_PRESET_ENTRIES[] = {
    {SSK("kosh"), mimic_mood::Default},
    {SSK("bash"), mimic_mood::Bash   },
    {SSK("sh"),   mimic_mood::Posix  },
};
constexpr StaticStringMap KOSHCONF_PRESETS{KOSHCONF_PRESET_ENTRIES};

constexpr usize LIST_VALUE_COLUMN = 44;

struct koshconf_operands
{
  const ArrayList<String> &values;
  const ArrayList<SourceLocation> &locations;
};

fn report_usage(const ExecContext &ec, EvalContext &cxt,
                SourceLocation location, StringView message) throws -> i32
{
  report_soft_builtin_error(ec, cxt, location, message,
                            "Run `koshconf --help` for the forms");
  return 2;
}

fn report_caught_error(const ExecContext &ec, EvalContext &cxt,
                       SourceLocation location, const Error &error) throws
    -> void
{
  if (error.detail_message().is_empty()) {
    report_soft_builtin_error(ec, cxt, location, error.message().view());
    return;
  }

  report_soft_builtin_error(ec, cxt, location, error.message().view(),
                            error.detail_message());
}

fn find_named_option(const ExecContext &ec, EvalContext &cxt,
                     const koshconf_operands &operands, usize index) throws
    -> const option_descriptor *
{
  let const *option = find_option_by_koshconf_name(operands.values[index]);
  if (option == nullptr)
    report_soft_builtin_error(ec, cxt, operands.locations[index],
                              StringView{"Unknown option '"} +
                                  operands.values[index] + "'",
                              "Run `koshconf list` for the option names");
  return option;
}

fn require_user_path(const ExecContext &ec, EvalContext &cxt) throws
    -> Maybe<Path>
{
  let path = get_user_koshconf_path();
  if (!path.has_value())
    report_soft_builtin_error(
        ec, cxt,
        "Neither XDG_CONFIG_HOME nor HOME specifies a directory, so the user "
        "configuration file cannot be located");
  return path;
}

fn run_create(const ExecContext &ec, EvalContext &cxt,
              const koshconf_operands &operands) throws -> i32
{
  if (operands.values.count() != 3)
    return report_usage(ec, cxt, ec.source_location(),
                        "The create form takes one preset name");
  let const preset = KOSHCONF_PRESETS.find(operands.values[2]);
  if (!preset.has_value())
    return report_usage(ec, cxt, operands.locations[2],
                        StringView{"Unknown preset '"} + operands.values[2] +
                            "', expected 'bash', 'sh', or 'kosh'");

  let const path = require_user_path(ec, cxt);
  if (!path.has_value()) return 1;
  if (os::path_exists(path->text().view()) && !FLAG_FORCE.is_enabled()) {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              "The configuration file '" + path->text() +
                                  "' already exists",
                              "Pass --force to replace it");
    return 1;
  }

  try {
    write_koshconf_file(*path, make_koshconf_preset(*preset).view());
  } catch (const Error &error) {
    report_caught_error(ec, cxt, ec.source_location(), error);
    return 1;
  }

  return 0;
}

fn run_set(const ExecContext &ec, EvalContext &cxt,
           const koshconf_operands &operands) throws -> i32
{
  if (operands.values.count() != 4)
    return report_usage(ec, cxt, ec.source_location(),
                        "The set form takes an option name and a value");
  let const *option = find_named_option(ec, cxt, operands, 2);
  if (option == nullptr) return 1;
  let const value = operands.values[3].view();

  let const should_persist = FLAG_PERSIST.is_enabled();
  if (should_persist && !option->is_configurable()) {
    report_soft_builtin_error(
        ec, cxt, operands.locations[2],
        StringView{"The '"} + option->koshconf_name +
            "' option cannot be written with --persist",
        "Read-only, invocation-only, and session-dependent options are not "
        "written to the file");
    return 1;
  }

  if (let const problem = find_koshconf_value_problem(*option, value);
      problem.has_value())
  {
    report_soft_builtin_error(ec, cxt, operands.locations[3], problem->view());
    return 1;
  }

  let persisted_value = String{heap_allocator()};
  if (should_persist) {
    persisted_value = option->type == option_type::String
                          ? String{value}
                          : format_option_number(
                                *option, *parse_option_number(*option, value));
    try {
      unused(format_koshconf_line(*option, persisted_value.view()));
    } catch (const Error &error) {
      report_soft_builtin_error(ec, cxt, operands.locations[3],
                                error.message().view());
      return 1;
    }
  }

  try {
    write_option_text(cxt, *option, value, option_origin::Koshconf);
  } catch (const Error &error) {
    report_caught_error(ec, cxt, operands.locations[3], error);
    return 1;
  }

  if (!should_persist) return 0;
  let const path = require_user_path(ec, cxt);
  if (!path.has_value()) return 1;
  try {
    persist_koshconf_setting(*path, *option, persisted_value.view());
  } catch (const Error &error) {
    report_caught_error(ec, cxt, ec.source_location(), error);
    return 1;
  }

  return 0;
}

fn run_get(const ExecContext &ec, EvalContext &cxt,
           const koshconf_operands &operands) throws -> i32
{
  if (operands.values.count() != 3)
    return report_usage(ec, cxt, ec.source_location(),
                        "The get form takes one option name");
  let const *option = find_named_option(ec, cxt, operands, 2);
  if (option == nullptr) return 1;

  let line = read_option_text(cxt, *option);
  line += '\n';
  ec.print_to_stdout(line);
  return 0;
}

fn run_list(const ExecContext &ec, EvalContext &cxt,
            const koshconf_operands &operands) throws -> i32
{
  if (operands.values.count() != 2)
    return report_usage(ec, cxt, operands.locations[2],
                        "The list form takes no operand");

  let out = String{cxt.scratch_allocator()};
  for (let const &option : get_option_registry()) {
    if (option.is_set_alias) continue;

    let const line = format_koshconf_display_line(
        option, read_option_text(cxt, option).view());
    out += line.view();
    out.append_repeated(' ', line.count() < LIST_VALUE_COLUMN
                                 ? LIST_VALUE_COLUMN - line.count()
                                 : 1);
    out += option.category == option_class::Interactive ? "interactive"
                                                        : "semantic";
    out += '\n';
  }
  ec.print_to_stdout(out);
  return 0;
}

fn run_load(const ExecContext &ec, EvalContext &cxt,
            const koshconf_operands &operands) throws -> i32
{
  if (operands.values.count() != 3)
    return report_usage(ec, cxt, ec.source_location(),
                        "The load form takes one encoded value");

  let reading = koshconf_reading{};
  if (!read_koshconf_blob(operands.values[2].view(), reading)) {
    report_soft_builtin_error(ec, cxt, operands.locations[2],
                              "The value is not a valid KOSHCONF encoding");
    return 1;
  }
  apply_koshconf_settings(cxt, reading.settings, option_origin::Koshconf,
                          reading.warnings);
  for (let const &warning : reading.warnings) {
    ec.print_to_stderr(warning.view());
    ec.print_to_stderr("\n");
  }
  return 0;
}

} /* namespace */

Koshconf::Koshconf() = default;

pure fn Koshconf::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Koshconf;
}

fn Koshconf::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = parse_flags_vec(
      FLAG_LIST, ec.args(), ec.source_location().position, nullptr,
      &ec.arg_locations(), &operand_locations,
      builtin_error_context(ec.program()),
      flag_parse_options{.should_allow_options_after_operands = true});
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() < 2)
    return report_usage(ec, cxt, ec.source_location(),
                        "A subcommand is required");
  let const command = KOSHCONF_COMMANDS.find(args[1]);
  if (!command.has_value())
    return report_usage(ec, cxt, operand_locations[1],
                        StringView{"Unknown subcommand '"} + args[1] +
                            "', expected 'create', 'set', 'get', 'list', or "
                            "'load'");
  if (FLAG_PERSIST.is_enabled() && *command != koshconf_command::Set) {
    return report_usage(ec, cxt, ec.source_location(),
                        "Only the set form accepts --persist");
  }
  if (FLAG_FORCE.is_enabled() && *command != koshconf_command::Create) {
    return report_usage(ec, cxt, ec.source_location(),
                        "Only the create form accepts --force");
  }

  let const is_mutation = *command == koshconf_command::Create ||
                          *command == koshconf_command::Set ||
                          *command == koshconf_command::Load;
  if (is_mutation &&
      cxt.runtime_state().option_is_enabled(shell_option_id::Restricted))
  {
    report_soft_builtin_error(
        ec, cxt,
        "Changing settings with koshconf is forbidden in a restricted shell");
    return 1;
  }

  LOG(Debug, "koshconf running the '%s' form", args[1].c_str());
  let const operands = koshconf_operands{args, operand_locations};
  switch (*command) {
  case koshconf_command::Create: return run_create(ec, cxt, operands);
  case koshconf_command::Set: return run_set(ec, cxt, operands);
  case koshconf_command::Get: return run_get(ec, cxt, operands);
  case koshconf_command::List: return run_list(ec, cxt, operands);
  case koshconf_command::Load: return run_load(ec, cxt, operands);
  }
  unreachable("Unhandled koshconf form");
}

} /* namespace koshka */
