/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements command, process, redirect, and function substitution.
 * It captures output, manages pipes and children, preserves nested source
 * frames, isolates evaluator state when required, and cleans up outstanding
 * substitutions. The split confines process lifetimes and stream handling
 * outside the word-expansion coordinator.
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
#include "base/Debug.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"

namespace koshka {

static fn mark_substitution_frames_printed(SourceStore &store) wontthrow -> void
{
  for (let &frame : store.source_frames()) {
    if (!frame.is_source_changing) frame.was_printed = true;
  }
}

fn EvalContext::render_contained_substitution_error(
    const std::exception_ptr &error, StringView source) throws -> void
{
  try {
    std::rethrow_exception(error);
  } catch (ErrorWithLocationAndDetails &e) {
    if (e.was_rendered()) return;
    show_message(e.to_string(source, this));
    show_message(e.details_to_string(source, this));
    print_source_backtrace(e.location());
    e.set_rendered();
  } catch (ErrorWithLocation &e) {
    if (e.was_rendered()) return;
    show_message(e.to_string(source, this));
    print_source_backtrace(e.location());
    e.set_rendered();
  } catch (const Error &e) {
    show_message(e.to_string());
    print_source_backtrace();
  }
}

fn EvalContext::register_embedded_source(StringView inner,
                                         const SourceLocation &parent_location,
                                         const String *body) throws -> bool
{
  let const *parent = source_store().current_source();
  Maybe<usize> inner_offset = None;
  let const span = parent != nullptr && parent_location.length != 0
                       ? source_text_in_span(parent_location, 0)
                       : StringView{};
  if (!span.is_empty() && !inner.is_empty()) {
    static constexpr usize PREFIX_LENGTHS[] = {2, 1, 0};
    for (let const prefix_length : PREFIX_LENGTHS) {
      if (span.length >= prefix_length &&
          span.substring(prefix_length).starts_with(inner))
      {
        inner_offset = prefix_length;
        break;
      }
    }
  }
  if (!inner_offset.has_value() && body == nullptr) {
    return false;
  }

  source_store().push_embedded_source(embedded_source{
      inner_offset.has_value() ? inner : StringView{}, parent, parent_location,
      inner_offset.has_value() ? *inner_offset : 0, body,
      function_store().call_frames().count(),
      source_depth_floor(source_store().source_frames().count()),
      inner_offset.has_value()});
  return true;
}

fn EvalContext::unregister_embedded_source() wontthrow -> void
{
  source_store().pop_embedded_source();
}

SubstitutionFrame::~SubstitutionFrame()
{
  if (m_did_register_embedded) m_context.unregister_embedded_source();
  pop_source_frame();
}

fn SubstitutionFrame::push_source_frame(const WordSegment &segment,
                                        StringView origin) throws -> void
{
  ASSERT(!m_did_push_source_frame);
  m_did_push_source_frame =
      m_context.push_substitution_source_frame(segment, origin);
}

fn SubstitutionFrame::push_source_frame(const SourceLocation &location,
                                        StringView origin) throws -> void
{
  ASSERT(!m_did_push_source_frame);
  m_did_push_source_frame =
      m_context.push_substitution_source_frame(location, origin);
}

fn SubstitutionFrame::register_embedded(StringView inner,
                                        const SourceLocation &parent_location,
                                        const String *body) throws -> void
{
  ASSERT(!m_did_register_embedded);
  m_did_register_embedded =
      m_context.register_embedded_source(inner, parent_location, body);
}

fn SubstitutionFrame::pop_source_frame() wontthrow -> void
{
  if (!m_did_push_source_frame) return;

  m_context.source_store().source_frames().pop_back();
  m_did_push_source_frame = false;
}

fn EvalContext::map_embedded_site(StringView &rendered_source,
                                  SourceLocation &location) const wontthrow
    -> const String *
{
  const String *mapped_parent = nullptr;
  usize remaining_steps = source_store().embedded_sources().count();
  while (remaining_steps-- > 0) {
    const embedded_source *match = nullptr;
    for (usize index = source_store().embedded_sources().count(); index > 0;
         index--)
    {
      let const &candidate = source_store().embedded_sources()[index - 1];
      if (candidate.is_mapped && candidate.text.data == rendered_source.data &&
          candidate.text.length == rendered_source.length)
      {
        match = &candidate;
        break;
      }
    }
    if (match == nullptr) break;

    location.position =
        static_cast<u32>(match->parent_location.position + match->inner_offset +
                         location.position);
    location.source_name_index = match->parent_location.source_name_index;
    mapped_parent = match->parent;
    rendered_source = match->parent->view();
  }

  return mapped_parent;
}

pure fn EvalContext::embedded_source_name_index() const wontthrow -> Maybe<u32>
{
  const embedded_source *entry = nullptr;
  for (let const &candidate : source_store().embedded_sources()) {
    if (candidate.is_mapped) entry = &candidate;
  }
  if (entry == nullptr) return None;

  for (usize step = 0; step < source_store().embedded_sources().count(); step++)
  {
    const embedded_source *outer = nullptr;
    for (let const &candidate : source_store().embedded_sources()) {
      if (candidate.is_mapped &&
          candidate.text.data == entry->parent->view().data &&
          candidate.text.length == entry->parent->view().length)
      {
        outer = &candidate;
      }
    }
    if (outer == nullptr) break;

    entry = outer;
  }

  return entry->parent_location.source_name_index;
}

static constexpr usize DRAIN_CHUNK_LENGTH = 4096;

struct command_substitution_drain_context
{
  char *data;
  usize length;
  usize capacity;
  os::descriptor read_fd;
};

static fn drain_command_substitution_pipe(opaque *raw_context) wontthrow -> void
{
  let drain = static_cast<command_substitution_drain_context *>(raw_context);
  loop
  {
    if (drain->length + DRAIN_CHUNK_LENGTH > drain->capacity) {
      usize grown_capacity =
          drain->capacity == 0 ? DRAIN_CHUNK_LENGTH * 2 : drain->capacity * 2;
      while (grown_capacity < drain->length + DRAIN_CHUNK_LENGTH)
        grown_capacity *= 2;

      let const allocator = uncached_heap_allocator();
      try {
        drain->data = static_cast<char *>(allocator.raw_realloc(
            drain->data, drain->capacity, grown_capacity, alignof(char)));
      } catch (...) {
        break;
      }
      drain->capacity = grown_capacity;
    }

    let const bytes_read = os::read_fd(
        drain->read_fd, drain->data + drain->length, DRAIN_CHUNK_LENGTH);
    if (!bytes_read.has_value() || *bytes_read == 0) {
      break;
    }
    drain->length += static_cast<usize>(*bytes_read);
  }
}

fn EvalContext::read_redirect_substitution(StringView source) throws
    -> Maybe<String>
{
  usize i = 0;
  while (i < source.length &&
         (source[i] == ' ' || source[i] == '\t' || source[i] == '\n'))
    i++;
  if (i >= source.length || source[i] != '<') {
    return None;
  }
  i++;

  if (arena_store().parse_arena() == nullptr) return None;
  let const ast_mark = arena_store().parse_arena()->mark();
  defer { arena_store().parse_arena()->release(ast_mark); };
  let lexer =
      Lexer{source.substring_of_length(i, source.length - i),
            *arena_store().parse_arena(), None, runtime_state().get_mood()};
  Token *name = lexer.next_shell_token();
  if (name == nullptr || name->kind() != Token::Kind::Word) {
    return None;
  }
  /* Anything after the single filename means this is not the bare read form. */
  Token *after = lexer.next_shell_token();
  if (after != nullptr && after->kind() != Token::Kind::EndOfFile &&
      after->kind() != Token::Kind::Newline)
  {
    return None;
  }

  let const filename = expand_word_for_assignment(
      static_cast<const tokens::WordToken *>(name)->word());
  LOG(Debug, "the substitution is a bare file read of '%s'", filename.c_str());
  let content = Path{filename.view()}.read_entire_file();
  if (!content.has_value()) {
    LOG(Debug, "the file read substitution of '%s' failed, expanding to empty",
        filename.c_str());
    return String{heap_allocator()};
  }
  let result = steal(*content);
  result.strip_trailing_newlines();
  return result;
}

fn EvalContext::capture_command_substitution(
    const String &source, Maybe<StringView> filename,
    const SourceLocation *call_site) throws -> String
{
  LOG(Debug, "capturing a command substitution of %zu bytes", source.count());
  if (Maybe<String> file = read_redirect_substitution(source.view());
      file.has_value())
    return steal(*file);

  /* A caller such as the make $(shell) names a filename, so an error inside the
     command carets that source rather than a bare unnamed line. */
  if (arena_store().parse_arena() == nullptr)
    throw Error{"Command substitution outside of a parse"};
  let const ast_mark = arena_store().parse_arena()->mark();
  defer { arena_store().parse_arena()->release(ast_mark); };

  enter_substitution();
  defer { leave_substitution(); };

  let normalized_source = source.clone();
  normalized_source.normalize_crlf_line_endings();

  let frame = SubstitutionFrame{*this};
  if (call_site != nullptr) {
    frame.push_source_frame(*call_site, StringView{"command substitution"});
    frame.register_embedded(normalized_source.view(), *call_site);
  }
  let parser = Parser{
      Lexer{normalized_source.view(), *arena_store().parse_arena(),
            steal(filename), runtime_state().get_mood()}
  };
  const Expression *ast;
  try {
    ast = parser.construct_ast();
  } catch (ErrorWithLocation &error) {
    frame.pop_source_frame();
    mark_substitution_frames_printed(source_store());
    render_contained_substitution_error(std::current_exception(),
                                        normalized_source.view());
    error.set_rendered();
    throw;
  } catch (...) {
    frame.pop_source_frame();
    mark_substitution_frames_printed(source_store());
    render_contained_substitution_error(std::current_exception(),
                                        normalized_source.view());
    throw;
  }
  ASSERT(ast != nullptr);

  return run_captured_substitution(ast, normalized_source,
                                   call_site != nullptr
                                       ? Maybe<SourceLocation>{*call_site}
                                       : Maybe<SourceLocation>{None});
}

fn EvalContext::setup_process_substitution(const WordSegment &segment) throws
    -> String
{
  return setup_process_substitution(
      segment.text.view(),
      segment.get_source_location(
          source_store().current_location().source_name_index));
}

fn EvalContext::setup_process_substitution(
    StringView text, Maybe<SourceLocation> segment_location) throws -> String
{
  if (arena_store().parse_arena() == nullptr)
    throw Error{"Process substitution outside of a parse"};
  ASSERT(!text.is_empty());

  /* The first byte is the direction marker the lexer wrote. */
  let const direction = text[0];
  let const command_writes_the_pipe = direction == '<';
  LOG(Debug, "setting up a process substitution where the command %s the pipe",
      command_writes_the_pipe ? "writes" : "reads");

  let const ast_mark = arena_store().parse_arena()->mark();
  defer { arena_store().parse_arena()->release(ast_mark); };
  let const substitution_source = String{heap_allocator(), text.substring(1)};
  let frame = SubstitutionFrame{*this};
  if (segment_location.has_value()) {
    frame.push_source_frame(*segment_location,
                            StringView{"process substitution"});
    frame.register_embedded(substitution_source.view(), *segment_location,
                            &substitution_source);
  }
  let parser = Parser{
      Lexer{substitution_source.view(), *arena_store().parse_arena(), None,
            runtime_state().get_mood()}
  };
  const Expression *ast;
  try {
    ast = parser.construct_ast();
  } catch (...) {
    render_contained_substitution_error(std::current_exception(),
                                        substitution_source.view());
    throw;
  }
  ASSERT(ast != nullptr);

  let bootstrap = os::subshell_bootstrap{};
  let const do_launch = [&]() throws -> os::process_substitution_launch {
    try {
      let const evaluator = make_child_evaluator_state(bootstrap);
      set_child_source_origin(bootstrap, substitution_source.view(),
                              SourceLocation{});
      return os::launch_process_substitution(os::process_substitution_options{
          .source = substitution_source.view(),
          .should_trace_sources = diagnostics_store().source_traces_enabled(),
          .evaluator = evaluator,
          .direction = command_writes_the_pipe
                           ? os::process_substitution_direction::CommandWrites
                           : os::process_substitution_direction::CommandReads});
    } catch (const ErrorBase &error) {
      if (!segment_location.has_value() ||
          source_store().current_source() == nullptr)
      {
        throw;
      }

      try {
        relocate_error(error, *segment_location);
      } catch (...) {
        render_contained_substitution_error(
            std::current_exception(), source_store().current_source()->view());
        throw;
      }
    }
  };

  let launch = do_launch();
  if (launch.should_evaluate_child) {
    if (launch.child_close_fd.has_value()) os::close_fd(*launch.child_close_fd);
    enter_subshell();
    hide_coprocess_descriptors();
    i32 status = 0;
    let const source_scope = enter_source_scope(&substitution_source,
                                                String{"process substitution"});
    try {
      ast->evaluate(*this);
      status = execution_store().last_exit_status();
    } catch (...) {
      LOG(Debug,
          "the process substitution child swallowed an error, exiting with "
          "status 1");
      render_contained_substitution_error(std::current_exception(),
                                          substitution_source.view());
      status = 1;
    }
    os::exit_process_immediately(status);
  }

  ASSERT(launch.retained_fd.has_value());
  ASSERT(launch.child != KOSH_INVALID_PROCESS);
  let const location = source_store().current_location();
  let const source = source_store().current_source() != nullptr
                         ? source_store().current_source()->view()
                         : StringView{};
  let const process_id = os::process_id_of(launch.child);
  expansion_store().pending_process_substitutions().push(
      process_substitution{*launch.retained_fd, launch.child, process_id,
                           launch.cleanup, location, source});
  job_table_store().set_last_background_pid(process_id);

  LOG(Debug, "the process substitution is reachable at '%s'",
      launch.path.c_str());
  return steal(launch.path);
}

fn EvalContext::mark_process_substitutions() const wontthrow
    -> process_substitution_mark
{
  return {expansion_store().pending_process_substitutions().count()};
}

fn EvalContext::hold_process_substitutions(
    process_substitution_mark mark) wontthrow -> void
{
  let &pending = expansion_store().pending_process_substitutions();
  let &held = expansion_store().held_process_substitutions();
  for (usize i = mark.pending; i < pending.count(); i++) {
    process_substitution &sub = pending[i];
    if (sub.shell_fd != KOSH_INVALID_FD) os::close_fd(sub.shell_fd);
    sub.shell_fd = KOSH_INVALID_FD;
    sub.source = StringView{};
    sub.location = SourceLocation{};
    try {
      held.push(sub);
    } catch (...) {
      LOG(Debug, "holding a process substitution failed, it is left unreaped");
    }
  }

  while (pending.count() > mark.pending)
    pending.remove(pending.count() - 1);
}

fn EvalContext::release_finished_held_process_substitutions() wontthrow -> void
{
  let &held = expansion_store().held_process_substitutions();
  for (usize i = held.count(); i > 0; i--) {
    process_substitution &sub = held[i - 1];
    if (sub.child != KOSH_INVALID_PROCESS) {
      i32 status = 0;
      if (os::poll_process(sub.child, status) != os::process_state::Exited)
        continue;
      sub.child = KOSH_INVALID_PROCESS;
      job_table_store().remember_finished_status(sub.process_id, status);
    }

    if (!os::release_finished_process_substitution(sub.platform_cleanup))
      continue;

    held.remove(i - 1);
  }
}

fn EvalContext::wait_for_process_substitution(i64 process_id) wontthrow
    -> Maybe<i32>
{
  let const do_reap_matching = [&](process_substitution &sub)
                                   wontthrow -> bool {
    if (sub.process_id != process_id || sub.child == KOSH_INVALID_PROCESS) {
      return false;
    }

    i32 status = 127;
    try {
      status = os::reap_process_quietly(sub.child);
    } catch (...) {
      LOG(Debug, "waiting for a process substitution failed");
    }
    sub.child = KOSH_INVALID_PROCESS;
    job_table_store().remember_finished_status(process_id, status);

    return true;
  };

  bool was_pending = false;
  for (process_substitution &sub :
       expansion_store().pending_process_substitutions())
  {
    if (do_reap_matching(sub)) {
      was_pending = true;
      break;
    }
  }

  let &held = expansion_store().held_process_substitutions();
  for (usize i = held.count(); !was_pending && i > 0; i--) {
    if (!do_reap_matching(held[i - 1])) continue;

    if (os::release_finished_process_substitution(held[i - 1].platform_cleanup))
      held.remove(i - 1);

    break;
  }

  return job_table_store().find_finished_status(process_id);
}

fn EvalContext::cleanup_process_substitutions(
    process_substitution_mark mark) wontthrow -> void
{
  if (!expansion_store().held_process_substitutions().is_empty())
    release_finished_held_process_substitutions();

  LOG(Debug, "cleaning up %zu pending process substitutions",
      expansion_store().pending_process_substitutions().count() - mark.pending);
  for (usize i = mark.pending;
       i < expansion_store().pending_process_substitutions().count(); i++)
  {
    process_substitution &sub =
        expansion_store().pending_process_substitutions()[i];
    os::finish_process_substitution(sub.platform_cleanup);
    /* Closing the shell end first sends SIGPIPE to a producer that still has
       output queued, so it ends rather than blocking the wait below. */
    if (sub.shell_fd != KOSH_INVALID_FD) os::close_fd(sub.shell_fd);
    if (sub.child == KOSH_INVALID_PROCESS) continue;

    try {
      job_table_store().remember_finished_status(
          sub.process_id, os::reap_process_quietly(sub.child));
    } catch (const Error &e) {
      LOG(Debug, "a process substitution reap failed and was swallowed: %s",
          e.message().c_str());
      /* bash stays silent here, so the warning is suppressed in bash mode. */
      if (!runtime_state().is_bash_compatible()) {
        try {
          let const text =
              "A process substitution child could not be reaped. " +
              e.message();
          show_message(sub.source.is_empty()
                           ? Warning{text}.to_string()
                           : WarningWithLocation{sub.location, text}.to_string(
                                 sub.source, this));
        } catch (...) {
          LOG(Debug, "showing the reap warning failed, the error is swallowed");
        }
      }
    } catch (...) {
      LOG(Debug, "a process substitution reap failed with an unknown error, "
                 "swallowed");
      if (!runtime_state().is_bash_compatible()) {
        try {
          let const text =
              StringView{"A process substitution child could not be reaped."};
          show_message(sub.source.is_empty()
                           ? Warning{text}.to_string()
                           : WarningWithLocation{sub.location, text}.to_string(
                                 sub.source, this));
        } catch (...) {
          LOG(Debug, "showing the fallback reap warning failed, the error is "
                     "swallowed");
        }
      }
    }
  }
  while (expansion_store().pending_process_substitutions().count() >
         mark.pending)
    expansion_store().pending_process_substitutions().remove(
        expansion_store().pending_process_substitutions().count() - 1);
}

fn EvalContext::capture_command_substitution(const WordSegment &segment) throws
    -> String
{
  if (Maybe<String> file = read_redirect_substitution(segment.text.view());
      file.has_value())
    return steal(*file);

  if (arena_store().parse_arena() == nullptr)
    throw Error{"Command substitution outside of a parse"};

  enter_substitution();
  defer { leave_substitution(); };

  let cache_arena = segment.is_substitution_cache_in_function_arena
                        ? arena_store().function_arena()
                        : arena_store().parse_arena();
  ASSERT(cache_arena != nullptr);
  let frame = SubstitutionFrame{*this};
  frame.push_source_frame(segment, StringView{"command substitution"});
  let &cache = segment.get_eval_cache(cache_arena);
  if (cache.substitution_ast == nullptr ||
      !cache_arena->is_lifetime_valid(cache.substitution_lifetime))
  {
    LOG(Debug, "command substitution ast cache miss, reparsing");
    let const segment_location = segment.get_source_location(
        source_store().current_location().source_name_index);
    let reparse_frame = SubstitutionFrame{*this};
    if (segment_location.has_value())
      reparse_frame.register_embedded(segment.text.view(), *segment_location);
    let const allocation_kind = segment.is_substitution_cache_in_function_arena
                                    ? ParseSession::AllocationKind::FunctionBody
                                    : ParseSession::AllocationKind::Syntax;
    let parser = Parser{
        Lexer{segment.text.view(), *cache_arena, None,
              runtime_state().get_mood(), allocation_kind}
    };
    try {
      cache.substitution_ast = parser.construct_ast();
    } catch (ErrorWithLocation &error) {
      frame.pop_source_frame();
      mark_substitution_frames_printed(source_store());
      render_contained_substitution_error(std::current_exception(),
                                          segment.text.view());
      error.set_rendered();
      throw;
    } catch (...) {
      frame.pop_source_frame();
      mark_substitution_frames_printed(source_store());
      render_contained_substitution_error(std::current_exception(),
                                          segment.text.view());
      throw;
    }
    cache.substitution_lifetime = cache_arena->register_lifetime();
  }
  ASSERT(cache.substitution_ast != nullptr);

  return run_captured_substitution(
      cache.substitution_ast, String{heap_allocator(), segment.text.view()},
      segment.get_source_location(
          source_store().current_location().source_name_index));
}

fn EvalContext::push_substitution_source_frame(const WordSegment &segment,
                                               StringView origin) throws -> bool
{
  let const location = segment.get_source_location(
      source_store().current_location().source_name_index);
  if (!location.has_value()) return false;
  return push_substitution_source_frame(*location, origin);
}

fn EvalContext::push_substitution_source_frame(const SourceLocation &location,
                                               StringView origin) throws -> bool
{
  if (!diagnostics_store().source_traces_enabled() ||
      source_store().current_source() == nullptr || location.length == 0)
  {
    return false;
  }

  source_store().source_frames().push(source_frame{
      String{heap_allocator(), origin},
      location,
      source_store().current_source(),
      source_generation_for(source_store().current_source()),
      String{heap_allocator()},
      source_frame_kind::Ordinary
  });
  source_store().source_frames().back().function_call_depth =
      function_store().call_frames().count();
  source_store().source_frames().back().is_source_changing = false;
  return true;
}

fn EvalContext::run_captured_substitution(
    const Expression *ast, const String &source,
    Maybe<SourceLocation> call_site) throws -> String
{
  ASSERT(ast != nullptr);
  LOG(Debug, "running a captured substitution body of %zu bytes",
      source.count());

  /* The inner scratch is reclaimed at the substitution boundary, so a $(...)
     inside a loop does not grow the arena across iterations. The captured
     output is heap and escapes. */
  let const substitution_mark = expansion_store().scratch_arena().mark();
  defer { expansion_store().scratch_arena().release(substitution_mark); };

  let frame = SubstitutionFrame{*this};
  if (call_site.has_value())
    frame.register_embedded(source.view(), *call_site, &source);
  let const source_scope =
      enter_source_scope(&source, String{source_store().current_origin()});
  let const previous_source = source_scope.get_source();
  let const previous_location = source_scope.get_location();

  Maybe<eval_state_snapshot> in_process_snapshot;
  let active_functions = HashSet{scratch_allocator()};
  bool should_evaluate_in_process =
      !execution_store().shell_is_interactive() &&
      expansion_store().substitution_depth() <= 16 &&
      trap_store().count() == 0 &&
      ast->can_evaluate_in_process_substitution(*this, active_functions);
  if (should_evaluate_in_process) {
    try {
      in_process_snapshot = snapshot_state();
    } catch (const Error &) {
      should_evaluate_in_process = false;
    }
  }
  if (!should_evaluate_in_process && !os::can_fork_evaluator()) {
    in_process_snapshot = snapshot_state();
    should_evaluate_in_process = true;
  }
  if (!should_evaluate_in_process) {
    LOG(Debug, "running the captured substitution in a child process");
    prepare_child_environment();
    let const pipe = os::make_pipe();
    if (!pipe)
      throw ErrorWithLocation{previous_location,
                              "Could not open a pipe for command substitution"};
    bool was_pipe_handed_off = false;
    defer
    {
      if (!was_pipe_handed_off) {
        os::close_fd(pipe->in);
        os::close_fd(pipe->out);
      }
    };

    koshka::flush();
    let const forked_child =
        os::try_fork_compound_stage(os::fork_compound_stage_options{
            .out_fd = pipe->out,
            .location = previous_location,
            .diagnostic_source = previous_source != nullptr
                                     ? previous_source->view()
                                     : StringView{}});

    if (!forked_child.has_value()) {
      os::close_fd(pipe->in);
      os::close_fd(pipe->out);
      was_pipe_handed_off = true;
      in_process_snapshot = snapshot_state();
    } else {
      let const child = *forked_child;
      was_pipe_handed_off = true;
      if (child == 0) {
        os::close_fd(pipe->in);
        execution_store().set_shell_is_interactive(false);
        enter_subshell();
        hide_coprocess_descriptors();
        if (runtime_state().get_mood() == mimic_mood::Bash &&
            !is_shopt_enabled("inherit_errexit"))
        {
          runtime_state().set_error_exit(false);
        }
        clear_inherited_exit_trap();
        reset_inherited_signal_traps();
        execution_store().allow_terminal_exec_at_current_depth();
        std::exception_ptr error;
        try {
          ast->evaluate(*this);
        } catch (...) {
          error = std::current_exception();
        }
        if (control_flow_store().has_pending()) {
          if (control_flow_store().pending().kind == control_flow::Kind::Exit)
            execution_store().set_last_exit_status(
                static_cast<i32>(control_flow_store().pending().value));
          control_flow_store().clear();
        }
        if (!error) {
          try {
            /* A status the action exits with lands in the exit status the
               child process below reports. */
            unused(run_subshell_exit_trap());
          } catch (...) {
            error = std::current_exception();
          }
        }
        if (error) {
          render_contained_substitution_error(error, source.view());
          execution_store().set_last_exit_status(1);
        }
        koshka::flush();
        os::exit_process_immediately(execution_store().last_exit_status());
      }

      os::close_fd(pipe->out);
      bool is_input_open = true;
      bool has_reaped_child = false;
      defer
      {
        if (is_input_open) os::close_fd(pipe->in);
        if (!has_reaped_child) {
          unused(os::signal_process(child, 9));
          os::reap_process_quietly(child);
        }
      };

      let captured = os::read_fd_to_string(pipe->in, heap_allocator());
      let const was_read_interrupted =
          !captured.has_value() && os::INTERRUPT_REQUESTED;
      os::close_fd(pipe->in);
      is_input_open = false;
      if (was_read_interrupted) {
        unused(os::signal_process(child, 2));
        os::reap_process_quietly(child);
        has_reaped_child = true;
        os::INTERRUPT_REQUESTED = 0;
        throw InterruptErrorWithLocation{previous_location};
      }

      let was_stopped = false;
      let const status = os::wait_and_monitor_process(child, &was_stopped);
      has_reaped_child = true;
      unused(was_stopped);
      if (os::INTERRUPT_REQUESTED) {
        os::INTERRUPT_REQUESTED = 0;
        throw InterruptErrorWithLocation{previous_location};
      }
      execution_store().set_last_exit_status(status);
      if (!captured.has_value())
        throw ErrorWithLocation{previous_location,
                                "Could not read command substitution output"};
      captured->strip_trailing_newlines();
      return steal(*captured);
    }
  }

  LOG(Debug, "running the captured substitution in process");
  ASSERT(in_process_snapshot.has_value());
  let snapshot = steal(*in_process_snapshot);
  bool did_begin_restoration = false;
  let const do_evaluate_in_process = [&]() throws -> String {
    let const pipe = os::make_pipe();
    if (!pipe) throw Error{"Could not open a pipe for command substitution"};
    bool is_pipe_input_open = true;
    bool is_pipe_output_open = true;
    defer
    {
      if (is_pipe_output_open) os::close_fd(pipe->out);
      if (is_pipe_input_open) os::close_fd(pipe->in);
    };

    let captured = String{heap_allocator()};
    let drain_context =
        command_substitution_drain_context{nullptr, 0, 0, pipe->in};
    let const reader =
        os::start_thread(drain_command_substitution_pipe, &drain_context);
    if (!reader) {
      os::close_fd(pipe->in);
      is_pipe_input_open = false;
      os::close_fd(pipe->out);
      is_pipe_output_open = false;
      throw Error{"Could not start a thread for command substitution"};
    }

    bool is_reader_running = true;
    bool is_stdout_redirected = false;
    bool did_change_interactive_state = false;
    bool did_enter_subshell = false;
    os::descriptor saved_stdout = KOSH_INVALID_FD;
    let const was_interactive = execution_store().shell_is_interactive();
    let const do_cleanup = [&]() wontthrow -> void {
      if (did_enter_subshell) {
        leave_subshell();
        did_enter_subshell = false;
      }
      if (did_change_interactive_state) {
        execution_store().set_shell_is_interactive(was_interactive);
        did_change_interactive_state = false;
      }
      if (is_stdout_redirected) {
        koshka::flush();
        os::restore_stdout(saved_stdout);
        is_stdout_redirected = false;
      }
      if (is_pipe_output_open) {
        os::close_fd(pipe->out);
        is_pipe_output_open = false;
      }
      if (is_reader_running) {
        os::join_thread(*reader);
        is_reader_running = false;
      }
      if (is_pipe_input_open) {
        os::close_fd(pipe->in);
        is_pipe_input_open = false;
      }
    };
    defer
    {
      do_cleanup();
      uncached_heap_allocator().free_array(drain_context.data,
                                           drain_context.capacity);
    };

    koshka::flush();
    saved_stdout = os::redirect_stdout(pipe->out);
    is_stdout_redirected = true;

    execution_store().set_shell_is_interactive(false);
    did_change_interactive_state = true;

    /* A break, continue, return, or exit inside a substitution acts only within
       it and must not escape into the enclosing loop, function, or shell. */
    enter_subshell();
    did_enter_subshell = true;
    hide_coprocess_descriptors();
    if (runtime_state().get_mood() == mimic_mood::Bash &&
        !is_shopt_enabled("inherit_errexit"))
    {
      runtime_state().set_error_exit(false);
    }
    clear_inherited_exit_trap();
    reset_inherited_signal_traps();
    std::exception_ptr error;
    try {
      ast->evaluate(*this);
    } catch (...) {
      error = std::current_exception();
    }
    if (control_flow_store().has_pending()) {
      if (control_flow_store().pending().kind == control_flow::Kind::Exit)
        execution_store().set_last_exit_status(
            static_cast<i32>(control_flow_store().pending().value));
      control_flow_store().clear();
    }
    /* The substitution's own EXIT action runs while stdout still points at the
       pipe. Its output joins the captured value. A status the action exits
       with stays in the exit status the substitution reports. */
    if (!error) {
      try {
        unused(run_subshell_exit_trap());
      } catch (...) {
        error = std::current_exception();
      }
    }
    if (error) render_contained_substitution_error(error, source.view());
    do_cleanup();

    if (drain_context.data != nullptr) {
      captured.append(StringView{drain_context.data, drain_context.length});
      uncached_heap_allocator().free_array(drain_context.data,
                                           drain_context.capacity);
      drain_context.data = nullptr;
    }

    did_begin_restoration = true;
    restore_state(steal(snapshot));
    note_subshell_child_exit();

    if (error) {
      /* A throw inside the substitution is contained to its subshell the way
         bash holds a fatal expansion error to the command substitution. */
      LOG(Debug, "the command substitution failed, containing the error with "
                 "status 1");
      execution_store().set_last_exit_status(1);
    }

    captured.strip_trailing_newlines();
    return captured;
  };

  try {
    return do_evaluate_in_process();
  } catch (...) {
    if (!did_begin_restoration) {
      let const error = std::current_exception();
      try {
        restore_state(steal(snapshot));
        note_subshell_child_exit();
      } catch (...) {
        LOG(Debug, "restoring an interrupted command substitution failed");
      }
      std::rethrow_exception(error);
    }
    throw;
  }
}

fn EvalContext::capture_function_substitution(const WordSegment &segment) throws
    -> String
{
  if (arena_store().parse_arena() == nullptr)
    throw Error{"Function substitution outside of a parse"};

  let cache_arena = segment.is_substitution_cache_in_function_arena
                        ? arena_store().function_arena()
                        : arena_store().parse_arena();
  ASSERT(cache_arena != nullptr);
  let frame = SubstitutionFrame{*this};
  frame.push_source_frame(segment, StringView{"function substitution"});
  let &cache = segment.get_eval_cache(cache_arena);
  if (cache.substitution_ast == nullptr ||
      !cache_arena->is_lifetime_valid(cache.substitution_lifetime))
  {
    LOG(Debug, "function substitution ast cache miss, reparsing");
    let const allocation_kind = segment.is_substitution_cache_in_function_arena
                                    ? ParseSession::AllocationKind::FunctionBody
                                    : ParseSession::AllocationKind::Syntax;
    let parser = Parser{
        Lexer{segment.text.view(), *cache_arena, None,
              runtime_state().get_mood(), allocation_kind}
    };
    try {
      cache.substitution_ast = parser.construct_ast();
    } catch (...) {
      render_contained_substitution_error(std::current_exception(),
                                          segment.text.view());
      throw;
    }
    cache.substitution_lifetime = cache_arena->register_lifetime();
  }
  ASSERT(cache.substitution_ast != nullptr);

  let const ast = cache.substitution_ast;
  /* The trace and LINENO paths hold the address of the running source for the
     whole body, so the segment text is materialized here. */
  let const source = String{heap_allocator(), segment.text.view()};
  LOG(Debug, "running a function substitution body of %zu bytes",
      source.count());

  /* The body runs against the live state, no snapshot and no subshell, so its
     assignments, cd, and definitions persist the way the bash 5.3 funsub
     leaves them. */
  let const source_scope =
      enter_source_scope(&source, String{"function substitution"});

  let const pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for function substitution"};

  let captured = String{heap_allocator()};
  let drain_context =
      command_substitution_drain_context{nullptr, 0, 0, pipe->in};
  let const reader =
      os::start_thread(drain_command_substitution_pipe, &drain_context);
  if (!reader) {
    os::close_fd(pipe->in);
    os::close_fd(pipe->out);
    throw Error{"Could not start a thread for function substitution"};
  }

  koshka::flush();
  let const saved = os::redirect_stdout(pipe->out);

  let const was_interactive = execution_store().shell_is_interactive();
  execution_store().set_shell_is_interactive(false);

  std::exception_ptr error;
  try {
    ast->evaluate(*this);
  } catch (...) {
    error = std::current_exception();
  }
  /* A break, continue, or return acts only within the body and is consumed
     here. An exit stays pending, so the shell ends after the surrounding
     command finishes, the way bash exits from a funsub. */
  if (control_flow_store().has_pending() &&
      control_flow_store().pending().kind != control_flow::Kind::Exit)
  {
    control_flow_store().clear();
  }

  execution_store().set_shell_is_interactive(was_interactive);

  koshka::flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  os::join_thread(*reader);
  os::close_fd(pipe->in);

  if (drain_context.data != nullptr) {
    captured.append(StringView{drain_context.data, drain_context.length});
    uncached_heap_allocator().free_array(drain_context.data,
                                         drain_context.capacity);
  }

  if (error) {
    LOG(Debug,
        "the function substitution failed, containing the error with status 1");
    render_contained_substitution_error(error, source.view());
    execution_store().set_last_exit_status(1);
  }

  captured.strip_trailing_newlines();
  return captured;
}

} /* namespace koshka */
