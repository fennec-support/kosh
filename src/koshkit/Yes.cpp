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

KOSHKIT_UTIL_DECL(
    "[string ...]",
    "The yes utility writes the given string on its own line repeatedly.");

REGISTER_KOSHKIT_UTIL_FLAGS(Yes);

namespace koshka::koshkit {

fn Yes::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args,
                const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
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

  let const out_fd = ec.out_fd.value_or(KOSH_STDOUT);
  loop
  {
    if (os::INTERRUPT_REQUESTED) return 130;

    usize written_count = 0;
    while (written_count < line.count()) {
      let const chunk = os::write_fd(out_fd, line.view().data + written_count,
                                     line.count() - written_count);
      if (!chunk.has_value())
        return errno == EPIPE ? KOSH_BROKEN_PIPE_EXIT_STATUS : 0;
      if (*chunk == 0) return 0;
      written_count += *chunk;
    }
  }
}

} /* namespace koshka::koshkit */
