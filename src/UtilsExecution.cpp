/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shared command execution and process lifecycle
 * helpers. It dispatches builtins and programs, constructs pipelines and
 * jobs, records PIPESTATUS, updates foreground titles, and handles shutdown
 * and memory reports.
 */

#include "Builtin.hpp"
#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "EvalVariablesInternal.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"
#include "base/Containers.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace utils {

fn set_foreground_program_title(const ArrayList<String> &arguments,
                                EvalContext &cxt) throws -> void
{
  if (arguments.is_empty()) return;

  if (!cxt.execution_store().shell_is_interactive() ||
      !cxt.startup_store().startup_finished() ||
      cxt.execution_store().completion_function_running() ||
      cxt.execution_store().prompt_command_running())
  {
    return;
  }

  let &command_title = cxt.job_table_store().foreground_program_title_buffer();
  command_title.clear();
  for (usize index = 0; index < arguments.count(); index++) {
    if (index > 0) command_title.push(' ');
    append_shell_quoted_arg(command_title, arguments[index]);
  }
  toiletline::set_title(command_title.view());
}

fn execute_context(ExecContext &&ec, EvalContext &cxt,
                   execution_mode mode) throws -> i32
{
  let const is_async = mode == execution_mode::Background;
  if (ec.is_builtin()) {
    if (is_async) {
      let contexts = ArrayList<ExecContext>{cxt.scratch_allocator()};
      contexts.push(steal(ec));
      return execute_contexts_with_pipes(steal(contexts), cxt, mode);
    }

    LOG(Debug, "dispatching the builtin '%s'", ec.program().c_str());
    return execute_builtin(steal(ec), cxt);
  }

  let const can_replace_shell = cxt.can_replace_process() && !cxt.in_subshell();

  if (cxt.runtime_state().is_mimicry_enabled() && !is_async) {
    if (Maybe<mimic_mood> mode = ec.program_path().detect_mimic_shell();
        mode.has_value())
    {
      LOG(Debug, "execute_context mimicking the shell for '%s'",
          ec.program().c_str());
      if (cxt.execution_store().shell_is_interactive() &&
          os::shell_has_controlling_terminal())
      {
        let command = String{heap_allocator()};
        for (usize index = 0; index < ec.args().count(); index++) {
          if (index > 0) command.push(' ');
          append_shell_quoted_arg(command, ec.args()[index]);
        }

        let const sync_pipe = os::make_pipe();

        koshka::flush();
        let const forked_child = os::try_fork_job_process();
        if (forked_child.has_value()) {
          const os::process child = *forked_child;
          if (os::process_id_of(child) == 0) {
            if (sync_pipe.has_value()) {
              os::close_fd(sync_pipe->out);
              char handoff_byte = 0;
              (void) os::read_fd(sync_pipe->in, &handoff_byte, 1);
              os::close_fd(sync_pipe->in);
            }
            i32 status = 1;
            try {
              status =
                  cxt.run_mimicked_script(ec, *mode, script_isolation::Shared);
            } catch (const ErrorBase &error) {
              show_message(error.to_string(
                  cxt.source_store().current_source_view(), &cxt));
              status = static_cast<i32>(error.command_status());
            } catch (...) {}
            os::exit_process_immediately(status);
          }

          if (sync_pipe.has_value()) {
            os::close_fd(sync_pipe->in);
          }
          os::give_controlling_terminal_to(child);
          if (sync_pipe.has_value()) {
            (void) os::write_fd(sync_pipe->out, "x", 1);
            os::close_fd(sync_pipe->out);
          }

          let was_stopped = false;
          const i32 status = os::wait_and_monitor_process(child, &was_stopped);
          os::reclaim_controlling_terminal();

          if (was_stopped) {
            const i32 id = cxt.job_table_store().register_stopped_job(
                child, command, status, os::process_id_of(child));
            cxt.job_table_store().notify_stopped_job(id);
          }
          return status;
        }

        if (sync_pipe.has_value()) {
          os::close_fd(sync_pipe->in);
          os::close_fd(sync_pipe->out);
        }
      }
      return cxt.run_mimicked_script(ec, *mode,
                                     can_replace_shell
                                         ? script_isolation::Shared
                                         : script_isolation::Isolated);
    }
  }

  if (!is_async && can_replace_shell) {
    LOG(Debug,
        "execute_context replacing the shell with the terminal command '%s'",
        ec.program().c_str());
    flush();
    cxt.prepare_child_environment();
    try {
      os::replace_process(steal(ec));
    } catch (const ErrorWithLocation &error) {
      show_message(
          error.to_string(cxt.source_store().current_source_view(), &cxt));
      quit(126, farewell_policy::Silent);
    } catch (const Error &error) {
      let located = ErrorWithLocation{ec.source_location(), error.message()};
      located.set_command_status(error.command_status());
      show_message(
          located.to_string(cxt.source_store().current_source_view(), &cxt));
      quit(127, farewell_policy::Silent);
    }
    LOG(Debug, "running the file as a shell script in place");
    ec.in_fd.reset();
    ec.out_fd.reset();
    ec.err_fd.reset();
    const mimic_mood mode = cxt.runtime_state().get_mood();
    quit(cxt.run_program_fallback(ec, mode,
                                  can_replace_shell
                                      ? script_isolation::Shared
                                      : script_isolation::Isolated),
         farewell_policy::Silent);
  }

  LOG(Debug, "spawning the external command '%s'%s", ec.program().c_str(),
      is_async ? " in the background" : "");
  cxt.release_finished_coprocess();

  let const is_foreground_job = !is_async &&
                                cxt.execution_store().shell_is_interactive() &&
                                os::shell_has_controlling_terminal();

  let command = String{heap_allocator()};
  if (is_async || is_foreground_job) {
    for (usize i = 0; i < ec.args().count(); i++) {
      if (i > 0) command += ' ';
      append_shell_quoted_arg(command, ec.args()[i]);
    }
  }

  cxt.evaluation_metrics_store().add_external_command_run(
      cxt.runtime_state().stats_enabled());

  let const source_view = cxt.source_store().current_source_view();
  cxt.prepare_child_environment();
  cxt.job_table_store().forget_waited_jobs();
  os::process p = os::execute_program(
      ec, os::program_execution_options{
              .source = source_view,
              .fallback = is_async ? os::script_fallback_policy::Reject
                                   : os::script_fallback_policy::Allow,
              .process_group = is_async ? os::process_group_mode::NewBackground
                               : is_foreground_job
                                   ? os::process_group_mode::New
                                   : os::process_group_mode::Inherit});
  if (p == KOSH_INVALID_PROCESS) {
    LOG(Debug, "running the file as a shell script in this process");
    const mimic_mood mode = cxt.runtime_state().get_mood();
    return cxt.run_program_fallback(ec, mode,
                                    can_replace_shell
                                        ? script_isolation::Shared
                                        : script_isolation::Isolated);
  }
  if (is_async) {
    cxt.job_table_store().set_last_background_pid(os::process_id_of(p));
    let const process_group_id = os::process_id_of(p);
    const i32 id =
        cxt.job_table_store().register_job(p, command, process_group_id);
    if (cxt.execution_store().shell_is_interactive())
      koshka::print_error("[" + String::from(id, heap_allocator()) + "] " +
                          String::from(static_cast<u64>(os::process_id_of(p)),
                                       heap_allocator()) +
                          "\n");
    return 0;
  }

  LOG(Debug, "waiting for the foreground child to finish");
  let was_stopped = false;
  i32 foreground_status;
  if (is_foreground_job) {
    os::give_controlling_terminal_to(p);
    defer { os::reclaim_controlling_terminal(); };
    foreground_status = os::wait_and_monitor_process(p, &was_stopped);
  } else {
    foreground_status = os::wait_and_monitor_process(p);
  }
  if (was_stopped) {
    const i32 id = cxt.job_table_store().register_stopped_job(
        p, command, foreground_status, os::process_id_of(p));
    cxt.job_table_store().notify_stopped_job(id);
  }
  let const was_interrupt_to_reraise_for_completion =
      foreground_status == 130 &&
      cxt.execution_store().completion_function_running();
  if (was_interrupt_to_reraise_for_completion) os::INTERRUPT_REQUESTED = 1;
  return foreground_status;
}

