/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the mkdir utility. It creates named directories or
 * missing parent chains and applies an optional parsed creation mode.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"
#include "../base/Trace.hpp"

KOSHKIT_UTIL_DECL("[-p] [-m mode] directory ...",
                  "The mkdir utility creates each named directory.");

FLAG(MKDIR_PARENTS, Bool, 'p', "",
     "Create the missing parent directories and ignore one that already "
     "exists.");
FLAG(MKDIR_MODE, String, 'm', "",
     "Set the file mode of the named directory, an octal or symbolic "
     "operand.");

REGISTER_KOSHKIT_UTIL_FLAGS(Mkdir);

namespace koshka::koshkit {

enum class mode_application : u8
{
  RespectUmask,
  Exact,
};

enum class existing_directory_policy : u8
{
  Reject,
  Accept,
};

static fn make_one(StringView path, u32 mode, mode_application application,
                   existing_directory_policy existing_policy) throws -> bool
{
  let const should_set_exact_mode = application == mode_application::Exact;
  let const should_ignore_existing =
      existing_policy == existing_directory_policy::Accept;
  if (os::make_directory(path, mode)) {
    if (should_set_exact_mode && !os::set_file_mode(path, mode)) {
      return false;
    }
    return true;
  }

  if (should_ignore_existing && Path{path}.is_directory()) {
    return true;
  }
  return false;
}

fn Mkdir::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const should_make_parents = FLAG_MKDIR_PARENTS.is_enabled();

  u32 named_mode = 0777;
  if (FLAG_MKDIR_MODE.is_set()) {
    let const parsed = utils::parse_file_mode(FLAG_MKDIR_MODE.value(), 0777,
                                              os::get_file_creation_mask(),
                                              utils::file_kind_mode::Directory);
    if (!parsed.has_value()) {
      throw ErrorWithDetails{
          "invalid mode '" +
              String{cxt.scratch_allocator(), FLAG_MKDIR_MODE.value()}
              + "'",
          "A mode is octal such as 0755 or symbolic such as u=rwx,go=rx"
      };
    }

    named_mode = *parsed;
  }

  constexpr u32 owner_write_and_search_bits = 0300;
  let const intermediate_mode =
      (0777u & ~os::get_file_creation_mask()) | owner_write_and_search_bits;

  i32 status = 0;
  for (let const &operand : operands) {
    if (should_make_parents) {
      let const text = operand.view();

      if (text.is_empty()) {
        if (!make_one(text, intermediate_mode, mode_application::RespectUmask,
                      existing_directory_policy::Accept))
        {
          report_soft_koshkit_util_error(
              ec, cxt, args[0].view(),
              "cannot create directory '" + operand +
                  "': " + os::last_system_error_message());
          status = 1;
        }
        continue;
      }

      for (usize i = 1; i <= text.length; i++) {
        if (i < text.length && text[i] != '/') {
          continue;
        }
        let const prefix = text.substring_of_length(0, i);
        if (prefix.is_empty()) continue;
        let const is_named_directory = (i == text.length);
        let const mode = is_named_directory ? named_mode : intermediate_mode;
        let const should_set_exact_mode =
            !is_named_directory || FLAG_MKDIR_MODE.is_set();
        if (!make_one(prefix, mode,
                      should_set_exact_mode ? mode_application::Exact
                                            : mode_application::RespectUmask,
                      existing_directory_policy::Accept))
        {
          report_soft_koshkit_util_error(
              ec, cxt, args[0].view(),
              "cannot create directory '" +
                  String{cxt.scratch_allocator(), prefix} +
                  "': " + os::last_system_error_message());
          status = 1;
          break;
        }
      }
    } else if (!make_one(operand.view(), named_mode,
                         FLAG_MKDIR_MODE.is_set()
                             ? mode_application::Exact
                             : mode_application::RespectUmask,
                         existing_directory_policy::Reject))
    {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "cannot create directory '" + operand +
              "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} /* namespace koshka::koshkit */
