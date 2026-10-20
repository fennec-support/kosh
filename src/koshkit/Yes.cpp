/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the yes utility. It builds one repeated output record
 * and writes buffered copies until output closes or interruption is requested.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("[string ...]",
                  "Write the given string on its own line repeatedly.");

REGISTER_KOSHKIT_UTIL_FLAGS(Yes);

namespace koshka::koshkit {

fn
Yes::execute(const ExecContext               &ec,
             EvalContext                     &cxt,
             const ArrayList<String>         &args,
             const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  String line{cxt.scratch_allocator()};

  if (operands.is_empty())
    line += "y";
  else
    for (usize i = 0; i < operands.count(); i++) {
      if (i > 0) line += ' ';
      line += operands[i].view();
    }

  line += '\n';

  static constexpr usize OUTPUT_BUFFER_BYTE_COUNT = 64 * 1024;

  let output = String{cxt.scratch_allocator()};

  output.reserve(OUTPUT_BUFFER_BYTE_COUNT);

  do {
    output += line;
  } while (output.count() + line.count() <= OUTPUT_BUFFER_BYTE_COUNT);

  let const out_fd = ec.out_fd.value_or(KOSH_STDOUT);

  loop {
    if (os::INTERRUPT_REQUESTED) return 130;

    if (!os::write_all(out_fd, output.data(), output.count())) {
      if (errno == EPIPE) return KOSH_BROKEN_PIPE_EXIT_STATUS;

      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "write error: " +
                                         os::last_system_error_message());

      return 1;
    }
  }
}

} /* namespace koshka::koshkit */
