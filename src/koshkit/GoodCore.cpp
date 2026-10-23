/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements goodcore. It captures a running process or accepts
 * an existing core, collects its executable and mapped libraries, records host
 * metadata, and creates a compressed debugging archive.
 */

#include "../BakedScripts.hpp"
#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../ProgramResolver.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL(
    "[-q] [-b executable] [-o archive] [--no-compress] "
    "(-p pid | core)",
    "The goodcore utility can capture or package a core dump with "
    "its executable, mapped libraries, and host metadata.");

FLAG(GOODCORE_PID, String, 'p', "pid", "Capture this running process.");
FLAG(GOODCORE_BINARY,
     String,
     'b',
     "binary",
     "Use this executable for an existing core dump.");
FLAG(GOODCORE_OUTPUT, String, 'o', "output", "Write the archive to this path.");
FLAG(GOODCORE_QUIET, Bool, 'q', "quiet", "Print only errors.");
FLAG(GOODCORE_NO_COMPRESS,
     Bool,
     '\0',
     "no-compress",
     "Create an uncompressed tar archive.");

REGISTER_KOSHKIT_UTIL_FLAGS(GoodCore);

namespace koshka::koshkit {

namespace {

enum class goodcore_progress_mode : u8
{
  Hidden,
  Visible,
};

fn
print_progress(const ExecContext     &ec,
               StringView             message,
               goodcore_progress_mode mode) throws -> void
{
  if (mode == goodcore_progress_mode::Hidden) return;

  let const is_terminal  = colors::stderr_is_a_terminal();
  let const should_color = colors::stderr_wants_color();
  let       output       = String{heap_allocator()};

  if (is_terminal) output += "\r\x1b[2K";
  append_report_text(output,
                     "goodcore: ", colors::soft(colors::ansi::BOLD_CYAN),
                     should_color);
  append_report_text(output, message, colors::ansi::BOLD_WHITE, should_color);
  if (!is_terminal) output += "\n";
  ec.print_to_stderr(output);
}

fn
clear_progress(const ExecContext     &ec,
               goodcore_progress_mode mode) throws -> void
{
  if (mode == goodcore_progress_mode::Hidden) return;
  if (!colors::stderr_is_a_terminal()) return;

  ec.print_to_stderr("\r\x1b[2K");
}

fn
run_tool(const Path         &tool,
         ArrayList<String>   arguments,
         os::measured_output output) throws -> bool
{
  let command = ArrayList<String>{heap_allocator()};

  command.reserve(arguments.count() + 1);
  command.push(tool.text().clone());

  for (let &argument : arguments)
    command.push(steal(argument));

  let const result = os::run_measured(command, {}, output);

  return result.has_value() && result->exit_status == 0;
}

fn
write_text_file(StringView path, StringView text) throws -> bool
{
  let const descriptor =
      os::open_file_descriptor(path, os::file_open_mode::Truncate);

  if (!descriptor.has_value()) return false;
  defer { unused(os::close_fd(*descriptor)); };

  return os::write_all(*descriptor, text.data, text.length);
}

pure fn
stripped_root(StringView path) wontthrow -> StringView
{
  usize start_position = 0;

  while (start_position < path.length &&
         (path[start_position] == '/' || path[start_position] == '\\'))
  {
    start_position++;
  }

  return path.substring_of_length(start_position, path.length - start_position);
}

fn
is_followed_regular_file(StringView path) wontthrow -> bool
{
  os::file_status status{};

  if (!os::stat_path_following(path, status)) return false;

  return os::file_type_letter(status.mode) == '-';
}

fn
copy_into_root(const Path &stage, StringView source) throws -> bool
{
  if (!is_followed_regular_file(source)) return false;

  let const relative = stripped_root(source);

  if (relative.is_empty()) return false;

  let destination = stage.clone();

  destination.append("root");
  destination.append(relative);
  if (!make_directories(destination.parent(), 0700)) return false;

  /* at this point: source is a regular file, relative is non-empty, and
     the parent of destination exists under stage/root. */
  return copy_file_contents(source, destination.view(),
                            copy_force_mode::Normal) ==
         copy_file_result::Success;
}

fn
append_unique_path(ArrayList<String> &paths,
                   StringView         path,
                   Allocator          allocator) throws -> void
{
  if (path.is_empty() || path[0] != '/') {
    return;
  }

  let const normalized = Path{path, allocator}.normalized();

  for (let const &existing : paths) {
    if (existing.view() == normalized.view()) return;
  }

  /* at this point: normalized is absolute, has no parent references, and is
     not yet in paths. */
  if (is_followed_regular_file(normalized.view()))
    paths.push_managed(normalized.view());
}

fn
collect_paths_from_output(StringView         output,
                          ArrayList<String> &paths,
                          Allocator          allocator) throws -> void
{
  for (let view : utils::split_lines(output, allocator,
                                     utils::line_terminator_mode::Discard))
  {
    view = view.trim_blanks();

    let const start = view.find_character('/');

    if (!start.has_value()) continue;

    /* e.g. view = "Yes (*) /lib64/ld.so", start = 8
            end_position stops at view.length = 20, since none of the
            characters in "/lib64/ld.so" is a blank, tab, ')', or '('.
            path = "/lib64/ld.so" */
    usize end_position = *start;

    while (end_position < view.length && view[end_position] != ' ' &&
           view[end_position] != '\t' && view[end_position] != ')' &&
           view[end_position] != '(')
    {
      end_position++;
    }

    append_unique_path(paths,
                       view.substring_of_length(*start, end_position - *start),
                       allocator);
  }
}

fn
infer_binary(EvalContext &cxt,
             StringView   core,
             Allocator    allocator) throws -> Maybe<String>
{
  let const file = resolve_util_program(cxt, "file");

  if (!file.has_value()) return None;

  let arguments = ArrayList<String>{heap_allocator()};

  arguments.push_managed(core);

  let const output =
      capture_util_program_output(*file, steal(arguments), 30'000'000'000);

  if (!output.has_value()) return None;

  constexpr StringView markers[] = {"execfn: '", "from '"};

  for (let const marker : markers) {
    let const marker_position = output->view().find_substring(marker);

    if (!marker_position.has_value()) continue;

    let const start_position = *marker_position + marker.length;
    let const remainder      = output->view().substring_of_length(
        start_position, output->length() - start_position);
    let const end_position = remainder.find_character('\'');

    if (!end_position.has_value()) continue;

    let const candidate = remainder.substring_of_length(0, *end_position);

    if (is_followed_regular_file(candidate))
      return Path{candidate, allocator}.to_absolute().text().clone();

    let const matches =
        cxt.program_resolver().search(Path{candidate, allocator}.filename(),
                                      ProgramResolver::SearchMode::First,
                                      ProgramResolver::Requirement::Runnable,
                                      ProgramResolver::CachePolicy::Bypass);

    if (!matches.is_empty()) return matches[0].text().clone();
  }

  return None;
}

fn
collect_core_libraries(EvalContext       &cxt,
                       StringView         core,
                       StringView         binary,
                       ArrayList<String> &paths,
                       Allocator          allocator) throws -> void
{
  if (let const gdb = resolve_util_program(cxt, "gdb")) {
    let arguments = ArrayList<String>{heap_allocator()};

    arguments.push_managed("--batch");
    arguments.push_managed("--nx");
    arguments.push_managed("--eval-command=info sharedlibrary");
    arguments.push_managed("-c");
    arguments.push_managed(core);
    arguments.push_managed(binary);
    if (let const output =
            capture_util_program_output(*gdb, steal(arguments), 30'000'000'000))
      collect_paths_from_output(output->view(), paths, allocator);
  }

  let const platform_tools = os::goodcore_tools();

  if (let const dependency_tool =
          resolve_util_program(cxt, platform_tools.dependency_program))
  {
    let arguments = ArrayList<String>{heap_allocator()};

    if (platform_tools.is_library_flag_dependency) arguments.push_managed("-L");
    arguments.push_managed(binary);
    if (let const output = capture_util_program_output(
            *dependency_tool, steal(arguments), 30'000'000'000))
      collect_paths_from_output(output->view(), paths, allocator);
  }
}

fn
remove_stage(const Path &stage, Allocator allocator) throws -> void
{
  unused(remove_path(stage.view(), allocator, removal_mode::Recursive));
}

} /* namespace */

fn
GoodCore::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const allocator     = cxt.scratch_allocator();
  let const has_pid       = FLAG_GOODCORE_PID.is_set();
  let const progress_mode = FLAG_GOODCORE_QUIET.is_enabled()
                                ? goodcore_progress_mode::Hidden
                                : goodcore_progress_mode::Visible;

  defer { clear_progress(ec, progress_mode); };

  let const do_report_failure = [&](StringView message,
                                    StringView note) throws {
    clear_progress(ec, progress_mode);
    report_soft_koshkit_error(ec, cxt, message, note);
  };

  if (!has_pid && operands.is_empty()) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  if (has_pid && !operands.is_empty()) {
    let conflict_location = FLAG_GOODCORE_PID.value_location();

    if (operand_locations[0].position > conflict_location.position)
      conflict_location = operand_locations[0];
    KOSHKIT_REPORT_ERROR_AT(conflict_location, "Conflicting input",
                            "Pass either one running pid or one core file");

    return 2;
  }

  if (operands.count() > 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                            "Extra operand '" + operands[1] + "'",
                            "Pass exactly one core file");

    return 2;
  }

  if (has_pid && FLAG_GOODCORE_BINARY.is_set()) {
    let conflict_location = FLAG_GOODCORE_PID.value_location();

    if (FLAG_GOODCORE_BINARY.value_location().position >
        conflict_location.position)
    {
      conflict_location = FLAG_GOODCORE_BINARY.value_location();
    }

    KOSHKIT_REPORT_ERROR_AT(
        conflict_location, "Conflicting flags",
        "--binary applies only when packaging an existing core file");

    return 2;
  }

  i64 process_id = 0;

  if (has_pid) {
    let const parsed = utils::parse_integer_in_base(FLAG_GOODCORE_PID.value(),
                                                    nullptr, int_base::decimal);

    if (parsed.is_error() || parsed.value() <= 0) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_GOODCORE_PID.value_location(),
                              "Invalid process id",
                              "The process id must be a positive integer");

      return 1;
    }

    process_id = parsed.value();
  }

  let binary = Maybe<String>{};
  let paths  = ArrayList<String>{allocator};

  if (has_pid) {
    for (let const &file : os::list_process_open_files(
             process_id, allocator, os::process_open_file_detail::PathsOnly))
    {
      if (file.use == os::process_file_use::Executable) {
        binary = file.path.clone();
      }

      if (file.use == os::process_file_use::Executable ||
          file.use == os::process_file_use::Mapped)
      {
        append_unique_path(paths, file.path.view(), allocator);
      }
    }
  } else if (FLAG_GOODCORE_BINARY.is_set()) {
    binary = Path{FLAG_GOODCORE_BINARY.value(), allocator}
                 .to_absolute()
                 .text()
                 .clone();
  } else if (!is_followed_regular_file(operands[0].view())) {
    do_report_failure("Core file not found",
                      "The operand must refer to a regular file");

    return 1;
  } else {
    binary = infer_binary(cxt, operands[0].view(), allocator);
  }

  if (!binary.has_value() || !is_followed_regular_file(binary->view())) {
    let const error_location = FLAG_GOODCORE_BINARY.is_set()
                                   ? FLAG_GOODCORE_BINARY.value_location()
                               : has_pid ? FLAG_GOODCORE_PID.value_location()
                                         : operand_locations[0];

    report_soft_koshkit_error(ec, cxt, error_location, "Executable not found",
                              "Pass its path with --binary");

    return 1;
  }

  /* at this point: binary holds a path to a regular file, and has_pid selects
     between capturing a process and copying a core file. */
  let const stage_directory =
      os::make_temp_directory(Path::temp_directory(), "goodcore");

  if (!stage_directory.has_value()) {
    do_report_failure("Cannot create staging directory",
                      os::last_system_error_message());

    return 1;
  }

  let const stage = *stage_directory;

  defer { remove_stage(stage, allocator); };

  let dump_directory = stage.clone();

  dump_directory.append("dump");
  if (!os::make_directory(dump_directory.view(), 0700)) {
    do_report_failure("Cannot create dump directory",
                      os::last_system_error_message());

    return 1;
  }

  let core = dump_directory.clone();

  core.append("core");
  if (has_pid) {
    print_progress(ec, "capturing process core", progress_mode);

    let const platform_tools = os::goodcore_tools();
    let       debugger       = Maybe<Path>{};

    if (!platform_tools.debugger_program.is_empty())
      debugger = resolve_util_program(cxt, platform_tools.debugger_program);

    let capture_arguments = ArrayList<String>{heap_allocator()};

    switch (platform_tools.capture_mode) {
    case os::goodcore_capture_mode::Lldb:
      capture_arguments.push_managed("--batch");
      capture_arguments.push_managed("-p");
      capture_arguments.push(String::from(process_id, heap_allocator()));
      capture_arguments.push_managed("-o");
      capture_arguments.push_managed(
          StringView{"process save-core " + core.text()});
      capture_arguments.push_managed("-o");
      capture_arguments.push_managed("process detach");
      break;

    case os::goodcore_capture_mode::Gcore:
      capture_arguments.push_managed("-o");
      capture_arguments.push(core.text().clone());
      capture_arguments.push(String::from(process_id, heap_allocator()));
      break;

    case os::goodcore_capture_mode::Unsupported: break;
    }

    if (!debugger.has_value() || !run_tool(*debugger, steal(capture_arguments),
                                           os::measured_output::Inherit))
    {
      do_report_failure(
          "Capture failed",
          "Install the platform debugger and check process permissions");

      return 1;
    }

    if (platform_tools.capture_mode == os::goodcore_capture_mode::Gcore) {
      let const captured_core = Path{
          core.text() + "." + String::from(process_id, allocator), allocator};

      if (!os::rename_path(captured_core.view(), core.view())) {
        do_report_failure("Capture failed",
                          "The debugger produced no usable core file");

        return 1;
      }
    }
  } else {
    print_progress(ec, "copying existing core", progress_mode);

    let const source = Path{operands[0].view(), allocator}.to_absolute();

    if (!source.is_regular_file()) {
      do_report_failure("Core file not found",
                        "The operand must refer to a regular file");

      return 1;
    }

    if (copy_file_contents(source.view(), core.view(),
                           copy_force_mode::Normal) !=
        copy_file_result::Success)
    {
      do_report_failure("Cannot copy core file",
                        os::last_system_error_message());

      return 1;
    }
  }

  os::file_status core_status{};

  if (!os::stat_path_following(core.view(), core_status) ||
      core_status.size == 0)
  {
    do_report_failure("Capture failed", "The core file is missing or empty");

    return 1;
  }

  /* at this point: core exists and has a non-zero size. */
  print_progress(ec, "collecting executable and libraries", progress_mode);
  collect_core_libraries(cxt, core.view(), binary->view(), paths, allocator);
  print_progress(ec,
                 "collected " + String::from(paths.count(), allocator) +
                     " candidate files",
                 progress_mode);

  append_unique_path(paths, binary->view(), allocator);

  for (let const &path : paths) {
    print_progress(ec, "copying " + path.view(), progress_mode);
    if (!copy_into_root(stage, path.view())) {
      do_report_failure("Cannot copy required file", path.view());

      return 1;
    }
  }

  let metadata = String{allocator};

  metadata += "Core: ";
  metadata += "dump/core";
  metadata += "\nExecutable: ";
  metadata += binary->view();
  metadata += "\nCollected files: ";
  metadata += String::from(paths.count(), allocator).view();
  metadata += "\n";
  if (let const uname = resolve_util_program(cxt, "uname")) {
    let uname_arguments = ArrayList<String>{heap_allocator()};

    uname_arguments.push_managed("-a");
    if (let const output = capture_util_program_output(
            *uname, steal(uname_arguments), 30'000'000'000))
    {
      metadata += "Host: ";
      metadata += output->view();
      if (metadata[metadata.length() - 1] != '\n') {
        metadata += "\n";
      }
    }
  }

  metadata += "Debug: ./debug-gdb.sh or ./debug-lldb.sh from the extracted "
              "archive\n";

  let metadata_path = stage.clone();

  metadata_path.append("INFO.txt");
  if (!write_text_file(metadata_path.view(), metadata.view())) {
    do_report_failure("Cannot write metadata", os::last_system_error_message());

    return 1;
  }

  let quoted_executable = String{allocator};

  utils::append_shell_quoted(quoted_executable, stripped_root(binary->view()),
                             true);

  let gdb_script = String{allocator, GOODCORE_DEBUG_SCRIPT_PRELUDE};

  gdb_script += GOODCORE_GDB_SCRIPT_HEAD;
  gdb_script += quoted_executable.view();
  gdb_script += GOODCORE_GDB_SCRIPT_TAIL;

  let lldb_script = String{allocator, GOODCORE_DEBUG_SCRIPT_PRELUDE};

  lldb_script += GOODCORE_LLDB_SCRIPT_HEAD;
  lldb_script += quoted_executable.view();
  lldb_script += GOODCORE_LLDB_SCRIPT_TAIL;

  let const do_write_script =
      [&](StringView script_name,
          StringView script_text) throws -> bool {
    let script_path = stage.clone();

    script_path.append(script_name);
    LOG(Debug, "writing the debugger script '%s'", script_path.c_str());
    if (write_text_file(script_path.view(), script_text) &&
        os::set_file_mode(script_path.view(), 0755))
    {
      return true;
    }

    do_report_failure("Cannot write the debugger script '" +
                          script_path.text() + "'",
                      os::last_system_error_message());

    return false;
  };

  if (!do_write_script("debug-gdb.sh", gdb_script.view()) ||
      !do_write_script("debug-lldb.sh", lldb_script.view()))
  {
    return 1;
  }

  let const zstd = FLAG_GOODCORE_NO_COMPRESS.is_enabled()
                       ? Maybe<Path>{None}
                       : resolve_util_program(cxt, "zstd");
  let const output =
      FLAG_GOODCORE_OUTPUT.is_set()
          ? Path{FLAG_GOODCORE_OUTPUT.value(), allocator}.to_absolute()
          : Path{"goodcore-" + core.filename() +
                 (FLAG_GOODCORE_NO_COMPRESS.is_enabled() ? StringView{".tar"}
                  : zstd.has_value() ? StringView{".tar.zst"}
                                     : StringView{".tar.gz"})}
                .to_absolute();

  if (output.is_same_file_as(Path{binary->view(), allocator}) ||
      (!has_pid && output.is_same_file_as(Path{operands[0].view(), allocator})))
  {
    do_report_failure("Unsafe output path",
                      "The archive cannot replace an input file");

    return 1;
  }

  let const tar = resolve_util_program(cxt, "tar");

  if (!tar.has_value()) {
    do_report_failure("The tar program is unavailable",
                      "Install tar or place it on PATH");

    return 1;
  }

  /* the temporary archive shares the directory of output, so rename_path()
     stays on one filesystem. rename() fails across filesystems. */
  let const temporary_output =
      os::write_to_named_temp_file(output.parent(), ".goodcore", StringView{});

  if (!temporary_output.has_value()) {
    do_report_failure("Cannot create temporary archive",
                      os::last_system_error_message());

    return 1;
  }

  defer {
    unused(os::remove_file(temporary_output->text().view()));
  };

  let archive_arguments = ArrayList<String>{heap_allocator()};

  archive_arguments.push_managed("-cf");
  archive_arguments.push(temporary_output->text().clone());
  archive_arguments.push_managed("-C");
  archive_arguments.push(stage.text().clone());
  archive_arguments.push_managed(".");

  bool did_archive = false;

  if (FLAG_GOODCORE_NO_COMPRESS.is_enabled()) {
    print_progress(ec, "creating tar archive", progress_mode);
    did_archive =
        run_tool(*tar, steal(archive_arguments), os::measured_output::Suppress);
  } else if (zstd.has_value()) {
    print_progress(ec, "creating tar archive and compressing with zstd",
                   progress_mode);

    let const temporary_tar = os::write_to_named_temp_file(
        output.parent(), ".goodcore-tar", StringView{});

    if (!temporary_tar.has_value()) {
      do_report_failure("Cannot create temporary archive",
                        os::last_system_error_message());

      return 1;
    }

    defer {
      unused(os::remove_file(temporary_tar->text().view()));
    };

    archive_arguments[1] = temporary_tar->text().clone();
    did_archive =
        run_tool(*tar, steal(archive_arguments), os::measured_output::Suppress);
    if (did_archive) {
      let compression_arguments = ArrayList<String>{heap_allocator()};

      compression_arguments.push_managed("-q");
      compression_arguments.push_managed("-f");
      compression_arguments.push(temporary_tar->text().clone());
      compression_arguments.push_managed("-o");
      compression_arguments.push(temporary_output->text().clone());
      did_archive = run_tool(*zstd, steal(compression_arguments),
                             os::measured_output::Suppress);
    }
  } else {
    print_progress(ec, "creating tar archive and compressing with gzip",
                   progress_mode);
    archive_arguments[0] = String{archive_arguments[0].allocator(), "-czf"};
    did_archive =
        run_tool(*tar, steal(archive_arguments), os::measured_output::Suppress);
  }

  if (!did_archive) {
    do_report_failure("Archive creation failed",
                      "The selected archiver returned a failure");

    return 1;
  }

  if (!os::rename_path(temporary_output->view(), output.view())) {
    do_report_failure("Cannot publish archive",
                      os::last_system_error_message());

    return 1;
  }

  if (!FLAG_GOODCORE_QUIET.is_enabled()) {
    let const should_color = koshkit_should_color();
    let       table        = ReportTable{allocator};
    let       cells        = ArrayList<report_table_cell_view>{allocator};

    cells.reserve(2);
    cells.push({"Archive", colors::soft(colors::ansi::BOLD_CYAN)});
    cells.push({output.view(), colors::soft(colors::ansi::GREEN)});
    table.add_row(cells);
    cells.clear();
    cells.push({"Executable", colors::soft(colors::ansi::BOLD_CYAN)});
    cells.push({binary->view(), colors::soft(colors::ansi::GREEN)});
    table.add_row(cells);
    cells.clear();

    let const file_count = String::from(paths.count(), allocator);

    cells.push({"Files", colors::soft(colors::ansi::BOLD_CYAN)});
    cells.push({file_count.view(), colors::soft(colors::ansi::GREEN)});
    table.add_row(cells);

    let result = String{allocator};

    result += table.to_string(should_color, "").view();
    clear_progress(ec, progress_mode);
    ec.print_to_stdout(result);
  }

  return 0;
}

} /* namespace koshka::koshkit */
