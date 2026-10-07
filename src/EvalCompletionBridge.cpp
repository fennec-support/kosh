/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file connects evaluator-owned completion specifications with
 * completion functions, their Bash argument frames, COMPREPLY execution, and
 * the captured output of a -C completion command. The split exists because
 * completion evaluation needs completion types and callbacks that the
 * evaluator core does not otherwise include.
 */

#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Utils.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

namespace koshka {

fn EvalContext::run_completion_command(StringView command,
                                       StringView command_name, StringView word,
                                       StringView previous_word,
                                       StringView line, usize point) throws
    -> ArrayList<String>
{
  LOG(Info, "running the completion command '%.*s' for the word '%.*s'",
      static_cast<int>(command.length), command.data,
      static_cast<int>(word.length), word.data);

  let source = String{heap_allocator(), command};
  for (let const argument : {command_name, word, previous_word}) {
    source.push(' ');
    append_shell_quoted_arg(source, argument, true);
  }

  let const do_export = [&](StringView name, StringView value) throws -> void {
    force_unset_shell_variable(name);
    record_environment_change(name);
    os::set_environment_variable(name, value);
    mark_exported(name);
  };

  char number_buffer[32];
  let snapshot = snapshot_state();
  let output = String{heap_allocator()};
  let was_interrupted = false;
  {
    enter_subshell();
    defer { leave_subshell(); };

    try {
      do_export("COMP_LINE", line);
      do_export("COMP_POINT",
                utils::int_to_text_into(static_cast<i64>(point), number_buffer,
                                        sizeof(number_buffer)));
      do_export("COMP_KEY", "9");
      do_export("COMP_TYPE", "9");
      output = capture_command_substitution(source);
    } catch (const InterruptErrorWithLocation &) {
      was_interrupted = true;
      LOG(Debug, "completion command '%.*s' was interrupted",
          static_cast<int>(command.length), command.data);
    } catch (const ErrorBase &error) {
      LOG(Debug, "completion command '%.*s' threw: %s",
          static_cast<int>(command.length), command.data,
          error.message().c_str());
    }
  }

  restore_state(steal(snapshot));
  if (was_interrupted) {
    os::INTERRUPT_REQUESTED = 1;
    return ArrayList<String>{heap_allocator()};
  }

  let candidates = ArrayList<String>{heap_allocator()};
  let const text = output.view();
  usize line_start = 0;
  while (line_start < text.length) {
    usize line_end = line_start;
    while (line_end < text.length && text[line_end] != '\n') {
      if (text[line_end] == '\\' && line_end + 1 < text.length &&
          text[line_end + 1] == '\n')
      {
        line_end++;
      }

      line_end++;
    }

    if (line_end > line_start) {
      candidates.push(
          String{heap_allocator(),
                 text.substring_of_length(line_start, line_end - line_start)});
    }

    line_start = line_end + 1;
  }

  LOG(Info, "completion command '%.*s' returned %zu candidates",
      static_cast<int>(command.length), command.data, candidates.count());
  return candidates;
}

fn EvalContext::run_completion_function(StringView function_name,
                                        StringView command_name,
                                        const ArrayList<String> &words,
                                        usize cword, StringView line,
                                        usize point, i32 *out_exit_status,
                                        bool should_mark_directories) throws
    -> ArrayList<String>
{
  FunctionBodyHandle body_storage{};
  if (function_store().has_functions()) {
    if (let const *storage = function_store().find_storage(function_name);
        storage != nullptr)
    {
      body_storage = *storage;
    }
  }
  const Expression *body =
      body_storage.has_value() ? body_storage.get_body() : nullptr;
  if (body == nullptr) return ArrayList<String>{heap_allocator()};

  LOG(Info,
      "running the completion function '%.*s' with %zu words, cursor word %zu",
      static_cast<int>(function_name.length), function_name.data, words.count(),
      cword);

  execution_store().completion_function_running() = true;
  execution_store().should_mark_completion_directories() =
      should_mark_directories;
  execution_store().set_completion_command_name(command_name);
  defer { execution_store().completion_function_running() = false; };

  let defining_state = definition_state::from(runtime_state());
  if (let const *definition_info = body_storage.get_definition_info();
      definition_info != nullptr)
    defining_state = definition_info->defining_state;
  else
    defining_state.mood = mimic_mood::Bash;
  let const definition_scope = DefinitionStateScope{
      *this, defining_state, definition_state_exit::RestoreCaller};

  let const do_reset_array = [&](StringView name)
                                 throws -> ArrayList<String> & {
    if (is_readonly(name)) {
      throw Error{"Unable to assign '" + name + "' because it is read only"};
    }

    variable_store().shell_variables().erase(name);
    clear_sparse_array(name);
    let &storage = variable_store().indexed_arrays().get_or_create(
        name, ArrayList<String>{heap_allocator()});
    storage.clear();

    return storage;
  };

  let &comp_words = do_reset_array("COMP_WORDS");
  comp_words.reserve(words.count());
  for (let const &word : words)
    comp_words.push_managed(word.view());

  char number_buffer[32];
  set_shell_variable("COMP_CWORD", utils::int_to_text_into(
                                       static_cast<i64>(cword), number_buffer,
                                       sizeof(number_buffer)));
  set_shell_variable("COMP_LINE", line);
  set_shell_variable("COMP_POINT", utils::int_to_text_into(
                                       static_cast<i64>(point), number_buffer,
                                       sizeof(number_buffer)));
  if (!has_variable_name("COMP_WORDBREAKS")) {
    set_shell_variable("COMP_WORDBREAKS", StringView{" \t\n\"'><=;|&(:"});
  }

  do_reset_array("COMPREPLY");

  let call_params = ArrayList<String>{heap_allocator()};
  call_params.reserve(3);
  call_params.push(String{heap_allocator(), command_name});
  call_params.push(cword < words.count()
                       ? String{heap_allocator(), words[cword].view()}
                       : String{heap_allocator()});
  let const previous_index = cword > 0 ? cword - 1 : 0;
  call_params.push(previous_index < words.count()
                       ? String{heap_allocator(), words[previous_index].view()}
                       : String{heap_allocator()});

  let bash_argument_frame_context = BashArgumentFrameContext{};
  enter_bash_function_argument_frame(bash_argument_frame_context, call_params);
  defer { leave_bash_argument_frame(bash_argument_frame_context); };
  let saved_params = steal(variable_store().positional_params());
  variable_store().positional_params() = steal(call_params);
  defer { variable_store().positional_params() = steal(saved_params); };

  enter_function_call(SourceLocation{});
  defer { leave_function_call(); };
  let const saved_loop_depth = execution_store().loop_depth();
  execution_store().loop_depth() = 0;
  defer { execution_store().loop_depth() = saved_loop_depth; };
  enter_function_scope();
  push_function_call_name(function_name, body_storage);
  defer
  {
    pop_function_call_name();
    leave_function_scope();
  };
  let const saved_terminal_exec = execution_store().terminal_exec_allowed();
  execution_store().terminal_exec_allowed() = false;
  defer { execution_store().terminal_exec_allowed() = saved_terminal_exec; };

  /* A completion function that errors must not abort the prompt, so any error
     is swallowed and a stray break or return is consumed. */
  let was_interrupted = false;
  try {
    body->evaluate(*this);
  } catch (const InterruptErrorWithLocation &) {
    /* The throw already consumed the interrupt request on its way out, so it is
       raised again for the caller. Without it the editor would see a settled
       flag and offer whatever the function had filled in before the user asked
       it to stop. */
    was_interrupted = true;
    os::INTERRUPT_REQUESTED = 1;
    LOG(Debug, "completion function '%.*s' was interrupted",
        static_cast<int>(function_name.length), function_name.data);
  } catch (const ErrorBase &error) {
    LOG(Debug, "completion function '%.*s' threw: %s",
        static_cast<int>(function_name.length), function_name.data,
        error.message().c_str());
  }
  /* The return status is read before the control flow is cleared, so a dynamic
     loader that returns 124 to request a retry is seen by the caller. */
  if (out_exit_status != nullptr)
    *out_exit_status = execution_store().last_exit_status();
  if (control_flow_store().has_pending()) control_flow_store().clear();

  let result = ArrayList<String>{heap_allocator()};
  if (let reply = variable_store().indexed_arrays().find("COMPREPLY");
      !was_interrupted && reply.has_value())
  {
    result = steal(*reply.value());
  }
  LOG(Info, "completion function '%.*s' returned %zu candidates with status %d",
      static_cast<int>(function_name.length), function_name.data,
      result.count(),
      out_exit_status != nullptr ? *out_exit_status
                                 : execution_store().last_exit_status());
  return result;
}

} /* namespace koshka */
