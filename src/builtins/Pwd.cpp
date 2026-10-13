/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the pwd builtin, which prints the absolute path of the
 * current working directory.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");
HELP_DESCRIPTION_DECL(
    "This builtin prints the absolute path of the current working directory.");

FLAG(PWD_LOGICAL,
     Bool,
     'L',
     "",
     "Print the logical directory, keeping the symlinks the path was reached "
     "through. This is the default.");
FLAG(PWD_PHYSICAL,
     Bool,
     'P',
     "",
     "Print the physical directory, with every symlink resolved.");
FLAG(PWD_WINDOWS,
     Bool,
     'W',
     "",
     "Print the physical directory in the Windows form with forward slashes. "
     "On other systems, it is the same as -P.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Pwd);

namespace koshka {

fn
Pwd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let       output                  = String{cxt.scratch_allocator()};
  let const should_use_windows_form = FLAG_PWD_WINDOWS.is_enabled();
  let const should_use_physical =
      FLAG_PWD_PHYSICAL.is_enabled() || should_use_windows_form;
  let const directory = should_use_physical ? Path::current_directory()
                                            : logical_working_directory(cxt);

  if (should_use_windows_form && os::IS_BACKSLASH_A_SEPARATOR) {
    let const text = directory.text().view();

    for (usize position = 0; position < text.length; position++) {
      output.push(text[position] == '\\' ? '/' : text[position]);
    }
  } else {
    output.append(directory.text());
  }

  output += '\n';
  ec.print_to_stdout(output);

  return 0;
}

} /* namespace koshka */