fn terminate_and_reap_processes(const ArrayList<os::process> &processes,
                                usize first_process_position) wontthrow -> void
{
  for (usize position = first_process_position; position < processes.count();
       position++)
    unused(os::signal_process(processes[position], 9));

  for (usize position = first_process_position; position < processes.count();
       position++)
  {
    try {
      os::reap_process_quietly(processes[position]);
    } catch (...) {}
  }
}

static fn report_unresolved_stage(EvalContext &cxt,
                                  const ExecContext &stage) throws -> void
{
  let const does_error_follow_output =
      stage.should_duplicate_error_to_output &&
      (!stage.should_duplicate_output_to_error ||
       stage.was_output_to_error_last);
  let target = does_error_follow_output ? stage.out_fd : stage.err_fd;

  if (does_error_follow_output && !target.has_value())
    target = os::descriptor_for_shell_fd(1);

  if (!target.has_value()) {
    show_message(stage.get_unresolved_diagnostic());
    cxt.print_source_backtrace(stage.source_location());
    return;
  }

  koshka::flush();

  let const saved = os::save_and_replace_descriptor(2, *target);
  defer { os::restore_descriptor(saved); };

  show_message(stage.get_unresolved_diagnostic());

  cxt.print_source_backtrace(stage.source_location(), !saved.is_dup2_ok);
}

