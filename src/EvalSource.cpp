/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file parses and evaluates source text, sourced files, heredoc bodies,
 * and scripts delegated to compatibility shells. It owns source isolation,
 * mood initialization, retained syntax trees, path resolution, and fallback
 * execution. The split confines recursive source lifetimes and compatibility
 * delegation outside ordinary evaluator operations.
 */

#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"

namespace koshka {

static constexpr usize MAX_MIMICRY_DEPTH = 16;

enum class mimicked_error_status_mode : u8
{
  Default,
  Posix,
};

enum class script_exit_context : u8
{
  Shell,
  Subshell,
};

static fn mimicked_error_is_interrupt(const std::exception_ptr &error) throws
    -> bool
{
  if (error == nullptr) return false;

  try {
    std::rethrow_exception(error);
  } catch (const InterruptErrorWithLocation &) {
    return true;
  } catch (...) {
    return false;
  }
}

static fn mimicked_error_status(const std::exception_ptr &error,
                                mimicked_error_status_mode mode) throws -> i32
{
  ASSERT(error != nullptr);

  try {
    std::rethrow_exception(error);
  } catch (const ErrorBase &caught_error) {
    let const status = caught_error.command_status();
    return static_cast<i32>(status == 1 &&
                                    mode == mimicked_error_status_mode::Posix &&
                                    caught_error.is_script_fatal()
                                ? 2
                                : status);
  } catch (...) {
    return 1;
  }
}

fn EvalContext::run_program_fallback(ExecContext &ec, mimic_mood mode,
                                     script_isolation isolation) throws -> i32
{
  struct saved_environment_variable
  {
    String name;
    String value;
  };

  let saved_environment =
      ArrayList<saved_environment_variable>{heap_allocator()};
  if (ec.should_use_empty_environment) {
    let const environment_names = os::environment_names();
    saved_environment.reserve(environment_names.count());
    for (let const &name : environment_names) {
      if (let value = os::get_environment_variable(name.view())) {
        saved_environment.push(
            saved_environment_variable{String{name.view()}, value.take()});
      }
      os::unset_environment_variable(name.view());
    }
  }
  defer
  {
    for (let const &variable : saved_environment)
      os::set_environment_variable(variable.name.view(), variable.value.view());
  };

  let fallback_context = EvalContext{startup_options{}};
  fallback_context.arena_store().set_parse_arena(arena_store().parse_arena());
  fallback_context.arena_store().set_function_arena(
      arena_store().function_arena());
  fallback_context.set_current_source(
      source_store().current_source(),
      String{heap_allocator(), source_store().current_origin().view()});
  fallback_context.source_store().set_mimicry_depth(
      source_store().mimicry_depth());
  fallback_context.source_retention().set_generation(
      source_retention().get_generation());
  fallback_context.execution_store().set_shell_executable_path(
      String{execution_store().get_shell_executable_path()});
  fallback_context.runtime_state().set_koshkit(runtime_state().koshkit());
  fallback_context.runtime_state().set_inheritable_analysis_state(
      runtime_state().get_inheritable_analysis_state());
  fallback_context.diagnostics_store().set_source_traces_enabled(
      diagnostics_store().source_traces_enabled());
  fallback_context.source_store().source_frames().reserve(
      source_store().source_frames().count());
  for (let const &frame : source_store().source_frames()) {
    fallback_context.source_store().source_frames().push(
        source_frame{String{frame.origin.view()}, frame.call_site,
                     frame.parent_source, frame.parent_source_generation,
                     String{frame.source_path.view()}, frame.kind});
    fallback_context.source_store().source_frames().back().function_call_depth =
        frame.function_call_depth;
    fallback_context.source_store().source_frames().back().was_printed =
        frame.was_printed;
    fallback_context.source_store().source_frames().back().should_defer_trace =
        frame.should_defer_trace;
    fallback_context.source_store().source_frames().back().has_deferred_trace =
        frame.has_deferred_trace;
    fallback_context.source_store()
        .source_frames()
        .back()
        .deferred_trace_location = frame.deferred_trace_location;
  }
  defer
  {
    let const shared_frame_count =
        source_store().source_frames().count() <
                fallback_context.source_store().source_frames().count()
            ? source_store().source_frames().count()
            : fallback_context.source_store().source_frames().count();
    for (usize frame_index = 0; frame_index < shared_frame_count; frame_index++)
    {
      source_store().source_frames()[frame_index].was_printed =
          source_store().source_frames()[frame_index].was_printed ||
          fallback_context.source_store()
              .source_frames()[frame_index]
              .was_printed;
      source_store().source_frames()[frame_index].has_deferred_trace =
          source_store().source_frames()[frame_index].has_deferred_trace ||
          fallback_context.source_store()
              .source_frames()[frame_index]
              .has_deferred_trace;
      if (fallback_context.source_store()
              .source_frames()[frame_index]
              .deferred_trace_location.has_value())
      {
        source_store().source_frames()[frame_index].deferred_trace_location =
            fallback_context.source_store()
                .source_frames()[frame_index]
                .deferred_trace_location;
      }
    }
  };
  return fallback_context.run_mimicked_script(ec, mode, isolation);
}

fn EvalContext::run_mimicked_script(ExecContext &ec, mimic_mood mode,
                                    script_isolation isolation) throws -> i32
{
  let const isolated = isolation == script_isolation::Isolated;
  defer { ec.close_fds(); };

  if (source_store().mimicry_depth() >= MAX_MIMICRY_DEPTH)
    throw ErrorWithLocation{ec.source_location(),
                            "Unable to mimic '" + ec.program() +
                                "' because the script nesting is too deep"};
  if (arena_store().parse_arena() == nullptr)
    throw ErrorWithLocation{ec.source_location(), "Unable to mimic '" +
                                                      ec.program() +
                                                      "' outside of a parse"};
  let const ast_mark = arena_store().parse_arena()->mark();
  defer { arena_store().parse_arena()->release(ast_mark); };

  let contents = ec.program_path().read_entire_file();
  if (!contents.has_value())
    throw ErrorWithLocation{ec.source_location(),
                            "Unable to mimic '" + ec.program() +
                                "' because the script could not be read"};

  const usize binary_scan_limit = 128;
  let const head = contents->view();
  let const scan_length =
      head.length < binary_scan_limit ? head.length : binary_scan_limit;
  let const sample = head.substring_of_length(0, scan_length);
  let const first_line_break = sample.find_character('\n');
  let const first_line_length = first_line_break.value_or(sample.length);
  if (sample.substring_of_length(0, first_line_length)
          .find_character('\0')
          .has_value())
  {
    LOG(Debug,
        "a NUL byte before the first line break marks '%s' as a binary file",
        ec.program().c_str());
    let file_command = String{"file "};
    append_shell_quoted_arg(file_command, ec.program().view());
    let details =
        String{"The file is binary and the system has refused execution. "};
    details += "Use `";
    details += file_command;
    details += "` to check the file type.";
    show_message(ErrorWithLocationAndDetails{
        ec.source_location(),
        "Cannot execute `" + ec.program_path().text() + "` as a shell script.",
        steal(details)}
                     .to_string(source_store().current_source_view(), this));
    return 126;
  }

  contents->normalize_crlf_line_endings();

  let const previous_runtime = runtime_state();
  let const was_restricted_shell = startup_store().is_restricted_shell();
  let const previous_script_run = source_store().is_script_run();
  let previous_shell_name = String{execution_store().get_shell_name()};
  let source_scope = capture_source_scope();
  let const previous_location = source_scope.get_location();
  let isolated_snapshot = Maybe<eval_state_snapshot>{};
  if (isolated) isolated_snapshot = snapshot_state();

  bool should_restore_isolated_state = isolated;
  let const do_restore_auxiliary_state = [&]() throws {
    source_scope.restore();
    previous_runtime.restore(*this);
    startup_store().set_restricted_shell(was_restricted_shell);
    source_store().set_script_run(previous_script_run);
    execution_store().set_shell_name(steal(previous_shell_name));
  };
  defer
  {
    if (should_restore_isolated_state) {
      try {
        restore_state(steal(*isolated_snapshot));
        do_restore_auxiliary_state();
      } catch (...) {
        LOG(Debug, "restoring an interrupted mimicked script failed");
      }
    }
  };

  runtime_state().set_mood(mode);
  LOG(Debug, "mimicking the script '%s'%s", ec.program().c_str(),
      isolated ? " in an isolated subshell" : "");
  source_store().set_script_run(true);

  /* A mimicked script runs with the strictness of the mood it mimics, so a bash
     or sh script clears nounset, pipefail, and failglob while a kosh script
     keeps the strict default. */
  let const is_mimic_strict = mode == mimic_mood::Default;
  runtime_state().set_error_unset(is_mimic_strict);
  runtime_state().set_pipefail(is_mimic_strict);
  runtime_state().set_failglob(is_mimic_strict);
  LOG(Debug, "seeded the strict options for the %s mimicked run",
      is_mimic_strict ? "kosh" : "lax");

  let const script_filename = ec.program_path().view();
  source_store().source_frames().push(
      source_frame{String{ec.program().view()}, ec.source_location(),
                   source_store().current_source(),
                   source_generation_for(source_store().current_source()),
                   String{script_filename}, source_frame_kind::SourcedFile});
  source_store().source_frames().back().should_defer_trace = true;
  source_store().source_frames().back().function_call_depth =
      function_store().call_frames().count();
  defer
  {
    let &frame = source_store().source_frames().back();
    if (frame.has_deferred_trace) {
      try {
        print_source_backtrace(frame.deferred_trace_location, false, true);
      } catch (...) {
        LOG(Debug, "rendering a deferred source trace failed");
      }
    }
    source_store().source_frames().pop_back();
  };
  let parser = Parser{
      Lexer{contents->view(), *arena_store().parse_arena(), script_filename,
            runtime_state().get_mood()}
  };

  let params = ArrayList<String>{heap_allocator()};
  params.reserve(ec.args().count() - 1);
  for (usize i = 1; i < ec.args().count(); i++)
    params.push_managed(ec.args()[i].view());

  /* A standard descriptor with no staged redirect is backed up too, since the
     script may move it with an exec redirection that a fork would contain. */
  let saved_fds = ArrayList<os::saved_descriptor>{heap_allocator()};
  bool should_restore_fds = true;
  let const do_restore_fds = [&]() {
    for (usize i = saved_fds.count(); i > 0; i--)
      os::restore_descriptor(saved_fds[i - 1]);
    should_restore_fds = false;
  };
  defer
  {
    if (should_restore_fds) do_restore_fds();
  };
  saved_fds.push(
      ec.in_fd.has_value()
          ? os::save_and_replace_descriptor_out_of_reach(0, *ec.in_fd)
          : os::save_descriptor_out_of_reach(0));
  saved_fds.push(
      ec.out_fd.has_value()
          ? os::save_and_replace_descriptor_out_of_reach(1, *ec.out_fd)
          : os::save_descriptor_out_of_reach(1));
  saved_fds.push(
      ec.err_fd.has_value()
          ? os::save_and_replace_descriptor_out_of_reach(2, *ec.err_fd)
          : os::save_descriptor_out_of_reach(2));
  let const do_render_error = [&](const std::exception_ptr &error) {
    try {
      std::rethrow_exception(error);
    } catch (const ErrorWithLocationAndDetails &detailed_error) {
      show_message(detailed_error.to_string(contents->view(), this));
      show_message(detailed_error.details_to_string(contents->view(), this));
      print_source_backtrace(detailed_error.location());
    } catch (const ErrorWithLocation &located_error) {
      show_message(located_error.to_string(contents->view(), this));
      print_source_backtrace(located_error.location());
    } catch (const Error &caught_error) {
      show_message(caught_error.to_string());
      print_source_backtrace();
    }
  };
  let const do_finish_script =
      [&](std::exception_ptr &error, script_exit_context exit_context)
          throws -> bool {
    let is_interrupt = mimicked_error_is_interrupt(error);
    i32 final_status = execution_store().last_exit_status();
    bool was_error_rendered = false;
    if (error && !is_interrupt) {
      final_status = mimicked_error_status(
          error, runtime_state().is_posix_mode()
                     ? mimicked_error_status_mode::Posix
                     : mimicked_error_status_mode::Default);
      execution_store().set_last_exit_status(final_status);
      do_render_error(error);
      was_error_rendered = true;
    }
    if (!is_interrupt) {
      try {
        switch (exit_context) {
        case script_exit_context::Shell: run_exit_trap(); break;
        case script_exit_context::Subshell:
          if (let const requested_status = run_subshell_exit_trap();
              requested_status.has_value())
          {
            final_status = *requested_status;
          }
          break;
        }
      } catch (...) {
        if (!error) {
          error = std::current_exception();
          is_interrupt = mimicked_error_is_interrupt(error);
          if (!is_interrupt)
            final_status = mimicked_error_status(
                error, runtime_state().is_posix_mode()
                           ? mimicked_error_status_mode::Posix
                           : mimicked_error_status_mode::Default);
        }
      }
    }
    if (error && !is_interrupt && !was_error_rendered) {
      do_render_error(error);
    }
    execution_store().set_last_exit_status(final_status);
    return is_interrupt;
  };

  /* The kernel hands a shebang interpreter the resolved script path, so $0 and
     BASH_SOURCE read that path rather than the word as typed. */
  execution_store().set_shell_name(
      String{heap_allocator(), ec.should_use_fallback_argv0
                                   ? ec.args()[0].view()
                                   : ec.program_path().view()});
  set_fresh_source(&*contents, String{ec.program().view()});
  source_store().mimicry_depth()++;
  bool should_leave_mimicry = true;
  defer
  {
    if (should_leave_mimicry) source_store().mimicry_depth()--;
  };

  let const do_evaluate_script = [&]() throws {
    let const was_terminal_exec_allowed =
        execution_store().terminal_exec_allowed();
    defer
    {
      execution_store().terminal_exec_allowed() = was_terminal_exec_allowed;
    };

    loop
    {
      let const *ast = parser.construct_next_top_level_ast();
      if (ast == nullptr) break;
      execution_store().terminal_exec_allowed() =
          was_terminal_exec_allowed && parser.is_at_end();
      ast->evaluate(*this);
      if (control_flow_store().has_pending()) break;
    }
  };

  /* The terminal command the shell exits with needs no isolation, so the script
     runs against the current state with no snapshot. */
  if (!isolated) {
    variable_store().positional_params() = steal(params);
    seed_shell_identity_variables(mode == mimic_mood::Bash
                                      ? shell_identity_mode::Bash
                                      : shell_identity_mode::Native);
    std::exception_ptr error;
    try {
      do_evaluate_script();
    } catch (...) {
      error = std::current_exception();
    }
    let const is_interrupt =
        do_finish_script(error, script_exit_context::Shell);
    source_store().mimicry_depth()--;
    should_leave_mimicry = false;
    do_restore_fds();
    if (error) {
      if (is_interrupt) throw InterruptErrorWithLocation{previous_location};

      return execution_store().last_exit_status();
    }
    return execution_store().last_exit_status();
  }

  variable_store().positional_params() = steal(params);
  seed_shell_identity_variables(mode == mimic_mood::Bash
                                    ? shell_identity_mode::Bash
                                    : shell_identity_mode::Native);
  enter_subshell();
  clear_inherited_exit_trap();
  std::exception_ptr error;
  try {
    do_evaluate_script();
  } catch (...) {
    error = std::current_exception();
  }
  if (control_flow_store().has_pending()) {
    if (control_flow_store().pending().kind == control_flow::Kind::Exit)
      execution_store().set_last_exit_status(
          static_cast<i32>(control_flow_store().pending().value));
    control_flow_store().clear();
  }
  let const is_interrupt =
      do_finish_script(error, script_exit_context::Subshell);
  leave_subshell();
  source_store().mimicry_depth()--;
  should_leave_mimicry = false;
  do_restore_fds();

  let const status = execution_store().last_exit_status();
  should_restore_isolated_state = false;
  restore_state(steal(*isolated_snapshot));
  do_restore_auxiliary_state();
  if (error) {
    if (is_interrupt) throw InterruptErrorWithLocation{previous_location};

    return status;
  }
  return status;
}

pure fn EvalContext::shopt_default_is_on(StringView name) wontthrow -> bool
{
  static constexpr PackedStringKey KEYS[] = {
      SSK("progcomp"),           SSK("promptvars"),
      SSK("sourcepath"),         SSK("extquote"),
      SSK("complete_fullquote"), SSK("hostcomplete"),
      SSK("checkwinsize"),       SSK("force_fignore"),
      SSK("globasciiranges"),    SSK("globskipdots"),
      SSK("expand_aliases"),     SSK("interactive_comments"),
      SSK("patsub_replacement"),
  };
  static constexpr StaticStringSet DEFAULT_ON_SHOPT_NAMES{KEYS};
  return DEFAULT_ON_SHOPT_NAMES.contains(name);
}

fn EvalContext::run_source(StringView source, StringView origin,
                           Maybe<SourceLocation> call_site,
                           Maybe<StringView> filename,
                           Maybe<i32> *status_before_return,
                           const FunctionBodyHandle *cached_body,
                           return_handling handling, history_recording history,
                           const trap_definition *definition) throws -> i32
{
  if (cached_body != nullptr && (cached_body->get_body() == nullptr ||
                                 cached_body->get_source() == nullptr))
  {
    cached_body = nullptr;
  }

  let normalized_source = String{heap_allocator()};
  if (cached_body == nullptr) {
    normalized_source = String{source};
    normalized_source.normalize_crlf_line_endings();
    source = normalized_source.view();
  } else {
    source = cached_body->get_source()->view();
  }

  let const consume_return = handling == return_handling::Consume;
  let const reject_return = handling == return_handling::Reject;
  let const should_propagate_script_fatal = call_site.has_value();
  if (arena_store().parse_arena() == nullptr)
    throw Error{"Cannot run source outside of a parse"};

  let *parse_arena = arena_store().parse_arena();
  let const parse_mark = parse_arena->mark();
  let const retention_mark = source_retention().get_mark();
  let const process_substitution_count =
      expansion_store().pending_process_substitutions().count();
  bool did_complete_source = false;
  defer
  {
    if (!did_complete_source || control_flow_store().has_pending() ||
        expansion_store().pending_process_substitutions().count() !=
            process_substitution_count)
    {
      return;
    }

    parse_arena->release(parse_mark);
    source_retention().release_to(retention_mark);
    reset_runtime_diagnostic_highlight_cache();
  };

  LOG(Debug, "running source '%.*s' of %zu bytes at depth %zu",
      static_cast<int>(origin.length), origin.data, source.length,
      source_store().source_depth());

  /* Bound the source and eval nesting so a file that sources itself errors here
     rather than exhausting memory. */
  enter_source(call_site ? *call_site : SourceLocation{0, 0});
  defer { leave_source(); };

  let const parent_source =
      call_site ? source_store().current_source() : nullptr;
  let const frame_is_sourced_file =
      consume_return && filename.has_value() && !filename->is_empty();

  /* The window opens before the frame is entered, and its restore runs after
     the frame is left. The body of a sourced file the trace option does not
     follow runs without the DEBUG action the caller installed. */
  let const untraced_debug_scope = UntracedTrapScope{
      *this, UntracedTrapScope::Kind::Debug, frame_is_sourced_file};

  source_store().source_frames().push(source_frame{
      String{origin},
      call_site ? *call_site : SourceLocation{0, 0},
      parent_source, source_generation_for(parent_source),
      filename.has_value() ? String{*filename}
      : String{heap_allocator()},
      frame_is_sourced_file ? source_frame_kind::SourcedFile
                            : source_frame_kind::Ordinary
  });
  source_store().source_frames().back().definition = definition;
  source_store().source_frames().back().should_defer_trace =
      frame_is_sourced_file;
  source_store().source_frames().back().function_call_depth =
      function_store().call_frames().count();
  if (reject_return)
    source_store().set_rejected_return_source_frames(
        source_store().rejected_return_source_frames() + 1);
  defer
  {
    if (reject_return)
      source_store().set_rejected_return_source_frames(
          source_store().rejected_return_source_frames() - 1);
    let &frame = source_store().source_frames().back();
    if (frame.has_deferred_trace) {
      try {
        print_source_backtrace(frame.deferred_trace_location, false, true);
      } catch (...) {
        LOG(Debug, "rendering a deferred source trace failed");
      }
    }
    source_store().source_frames().pop_back();
  };

  let const should_hold_line_discard =
      execution_store().subshell_depth() ==
      execution_store().line_discard_subshell_depth();
  let const do_pass_line_discard = [&](ErrorBase &error) wontthrow -> bool {
    if (should_hold_line_discard || !error.is_line_discarding() ||
        error.is_script_fatal())
    {
      return false;
    }

    error.set_command_status(1);
    return true;
  };

  try {
    const Expression *ast = nullptr;
    const String *retained_source = nullptr;

    if (cached_body != nullptr) {
      ast = cached_body->get_body();
      retained_source = cached_body->get_source();
    } else {
      let parser = Parser{
          Lexer{source, *arena_store().parse_arena(), filename,
                runtime_state().get_mood()}
      };

      Expression *parsed_ast = nullptr;
      try {
        parsed_ast = parser.construct_ast();
      } catch (ErrorWithLocation &syntax_error) {
        syntax_error.set_command_status(SYNTAX_ERROR_STATUS);
        throw;
      }
      ASSERT(parsed_ast != nullptr);
      source_retention().reserve_one_more();

      /* Keep a copy of the source alive for as long as the AST, so a
         control-flow jump made inside it can point a caret at the right text
         after this call returns. */
      let const owned_source = heap_allocator().alloc_array<String>(1);
      if (owned_source == nullptr) throw std::bad_alloc{};
      try {
        new (owned_source) String{steal(normalized_source)};
      } catch (...) {
        heap_allocator().free_array(owned_source, 1);
        throw;
      }
      source_retention().retain(owned_source, parsed_ast);
      ast = parsed_ast;
      retained_source = owned_source;
    }
    source = retained_source->view();

    let const previous_recording_mark = history_recorder().get_mark();
    if (history == history_recording::Enabled)
      history_recorder().begin_recording(ast, source);
    defer { history_recorder().restore_mark(previous_recording_mark); };

    let const source_scope = capture_source_scope();
    let const did_register_line_base =
        call_site.has_value() && !frame_is_sourced_file &&
        cached_body == nullptr &&
        register_embedded_source(StringView{}, *call_site, retained_source);
    if (did_register_line_base) {
      source_store().set_embedded_depth_floor(
          source_depth_floor(source_store().source_frames().count() - 1));
    }
    defer
    {
      if (did_register_line_base) unregister_embedded_source();
    };
    set_fresh_source(retained_source, String{origin});

    let const previous_line_discard_root =
        execution_store().line_discard_root();
    let const previous_line_discard_source =
        execution_store().line_discard_source();
    let const previous_line_discard_status =
        execution_store().line_discard_status();
    if (should_hold_line_discard) {
      execution_store().line_discard_root() = ast;
      execution_store().line_discard_source() = source;
      execution_store().line_discard_status() = i64{1};
    }
    defer
    {
      execution_store().line_discard_root() = previous_line_discard_root;
      execution_store().line_discard_source() = previous_line_discard_source;
      execution_store().line_discard_status() = previous_line_discard_status;
    };

    ast->evaluate(*this);
    did_complete_source = true;
    /* A return at the top of a sourced file or an eval returns from that source
       with its status. Break, continue, and exit keep propagating. */
    if (consume_return && control_flow_store().has_pending() &&
        control_flow_store().pending().kind == control_flow::Kind::Return)
    {
      let const source_status =
          static_cast<i32>(control_flow_store().pending().value);
      if (status_before_return != nullptr)
        *status_before_return = trap_store().status_before_return();

      control_flow_store().clear();
      execution_store().set_last_exit_status(source_status);
      return source_status;
    }
    return execution_store().last_exit_status();
  } catch (const InterruptErrorWithLocation &) {
    /* An interrupt ends the whole shell command. It passes through the sourced
       file, the eval, and the trap action that was running. */
    throw;
  } catch (ErrorWithLocationAndDetails &detailed_error) {
    if (!detailed_error.was_rendered()) {
      show_message(detailed_error.to_string(source, this));
      show_message(detailed_error.details_to_string(source, this));
      print_source_backtrace(detailed_error.location());
      detailed_error.set_rendered();
    }
    if (detailed_error.is_script_fatal() && should_propagate_script_fatal) {
      throw;
    }
    if (do_pass_line_discard(detailed_error)) throw;
    did_complete_source = true;
    return static_cast<i32>(detailed_error.command_status());
  } catch (ErrorWithLocation &located_error) {
    if (!located_error.was_rendered()) {
      show_message(located_error.to_string(source, this));
      print_source_backtrace(located_error.location());
      located_error.set_rendered();
    }
    if (located_error.is_script_fatal() && should_propagate_script_fatal) {
      throw;
    }
    if (do_pass_line_discard(located_error)) throw;
    did_complete_source = true;
    return static_cast<i32>(located_error.command_status());
  } catch (Error &caught_error) {
    if (!caught_error.was_rendered()) {
      show_message(caught_error.to_string());
      print_source_backtrace();
      caught_error.set_rendered();
    }
    if (caught_error.is_script_fatal() && should_propagate_script_fatal) {
      throw;
    }
    if (do_pass_line_discard(caught_error)) throw;
    did_complete_source = true;
    return static_cast<i32>(caught_error.command_status());
  }
}

fn EvalContext::resolve_source_path(
    StringView path, source_tilde_expansion tilde_expansion) throws
    -> Maybe<Path>
{
  let expanded_path = String{heap_allocator(), path};
  if (tilde_expansion == source_tilde_expansion::Enabled &&
      path.starts_with("~"))
  {
    let const slash = path.find_character('/');
    let const prefix_end = slash.value_or(path.length);
    let const prefix = path.substring_of_length(1, prefix_end - 1);
    if (let directory = resolve_tilde_prefix(prefix); directory.has_value()) {
      expanded_path = directory.take();
      if (slash.has_value()) {
        if (expanded_path.is_empty() || expanded_path.back() != '/')
          expanded_path.push('/');
        expanded_path.append(path.substring(*slash + 1));
      }
      path = expanded_path.view();
    }
  }
  let source_path = Path{path};
  if (os::has_directory_separator(path)) return source_path;
  if (!runtime_state().is_shopt_enabled(shopt_option_id::Sourcepath))
    return source_path;

  let const path_matches =
      program_resolver().search(path, ProgramResolver::SearchMode::First,
                                ProgramResolver::Requirement::Regular,
                                ProgramResolver::CachePolicy::Bypass);
  if (!path_matches.is_empty()) return path_matches[0].clone();
  if (runtime_state().is_posix_mode()) return None;

  return source_path;
}

fn EvalContext::clear_retained_sources() wontthrow -> void
{
  LOG(All, "dropping %zu retained sources and their asts",
      source_retention().count());

#if !defined NDEBUG
  for (let const &frame : source_store().source_frames()) {
    if (frame.parent_source == nullptr) continue;
    ASSERT(!source_retention().owns(frame.parent_source),
           "a live source frame still borrows a dropped source");
  }
#endif

  /* A stashed source view or location may index a buffer freed just below, so
     both drop to the unlocated rendering. */
  for (process_substitution &sub :
       expansion_store().pending_process_substitutions())
  {
    sub.source = StringView{};
    sub.location = SourceLocation{};
  }

  if (control_flow_store().has_pending()) {
    control_flow_store().pending().source = nullptr;
    control_flow_store().pending().location = SourceLocation{};
  }

  source_retention().clear();

  reset_runtime_diagnostic_highlight_cache();

  source_store().current_source() = nullptr;
  source_store().current_source_generation() = EXTERNAL_SOURCE_GENERATION;
  source_store().current_origin().clear();
}

fn SourceRetention::free_sources_from(usize first) wontthrow -> void
{
  for (usize index = first; index < m_sources.count(); index++) {
    String *retained = m_sources[index];
    utils::invalidate_line_number_cache_for(retained->view());
    retained->~String();
    heap_allocator().free_array(retained, 1);
  }

  m_sources.truncate(first);
}

fn SourceRetention::release_to(source_retention_mark mark) wontthrow -> void
{
  m_asts.truncate(mark.ast_count);
  free_sources_from(mark.source_count);
  if (mark.source_count == 0) m_generation++;
}

fn SourceRetention::clear() wontthrow -> void
{
  m_asts.clear();
  free_sources_from(0);
  m_generation++;
}

pure fn
EvalContext::scan_source_generation(const String *source) const wontthrow -> u64
{
  return source_retention().generation_of(source);
}

pure fn EvalContext::source_generation_for(const String *source) const wontthrow
    -> u64
{
  if (source == source_store().current_source())
    return source_store().current_source_generation();

  return scan_source_generation(source);
}

pure fn EvalContext::borrowed_frame_source(
    const source_frame &frame) const wontthrow -> const String *
{
  if (source_retention().is_stale_generation(frame.parent_source_generation))
    return nullptr;

  return frame.parent_source;
}

fn EvalContext::expand_heredoc_body(
    StringView body, const SourceLocation *source_location) throws -> String
{
  LOG(Debug, "expanding a heredoc body of %zu bytes", body.length);
  let const was_expanding_here_document =
      expansion_store().is_expanding_here_document();
  expansion_store().is_expanding_here_document() = true;
  defer
  {
    expansion_store().is_expanding_here_document() =
        was_expanding_here_document;
  };

  return expand_modifier_word(body, false, false, source_location);
}

} /* namespace koshka */
