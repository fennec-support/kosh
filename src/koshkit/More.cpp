/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the more utility. It resolves tags and the initial
 * pager command, then pages the input one screen at a time on a terminal or
 * copies it when the output is not a terminal.
 */

#include "../CLI.hpp"
#include "../CLILive.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL("[-ceisu] [-n lines] [-p command] [-t tag] [file ...]",
                  "Display text one screen at a time.");

FLAG(MORE_CLEAR, Bool, 'c', "clear", "Clear each screen before displaying it.");
FLAG(MORE_EXIT, Bool, 'e', "exit", "Exit at end of input.");
FLAG(MORE_CASE, Bool, 'i', "ignore-case", "Ignore case in searches.");
FLAG(MORE_SQUEEZE, Bool, 's', "squeeze", "Collapse repeated blank lines.");
FLAG(MORE_PLAIN, Bool, 'u', "plain", "Suppress terminal underline handling.");
FLAG(MORE_LINES, String, 'n', "lines", "Use this many lines per screen.");
FLAG(MORE_COMMAND, String, 'p', "command", "Run this initial pager command.");
FLAG(MORE_TAG, String, 't', "tag", "Open the file containing this tag.");

REGISTER_KOSHKIT_UTIL_FLAGS(More);

namespace koshka::koshkit {

fn
More::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (FLAG_MORE_COMMAND.is_set() && FLAG_MORE_TAG.is_set()) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  pager_options options{};

  if (FLAG_MORE_LINES.is_set()) {
    let const parsed = parse_strict_count(FLAG_MORE_LINES.value());

    if (parsed.is_error() || parsed.value() == 0 || parsed.value() > SIZE_MAX) {
      report_soft_koshkit_error(ec, cxt, FLAG_MORE_LINES.value_location(),
                                "Invalid line count",
                                "Use a positive decimal integer");

      return 1;
    }

    options.page_row_count = static_cast<usize>(parsed.value());
  }

  let   tagged_source      = String{cxt.scratch_allocator()};
  usize tagged_line_number = 1;

  if (FLAG_MORE_TAG.is_set()) {
    if (!operands.is_empty())
      return report_usage_error(ec, cxt, args[0].view());

    let const tags = Path{"tags"}.read_entire_file();

    if (!tags.has_value()) {
      report_soft_koshkit_error(ec, cxt, "Cannot read tags");

      return 1;
    }

    /* e.g. tags = "main\tsrc/Main.cpp\t12\n", FLAG_MORE_TAG = main
            line = "main\tsrc/Main.cpp\t12", first_tab = 4 matches the tag
            after_name = "src/Main.cpp\t12", second_tab = 12
            tagged_source = "src/Main.cpp", tagged_line_number = 12 */
    usize position = 0;

    while (position < tags->length()) {
      let const remaining = tags->view().substring(position);
      let const line_length =
          remaining.find_character('\n').value_or(remaining.length);
      let const line      = remaining.substring_of_length(0, line_length);
      let const first_tab = line.find_character('\t');

      if (first_tab.has_value() &&
          line.substring_of_length(0, *first_tab) == FLAG_MORE_TAG.value())
      {
        let const after_name = line.substring(*first_tab + 1);
        let const second_tab = after_name.find_character('\t');

        if (second_tab.has_value()) {
          tagged_source =
              String{cxt.scratch_allocator(),
                     after_name.substring_of_length(0, *second_tab)};
          let const parsed =
              utils::parse_decimal_u64(after_name.substring(*second_tab + 1));

          if (!parsed.is_error() && parsed.value() != 0 &&
              parsed.value() <= SIZE_MAX)
          {
            tagged_line_number = static_cast<usize>(parsed.value());
          }

          break;
        }
      }

      position += line_length + (line_length < remaining.length ? 1 : 0);
    }

    if (tagged_source.is_empty()) {
      report_soft_koshkit_error(
          ec, cxt, "Tag '" + FLAG_MORE_TAG.value() + "' was not found");

      return 1;
    }
  }

  /* at this point: when FLAG_MORE_TAG is set, operands is empty, and
     tagged_source holds the source file of the tag. */
  let sources = source_list_from_operands(operands, cxt.scratch_allocator());

  if (FLAG_MORE_TAG.is_set()) {
    sources.clear();
    sources.push(tagged_source.view());
  } else if (operands.is_empty() &&
             os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT)) &&
             os::is_fd_a_tty(ec.in_fd.value_or(KOSH_STDIN)))
  {
    return report_usage_error(ec, cxt, args[0].view());
  }

  if (FLAG_MORE_COMMAND.is_set()) {
    let command = FLAG_MORE_COMMAND.value();

    if (!command.is_empty() && command[0] == '/') {
      command = command.substring(1);
    }

    options.initial_pattern = command;
  }

  options.first_line_number   = tagged_line_number;
  options.should_show_headers = sources.count() > 1;
  options.should_copy_headers = sources.count() > 1;
  options.should_ignore_case  = FLAG_MORE_CASE.is_enabled();
  options.should_squeeze      = FLAG_MORE_SQUEEZE.is_enabled();
  options.should_clear        = FLAG_MORE_CLEAR.is_enabled();
  options.should_exit_at_end  = FLAG_MORE_EXIT.is_enabled();

  return run_pager(ec, cxt, sources, options);
}

} /* namespace koshka::koshkit */