fn execute_contexts_with_pipes(ArrayList<ExecContext> &&ecs, EvalContext &cxt,
                               execution_mode mode) throws -> i32
{
  let const is_async = mode == execution_mode::Background;
  ASSERT(!ecs.is_empty());

  if (!is_async && cxt.execution_store().shell_is_interactive() &&
      cxt.startup_store().startup_finished() &&
      !cxt.execution_store().completion_function_running() &&
      !cxt.execution_store().prompt_command_running())
  {
    let command = String{cxt.scratch_allocator()};
    for (usize stage = 0; stage < ecs.count(); stage++) {
      if (stage > 0) command += " | ";
      for (usize argument = 0; argument < ecs[stage].args().count(); argument++)
      {
        if (argument > 0) command.push(' ');
        append_shell_quoted_arg(command, ecs[stage].args()[argument]);
      }
    }
    toiletline::set_title(command.view());
  }

  LOG(Debug, "running a pipeline of %zu stages%s", ecs.count(),
      is_async ? " in the background" : "");

  let job_command = String{heap_allocator()};
  if (is_async) {
    for (usize stage = 0; stage < ecs.count(); stage++) {
      if (stage > 0) job_command += " | ";
      for (usize argument = 0; argument < ecs[stage].args().count(); argument++)
      {
        if (argument > 0) job_command += ' ';
        append_shell_quoted_arg(job_command, ecs[stage].args()[argument]);
      }
    }
  }

  i32 ret = 0;

  let children = ArrayList<os::process>{heap_allocator()};
  os::process last_child = KOSH_INVALID_PROCESS;
  os::descriptor last_stdin = KOSH_INVALID_FD;
  i64 process_group_id = 0;
  bool should_reap_children_on_unwind = true;
  defer
  {
    if (should_reap_children_on_unwind) {
      for (ExecContext &pending_context : ecs)
        pending_context.close_fds();
      if (last_stdin != KOSH_INVALID_FD) os::close_fd(last_stdin);
      terminate_and_reap_processes(children);
    }
  };

  let const stage_count = ecs.count();
  let stage_status = ArrayList<i32>{heap_allocator()};
  stage_status.reserve(stage_count);
  for (usize i = 0; i < stage_count; i++)
    stage_status.push(0);
  let child_stage = ArrayList<usize>{heap_allocator()};

  let unresolved_stages = ArrayList<usize>{heap_allocator()};

  bool is_first = true;
  usize stage_index = 0;
  let bootstrap = os::subshell_bootstrap{};

  for (ExecContext &ec : ecs) {
    Maybe<os::Pipe> pipe;

    let const is_last = (&ec == &ecs.back());
    let const should_fork_last_builtin =
        is_last && !is_async &&
        (!unresolved_stages.is_empty() ||
         (cxt.runtime_state().is_bash_compatible() &&
          (!cxt.is_shopt_enabled("lastpipe") ||
           cxt.runtime_state().option_is_enabled(shell_option_id::Monitor))));

    if (!is_last) {
      pipe = os::make_pipe();
      if (!pipe) {
        throw ErrorWithLocation{ec.source_location(), "Could not open a pipe"};
      }
      let const has_leading_error_dup = ec.should_duplicate_error_to_output &&
                                        ec.did_output_file_follow_error_dup;
      let const has_leading_output_dup = ec.should_duplicate_output_to_error &&
                                         ec.did_error_file_follow_output_dup;

      bool did_stage_take_pipe = false;

      if (has_leading_error_dup && !ec.err_fd) {
        ec.err_fd = pipe->out;
        ec.should_duplicate_error_to_output = false;
        did_stage_take_pipe = true;
      }

      if (!ec.out_fd && !has_leading_output_dup && !did_stage_take_pipe) {
        ec.out_fd = pipe->out;
        did_stage_take_pipe = true;
      }

      if (!did_stage_take_pipe) os::close_fd(pipe->out);
    }

    if (!is_first) {
      if (!ec.in_fd)
        ec.in_fd = last_stdin;
      else
        os::close_fd(last_stdin);
    }
    if (!is_last) {
      last_stdin = pipe->in;
    }

    if (ec.is_unresolved()) {
      stage_status[stage_index] = ec.get_unresolved_status();
      unresolved_stages.push(stage_index);
    } else if (!ec.is_builtin()) {
      cxt.evaluation_metrics_store().add_external_command_run(
          cxt.runtime_state().stats_enabled());
      cxt.prepare_child_environment();
      let const process_group =
          !is_async ? os::process_group_mode::Inherit
                    : os::background_process_group_mode(process_group_id);
      let const child = os::execute_program(
          ec, os::program_execution_options{
                  .source = cxt.source_store().current_source_view(),
                  .process_group_id = process_group_id,
                  .fallback = os::script_fallback_policy::Reject,
                  .process_group = process_group});
      if (is_async && process_group_id == 0) {
        process_group_id = os::process_id_of(child);
      }
      children.push(child);
      child_stage.push(stage_index);
      last_child = child;
    } else if (!is_last || is_async || should_fork_last_builtin) {
      let const source_view = cxt.source_store().current_source_view();
      let const process_group =
          !is_async ? os::process_group_mode::Inherit
                    : os::background_process_group_mode(process_group_id);
      let forked_child = os::try_fork_compound_stage(
          os::fork_compound_stage_options{.in_fd = ec.in_fd,
                                          .out_fd = ec.out_fd,
                                          .err_fd = ec.err_fd,
                                          .location = ec.source_location(),
                                          .diagnostic_source = source_view,
                                          .process_group_id = process_group_id,
                                          .process_group = process_group});
      let preflight_status = Maybe<i32>{};
      let preflight_location = SourceLocation{};
      let preflight_message = String{cxt.scratch_allocator()};
      if (!forked_child.has_value()) {
        const usize utility_index = ec.program() == "koshkit" ? 1 : 0;
        bool should_restore_environment = false;
        if (ec.builtin_kind() == Builtin::Kind::Koshkit &&
            utility_index < ec.args().count())
        {
          let const utility_kind =
              koshkit::find_util(ec.args()[utility_index].view());
          if (utility_kind.has_value()) {
            if (!is_async && *utility_kind == koshkit::Utility::Kind::Timeout) {
              preflight_status = koshkit::preflight_timeout_stage(
                  ec, cxt, utility_index, preflight_location,
                  preflight_message);
            }
            should_restore_environment =
                *utility_kind == koshkit::Utility::Kind::Env;
          }
        }

        if (!preflight_status.has_value()) {
          let stage_source = String{cxt.scratch_allocator()};
          if (should_restore_environment) {
            static const StringView RESTORED_ENVIRONMENT_NAMES[] = {
                "PWD",
                "KOSH",
                "KOSH_VERSION",
                "KOSH_COMMIT",
                "KOSH_BUILD_MODE",
                "KOSH_OS",
                "BASH_VERSION",
                "BASH",
                "SHLVL",
                "PATH",
                "NO_COLOR",
                internal::SUPPRESS_ROOT_TRACE};
            for (let const name : RESTORED_ENVIRONMENT_NAMES) {
              let const value = os::get_environment_variable(name);
              if (value.has_value()) {
                stage_source.append("export ");
                stage_source.append(name);
                stage_source.push('=');
                append_shell_quoted_arg(stage_source, value->view(), true);
              } else {
                stage_source.append("unset ");
                stage_source.append(name);
              }
              stage_source.append("; ");
            }
          }
          if (ec.builtin_kind() == Builtin::Kind::Koshkit &&
              ec.program() != "koshkit")
          {
            stage_source.append("koshkit ");
          }
          for (usize argument_index = 0; argument_index < ec.args().count();
               argument_index++)
          {
            if (argument_index > 0) stage_source.push(' ');
            append_shell_quoted_arg(stage_source, ec.args()[argument_index],
                                    true);
          }

          let stage_out = Maybe<os::descriptor>{};
          let stage_err = Maybe<os::descriptor>{};
          ec.apply_output_routing(
              [&]() { stage_out = ec.out_fd; },
              [&]() { stage_err = ec.err_fd; },
              [&]() { stage_err = stage_out.value_or(KOSH_STDOUT); },
              [&]() { stage_out = stage_err.value_or(KOSH_STDERR); });
          try {
            let const launch =
                os::launch_compound_stage(os::compound_stage_options{
                    .source = stage_source.view(),
                    .in_fd = ec.in_fd,
                    .out_fd = stage_out,
                    .err_fd = stage_err,
                    .location = ec.source_location(),
                    .diagnostic_source = source_view,
                    .process_group_id = process_group_id,
                    .evaluator = cxt.make_child_evaluator_state(bootstrap),
                    .process_group = process_group});
            forked_child = launch.child;
          } catch (...) {
            ec.close_fds();
            os::close_fd(last_stdin);
            last_stdin = KOSH_INVALID_FD;
            throw;
          }
        }
      }

      if (preflight_status.has_value()) {
        let const error =
            ErrorWithLocation{preflight_location, preflight_message.view()};
        let diagnostic = String{cxt.scratch_allocator()};
        diagnostic += error.to_string(source_view, &cxt);
        diagnostic.push('\n');
        let diagnostic_out = Maybe<os::descriptor>{};
        let diagnostic_err = Maybe<os::descriptor>{};
        ec.apply_output_routing(
            [&]() { diagnostic_out = ec.out_fd; },
            [&]() { diagnostic_err = ec.err_fd; },
            [&]() { diagnostic_err = diagnostic_out.value_or(KOSH_STDOUT); },
            [&]() { diagnostic_out = diagnostic_err.value_or(KOSH_STDERR); });
        os::signal_internal_diagnostic();
        if (!os::write_all(diagnostic_err.value_or(KOSH_STDERR),
                           diagnostic.data(), diagnostic.count()))
        {
          let const saved_errno = errno;
          if (saved_errno == EPIPE) throw BrokenPipeExit{};
          throw Error{"Unable to write to stderr: " +
                      os::last_system_error_message()};
        }

        stage_status[stage_index] = *preflight_status;
        ec.close_fds();
      } else if (!forked_child.has_value()) {
        cxt.job_table_store().set_in_pipeline_stage(true);
        defer { cxt.job_table_store().set_in_pipeline_stage(false); };
        ret = execute_builtin(steal(ec), cxt);
        stage_status[stage_index] = ret;
      } else {
        const os::process child = *forked_child;
        if (os::process_id_of(child) == 0) {
          ec.in_fd = koshka::None;
          ec.out_fd = koshka::None;
          ec.err_fd = koshka::None;
          if (last_stdin != KOSH_INVALID_FD) os::close_fd(last_stdin);

          for (let const unresolved_index : unresolved_stages)
            ecs[unresolved_index].close_fds();

          cxt.job_table_store().set_in_pipeline_stage(true);
          cxt.enter_subshell();
          cxt.hide_coprocess_descriptors();
          cxt.job_table_store().inherit_parent_jobs(false);
          i32 child_status = 0;
          try {
            child_status = execute_builtin(steal(ec), cxt);
          } catch (const BrokenPipeExit &) {
            child_status = KOSH_BROKEN_PIPE_EXIT_STATUS;
          } catch (const ErrorWithLocation &e) {
            if (!e.was_rendered()) {
              koshka::show_message(
                  e.to_string(cxt.source_store().current_source_view(), &cxt));
            }
            child_status = static_cast<i32>(e.command_status());
          } catch (const Error &e) {
            koshka::show_message(e.to_string());
            child_status = static_cast<i32>(e.command_status());
          } catch (...) {
            child_status = 1;
          }
          koshka::flush();
          os::exit_process_immediately(child_status);
        }

        if (is_async && process_group_id == 0) {
          process_group_id = os::process_id_of(child);
        }
        ec.close_fds();
        children.push(child);
        child_stage.push(stage_index);
        last_child = child;
      }
    } else {
      cxt.job_table_store().set_in_pipeline_stage(true);
      defer { cxt.job_table_store().set_in_pipeline_stage(false); };
      ret = execute_builtin(steal(ec), cxt);
      stage_status[stage_index] = ret;
    }

    is_first = false;
    stage_index++;
  }

  for (let const unresolved_index : unresolved_stages) {
    report_unresolved_stage(cxt, ecs[unresolved_index]);
    ecs[unresolved_index].close_fds();
  }

  if (is_async) {
    if (last_child != KOSH_INVALID_PROCESS) {
      cxt.job_table_store().set_last_background_pid(
          os::process_id_of(last_child));
      const i32 id = cxt.job_table_store().register_pipeline_job(
          children, last_child, job_command.view(), process_group_id);
      should_reap_children_on_unwind = false;
      if (cxt.execution_store().shell_is_interactive())
        koshka::print_error(
            "[" + String::from(id, heap_allocator()) + "] " +
            String::from(static_cast<u64>(os::process_id_of(last_child)),
                         heap_allocator()) +
            "\n");
    }
    return ret;
  }

  usize waited_child_count = 0;
  try {
    for (; waited_child_count < children.count(); waited_child_count++)
      stage_status[child_stage[waited_child_count]] =
          os::wait_and_monitor_process(children[waited_child_count]);
  } catch (...) {
    terminate_and_reap_processes(children, waited_child_count);
    should_reap_children_on_unwind = false;
    throw;
  }
  should_reap_children_on_unwind = false;

  let pipe_status = ArrayList<String>{heap_allocator()};
  pipe_status.reserve(stage_count);
  for (usize i = 0; i < stage_count; i++)
    pipe_status.push(String::from(stage_status[i], heap_allocator()));
  cxt.publish_pipe_statuses(steal(pipe_status));

  if (cxt.runtime_state().pipefail()) {
    for (usize i = stage_count; i > 0; i--)
      if (stage_status[i - 1] != 0) return stage_status[i - 1];
    return 0;
  }

  return stage_status[stage_count - 1];
}

