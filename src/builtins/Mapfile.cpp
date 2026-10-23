/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file loads delimited records from a selected descriptor into an indexed
 * array for the mapfile builtin. It owns record limits, skipped records,
 * starting indexes, delimiter trimming, and callbacks before assignment.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL(
    "[-t] [-n count] [-s count] [-O origin] [-d delimiter] [-u fd]",
    "[-C callback] [-c quantum] [array]");

HELP_DESCRIPTION_DECL(
    "Lines from standard input can be stored in an indexed array.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(MAPFILE_TRIM, Bool, 't', "", "Strip the trailing newline from each line.");
FLAG(MAPFILE_COUNT,
     String,
     'n',
     "",
     "Read at most count lines, or all of them when count is zero.");
FLAG(MAPFILE_SKIP, String, 's', "", "Discard count lines before reading.");
FLAG(MAPFILE_ORIGIN, String, 'O', "", "Begin storing at the array index.");
FLAG(MAPFILE_DELIMITER, String, 'd', "", "Use the first byte as delimiter.");
FLAG(MAPFILE_FD, String, 'u', "", "Read from the file descriptor.");
FLAG(MAPFILE_CALLBACK, String, 'C', "", "Run the callback while reading.");
FLAG(MAPFILE_QUANTUM, String, 'c', "", "Run the callback every quantum lines.");

REGISTER_BUILTIN_FLAGS(Mapfile);

namespace koshka {

fn
Mapfile::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const do_parse_count = [&](const FlagString &flag, StringView label,
                                 StringView note,
                                 i64       &value) throws -> bool {
    if (!flag.is_set()) return true;

    let const parsed = flag.value().to<i64>();

    if (!parsed.is_error() && parsed.value() >= 0) {
      value = parsed.value();

      return true;
    }

    report_soft_builtin_error(ec, cxt, flag.value_location(),
                              flag.value() + label, note);

    return false;
  };

  i64 max_lines        = 0;
  i64 skip_count       = 0;
  i64 origin           = 0;
  i64 callback_quantum = 5000;

  if (!do_parse_count(FLAG_MAPFILE_COUNT, ": invalid line count",
                      "The -n count must be a whole number", max_lines) ||
      !do_parse_count(FLAG_MAPFILE_SKIP, ": invalid line count",
                      "The -s count must be a whole number", skip_count) ||
      !do_parse_count(FLAG_MAPFILE_ORIGIN, ": invalid array origin",
                      "The -O origin must be a whole number", origin) ||
      !do_parse_count(FLAG_MAPFILE_QUANTUM, ": invalid callback quantum",
                      "The -c quantum must be a positive number",
                      callback_quantum) ||
      callback_quantum == 0)
  {
    if (callback_quantum == 0)
      report_soft_builtin_error(ec, cxt, FLAG_MAPFILE_QUANTUM.value_location(),
                                "0: invalid callback quantum");

    return 1;
  }

  /* at this point: callback_quantum is at least 1, so the modulo in the
     callback check is defined, and max_lines, skip_count, and origin are
     not negative. */
  let read_fd = ec.in_fd.value_or(KOSH_STDIN);

  if (FLAG_MAPFILE_FD.is_set()) {
    let const parsed_fd = FLAG_MAPFILE_FD.value().to<i64>();

    if (parsed_fd.is_error() || parsed_fd.value() < 0 ||
        parsed_fd.value() > INT32_MAX)
    {
      report_soft_builtin_error(ec, cxt, FLAG_MAPFILE_FD.value_location(),
                                FLAG_MAPFILE_FD.value() +
                                    ": invalid file descriptor");

      return 1;
    }

    read_fd = os::descriptor_from_fd_number(parsed_fd.value());
  }

  let const array_name =
      args.count() > 1 ? args[1].view() : StringView{"MAPFILE"};

  if (!name_is_valid_identifier(array_name)) {
    report_invalid_identifier(ec, cxt, ec.arg_location_at(1), array_name);

    return 1;
  }

  let const should_strip_newline = FLAG_MAPFILE_TRIM.is_enabled();
  let const has_origin           = FLAG_MAPFILE_ORIGIN.is_set();
  let const has_callback         = FLAG_MAPFILE_CALLBACK.is_set();
  char      delimiter            = '\n';

  if (FLAG_MAPFILE_DELIMITER.is_set()) {
    let const value = FLAG_MAPFILE_DELIMITER.value();

    delimiter = value.is_empty() ? '\0' : value[0];
  }

  LOG(Debug, "mapfile reading lines into array '%.*s'",
      static_cast<int>(array_name.length), array_name.data);

  let const do_report_read_failure = [&]() throws -> i32 {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              StringView{"Unable to read mapfile input: "} +
                                  os::last_system_error_message());

    return 1;
  };

  for (i64 skipped = 0; skipped < skip_count; skipped++) {
    let const skipped_line = utils::read_line_from_fd(read_fd, delimiter);

    if (skipped_line.did_read_fail) {
      return do_report_read_failure();
    }

    if (!skipped_line.line.has_value()) break;
  }

  let lines = ArrayList<String>{heap_allocator()};

  if (has_callback && !has_origin) {
    cxt.set_indexed_array(array_name, ArrayList<String>{heap_allocator()});
  }

  let const was_terminal_exec_allowed =
      cxt.execution_store().is_terminal_exec_allowed();

  cxt.execution_store().is_terminal_exec_allowed() = false;
  defer {
    cxt.execution_store().is_terminal_exec_allowed() =
        was_terminal_exec_allowed;
  };

  /* without a line limit or a callback, the input is read to its end in
     large reads, since nothing else can take bytes after the last line. */
  let       whole_input      = Maybe<String>{None};
  usize     whole_position   = 0;
  let const should_read_rest = max_lines == 0 && !has_callback;

  if (should_read_rest) {
    whole_input = os::read_fd_to_string(read_fd, heap_allocator());

    if (!whole_input.has_value()) {
      return do_report_read_failure();
    }
  }

  let const do_next_line = [&]() throws -> utils::read_line_result {
    if (!should_read_rest) return utils::read_line_from_fd(read_fd, delimiter);

    let const rest = whole_input->view().substring(whole_position);

    if (rest.is_empty()) return utils::read_line_result{};

    let const end    = rest.find_character(delimiter);
    let const length = end.has_value() ? *end : rest.length;

    whole_position += length + (end.has_value() ? 1 : 0);

    return utils::read_line_result{
        String{heap_allocator(), rest.substring_of_length(0, length)},
        end.has_value()
    };
  };

  usize read_count = 0;

  loop {
    if (max_lines > 0 && static_cast<i64>(read_count) >= max_lines) {
      break;
    }

    let read = do_next_line();

    if (read.did_read_fail) {
      return do_report_read_failure();
    }

    if (!read.line.has_value()) break;

    let element = read.line.take();

    /* a NUL delimiter is never kept, since a shell value holds no NUL. */
    if (!should_strip_newline && read.was_delimiter_terminated &&
        delimiter != '\0')
    {
      element.push(delimiter);
    }

    let const array_index = origin + static_cast<i64>(read_count);
    char      index_text[24];
    let const subscript =
        utils::int_to_text_into(array_index, index_text, sizeof(index_text));

    if (has_callback &&
        (read_count + 1) % static_cast<usize>(callback_quantum) == 0)
    {
      let callback = String{heap_allocator(), FLAG_MAPFILE_CALLBACK.value()};

      callback.push(' ');
      append_shell_quoted_arg(callback, subscript, true);
      callback.push(' ');
      append_shell_quoted_arg(callback, element.view(), true);
      unused(cxt.run_source(callback.view(), "mapfile callback",
                            FLAG_MAPFILE_CALLBACK.value_location(), None,
                            nullptr, nullptr, return_handling::Propagate));
    }

    if (has_callback) {
      cxt.assign_array_element(array_name, subscript, element.view(),
                               assignment_update_mode::Replace);
    } else {
      lines.push(steal(element));
    }

    read_count++;
  }

  LOG(Debug, "mapfile stored %zu lines", read_count);

  if (has_callback) return 0;

  if (has_origin) {
    for (usize element_index = 0; element_index < lines.count();
         element_index++)
    {
      char      index_text[24];
      let const subscript =
          utils::int_to_text_into(origin + static_cast<i64>(element_index),
                                  index_text, sizeof(index_text));

      cxt.assign_array_element(array_name, subscript,
                               lines[element_index].view(),
                               assignment_update_mode::Replace);
    }
  } else {
    cxt.set_indexed_array(array_name, steal(lines));
  }

  return 0;
}

} /* namespace koshka */