static const EvalContext *QUIT_CONTEXT = nullptr;

fn set_quit_context(const EvalContext *context) wontthrow -> void
{
  QUIT_CONTEXT = context;
}

cold fn print_memory_report() wontthrow -> void
{
  if (QUIT_CONTEXT != nullptr &&
      QUIT_CONTEXT->arena_store().parse_arena() != nullptr)
    std::fprintf(
        stderr,
        "AST arena: used %zu, reserved %zu, blocks %zu, destructors "
        "%zu of %zu\n",
        QUIT_CONTEXT->arena_store().parse_arena()->bytes_used(),
        QUIT_CONTEXT->arena_store().parse_arena()->bytes_capacity(),
        QUIT_CONTEXT->arena_store().parse_arena()->block_count(),
        QUIT_CONTEXT->arena_store().parse_arena()->destructor_count(),
        QUIT_CONTEXT->arena_store().parse_arena()->destructor_capacity());
  if (QUIT_CONTEXT != nullptr) {
    let const stats = QUIT_CONTEXT->function_storage_stats();
    std::fprintf(stderr,
                 "Function arenas: used %zu, reserved %zu, blocks %zu, "
                 "destructors %zu of %zu\n",
                 stats.bytes_used, stats.bytes_capacity, stats.block_count,
                 stats.destructor_count, stats.destructor_capacity);
  }
  os::malloc_heap_stats heap_stats{};
  if (os::read_malloc_heap_stats(heap_stats))
    std::fprintf(stderr,
                 "Malloc heap: in use %zu, total arena %zu, mmapped %zu\n",
                 heap_stats.bytes_in_use, heap_stats.arena_bytes,
                 heap_stats.mapped_bytes);
}

wontreturn fn quit(i32 code, farewell_policy farewell) throws -> void
{
  let const should_goodbye = farewell == farewell_policy::Goodbye;
  LOG(Info, "quitting with code %d", code);

  if (QUIT_CONTEXT != nullptr &&
      QUIT_CONTEXT->runtime_state().memory_stats_enabled())
  {
    print_memory_report();
  }

  const u8 actual_code = static_cast<u8>(code);

  if (!os::is_child_process()) {
    if (toiletline::is_active()) {
      try {
        let const history_size_limit =
            QUIT_CONTEXT != nullptr
                ? QUIT_CONTEXT->variable_store().history_limit(
                      "KOSH_HISTORY_SIZE", 4096)
                : 4096;
        toiletline::exit(history_size_limit);
      } catch (const Error &e) {
        show_message(e.to_string());
      }
    }

    if (should_goodbye && QUIT_CONTEXT != nullptr &&
        QUIT_CONTEXT->execution_store().shell_is_interactive())
    {
      if (let const farewell =
              QUIT_CONTEXT->get_variable_value("KOSH_FAREWELL");
          farewell.has_value())
      {
        if (!farewell->is_empty()) {
          let message = String{heap_allocator(), farewell->view()};
          if (code != 0) {
            message += " (Code ";
            message += String::from(actual_code, heap_allocator());
            message += ')';
          }
          show_message(message);
        }
      } else {
        let message = String{heap_allocator(), "Goodbye :c"};
        if (code != 0) {
          message += " (Code ";
          message += String::from(actual_code, heap_allocator());
          message += ')';
        }
        show_message(message);
      }
    }
  }

  std::exit(actual_code);
}

}

}
