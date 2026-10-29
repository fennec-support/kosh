/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This private implementation header contains startup-file loading, streamed
 * analysis, formatting and lint application, debug drivers, script execution,
 * interactive history expansion and admission filtering, and prompt helpers.
 * Main.cpp includes it after defining invocation flags so those helpers and
 * flags remain in one translation unit.
 */

#pragma once

#include "ParserFormats.hpp"

namespace koshka {

/* the error is located at the file operand when the caller knows where the
   operand sits in the invocation. */
static fn
unrecognized_format_message(Maybe<StringView>     filename,
                            const SourceLocation *operand_location = nullptr,
                            StringView            invocation       = {},
                            EvalContext          *context          = nullptr) throws
    -> String
{
  let const message = "The format of `" +
                      (filename.has_value() ? *filename : StringView{"input"}) +
                      "` is not recognized";
  let note = String{heap_allocator(), "Recognized formats include "};

  usize supported_format_count = 0;

  for (usize kind_index = 0; kind_index < PARSER_FORMAT_KIND_COUNT;
       kind_index++)
  {
    let const name =
        parser_format_kind_name(static_cast<parser_format_kind>(kind_index));

    if (!name.is_empty()) supported_format_count++;
  }

  usize appended_format_count = 0;

  for (usize kind_index = 0; kind_index < PARSER_FORMAT_KIND_COUNT;
       kind_index++)
  {
    let const name =
        parser_format_kind_name(static_cast<parser_format_kind>(kind_index));

    if (name.is_empty()) continue;
    if (appended_format_count != 0)
      note.append(appended_format_count + 1 == supported_format_count ? ", and "
                                                                      : ", ");
    note.append(name);
    appended_format_count++;
  }

  if (operand_location != nullptr) {
    return ErrorWithLocationAndDetails{*operand_location, message.view(),
                                       note.view()}
        .to_string(invocation, context);
  }

  return ErrorWithDetails{message.view(), note.view()}.to_string();
}

fn
kosh_binary_flag_list() wontthrow -> const FlagList &
{
  return FLAG_LIST;
}

#if !defined NDEBUG
static fn
run_debug_completion_driver(StringView   driver_line,
                            EvalContext &context) throws -> i32
{
  context.program_resolver().initialize_path_map();

  let const driver_result = completion::complete(
      driver_line, driver_line.length, context, Path::current_directory(),
      nullptr, false, completion::completion_mode::Listing);
  let listing = String{heap_allocator()};

  for (let const &candidate : driver_result.candidates) {
    listing += candidate.view();
    listing += '\n';
  }

  print(listing);
  flush();

  return 0;
}

static fn
run_debug_highlight_driver(StringView   driver_line,
                           EvalContext &context) throws -> i32
{
  let const variable_name_visit_count_before =
      context.dynamic_runtime_store().debug_variable_name_enumeration_count();
  let const directory_read_count_before = utils::debug_directory_read_count();

  if (context.program_resolver().has_valid_command_names())
    context.program_resolver().revalidate_for_prompt();
  context.program_resolver().begin_explicit_completion(
      ProgramResolver::CompletionRefresh::Fresh);
  defer { context.program_resolver().end_explicit_completion(); };

  let const spans   = completion::highlight_line(driver_line, context);
  let       listing = String{heap_allocator()};

  for (let const &span : spans) {
    listing +=
        driver_line.substring_of_length(span.start, span.end - span.start);
    listing += '\t';
    listing += highlight_role_name(span.role);
    listing += '\n';
  }

  LOG(All, "highlighting visited %zu variables",
      context.dynamic_runtime_store().debug_variable_name_enumeration_count() -
          variable_name_visit_count_before);
  LOG(All, "highlighting read %zu directories",
      utils::debug_directory_read_count() - directory_read_count_before);
  if (LOGGER_VERBOSITY == verbosity::All)
    LOG(All, "the diagnostic highlight cache is stable %d",
        completion::debug_diagnostic_cache_is_stable(context));
  print(listing);
  flush();

  return 0;
}

/* prints the number of restart points the highlight scan of driver_text
   records and the number of spans that a scan from one of them colors
   differently from the scan from the start. */
static fn
run_debug_highlight_restart_driver(StringView   driver_text,
                                   EvalContext &context) throws -> i32
{
  let restarts = ArrayList<completion::highlight_restart>{heap_allocator()};
  let full     = ArrayList<highlight_span>{heap_allocator()};

  for (let const &span :
       completion::highlight_line_from(driver_text, nullptr, context, restarts))
    full.push(span);

  usize mismatch_count = 0;

  for (usize r = 0; r < restarts.count(); r++) {
    let ignored = ArrayList<completion::highlight_restart>{heap_allocator()};
    let const begin   = restarts[r].position;
    let const resumed = completion::highlight_line_from(
        driver_text, &restarts[r], context, ignored);
    usize full_index = 0;

    while (full_index < full.count() && full[full_index].start < begin) {
      full_index++;
    }

    if (full.count() - full_index != resumed.count()) {
      mismatch_count++;
      continue;
    }

    for (usize i = 0; i < resumed.count(); i++) {
      let const &expected = full[full_index + i];

      if (resumed[i].start != expected.start ||
          resumed[i].end != expected.end || resumed[i].role != expected.role)
      {
        mismatch_count++;
        break;
      }
    }
  }

  print("restart-points=" + String::from(restarts.count(), heap_allocator()) +
        " mismatches=" + String::from(mismatch_count, heap_allocator()) + "\n");
  flush();

  return mismatch_count == 0 ? 0 : 1;
}

static fn
run_debug_bracket_driver(StringView   driver_line,
                         EvalContext &context) throws -> i32
{
  let const spans   = completion::highlight_line(driver_line, context);
  let       listing = String{heap_allocator()};

  let const do_append_range = [&](usize start,
                                  usize end) throws -> void {
    listing += driver_line.substring_of_length(start, end - start);
    listing += '@';
    listing += String::from(start, heap_allocator());
  };

  for (usize cursor = 0; cursor <= driver_line.length; cursor++) {
    let const pair =
        completion::find_matching_bracket(driver_line, spans, cursor);

    if (!pair.has_value()) continue;

    listing += String::from(cursor, heap_allocator());
    listing += '\t';
    do_append_range(pair->open_start, pair->open_end);
    listing += '\t';
    do_append_range(pair->close_start, pair->close_end);
    listing += '\n';
  }

  print(listing);
  flush();

  return 0;
}

static fn
run_debug_hint_driver(StringView   driver_line,
                      EvalContext &context) throws -> i32
{
  let row = String{heap_allocator()};

  if (!completion::describe_syntax_problem(driver_line, driver_line.length,
                                           context.runtime_state().get_mood(),
                                           row) &&
      !completion::describe_analysis_finding(driver_line, context, row))
  {
    if (!completion::compose_command_hint(driver_line, driver_line.length,
                                          context, row))
    {
      row.clear();
    }
  }

  let printed = String{heap_allocator()};

  if (let const line_break = row.view().find_character('\n');
      line_break.has_value())
  {
    printed += '[';
    printed.append(row.view().substring_of_length(0, *line_break));
    printed.append(StringView{"] "});
    printed.append(row.view().substring(*line_break + 1));
  } else {
    printed.append(row.view());
  }

  if (printed.is_empty() || printed.view()[printed.length() - 1] != '\n') {
    printed += '\n';
  }
  print(printed);
  flush();

  return 0;
}

static fn
run_debug_ghost_driver(StringView   driver_line,
                       EvalContext &context) throws -> i32
{
  let const directory_stat_count_before = utils::debug_directory_stat_count();
  let const directory_read_count_before = utils::debug_directory_read_count();

  context.program_resolver().initialize_path_map();

  let const result = completion::complete(
      driver_line, driver_line.length, context, Path::current_directory(),
      nullptr, false, completion::completion_mode::Ghost);

  print("count=" + String::from(result.candidate_count, heap_allocator()) +
        "\nprefix=" + result.longest_common_prefix.view() + "\nsource-scans=" +
        String::from(result.source_candidate_scan_count, heap_allocator()) +
        "\nmaterialized=" +
        String::from(result.materialized_candidate_count, heap_allocator()) +
        "\ndirectory-stats=" +
        String::from(utils::debug_directory_stat_count() -
                         directory_stat_count_before,
                     heap_allocator()) +
        "\ndirectory-reads=" +
        String::from(utils::debug_directory_read_count() -
                         directory_read_count_before,
                     heap_allocator()) +
        "\n");
  flush();

  return 0;
}
#endif

pure static fn
resolve_session_mood(mimic_mood invocation_mood) wontthrow -> mimic_mood
{
  if (FLAG_MOOD.is_set())
    return parse_mood_name(FLAG_MOOD.value()).value_or(mimic_mood::Default);

  if (FLAG_DUMB.is_enabled()) return mimic_mood::Posix;
  if (FLAG_POSIX_COMPAT.is_enabled()) return mimic_mood::BashPosix;

  return invocation_mood;
}

pure static fn
resolve_session_tab_selector() wontthrow -> tab_selector_mode
{
  if (FLAG_TAB_SELECTOR.is_set()) {
    return parse_tab_selector_name(FLAG_TAB_SELECTOR.value())
        .value_or(tab_selector_mode::Interactive);
  }

  if (FLAG_DUMB.is_enabled()) return tab_selector_mode::Plain;

  return tab_selector_mode::Interactive;
}

/* memory: row is a heap String freed on return. listing grows by the wrapped
           row.
   time:   O(summary length) for the copy, plus one
           wrap_text() call on row. called once per diagnostic from the loops
           in print_help_or_version_status(). */
static fn
append_listed_diagnostic(String                      &listing,
                         const diagnostic_definition &definition) throws
    -> void
{
  let row = String{heap_allocator()};

  if (definition.shellcheck_code.has_value()) {
    char code_text[32];
    row += "SC";
    row += utils::int_to_text_into(*definition.shellcheck_code, code_text,
                                   sizeof(code_text));
    row += ": ";
  }

  row += definition.slug;
  row += " (";
  row += get_diagnostic_tier_name(definition.tier);
  row += "): ";

  let const summary = StringView{definition.summary};

  if (!summary.is_empty() && summary[0] >= 'a' && summary[0] <= 'z') {
    row += static_cast<char>(summary[0] - 'a' + 'A');
    row += summary.substring(1);
  } else {
    row += summary;
  }
  listing +=
      wrap_text(row.view(), HELP_INDENT, HELP_WRAP_WIDTH, HELP_INDENT + 2);
  listing += '\n';
}

static fn
print_help_or_version_status(const String &program_path) -> Maybe<int>
{
  if (FLAG_HELP.is_enabled()) {
    let h = String{heap_allocator()};

    h += "KOSHKA";
    h += "\n";
    h += wrap_text("Koshka is a fast and pedantic Bash-compatible command line "
                   "interpreter, formatter, linter, language server and a "
                   "friendly interactive shell.\n\n",
                   HELP_INDENT, HELP_WRAP_WIDTH);
    h += make_synopsis(program_path.view(), HELP_SYNOPSIS);
    h += '\n';
    h += wrap_text("Options are also read from the KOSH_FLAGS environment "
                   "variable. A flag "
                   "on the command line overrides one set there.\n\n",
                   HELP_INDENT, HELP_WRAP_WIDTH);

    let const should_color = colors::stderr_wants_color();

    h += make_flag_help(FLAG_LIST, should_color);
    h += '\n';
    h += '\n';
    h += "Report bugs and suggest features at "
         "<https://github.com/fennec-support/kosh>";
    h += '\n';
    print_error(format_cli_help(h.view(), should_color));

    return EXIT_SUCCESS;
  }

  if (FLAG_LIST_CHECKS.is_enabled()) {
    let l = String{heap_allocator()};

    for (let const is_shellcheck_section : {true, false}) {
      l += is_shellcheck_section ? "SHELLCHECK DIAGNOSTICS\n"
                                 : "\nNATIVE ANALYSIS DIAGNOSTICS\n";

      for (usize index = 0; index < get_diagnostic_count(); index++) {
        let const definition =
            get_diagnostic_definition(static_cast<diagnostic_id>(index));

        if (definition.shellcheck_code.has_value() != is_shellcheck_section)
          continue;

        append_listed_diagnostic(l, definition);
      }
    }

    print(l);

    return EXIT_SUCCESS;
  }

  if (FLAG_VERSION.is_enabled()) {
    show_version();

    return EXIT_SUCCESS;
  }

  if (FLAG_SHORT_VERSION.is_enabled()) {
    show_short_version();

    return EXIT_SUCCESS;
  }

  /* at this point: no help, list, version, or short version flag is enabled,
     so no status is reported. */
  return None;
}

static fn
report_escaped_control_flow(EvalContext &context, const String &fallback_source)
    -> void
{
  if (!context.control_flow_store().has_pending()) return;

  const control_flow &control = context.control_flow_store().pending();
  let                 what    = StringView{};

  switch (control.kind) {
  case control_flow::Kind::Break:
    what = "'break' used outside of a loop";
    break;

  case control_flow::Kind::Continue:
    what = "'continue' used outside of a loop";
    break;

  case control_flow::Kind::Return: {
    if (!context.execution_store().shell_is_interactive()) {
      i32 return_status = static_cast<i32>(control.value);

      context.control_flow_store().clear();
      context.run_exit_trap();
      utils::quit(return_status, utils::farewell_policy::Goodbye);
    }

    what = "'return' used outside of a function or a sourced script";
    break;
  }
  case control_flow::Kind::Exit:
  case control_flow::Kind::Normal: context.control_flow_store().clear(); return;
  }

  /* at this point: control is pending with kind Break, Continue, or an
     interactive Return, and what holds the message to show. */
  const String *source =
      control.source != nullptr ? control.source : &fallback_source;
  let const located = ErrorWithLocation{control.location, what};

  show_message(located.to_string(*source, &context));

  context.control_flow_store().clear();
}

/* memory: each unit is allocated in m_arena by next_unit() and freed by
           release_unit(), which rewinds m_arena to the mark that next_unit()
           took. */
class StreamedAnalysisUnits final : public AnalysisUnitStream
{
public:
  StreamedAnalysisUnits(Parser                       &parser,
                        BumpArena                    &arena,
                        ArrayList<String>            &parse_errors,
                        EvalContext                  &context,
                        ArrayList<source_diagnostic> *diagnostic_sink)
      : m_parser{parser}, m_arena{arena}, m_parse_errors{parse_errors},
        m_context{context}, m_diagnostic_sink{diagnostic_sink}
  {}

  fn
  next_unit() throws -> const Expression * override
  {
    m_mark = m_arena.mark();

    return m_parser.construct_next_top_level_ast(m_parse_errors, &m_context,
                                                 m_diagnostic_sink);
  }

  fn
  release_unit() throws -> void override
  {
    m_parser.drop_lexer_peek_cache();
    m_arena.release(m_mark);
  }

private:
  Parser                       &m_parser;
  BumpArena                    &m_arena;
  ArrayList<String>            &m_parse_errors;
  EvalContext                  &m_context;
  ArrayList<source_diagnostic> *m_diagnostic_sink;
  BumpArena::Mark               m_mark{};
};

struct script_run_options
{
  bool                     should_require_shebang{true};
  bool                     should_silence_unresolved_commands{false};
  bool                     should_print_ast{true};
  bool                     should_analyze{true};
  bool                     is_whole_line{false};
  bool                     should_use_command_string_status{true};
  bool                    *out_was_rejected{nullptr};
  StringView               display_source{};
  const ArrayList<String> *assumed_variables{nullptr};
};

static fn
exit_status_for(const Error &error,
                EvalContext &context,
                bool         is_command_string) wontthrow -> i32
{
  if (error.command_status() != 1) {
    return static_cast<i32>(error.command_status());
  }

  if (is_command_string && error.is_script_fatal() &&
      context.runtime_state().is_bash_compatible() &&
      !context.runtime_state().error_exit())
  {
    return BASH_COMMAND_STRING_FATAL_STATUS;
  }

  /* at this point: error.command_status() is 1, and the Bash command string
     fatal status does not apply. */
  return context.runtime_state().is_posix_mode() ? 2 : EXIT_FAILURE;
}

struct script_run_plan
{
  bool should_analyze;
  bool should_stream_units;
  bool should_stream_execution;
  bool should_preflight_syntax;
  bool is_whole_line;
};

struct script_run_input
{
  const String                 &contents;
  StringView                    display_source;
  Maybe<StringView>             filename;
  EvalContext                  &context;
  BumpArena                    &arena;
  ArrayList<source_diagnostic> *diagnostic_sink;
};

static fn
make_script_run_plan(EvalContext              &context,
                     bool                      has_precompiled_ast,
                     bool                      has_out_ast,
                     const script_run_options &run_options) wontthrow
    -> script_run_plan
{
  let      &state            = context.runtime_state();
  let const should_print_ast = run_options.should_print_ast;
  let const should_analyze =
      run_options.should_analyze && !has_precompiled_ast &&
      (FLAG_OPTIMIZER_DIAGNOSTICS.is_enabled() ||
       ((state.no_exec() ||
         !(state.is_bash_compatible() || state.is_posix_mode()) ||
         state.get_warning_level() > 0) &&
        !state.is_diagnostics_disabled()));
  let const should_keep_whole_tree = has_out_ast ||
                                     (should_print_ast && state.show_ast()) ||
                                     state.show_lexed_words();
  let const should_stream_execution =
      !has_precompiled_ast && !state.no_exec() && !should_keep_whole_tree;

  return script_run_plan{
      should_analyze,
      should_analyze && state.no_exec() && !should_keep_whole_tree,
      should_stream_execution, should_stream_execution && !should_analyze,
      run_options.is_whole_line};
}

static fn
report_parse_errors(const script_run_input  &input,
                    const ArrayList<String> &parse_errors) throws
    -> bool
{
  if (parse_errors.is_empty()) return false;

  if (input.diagnostic_sink == nullptr)
    for (let const &e : parse_errors)
      show_message(e);

  input.context.execution_store().set_last_exit_status(EXIT_FAILURE);

  return true;
}

static fn
scan_analysis_metadata(const script_run_input &input,
                       ArrayList<String>      &parse_errors,
                       analysis_directives    &directives) throws -> bool
{
  LOG(Debug, "scanning a chunk of %zu bytes for analysis scopes",
      input.contents.count());

  let scan_parser = Parser{
      Lexer{input.contents.view(), input.arena, input.filename,
            input.context.runtime_state().get_mood()}
  };

  scan_parser.set_display_source(input.display_source);

  scan_parser.set_analysis_metadata_collection_mode(
      analysis_metadata_collection_mode::Enabled);
  scan_parser.set_substitution_validation_mode(
      substitution_validation_mode::Enabled);

  let const scan_mark = input.arena.mark();

  loop {
    let const  unit_mark = input.arena.mark();
    let const *unit      = scan_parser.construct_next_top_level_ast(
        parse_errors, &input.context, input.diagnostic_sink);

    if (unit == nullptr) break;

    scan_parser.drop_lexer_peek_cache();
    input.arena.release(unit_mark);
  }

  input.arena.release(scan_mark);

  directives = scan_parser.take_analysis_directives();

  return report_parse_errors(input, parse_errors);
}

static fn
preflight_syntax(const script_run_input &input,
                 ArrayList<String>      &parse_errors) throws -> bool
{
  LOG(Debug, "checking a chunk of %zu bytes before streamed execution",
      input.contents.count());

  let preflight_parser = Parser{
      Lexer{input.contents.view(), input.arena, input.filename,
            input.context.runtime_state().get_mood()}
  };

  preflight_parser.set_display_source(input.display_source);

  preflight_parser.set_substitution_validation_mode(
      substitution_validation_mode::Enabled);

  loop {
    let const  unit_mark = input.arena.mark();
    let const *unit      = preflight_parser.construct_next_top_level_ast(
        parse_errors, &input.context, input.diagnostic_sink);

    if (unit == nullptr) {
      input.arena.release(unit_mark);
      break;
    }

    preflight_parser.drop_lexer_peek_cache();
    input.arena.release(unit_mark);
  }

  return report_parse_errors(input, parse_errors);
}

static fn
parse_whole_script(const script_run_input &input,
                   const script_run_plan  &plan,
                   bool                    should_print_ast,
                   ArrayList<String>      &parse_errors,
                   analysis_directives    &directives,
                   Expression            *&ast) throws -> bool
{
  EvalContext &context = input.context;

  LOG(Debug, "parsing a chunk of %zu bytes", input.contents.count());

  let p = Parser{
      Lexer{input.contents.view(), input.arena, input.filename,
            context.runtime_state().get_mood(),
            ParseSession::AllocationKind::Syntax,
            context.runtime_state().show_lexed_words()
                ? debug_word_collection_mode::Enabled
                : debug_word_collection_mode::Disabled}
  };

  p.set_display_source(input.display_source);

  p.set_analysis_metadata_collection_mode(
      plan.should_analyze ? analysis_metadata_collection_mode::Enabled
                          : analysis_metadata_collection_mode::Disabled);
  p.set_substitution_validation_mode(substitution_validation_mode::Enabled);

  ast = p.construct_ast(parse_errors, &context, input.diagnostic_sink);

  if (report_parse_errors(input, parse_errors)) return true;

  if (should_print_ast && context.runtime_state().show_ast()) {
    print(ast->to_ast_string());
    print("\n");
  }

  if (context.runtime_state().show_lexed_words()) {
    for (let const &word : p.debug_words()) {
      print(word.to_pretty_string());
      print("\n");
    }
  }

  directives = p.take_analysis_directives();

  return false;
}

static fn
analyze_script(const script_run_input    &input,
               const script_run_plan     &plan,
               Expression                *ast,
               ArrayList<String>         &parse_errors,
               const analysis_directives &directives,
               const analysis_outputs    &diagnostics,
               const script_run_options  &run_options) throws -> bool
{
  EvalContext &context               = input.context;
  let          followed_source_paths = HashSet{heap_allocator()};
  let          source_effects_cache =
      StringMap<followed_source_effects>{heap_allocator()};

  if (input.filename.has_value()) {
    if (let canonical_root = os::canonical_path(Path{*input.filename});
        canonical_root.has_value())
    {
      followed_source_paths.add(canonical_root->text().view());
    }
  }

  let  highlight_cache = completion::ShellHighlightCache{heap_allocator()};
  let *previous_highlight_cache =
      context.set_diagnostic_highlight_cache(&highlight_cache);

  defer {
    context.set_diagnostic_highlight_cache(previous_highlight_cache);
  };

  let options = analysis_options::from_runtime(context.runtime_state());

  options.should_silence_unresolved_commands =
      run_options.should_silence_unresolved_commands ||
      (options.warning_level > 0 &&
       context.execution_store().shell_is_interactive());
  options.should_report_optimizer_diagnostics =
      FLAG_OPTIMIZER_DIAGNOSTICS.is_enabled();
  options.display_source    = input.display_source;
  options.assumed_variables = run_options.assumed_variables;
  options.shebang_policy =
      run_options.should_require_shebang && input.filename.has_value()
          ? missing_shebang_policy::Report
          : missing_shebang_policy::Suppress;

  let const do_analyze = [&](AnalysisUnitStream *units) throws -> bool {
    return analyze_ast(ast, input.contents, context.function_store().names(),
                       context.scope_store().alias_names(), &context, options,
                       directives,
                       {&followed_source_paths, &source_effects_cache}, {},
                       diagnostics, nullptr, units);
  };

  if (!plan.should_stream_units) return do_analyze(nullptr);

  let unit_parser = Parser{
      Lexer{input.contents.view(), input.arena, input.filename,
            context.runtime_state().get_mood()}
  };

  unit_parser.set_display_source(input.display_source);

  unit_parser.set_analysis_scope_collection_mode(
      analysis_metadata_collection_mode::Enabled);

  let units = StreamedAnalysisUnits{unit_parser, input.arena, parse_errors,
                                    context, input.diagnostic_sink};

  return do_analyze(&units);
}

static fn
command_memory_byte_count(const os::own_memory_mark &shell_start)
    wontthrow -> u64
{
  let const child_peak_byte_count = os::exchange_command_peak_rss_byte_count(0);
  let const shell_now             = os::mark_own_memory();
  u64       shell_growth_byte_count = 0;

  if (shell_now.current_byte_count > shell_start.current_byte_count) {
    shell_growth_byte_count =
        shell_now.current_byte_count - shell_start.current_byte_count;
  }

  if (shell_now.peak_byte_count > shell_start.peak_byte_count &&
      shell_now.peak_byte_count - shell_start.current_byte_count >
          shell_growth_byte_count)
  {
    shell_growth_byte_count =
        shell_now.peak_byte_count - shell_start.current_byte_count;
  }

  LOG(Debug, "the children peaked at %llu bytes and the shell rose %llu",
      static_cast<unsigned long long>(child_peak_byte_count),
      static_cast<unsigned long long>(shell_growth_byte_count));

  return child_peak_byte_count > shell_growth_byte_count
             ? child_peak_byte_count
             : shell_growth_byte_count;
}

static fn
evaluate_script(const script_run_input &input,
                const script_run_plan  &plan,
                Expression             *ast,
                BumpArena::Mark         preflight_mark,
                Maybe<usize>            history_event_number,
                root_evaluation_mode    evaluation_mode) throws -> i32
{
  EvalContext &context   = input.context;
  i32          exit_code = EXIT_SUCCESS;

  LOG(Debug, "evaluating the chunk");

  let previous_history_event_number =
      context.history_recorder().get_event_number();

  context.history_recorder().set_event_number(steal(history_event_number));
  defer {
    context.history_recorder().set_event_number(
        steal(previous_history_event_number));
  };

  context.set_current_source(&input.contents,
                             String{heap_allocator(), "the script"});

  let const previous_line_discard_root =
      context.execution_store().line_discard_root();
  let const previous_top_level_line_discard_root =
      context.execution_store().top_level_line_discard_root();
  let const previous_line_discard_source =
      context.execution_store().line_discard_source();
  let const previous_line_discard_subshell_depth =
      context.execution_store().line_discard_subshell_depth();

  context.execution_store().line_discard_source() = input.contents.view();
  context.execution_store().line_discard_subshell_depth() =
      context.execution_store().subshell_depth();

  let const is_whole_line =
      plan.is_whole_line ||
      evaluation_mode == root_evaluation_mode::ContainedSubstitution;

  if (evaluation_mode == root_evaluation_mode::ContainedSubstitution)
    evaluation_mode = root_evaluation_mode::Normal;

  let const do_set_line_discard_root =
      [&](const Expression *root) wontthrow -> void {
    context.execution_store().line_discard_root() = root;
    context.execution_store().top_level_line_discard_root() =
        is_whole_line ? nullptr : root;
  };

  defer {
    context.execution_store().line_discard_root() = previous_line_discard_root;
    context.execution_store().top_level_line_discard_root() =
        previous_top_level_line_discard_root;
    context.execution_store().line_discard_source() =
        previous_line_discard_source;
    context.execution_store().line_discard_subshell_depth() =
        previous_line_discard_subshell_depth;
  };

  let const command_start_nanos = koshka::os::monotonic_nanos();
  let const shell_start         = koshka::os::mark_own_memory();

  unused(koshka::os::exchange_command_peak_rss_byte_count(0));

  if (plan.should_stream_execution) {
    input.arena.release(preflight_mark);

    let execution_parser = Parser{
        Lexer{input.contents.view(), input.arena, input.filename,
              context.runtime_state().get_mood()}
    };

    execution_parser.set_display_source(input.display_source);
    let const was_terminal_exec_allowed =
        context.execution_store().is_terminal_exec_allowed();

    defer {
      context.execution_store().is_terminal_exec_allowed() =
          was_terminal_exec_allowed;
    };

    let const is_command_counted =
        !context.execution_store().has_execution_string();

    loop {
      let const  unit_mark = input.arena.mark();
      let const *unit      = execution_parser.construct_next_top_level_ast();

      if (unit == nullptr) {
        input.arena.release(unit_mark);
        break;
      }

      if (is_command_counted)
        context.execution_store().advance_command_number();

      defer {
        context.clear_retained_sources();
        context.set_current_source(&input.contents,
                                   String{heap_allocator(), "the script"});
        execution_parser.drop_lexer_peek_cache();
        input.arena.release(unit_mark);
      };

      context.execution_store().is_terminal_exec_allowed() =
          was_terminal_exec_allowed && execution_parser.is_at_end();
      do_set_line_discard_root(unit);
      exit_code =
          static_cast<int>(unit->evaluate_root(context, evaluation_mode));
      evaluation_mode = root_evaluation_mode::Normal;
      if (context.control_flow_store().has_pending() ||
          (context.runtime_state().option_is_enabled(shell_option_id::Onecmd) &&
           !context.execution_store().has_execution_string()))
      {
        break;
      }
    }
  } else {
    do_set_line_discard_root(ast);
    exit_code = static_cast<int>(ast->evaluate_root(context, evaluation_mode));
  }

  context.execution_store().set_last_command_duration_nanos(
      koshka::os::monotonic_nanos() - command_start_nanos);
  context.execution_store().set_last_command_peak_rss_byte_count(
      command_memory_byte_count(shell_start));
  LOG(Debug, "the chunk finished with exit code %d", exit_code);
  if (koshka::os::SIGNAL_PENDING) context.run_pending_traps();
  report_escaped_control_flow(context, input.contents);
  context.set_current_source(nullptr, String{heap_allocator()});

  return exit_code;
}

static fn
run_script_contents(
    const String             &script_contents,
    EvalContext              &context,
    BumpArena                &ast_arena,
    Maybe<StringView>         filename             = None,
    Expression               *precompiled_ast      = nullptr,
    Expression              **out_ast              = nullptr,
    Maybe<usize>              history_event_number = None,
    const analysis_outputs   &diagnostics          = {},
    const script_run_options &run_options          = {},
    root_evaluation_mode evaluation_mode = root_evaluation_mode::Normal) -> int
{
  let const diagnostic_sink  = diagnostics.diagnostic_sink;
  let const should_print_ast = run_options.should_print_ast;
  let const display_contents = run_options.display_source.is_empty()
                                   ? script_contents.view()
                                   : run_options.display_source;
  let const is_contained_substitution =
      evaluation_mode == root_evaluation_mode::ContainedSubstitution;
  let const is_command_string =
      run_options.should_use_command_string_status && filename.has_value() &&
      filename->data == COMMAND_STRING_SOURCE_NAME.data;
  i32 exit_code = EXIT_SUCCESS;

  let const do_reject = [&](i32 status) {
    LOG(Debug, "the chunk was rejected before evaluation with status %d",
        status);
    if (run_options.out_was_rejected != nullptr)
      *run_options.out_was_rejected = true;

    return status;
  };

  try {
    defer { context.end_command(); };
    defer {
      utils::invalidate_line_number_cache_for(script_contents.view());
    };

    context.clear_retained_sources();
    ast_arena.reset();
    context.expansion_store().scratch_arena().reset();

    let const plan = make_script_run_plan(context, precompiled_ast != nullptr,
                                          out_ast != nullptr, run_options);
    let const input =
        script_run_input{script_contents, run_options.display_source,
                         filename,        context,
                         ast_arena,       diagnostic_sink};
    let parse_errors = ArrayList<String>{heap_allocator()};
    let directives   = analysis_directives{};

    let const   preflight_mark = ast_arena.mark();
    Expression *ast            = precompiled_ast;

    if (plan.should_stream_units) {
      if (scan_analysis_metadata(input, parse_errors, directives))
        return do_reject(SYNTAX_ERROR_STATUS);
    } else if (plan.should_preflight_syntax) {
      if (preflight_syntax(input, parse_errors))
        return do_reject(SYNTAX_ERROR_STATUS);
    } else if (precompiled_ast == nullptr) {
      if (parse_whole_script(input, plan, should_print_ast, parse_errors,
                             directives, ast))
        return do_reject(SYNTAX_ERROR_STATUS);
    }

    LOG(Debug, "the analysis stage %s for this chunk",
        plan.should_analyze ? "runs" : "is skipped");

    bool did_analysis_fail = false;
#if !defined NDEBUG
    let const diagnostic_highlight_byte_count_before =
        completion::debug_highlight_input_byte_count();
    let const diagnostic_lexical_scan_byte_count_before =
        completion::debug_shell_lexical_scan_byte_count();
#endif

    if (plan.should_analyze) {
      did_analysis_fail = !analyze_script(input, plan, ast, parse_errors,
                                          directives, diagnostics, run_options);
    }
#if !defined NDEBUG
    LOG(All, "diagnostic highlighting consumed %zu source bytes",
        completion::debug_highlight_input_byte_count() -
            diagnostic_highlight_byte_count_before);
    LOG(All, "diagnostic lexical replay consumed %zu source bytes",
        completion::debug_shell_lexical_scan_byte_count() -
            diagnostic_lexical_scan_byte_count_before);
#endif

    if (!did_analysis_fail && out_ast != nullptr) {
      *out_ast = ast;
    }

    if (did_analysis_fail) {
      exit_code = do_reject(EXIT_FAILURE);
    } else if (context.runtime_state().no_exec()) {
      exit_code = EXIT_SUCCESS;
    } else {
      exit_code = evaluate_script(input, plan, ast, preflight_mark,
                                  steal(history_event_number), evaluation_mode);
    }

    context.execution_store().set_last_exit_status(static_cast<i32>(exit_code));

    if (context.runtime_state().stats_enabled()) {
      print(context.make_stats_string());
      print("\n");
    }
  } catch (const ErrorWithLocationAndDetails &e) {
    if (!e.was_rendered()) {
      show_message(e.to_string(display_contents, &context));
      show_message(e.details_to_string(display_contents, &context));
      if (is_contained_substitution)
        context.print_source_backtrace(e.location());
    }

    exit_code = exit_status_for(e, context, is_command_string);
  } catch (const InterruptErrorWithLocation &e) {
    if (!e.was_rendered() && os::TERMINATION_SIGNAL == 0) {
      show_message(e.to_string(display_contents, &context));
      if (is_contained_substitution)
        context.print_source_backtrace(e.location());
    }

    exit_code = exit_status_for(e, context, is_command_string);
  } catch (const ErrorWithLocation &e) {
    if (!e.was_rendered()) {
      show_message(e.to_string(display_contents, &context));
      if (is_contained_substitution)
        context.print_source_backtrace(e.location());
    }

    exit_code = exit_status_for(e, context, is_command_string);
  } catch (const Error &e) {
    if (!e.was_rendered()) show_message(e.to_string());
    exit_code = exit_status_for(e, context, is_command_string);
  } catch (const std::exception &e) {
    exit_code = EXIT_FAILURE;
    show_message(
        ErrorWithDetails{"Uncaught exception while executing the AST, so the "
                         "command is aborted",
                         "The exception context is '" + StringView{e.what()} +
                             "' and the last system message is '" +
                             os::last_system_error_message() + "'"}
            .to_string());
  } catch (...) {
    show_message(ErrorWithDetails{
        "Unexpected system explosion while executing the AST, "
        "so the shell exits",
        "The last system message is '" + os::last_system_error_message() + "'"}
                     .to_string());
    utils::quit(EXIT_FAILURE);
  }

  context.execution_store().set_last_exit_status(exit_code);

  return exit_code;
}

fn
run_lint_document_contents(const String                 &source,
                           EvalContext                  &context,
                           BumpArena                    &ast_arena,
                           Maybe<StringView>             filename,
                           analysis_diagnostic_totals   *diagnostic_totals,
                           ArrayList<source_diagnostic> *diagnostic_sink,
                           bool                         *is_format_recognized,
                           bool                          should_print_ast,
                           Maybe<StringView>             shell_source_name,
                           const SourceLocation         *operand_location,
                           StringView                    invocation) throws -> int
{
  let const document =
      parse_format_document(parser_format_input{source.view(), filename, None});

  if (is_format_recognized != nullptr)
    *is_format_recognized = document.kind != parser_format_kind::UnknownHost;
  if (document.kind == parser_format_kind::UnknownHost) {
    let const message = unrecognized_format_message(filename, operand_location,
                                                    invocation, &context);

    show_message(message.view());

    return EXIT_FAILURE;
  }

  if (!document.is_host_format) {
    let const has_shell_source_name = shell_source_name.has_value();

    return run_script_contents(
        source, context, ast_arena,
        has_shell_source_name ? shell_source_name : filename, nullptr, nullptr,
        None, {diagnostic_totals, diagnostic_sink, nullptr},
        {!has_shell_source_name, false, should_print_ast});
  }

  let const saved_mood          = context.runtime_state().get_mood();
  let const saved_warning_level = context.runtime_state().get_warning_level();

  defer {
    context.runtime_state().set_mood(saved_mood);
    context.runtime_state().set_warning_level(saved_warning_level);
  };

  int status = EXIT_SUCCESS;

  for (let const &fragment : document.fragments) {
    context.runtime_state().set_mood(fragment.mood);
    context.runtime_state().set_warning_level(
        warning_level_for_mood(fragment.mood));

    let fragment_options = script_run_options{
        false, fragment.should_silence_unresolved_commands, should_print_ast};

    fragment_options.display_source    = source.view();
    fragment_options.assumed_variables = &fragment.assumed_variables;

    let const fragment_status = run_script_contents(
        fragment.analysis_source, context, ast_arena, filename, nullptr,
        nullptr, None, {diagnostic_totals, diagnostic_sink, nullptr},
        fragment_options);

    if (fragment_status != EXIT_SUCCESS) status = fragment_status;
  }

  return status;
}

static fn
format_document_source(StringView            source,
                       Maybe<StringView>     filename,
                       BumpArena            &ast_arena,
                       ArrayList<String>    &errors,
                       String               *ast_output     = nullptr,
                       BumpArena            *function_arena = nullptr,
                       mimic_mood            mood           = mimic_mood::Posix,
                       const SourceLocation *operand_location = nullptr,
                       StringView            invocation       = {},
                       EvalContext          *context          = nullptr) throws
    -> Maybe<String>
{
  let const document =
      parse_format_document(parser_format_input{source, filename, None});

  if (document.kind == parser_format_kind::UnknownHost) {
    errors.push(unrecognized_format_message(filename, operand_location,
                                            invocation, context));

    return None;
  }

  if (!document.is_host_format)
    return format_shell_source(source, ast_arena, errors, ast_output,
                               function_arena, mood, filename);

  /* at this point: document is a recognized host format, so the replacements
     built from its fragments are applied to source. */
  let replacements = ArrayList<parser_format_replacement>{heap_allocator()};

  for (let const &fragment : document.fragments) {
    if (fragment.should_skip_format) continue;

    let const formattable = parser_format_formattable_source(fragment);

    if (!formattable.has_value()) continue;

    let       fragment_ast = String{heap_allocator()};
    let const formatted =
        format_shell_source(formattable->view(), ast_arena, errors,
                            ast_output != nullptr ? &fragment_ast : nullptr,
                            function_arena, fragment.mood);

    if (!formatted.has_value()) return None;
    if (ast_output != nullptr) {
      if (!ast_output->is_empty()) ast_output->push('\n');
      ast_output->append(fragment_ast.view());
    }

    let const restored =
        parser_format_host_replacement(fragment, source, formatted->view());

    if (!restored.has_value()) continue;

    let encoded = parser_format_encode(fragment, restored->view());

    if (!encoded.has_value()) return None;
    replacements.push(parser_format_replacement{
        fragment.host_start, fragment.host_end, encoded.take()});
  }

  return parser_format_apply_replacements(source, replacements);
}

/* memory: commands holds copies of the PROMPT_COMMAND elements and frees
           them on return. the cached text and AST stay in the prompt store
           until the next parse replaces them.
   time:   one run_script_contents() call per non-empty element, plus one
           parse for each element that does not run from the cached AST. */
static fn
run_prompt_command(EvalContext &context, BumpArena &ast_arena) -> void
{
  if (context.is_associative_array("PROMPT_COMMAND")) return;

  let   commands      = context.collect_array_elements("PROMPT_COMMAND");
  usize command_count = 0;

  for (let &command : commands) {
    command.normalize_crlf_line_endings();
    if (!command.is_empty()) command_count++;
  }

  if (command_count == 0) return;

  /* at this point: commands holds CRLF-normalized elements, and at least
     one of them is not empty. */

  let      &prompt_store      = context.prompt_command_store();
  let      &execution         = context.execution_store();
  let const saved_exit_status = execution.last_exit_status();
  let const saved_command_duration_nanos =
      execution.last_command_duration_nanos();
  let const saved_command_peak_rss_byte_count =
      execution.last_command_peak_rss_byte_count();
  let const saved_last_argument = execution.get_last_argument().clone();

  execution.prompt_command_running() = true;
  defer { execution.prompt_command_running() = false; };

  for (let &command : commands) {
    if (command.is_empty()) continue;

    LOG(Info, "running a PROMPT_COMMAND hook, %zu bytes", command.count());
    execution.set_last_exit_status(saved_exit_status);

    let      &cached_text = prompt_store.get_cached_text();
    let const cached_ast  = prompt_store.get_cached_ast();
    i32       status      = EXIT_SUCCESS;

    if (command_count == 1 && cached_ast != nullptr &&
        cached_text.view() == command.view())
    {
      status = run_script_contents(cached_text, context, ast_arena,
                                   StringView{"$PROMPT_COMMAND"}, cached_ast,
                                   nullptr, None, {}, {false});
    } else {
      prompt_store.set_cached_ast(nullptr);
      cached_text = steal(command);

      Expression *parsed_ast = nullptr;

      status =
          run_script_contents(cached_text, context, prompt_store.get_arena(),
                              StringView{"$PROMPT_COMMAND"}, nullptr,
                              &parsed_ast, None, {}, {false});
      prompt_store.set_cached_ast(parsed_ast);
    }

    if (status != EXIT_SUCCESS)
      if (let const definition =
              context.special_variable_definition_location("PROMPT_COMMAND");
          definition.has_value())
        context.print_source_backtrace(definition);

    execution.set_last_argument(saved_last_argument.view());
  }

  execution.set_last_exit_status(saved_exit_status);
  execution.set_last_command_duration_nanos(saved_command_duration_nanos);
  execution.set_last_command_peak_rss_byte_count(
      saved_command_peak_rss_byte_count);
}

static fn
history_control_operator_byte_count(StringView source, usize position) wontthrow
    -> usize
{
  let const byte      = source[position];
  let const next_byte = position + 1 < source.length ? source[position + 1] : 0;
  let const third_byte =
      position + 2 < source.length ? source[position + 2] : 0;

  switch (byte) {
  case ';':
    if (next_byte == ';') return third_byte == '&' ? 3 : 2;

    return next_byte == '&' ? 2 : 1;
  case '&':
    if (next_byte == '>' && third_byte == '>') {
      return 3;
    }

    return next_byte == '&' || next_byte == '>' ? 2 : 1;
  case '|': return next_byte == '|' || next_byte == '&' ? 2 : 1;
  case '<':
    if (next_byte == '<') return third_byte == '<' || third_byte == '-' ? 3 : 2;

    return next_byte == '>' || next_byte == '&' ? 2 : 1;
  case '>':
    return next_byte == '>' || next_byte == '|' || next_byte == '&' ? 2 : 1;
  default: return 1;
  }
}

static fn
next_history_word(StringView source, usize &position) throws
    -> Maybe<StringView>
{
  while (position < source.length && is_ascii_whitespace(source[position])) {
    position++;
  }

  if (position >= source.length || source[position] == '#') {
    return None;
  }

  /* at this point: position is below source.length, and source[position]
     is neither whitespace nor '#', so a word starts at position. */

  let const start_position = position;
  let const first_byte     = source[position];
  let const is_process_substitution =
      (first_byte == '<' || first_byte == '>') &&
      position + 1 < source.length && source[position + 1] == '(';

  if (lexer::is_shell_sentinel(first_byte) && !is_process_substitution) {
    position += history_control_operator_byte_count(source, position);
  } else {
    char quote_byte = 0;

    while (position < source.length) {
      let const byte = source[position];

      if (byte == '\\' && quote_byte != '\'' && position + 1 < source.length) {
        position += 2;
        continue;
      }

      char closing_byte = 0;

      if (quote_byte != '\'' && byte == '$' && position + 1 < source.length &&
          (source[position + 1] == '(' || source[position + 1] == '{'))
      {
        closing_byte = source[position + 1] == '(' ? ')' : '}';
      } else if (quote_byte == 0 &&
                 (lexer::is_extglob_operator(byte) || byte == '<' ||
                  byte == '>') &&
                 position + 1 < source.length && source[position + 1] == '(')
      {
        closing_byte = ')';
      }

      if (closing_byte != 0) {
        let const end_position = lexer::scan_balanced_shell_region(
            source, position + 2, closing_byte);

        position = end_position.value_or(source.length);
        continue;
      }

      if (quote_byte != 0) {
        position++;
        if (byte == quote_byte) quote_byte = 0;
        continue;
      }

      if (byte == '\'' || byte == '"' || byte == '`') {
        quote_byte = byte;
        position++;
        continue;
      }

      if (is_ascii_whitespace(byte) || lexer::is_shell_sentinel(byte)) {
        break;
      }

      position++;
    }
  }

  /* e.g. source = "cat 'a b' x", position = 4: the quote opens at 4 and
     closes at 8, so the space at 9 ends the word.
     -> "'a b'", with position 9 */
  return source.substring_of_length(start_position, position - start_position);
}

static fn
find_history_search_word(StringView command, StringView needle) throws
    -> Maybe<StringView>
{
  usize position = 0;

  while (position < command.length) {
    let const word = next_history_word(command, position);

    if (!word.has_value()) break;
    if (word->find_substring(needle).has_value()) return word;
  }

  return None;
}

cold wontreturn static fn
throw_bad_history_word_specifier(StringView spelling) throws -> void
{
  let message = String{heap_allocator(), "Bad word specifier '"};

  message += spelling;
  message += "'";
  throw Error{message.view()};
}

static fn
select_history_words(StringView        command,
                     StringView        designator,
                     StringView        spelling,
                     Maybe<StringView> search_word,
                     String           &selected) throws -> StringView
{
  if (designator.is_empty()) throw_bad_history_word_specifier(spelling);
  if (designator == "%") {
    if (!search_word.has_value()) throw_bad_history_word_specifier(spelling);

    return *search_word;
  }

  usize word_count    = 0;
  usize scan_position = 0;

  while (scan_position < command.length) {
    if (!next_history_word(command, scan_position).has_value()) break;
    word_count++;
  }

  usize first_word_index = 0;
  usize end_word_index   = word_count;

  if (designator == "^") {
    first_word_index = 1;
    end_word_index   = 2;
  } else if (designator == "$") {
    if (word_count == 0) throw_bad_history_word_specifier(spelling);
    first_word_index = word_count - 1;
  } else if (designator == "*") {
    first_word_index = word_count > 0 ? 1 : 0;
  } else {
    usize separator_position = 0;

    if (designator[0] != '-') {
      while (separator_position < designator.length &&
             lexer::is_number(designator[separator_position]))
      {
        separator_position++;
      }

      let const parsed =
          designator.substring_of_length(0, separator_position).to<u64>();

      if (parsed.is_error() || parsed.value() > static_cast<u64>(SIZE_MAX)) {
        throw_bad_history_word_specifier(spelling);
      }

      first_word_index = static_cast<usize>(parsed.value());
      end_word_index   = first_word_index + 1;
    }

    if (separator_position < designator.length) {
      let const separator_byte = designator[separator_position++];

      if (separator_byte == '*') {
        end_word_index = word_count;
      } else if (separator_byte == '-') {
        if (separator_position == designator.length) {
          end_word_index = word_count > 0 ? word_count - 1 : 0;
        } else if (designator[separator_position] == '$' &&
                   separator_position + 1 == designator.length)
        {
          end_word_index = word_count;
        } else {
          let const parsed = designator.substring(separator_position).to<u64>();

          if (parsed.is_error() || parsed.value() >= static_cast<u64>(SIZE_MAX))
          {
            throw_bad_history_word_specifier(spelling);
          }

          end_word_index = static_cast<usize>(parsed.value()) + 1;
        }
      } else {
        throw_bad_history_word_specifier(spelling);
      }
    }
  }

  if (first_word_index > end_word_index || end_word_index > word_count) {
    throw_bad_history_word_specifier(spelling);
  }

  /* at this point: first_word_index <= end_word_index <= word_count, so
     every index in the selected range is below word_count. */

  scan_position = 0;

  usize word_index = 0;

  if (end_word_index == first_word_index + 1) {
    while (scan_position < command.length) {
      let const word = next_history_word(command, scan_position);

      if (!word.has_value()) break;
      if (word_index == first_word_index) return *word;
      word_index++;
    }

    throw_bad_history_word_specifier(spelling);
  }

  /* e.g. command = "cat 'a b' x", designator = "1-2" gives word_count 3,
     first_word_index 1 and end_word_index 3, so the words "'a b'" and "x"
     are selected. */
  scan_position = 0;
  word_index    = 0;

  while (scan_position < command.length) {
    let const word = next_history_word(command, scan_position);

    if (!word.has_value()) break;
    if (word_index >= first_word_index && word_index < end_word_index) {
      if (!selected.is_empty()) selected.push(' ');
      selected.append(*word);
    }

    word_index++;
  }

  return selected.view();
}

struct history_substitution
{
  String old_text;
  String replacement_text;
};

struct history_expansion_state
{
  Maybe<String>               last_search_word{};
  Maybe<history_substitution> last_substitution{};
};

enum class history_substitution_scope : u8
{
  First,
  All,
  FirstPerWord,
};

cold wontreturn static fn
throw_history_modifier_error(StringView spelling,
                             StringView reason) throws -> void
{
  let message = String{heap_allocator(), reason};

  message += " '";
  message += spelling;
  message += "'";
  throw Error{message.view()};
}

static pure fn
apply_history_path_modifier(StringView text, char modifier) wontthrow
    -> StringView
{
  /* e.g. text = "/usr/lib/x.c": the last "/" is at index 8.
     modifier 'h' gives "/usr/lib" and modifier 't' gives "x.c". */
  if (modifier == 'h' || modifier == 't') {
    for (usize position = text.length; position > 0; position--) {
      if (text[position - 1] != '/') continue;

      let const separator_position = position - 1;

      if (modifier == 't') return text.substring(position);

      return text.substring_of_length(0, separator_position);
    }

    return text;
  }

  for (usize position = text.length; position > 0; position--) {
    if (text[position - 1] != '.') continue;

    let const extension_position = position - 1;

    return modifier == 'r' ? text.substring_of_length(0, extension_position)
                           : text.substring(extension_position);
  }

  return text;
}

static fn
parse_history_substitution_field(StringView source,
                                 usize     &position,
                                 char       delimiter,
                                 Allocator  allocator,
                                 bool      *did_find_delimiter) throws
    -> String
{
  /* e.g. source = a\/b/c with delimiter '/' from position 0: the escaped
     slash adds '/', b is added, and the unescaped slash at 4 ends the field.
     the field is "a/b" and position is 5. */
  let field = String{allocator};

  if (did_find_delimiter != nullptr) *did_find_delimiter = false;

  while (position < source.length) {
    let const byte = source[position++];

    if (byte == delimiter) {
      if (did_find_delimiter != nullptr) *did_find_delimiter = true;
      break;
    }

    if (byte == '\\' && position < source.length &&
        source[position] == delimiter)
    {
      field.push(source[position++]);
      continue;
    }

    field.push(byte);
  }

  return field;
}

static fn
append_history_substitution_replacement(String    &output,
                                        StringView replacement,
                                        StringView matched) throws
    -> void
{
  for (usize position = 0; position < replacement.length; position++) {
    let const byte = replacement[position];

    if (byte == '\\' && position + 1 < replacement.length &&
        replacement[position + 1] == '&')
    {
      output.push('&');
      position++;
    } else if (byte == '&') {
      output.append(matched);
    } else {
      output.push(byte);
    }
  }
}

static fn
apply_history_substitution(StringView                 text,
                           StringView                 old_text,
                           StringView                 replacement,
                           history_substitution_scope scope,
                           String                    &output) throws -> bool
{
  if (old_text.is_empty()) return false;

  output.clear();
  output.reserve(text.length);

  bool  did_replace     = false;
  usize copied_position = 0;

  /* e.g. text = "aXbXc", old_text = "X", replacement = "-", and scope All:
     the match at 1 gives "a-", the match at 3 gives "a-b-", and the tail
     "c" gives "a-b-c". */
  if (scope == history_substitution_scope::FirstPerWord) {
    usize word_scan_position = 0;

    while (word_scan_position < text.length) {
      let const word = next_history_word(text, word_scan_position);

      if (!word.has_value()) break;

      let const match_position = word->find_substring(old_text);

      if (!match_position.has_value()) continue;

      let const absolute_match_position =
          static_cast<usize>(word->data - text.data) + *match_position;

      output.append(text.substring_of_length(
          copied_position, absolute_match_position - copied_position));
      append_history_substitution_replacement(output, replacement, old_text);
      copied_position = absolute_match_position + old_text.length;
      did_replace     = true;
    }
  } else {
    usize search_position = 0;

    while (search_position <= text.length) {
      let const match_position = text.find_substring(old_text, search_position);

      if (!match_position.has_value()) break;
      output.append(text.substring_of_length(
          copied_position, *match_position - copied_position));
      append_history_substitution_replacement(output, replacement, old_text);
      copied_position = *match_position + old_text.length;
      search_position = copied_position;
      did_replace     = true;
      if (scope == history_substitution_scope::First) break;
    }
  }

  if (!did_replace) return false;
  /* at this point: old_text is not empty and did_replace is true, so output
     holds the text before copied_position with the replacements applied. */
  output.append(text.substring(copied_position));

  return true;
}

static fn
quote_history_words(StringView text,
                    bool       should_split,
                    String    &output) throws -> void
{
  output.clear();
  if (!should_split) {
    append_shell_quoted_arg(output, text, true);

    return;
  }

  usize position = 0;

  while (position < text.length) {
    let const word = text.next_ascii_whitespace_word(position);

    if (word.is_empty()) break;
    if (!output.is_empty()) output.push(' ');
    append_shell_quoted_arg(output, word, true);
  }
}

static pure fn
resolve_history_substitution_old_text(
    StringView old_text, const history_expansion_state &state) wontthrow
    -> StringView
{
  if (!old_text.is_empty()) return old_text;
  if (state.last_substitution.has_value())
    return state.last_substitution->old_text.view();
  if (state.last_search_word.has_value()) return state.last_search_word->view();

  return {};
}

static fn
apply_history_modifiers(StringView               source,
                        usize                   &position,
                        StringView               selected,
                        history_expansion_state &state,
                        Allocator                allocator,
                        String                  &first_buffer,
                        String                  &second_buffer,
                        bool                    &should_execute) throws -> StringView
{
  bool should_use_first_buffer = true;

  while (position < source.length && source[position] == ':') {
    let const modifier_start_position = position++;

    if (position >= source.length)
      throw_history_modifier_error(":", "Unrecognized history modifier");

    let  scope                  = history_substitution_scope::First;
    bool has_substitution_scope = false;

    if (source[position] == 'g' || source[position] == 'a' ||
        source[position] == 'G')
    {
      has_substitution_scope = true;
      scope = source[position] == 'G' ? history_substitution_scope::FirstPerWord
                                      : history_substitution_scope::All;
      position++;
      if (position >= source.length)
        throw_history_modifier_error(
            source.substring(modifier_start_position + 1),
            "Unrecognized history modifier");
    }

    let const modifier = source[position++];

    if (has_substitution_scope && modifier != 's' && modifier != '&') {
      throw_history_modifier_error(
          source.substring_of_length(modifier_start_position + 1,
                                     position - modifier_start_position - 1),
          "Unrecognized history modifier");
    }

    if (modifier == 'h' || modifier == 't' || modifier == 'r' ||
        modifier == 'e')
    {
      selected = apply_history_path_modifier(selected, modifier);
      continue;
    }

    if (modifier == 'p') {
      should_execute = false;
      continue;
    }

    let &output = should_use_first_buffer ? first_buffer : second_buffer;

    should_use_first_buffer = !should_use_first_buffer;

    if (modifier == 'q' || modifier == 'x') {
      quote_history_words(selected, modifier == 'x', output);
      selected = output.view();
      continue;
    }

    StringView old_text{};
    StringView replacement{};
    let        parsed_old_text    = String{allocator};
    let        parsed_replacement = String{allocator};

    if (modifier == 's') {
      if (position >= source.length)
        throw_history_modifier_error("s", "Unrecognized history modifier");

      let const delimiter         = source[position++];
      bool      has_old_delimiter = false;

      parsed_old_text = parse_history_substitution_field(
          source, position, delimiter, allocator, &has_old_delimiter);
      if (!has_old_delimiter)
        throw_history_modifier_error(
            source.substring_of_length(modifier_start_position,
                                       position - modifier_start_position),
            "Substitution failed for");

      parsed_replacement = parse_history_substitution_field(
          source, position, delimiter, allocator, nullptr);
      old_text =
          resolve_history_substitution_old_text(parsed_old_text.view(), state);
      replacement = parsed_replacement.view();
    } else if (modifier == '&') {
      if (!state.last_substitution.has_value())
        throw_history_modifier_error(
            source.substring_of_length(modifier_start_position,
                                       position - modifier_start_position),
            "Substitution failed for");
      old_text    = state.last_substitution->old_text.view();
      replacement = state.last_substitution->replacement_text.view();
    } else {
      throw_history_modifier_error(source.substring_of_length(position - 1, 1),
                                   "Unrecognized history modifier");
    }

    if (!apply_history_substitution(selected, old_text, replacement, scope,
                                    output))
    {
      throw_history_modifier_error(
          source.substring_of_length(modifier_start_position,
                                     position - modifier_start_position),
          "Substitution failed for");
    }

    selected = output.view();

    if (modifier == 's') {
      let next_substitution = history_substitution{
          String{heap_allocator(), old_text   },
          String{heap_allocator(), replacement}
      };

      state.last_substitution = steal(next_substitution);
    }
  }

  return selected;
}

struct interactive_history_expansion
{
  String command;
  bool   should_execute;
};

static fn
is_history_expansion_inhibited(StringView source, usize position) wontthrow
    -> bool
{
  let const rest = source.substring(position + 1);

  if (position > 0 && source[position - 1] == '[' &&
      rest.find_character(']').has_value())
  {
    return true;
  }

  if (position > 1 && source[position - 1] == '{' &&
      source[position - 2] == '$' && rest.find_character('}').has_value())
  {
    return true;
  }

  return position > 0 && source[position - 1] == '$';
}

static fn
expand_interactive_history_spans(StringView               source,
                                 Maybe<usize>             accepted_event_number,
                                 history_expansion_state &state,
                                 EvalContext             &context,
                                 usize                   &designator_start) throws
    -> Maybe<interactive_history_expansion>
{
  let const scratch_mark = context.expansion_store().scratch_arena().mark();

  defer {
    context.expansion_store().scratch_arena().release(scratch_mark);
  };

  bool  is_single_quoted = false;
  bool  is_double_quoted = false;
  bool  is_at_word_start = true;
  bool  did_expand       = false;
  bool  should_execute   = true;
  usize copied_position  = 0;
  let   expanded         = String{heap_allocator()};

  if (!source.is_empty() && source[0] == '^') {
    designator_start = 0;

    let const selected_event = toiletline::get_relative_history_event(
        context.scratch_allocator(), 1, accepted_event_number);

    if (!selected_event.has_value())
      throw Error{"History event '!!' was not found"};

    usize     substitution_position = 1;
    bool      has_old_delimiter     = false;
    let const old_text              = parse_history_substitution_field(
        source, substitution_position, '^', context.scratch_allocator(),
        &has_old_delimiter);
    let const replacement =
        parse_history_substitution_field(source, substitution_position, '^',
                                         context.scratch_allocator(), nullptr);
    let const effective_old_text =
        resolve_history_substitution_old_text(old_text.view(), state);
    if (!has_old_delimiter) {
      let message = String{heap_allocator(), "The quick substitution '"};

      message += source;
      message += "' has no '^' after the old text";
      throw Error{message.view()};
    }

    let substituted = String{context.scratch_allocator()};

    if (!apply_history_substitution(
            selected_event->command.view(), effective_old_text,
            replacement.view(), history_substitution_scope::First, substituted))
    {
      let message =
          String{heap_allocator(), "The previous command does not contain '"};

      message += effective_old_text;
      message += "'";
      throw Error{message.view()};
    }

    let next_substitution = history_substitution{
        String{heap_allocator(), effective_old_text},
        String{heap_allocator(), replacement.view()}
    };

    state.last_substitution = steal(next_substitution);

    /* at this point: selected_event holds a value, has_old_delimiter is
       true, and substituted holds the rewritten command. */
    let first_modifier_buffer  = String{context.scratch_allocator()};
    let second_modifier_buffer = String{context.scratch_allocator()};
    let selected               = substituted.view();

    if (substitution_position < source.length &&
        source[substitution_position] == ':')
    {
      selected = apply_history_modifiers(
          source, substitution_position, selected, state,
          context.scratch_allocator(), first_modifier_buffer,
          second_modifier_buffer, should_execute);
    }

    expanded.append(selected);
    expanded.append(source.substring(substitution_position));

    return interactive_history_expansion{steal(expanded), should_execute};
  }

  let const do_terminate_designator = [](char byte) wontthrow -> bool {
    return is_ascii_whitespace(byte) || byte == ':' || byte == '"' ||
           byte == '\'' || byte == '\\' || byte == ';' || byte == '&' ||
           byte == '|' || byte == '(' || byte == ')' || byte == '<' ||
           byte == '>';
  };

  for (usize position = 0; position < source.length; position++) {
    let const byte = source[position];

    if (!is_single_quoted && !is_double_quoted && byte == '#' &&
        is_at_word_start)
    {
      break;
    }

    if (byte == '\'' && !is_double_quoted) {
      is_single_quoted = !is_single_quoted;
      is_at_word_start = false;
      continue;
    }

    if (byte == '"' && !is_single_quoted) {
      is_double_quoted = !is_double_quoted;
      is_at_word_start = false;
      continue;
    }

    if (byte == '\\' && !is_single_quoted && position + 1 < source.length) {
      is_at_word_start = false;
      position++;
      continue;
    }

    if (!is_single_quoted && !is_double_quoted) {
      is_at_word_start = is_ascii_whitespace(byte) || byte == ';' ||
                         byte == '&' || byte == '|' || byte == '(' ||
                         byte == ')' || byte == '<' || byte == '>';
    }

    if (byte != '!' || is_single_quoted || position + 1 >= source.length) {
      continue;
    }

    let const next_byte = source[position + 1];

    if (is_ascii_whitespace(next_byte) || next_byte == '=' ||
        next_byte == '"' || next_byte == '\'' || next_byte == '(' ||
        is_history_expansion_inhibited(source, position))
    {
      continue;
    }

    designator_start = position;

    usize      reference_end_position = position + 2;
    StringView replacement{};
    let        selected_event         = Maybe<toiletline::history_event>{};
    let        current_line           = String{context.scratch_allocator()};
    let        first_modifier_buffer  = String{context.scratch_allocator()};
    let        second_modifier_buffer = String{context.scratch_allocator()};

    if (next_byte == '#') {
      current_line.append(expanded.view());
      current_line.append(source.substring_of_length(
          copied_position, position - copied_position));
      replacement = current_line.view();
    } else {
      let search_word = Maybe<StringView>{};

      if (state.last_search_word.has_value())
        search_word = state.last_search_word->view();

      bool has_implicit_word_designator = false;

      if (next_byte == '!' || next_byte == ':' || next_byte == '^' ||
          next_byte == '$' || next_byte == '*' || next_byte == '%')
      {
        selected_event = toiletline::get_relative_history_event(
            context.scratch_allocator(), 1, accepted_event_number);
        has_implicit_word_designator = next_byte != '!';
        if (has_implicit_word_designator) {
          reference_end_position--;
        }
      } else if (next_byte == '?') {
        let close_position = source.find_substring("?", reference_end_position);

        if (!close_position.has_value()) {
          close_position = source.length;
        }

        let const needle = source.substring_of_length(
            reference_end_position, *close_position - reference_end_position);

        reference_end_position = *close_position < source.length
                                     ? *close_position + 1
                                     : *close_position;
        selected_event         = toiletline::get_containing_history_event(
            context.scratch_allocator(), needle, accepted_event_number);
        if (selected_event.has_value()) {
          let const matched_word =
              find_history_search_word(selected_event->command.view(), needle);

          if (matched_word.has_value()) {
            state.last_search_word =
                String{heap_allocator(), matched_word.value()};
            search_word = state.last_search_word->view();
          }
        }
      } else {
        if (lexer::is_number(next_byte) || next_byte == '-') {
          while (reference_end_position < source.length &&
                 lexer::is_number(source[reference_end_position]))
          {
            reference_end_position++;
          }
        } else {
          while (reference_end_position < source.length &&
                 !do_terminate_designator(source[reference_end_position]))
          {
            reference_end_position++;
          }
        }

        let const designator = source.substring_of_length(
            position + 1, reference_end_position - position - 1);

        if (designator.length > 1 && designator[0] == '-' &&
            designator.substring(1).is_all_decimal_digits())
        {
          let const parsed = designator.substring(1).to<u64>();

          if (!parsed.is_error() && parsed.value() > 0 &&
              parsed.value() <= static_cast<u64>(static_cast<usize>(-1)))
          {
            selected_event = toiletline::get_relative_history_event(
                context.scratch_allocator(), static_cast<usize>(parsed.value()),
                accepted_event_number);
          }
        } else if (designator.is_all_decimal_digits()) {
          let const parsed = designator.to<u64>();

          if (!parsed.is_error() &&
              parsed.value() <= static_cast<u64>(static_cast<usize>(-1)))
          {
            selected_event = toiletline::get_numbered_history_event(
                context.scratch_allocator(), static_cast<usize>(parsed.value()),
                accepted_event_number);
          }
        } else {
          selected_event = toiletline::get_prefixed_history_event(
              context.scratch_allocator(), designator, accepted_event_number);
        }
      }

      if (selected_event.has_value()) {
        replacement = selected_event->command.view();

        let const selector_start_position = reference_end_position;
        usize     selector_end_position   = selector_start_position;

        if (selector_end_position < source.length &&
            source[selector_end_position] == ':')
        {
          selector_end_position++;
        }

        if (selector_end_position < source.length &&
            (lexer::is_number(source[selector_end_position]) ||
             source[selector_end_position] == '^' ||
             source[selector_end_position] == '$' ||
             source[selector_end_position] == '*' ||
             source[selector_end_position] == '%' ||
             source[selector_end_position] == '-'))
        {
          if (source[selector_end_position] == '^' ||
              source[selector_end_position] == '$' ||
              source[selector_end_position] == '*' ||
              source[selector_end_position] == '%')
          {
            selector_end_position++;
          } else {
            if (source[selector_end_position] == '-') {
              selector_end_position++;
            }

            while (selector_end_position < source.length &&
                   lexer::is_number(source[selector_end_position]))
            {
              selector_end_position++;
            }

            if (selector_end_position < source.length &&
                (source[selector_end_position] == '*' ||
                 source[selector_end_position] == '-'))
            {
              selector_end_position++;
              if (selector_end_position < source.length &&
                  source[selector_end_position] == '$')
              {
                selector_end_position++;
              } else {
                while (selector_end_position < source.length &&
                       lexer::is_number(source[selector_end_position]))
                {
                  selector_end_position++;
                }
              }
            }
          }

          let const designator_start_position =
              source[selector_start_position] == ':'
                  ? selector_start_position + 1
                  : selector_start_position;

          replacement = select_history_words(
              replacement,
              source.substring_of_length(designator_start_position,
                                         selector_end_position -
                                             designator_start_position),
              source.substring_of_length(selector_start_position,
                                         selector_end_position -
                                             selector_start_position),
              search_word, current_line);
          reference_end_position = selector_end_position;
        } else if (has_implicit_word_designator && next_byte != ':') {
          throw_bad_history_word_specifier(
              source.substring_of_length(position + 1, 1));
        }

        replacement = apply_history_modifiers(
            source, reference_end_position, replacement, state,
            context.scratch_allocator(), first_modifier_buffer,
            second_modifier_buffer, should_execute);
      } else {
        let message = String{heap_allocator(), "History event '"};

        message += source.substring_of_length(position, reference_end_position -
                                                            position);
        message += "' was not found";
        throw Error{steal(message)};
      }
    }

    if (!did_expand) expanded.reserve(source.length + replacement.length);
    expanded.append(source.substring_of_length(copied_position,
                                               position - copied_position));
    expanded.append(replacement);
    copied_position = reference_end_position;
    position        = reference_end_position - 1;
    did_expand      = true;
  }

  if (!did_expand) return None;
  /* at this point: did_expand is true, and expanded holds the text before
     copied_position with each history reference replaced. */
  expanded.append(source.substring(copied_position));

  return interactive_history_expansion{steal(expanded), should_execute};
}

/* expands the history designators of an interactive line. An error that a
   designator raises points at that designator in the line and notes how to
   take it literally. */
static fn
expand_interactive_history(StringView               source,
                           Maybe<usize>             accepted_event_number,
                           history_expansion_state &state,
                           EvalContext             &context) throws
    -> Maybe<interactive_history_expansion>
{
  usize designator_start = 0;

  try {
    return expand_interactive_history_spans(source, accepted_event_number,
                                            state, context, designator_start);
  } catch (const ErrorWithLocation &) {
    throw;
  } catch (const Error &error) {
    usize designator_end = designator_start + 1;

    while (designator_end < source.length &&
           !is_ascii_whitespace(source[designator_end]))
    {
      designator_end++;
    }

    throw ErrorWithLocationAndDetails{
        SourceLocation{designator_start, designator_end - designator_start},
        error.message().view(),
        "Quote the '!' or a leading '^', or turn history expansion off with "
        "`set +H`, to keep it literal"
    };
  }
}

enum class startup_file_requirement : u8
{
  Optional,
  Explicit,
};

/* memory: contents is owned by this call and freed when it returns.
   time:   O(file bytes) for read_entire_file(); run_source() then runs the
           script, so its cost follows the script contents. */
static fn
source_file(const Path              &path,
            EvalContext             &context,
            startup_file_requirement requirement =
                startup_file_requirement::Optional) -> bool
{
  Maybe<String> contents = path.read_entire_file();

  if (!contents) {
    let const is_missing = os::last_system_error_is_missing_file();
    let const reason     = os::last_system_error_message();

    if (requirement == startup_file_requirement::Explicit && !is_missing) {
      show_message(
          Error{"Unable to read startup file '" + path.text() + "': " + reason}
              .to_string());
    }

    LOG(Info, "skipping '%s' because the file is missing or unreadable",
        path.c_str());

    return false;
  }

  /* at this point: contents holds the bytes of path. */
  LOG(Info, "sourcing '%s', %zu bytes", path.c_str(), contents->count());

  context.run_source(*contents, path.view(), None, path.view(), nullptr,
                     nullptr, return_handling::Consume);

  return true;
}

static pure fn
selected_rcfile() wontthrow -> Maybe<StringView>
{
  if (!FLAG_RCFILE.is_set() && !FLAG_INIT_FILE.is_set()) {
    return None;
  }

  if (!FLAG_RCFILE.is_set() ||
      (FLAG_INIT_FILE.is_set() &&
       FLAG_INIT_FILE.position() > FLAG_RCFILE.position()))
  {
    return FLAG_INIT_FILE.value();
  }

  return FLAG_RCFILE.value();
}

static fn
source_custom_rcfile(StringView name, EvalContext &context) throws
    -> void
{
  let const home_expanded = utils::expand_leading_tilde_path(name);

  source_file(Path{home_expanded.has_value() ? home_expanded->view() : name},
              context, startup_file_requirement::Explicit);
}

static fn
source_environment_file(StringView   variable_name,
                        EvalContext &context) throws -> void
{
  let const value = context.get_variable_value(variable_name);

  if (!value.has_value() || value->is_empty()) {
    return;
  }

  let const expanded = context.expand_modifier_word(value->view(), false);

  if (expanded.is_empty()) return;

  /* at this point: value is set and nonempty, and expanded is nonempty. */
  source_custom_rcfile(expanded.view(), context);
}

static fn
source_home_file(StringView name, EvalContext &context) throws -> void
{
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    Path path = home->clone();

    path.append(name);
    source_file(path, context);
  }
}

static fn
source_posix_login_files(EvalContext &context) throws -> void
{
  LOG(Info, "sourcing the posix login files");
  source_file(Path{"/etc/profile"}, context);
  source_home_file(".profile", context);
}

static fn
source_bash_login_files(EvalContext &context) throws -> void
{
  LOG(Info, "sourcing the bash login files in bash order");
  source_file(Path{"/etc/profile"}, context);
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    for (let const name : {".bash_profile", ".bash_login", ".profile"}) {
      Path candidate = home->clone();

      candidate.append(name);
      if (source_file(candidate, context)) break;
    }
  }
}

/* sources rc.kosh beside the system kosh.conf when it passes the same
   ownership and permission checks as that file. */
static fn
source_system_rc(EvalContext &context) throws -> void
{
  let const system_koshconf = find_system_koshconf_path();

  if (!system_koshconf.has_value()) return;

  let path = system_koshconf->parent_or_current();

  path.append("rc.kosh");

  let const system_file = os::read_system_owned_file(path);

  if (!system_file.rejection.is_empty()) {
    show_message(Warning{system_file.rejection.view()}.to_string());
  }

  if (!system_file.contents.has_value()) return;

  LOG(Info, "sourcing the system '%s', %zu bytes", path.c_str(),
      system_file.contents->count());
  context.run_source(*system_file.contents, path.view(), None, path.view(),
                     nullptr, nullptr, return_handling::Consume);
}

static fn
source_bash_system_rc(EvalContext &context) throws -> void
{
  LOG(Info, "looking for the system bashrc");

  for (let const path : {"/etc/bash/bashrc", "/etc/bash.bashrc"})
    if (source_file(Path{path}, context)) break;
}

static fn
ensure_bash_completion_loaded(EvalContext &context) throws -> void
{
  if (context.completion_store().get_slot_spec(completion_spec_slot::Default) !=
      nullptr)
  {
    LOG(Info, "skipping the bash-completion bootstrap because a "
              "default completion spec is already registered");

    return;
  }

  if (context.get_variable_value("BASH_COMPLETION_VERSINFO").has_value()) {
    LOG(Info, "skipping the bash-completion bootstrap because the "
              "rc chain already loaded the script");

    return;
  }

  /* at this point: no default completion spec is registered and
     BASH_COMPLETION_VERSINFO is unset. */
  LOG(Info, "sourcing the stock bash-completion script");

  let bash_completion_state = definition_state::from(context.runtime_state());

  bash_completion_state.mood = mimic_mood::Bash;

  let const definition_scope = DefinitionStateScope{
      context, bash_completion_state, definition_state_exit::RestoreCaller};

  source_file(Path{"/usr/share/bash-completion/bash_completion"}, context);
}

fn
source_init_moods(EvalContext                 &context,
                  const ArrayList<mimic_mood> &moods,
                  bool                         is_login_shell,
                  bool                         should_be_interactive) throws -> void
{
  bool did_source_bash_rc  = false;
  bool did_source_bash_env = false;

  for (let flavor : moods) {
    let const should_consider_bash_env =
        flavor == mimic_mood::Bash &&
        !context.runtime_state().option_is_enabled(
            shell_option_id::Privileged) &&
        !context.startup_store().startup_finished() && !did_source_bash_env;

    if (!is_login_shell && !should_be_interactive && !should_consider_bash_env)
    {
      continue;
    }

    if (context.runtime_control_store().init_mood_sourcing(flavor)) {
      LOG(Info,
          "skipping the %.*s mood, its startup files are already sourcing",
          static_cast<int>(mood_name(flavor).length), mood_name(flavor).data);
      continue;
    }

    context.runtime_control_store().set_init_mood_sourcing(flavor, true);
    defer {
      context.runtime_control_store().set_init_mood_sourcing(flavor, false);
    };

    /* at this point: flavor is marked as sourcing, and at least one of
       is_login_shell, should_be_interactive, or should_consider_bash_env
       holds. */
    context.runtime_state().set_mood(flavor);
    LOG(Info, "sourcing the startup files for the %.*s mood",
        static_cast<int>(mood_name(flavor).length), mood_name(flavor).data);
    switch (flavor) {
    case mimic_mood::Default:
      if (is_login_shell || (should_be_interactive && !FLAG_NORC.is_enabled() &&
                             !context.runtime_state().option_is_enabled(
                                 shell_option_id::Privileged)))
      {
        source_system_rc(context);
        if (let const rcfile = selected_rcfile();
            rcfile.has_value() && !is_login_shell)
        {
          source_custom_rcfile(*rcfile, context);
        } else if (let const script = get_user_script_path("rc.kosh");
                   script.has_value())
        {
          source_file(*script, context);
        }
      }

      break;

    case mimic_mood::Posix:
      if (is_login_shell) source_posix_login_files(context);
      if (should_be_interactive && !context.runtime_state().option_is_enabled(
                                       shell_option_id::Privileged))
      {
        if (Maybe<String> env = context.get_variable_value("ENV");
            env.has_value() && !env->is_empty())
        {
          source_file(Path{env->view()}, context);
        }
      }

      break;

    case mimic_mood::Bash:
    case mimic_mood::BashPosix:
      if (is_login_shell) source_bash_login_files(context);
      if (flavor == mimic_mood::Bash &&
          !context.runtime_state().option_is_enabled(
              shell_option_id::Privileged) &&
          !should_be_interactive &&
          !context.startup_store().startup_finished() && !did_source_bash_env)
      {
        source_environment_file("BASH_ENV", context);
        did_source_bash_env = true;
      }

      if (should_be_interactive && !is_login_shell && !FLAG_NORC.is_enabled()) {
        did_source_bash_rc = true;
        source_bash_system_rc(context);
        if (let const rcfile = selected_rcfile(); rcfile.has_value())
          source_custom_rcfile(*rcfile, context);
        else
          source_home_file(".bashrc", context);
      }

      break;
    }

    if (is_login_shell || should_be_interactive) {
      context.runtime_control_store().mark_mood_initialized(flavor);
    }
  }

  if (did_source_bash_rc &&
      context.runtime_state().option_is_enabled(shell_option_id::TabCompletion))
  {
    LOG(Info, "bootstrapping the bash programmable completion");
    ensure_bash_completion_loaded(context);
  }
}

pure fn
quoted_argv_offset_until(int                argc,
                         const char *const *argv,
                         StringView         needle) wontthrow -> usize
{
  usize offset = 0;

  for (int a = 0; a < argc; a++) {
    if (needle == StringView{argv[a], strlen(argv[a])}) break;
    offset += shell_quoted_arg_length(StringView{argv[a], strlen(argv[a])}) + 1;
  }

  return offset;
}

/* each operand is located at its first appearance in the invocation. */
static fn
collect_operand_locations(int                      argc,
                          const char *const       *argv,
                          const ArrayList<String> &file_names) throws
    -> ArrayList<SourceLocation>
{
  let locations = ArrayList<SourceLocation>{heap_allocator()};

  for (let const &file_name : file_names) {
    locations.push(
        SourceLocation{quoted_argv_offset_until(argc, argv, file_name.view()),
                       shell_quoted_arg_length(file_name.view())});
  }

  return locations;
}

/* the error is located at the file operand when a location is given. */
static fn
show_operand_error(const SourceLocation *operand_location,
                   StringView            invocation,
                   EvalContext          &context,
                   StringView            message) throws -> void
{
  if (operand_location == nullptr) {
    show_message(Error{message}.to_string());

    return;
  }

  show_message(ErrorWithLocation{*operand_location, message}.to_string(
      invocation, &context));
}

struct apply_file_snapshot
{
  Path            operand_path;
  Path            target_path;
  os::file_status status;
  String          contents;
  SourceLocation  operand_location;
};

static fn
read_apply_file(const Path           &operand_path,
                const SourceLocation &operand_location,
                StringView            invocation,
                EvalContext          &context) throws
    -> Maybe<apply_file_snapshot>
{
  let target_path = os::canonical_path(operand_path);

  if (!target_path.has_value()) {
    show_operand_error(&operand_location, invocation, context,
                       "Unable to resolve '" + operand_path.text() +
                           "': " + os::last_system_error_message());

    return None;
  }

  let status = os::file_status{};

  if (!os::stat_path_following(target_path->text().view(), status)) {
    show_operand_error(&operand_location, invocation, context,
                       "Unable to inspect '" + operand_path.text() +
                           "': " + os::last_system_error_message());

    return None;
  }

  if (os::file_type_letter(status.mode) != '-') {
    show_operand_error(&operand_location, invocation, context,
                       "The '--apply' option requires a regular file: '" +
                           operand_path.text() + "'");

    return None;
  }

  let contents = target_path->read_entire_file();

  if (!contents.has_value()) {
    show_operand_error(&operand_location, invocation, context,
                       "Unable to read '" + operand_path.text() +
                           "': " + os::last_system_error_message());

    return None;
  }

  let verified_status = os::file_status{};

  if (!os::stat_path_following(target_path->text().view(), verified_status) ||
      !os::file_status_matches(status, verified_status))
  {
    show_operand_error(&operand_location, invocation, context,
                       "Refusing to read '" + operand_path.text() +
                           "' because it changed while being processed");

    return None;
  }

  /* at this point: target_path is the canonical path, status describes a
     regular file, and contents was read between two stat calls that matched. */
  return apply_file_snapshot{operand_path.clone(), target_path.take(), status,
                             contents.take(), operand_location};
}

/* memory: the replacement temp file holds the new contents on disk until the
           rename; the defer removes it when this returns.
   time:   O(replacement bytes + target bytes) for the temp file write and the
           comparison read of the target; the stat, mode, and rename calls are
           constant. */
static fn
replace_file_contents(const apply_file_snapshot &snapshot,
                      StringView                 replacement,
                      StringView                 invocation,
                      EvalContext               &context) throws -> bool
{
  let replacement_path = os::write_to_named_temp_file(
      snapshot.target_path.parent(), ".kosh_apply", replacement);

  if (!replacement_path.has_value()) {
    show_operand_error(&snapshot.operand_location, invocation, context,
                       "Unable to create a replacement for '" +
                           snapshot.operand_path.text() +
                           "': " + os::last_system_error_message());

    return false;
  }

  defer {
    unused(os::remove_file(replacement_path->text().view()));
  };

  if (!os::set_file_mode(replacement_path->text().view(), snapshot.status.mode))
  {
    show_operand_error(&snapshot.operand_location, invocation, context,
                       "Unable to preserve the mode of '" +
                           snapshot.operand_path.text() +
                           "': " + os::last_system_error_message());

    return false;
  }

  let const resolved_operand = os::canonical_path(snapshot.operand_path);
  let       current_status   = os::file_status{};
  let const current_contents = snapshot.target_path.read_entire_file();

  if (!resolved_operand.has_value() ||
      resolved_operand->text() != snapshot.target_path.text() ||
      !current_contents.has_value() ||
      current_contents->view() != snapshot.contents.view() ||
      !os::stat_path_following(snapshot.target_path.view(), current_status) ||
      !os::file_status_matches(snapshot.status, current_status))
  {
    show_operand_error(&snapshot.operand_location, invocation, context,
                       "Refusing to replace '" + snapshot.operand_path.text() +
                           "' because it changed while being processed");

    return false;
  }

  /* at this point: the target still holds snapshot.contents, still has the
     status captured in snapshot, and resolves to the same path.
     replacement_path holds the new contents with the original mode. */
  if (!os::rename_path(replacement_path->text().view(),
                       snapshot.target_path.view()))
  {
    show_operand_error(&snapshot.operand_location, invocation, context,
                       "Unable to replace '" + snapshot.operand_path.text() +
                           "': " + os::last_system_error_message());

    return false;
  }

  return true;
}

struct applied_fix_tally
{
  diagnostic_id  id;
  error_severity severity;
  usize          count;
};

static fn
record_applied_fix(ArrayList<applied_fix_tally> &tallies,
                   diagnostic_id                 id,
                   error_severity                severity) throws -> void
{
  for (let &tally : tallies) {
    if (tally.id != id || tally.severity != severity) {
      continue;
    }

    tally.count++;

    return;
  }

  tallies.push(applied_fix_tally{id, severity, 1});
}

static fn
append_applied_fix_label(String &label, diagnostic_id id) throws -> void
{
  let const &definition = get_diagnostic_definition(id);

  label.append(definition.slug);

  if (!definition.shellcheck_code.has_value()) return;

  label.append(" (SC");
  label.append(String::from(*definition.shellcheck_code, heap_allocator()));
  label.push(')');
}

/* memory: labels and their line strings are heap-allocated and freed when
           this returns.
   time:   O(t log t) to sort t tallies, then O(t) for the labels and the output
           lines. */
cold static fn
print_applied_fix_summary(
    ArrayList<applied_fix_tally> &&collected_tallies) throws -> void
{
  if (collected_tallies.is_empty()) return;

  let const tallies =
      steal(collected_tallies)
          .make_sorted([](const applied_fix_tally &left,
                          const applied_fix_tally &right) wontthrow -> bool {
            if (left.count != right.count) return left.count > right.count;

            return ENUM(left.id) < ENUM(right.id);
          });

  usize warning_count        = 0;
  usize error_count          = 0;
  usize max_label_byte_count = 0;
  let   labels               = ArrayList<String>{heap_allocator()};

  for (let const &tally : tallies) {
    if (tally.severity == error_severity::warning)
      warning_count += tally.count;
    else
      error_count += tally.count;

    let label = String{heap_allocator()};

    append_applied_fix_label(label, tally.id);
    if (label.count() > max_label_byte_count)
      max_label_byte_count = label.count();
    labels.push(steal(label));
  }

  let const should_color = colors::stderr_wants_color();
  let const warning_color =
      should_color ? colors::soft(colors::ansi::YELLOW) : StringView{};
  let const error_color =
      should_color ? colors::soft(colors::ansi::BOLD_BRIGHT_RED) : StringView{};
  let const reset = should_color ? colors::ansi::RESET : StringView{};

  let summary = String{heap_allocator(), "Fixed "};

  if (warning_count > 0) {
    summary.append(warning_color);
    summary.append(String::from(warning_count, heap_allocator()));
    summary.append(warning_count == 1 ? " warning" : " warnings");
    summary.append(reset);
  }

  if (warning_count > 0 && error_count > 0) {
    summary.append(" and ");
  }

  if (error_count > 0) {
    summary.append(error_color);
    summary.append(String::from(error_count, heap_allocator()));
    summary.append(error_count == 1 ? " error" : " errors");
    summary.append(reset);
  }

  summary.append(".");
  show_message(summary.view());

  for (usize tally_index = 0; tally_index < tallies.count(); tally_index++) {
    let const &tally      = tallies[tally_index];
    let const  is_warning = tally.severity == error_severity::warning;

    let line = String{heap_allocator(), "  "};

    line.append(labels[tally_index].view());

    for (usize padding = labels[tally_index].count();
         padding < max_label_byte_count; padding++)
      line.push(' ');

    line.append("  ");
    line.append(is_warning ? warning_color : error_color);
    line.append(String::from(tally.count, heap_allocator()));
    if (is_warning)
      line.append(tally.count == 1 ? " warning" : " warnings");
    else
      line.append(tally.count == 1 ? " error" : " errors");
    line.append(reset);

    show_message(line.view());
  }
}

static fn
run_format_operation(const ArrayList<String>         &file_names,
                     const ArrayList<SourceLocation> &operand_locations,
                     StringView                       invocation,
                     bool                             should_apply,
                     bool                             should_apply_lint_fixes,
                     BumpArena                       &ast_arena,
                     EvalContext                     &context,
                     mimic_mood                       mood) throws -> int
{
  bool  did_fail    = false;
  usize input_count = file_names.count();

  if (!should_apply && input_count == 0) {
    input_count = 1;
  }

  for (usize input_index = 0; input_index < input_count; input_index++) {
    let const is_standard_input =
        file_names.is_empty() || file_names[input_index] == "-";
    Maybe<apply_file_snapshot> snapshot;
    let                        source = String{heap_allocator()};
    let const                  operand_location =
        is_standard_input ? nullptr : &operand_locations[input_index];

    if (is_standard_input) {
      source = utils::read_entire_standard_input();
    } else if (should_apply) {
      snapshot = read_apply_file(Path{file_names[input_index].view()},
                                 *operand_location, invocation, context);
      if (!snapshot.has_value()) {
        did_fail = true;
        continue;
      }

      source = snapshot->contents.clone();
    } else {
      let const path     = Path{file_names[input_index].view()};
      let       contents = path.read_entire_file();

      if (!contents.has_value()) {
        show_operand_error(operand_location, invocation, context,
                           "Could not open '" + path.text() +
                               "': " + os::last_system_error_message());
        did_fail = true;
        continue;
      }

      source = contents.take();
    }

    if (should_apply_lint_fixes) {
      let normalized_source = source.clone();

      normalized_source.normalize_crlf_line_endings();

      let       diagnostics = ArrayList<source_diagnostic>{heap_allocator()};
      let const source_name = is_standard_input
                                  ? Maybe<StringView>{}
                                  : Maybe<StringView>{file_names[input_index]};
      bool      is_format_recognized = true;

      unused(run_lint_document_contents(normalized_source, context, ast_arena,
                                        source_name, nullptr, &diagnostics,
                                        &is_format_recognized, false, None,
                                        operand_location, invocation));
      if (!is_format_recognized) {
        did_fail = true;
        continue;
      }

      let normalized_fixes = ArrayList<source_fix>{heap_allocator()};

      for (let const &diagnostic : diagnostics) {
        if (!diagnostic.source_name.is_empty() &&
            (!source_name.has_value() ||
             diagnostic.source_name != *source_name))
        {
          continue;
        }

        for (let const &fix : diagnostic.fixes) {
          let edits = ArrayList<source_edit>{heap_allocator()};

          for (let const &edit : fix.edits)
            edits.push(source_edit{edit.start_position, edit.end_position,
                                   edit.expected.clone(),
                                   edit.replacement.clone()});

          normalized_fixes.push(
              source_fix{fix.title.clone(), steal(edits), fix.is_preferred,
                         fix.is_safe_for_fix_all, fix.origin});
        }
      }

      let const fixes = source_fixes_for_original_line_endings(
          source.view(), normalized_fixes);
      let fixed = apply_source_fixes(source.view(), fixes);

      if (!fixed.has_value()) {
        show_operand_error(operand_location, invocation, context,
                           "Unable to apply non-conflicting fixes to the "
                           "input");
        did_fail = true;
        continue;
      }

      source = fixed.take();
    }

    let       errors      = ArrayList<String>{heap_allocator()};
    let const source_name = is_standard_input
                                ? Maybe<StringView>{}
                                : Maybe<StringView>{file_names[input_index]};
    let       ast_output  = String{heap_allocator()};
    let const formatted   = format_document_source(
        source.view(), source_name, ast_arena, errors,
        context.runtime_state().show_ast() ? &ast_output : nullptr,
        context.arena_store().function_arena(), mood, operand_location,
        invocation, &context);

    if (!formatted.has_value()) {
      for (let const &error : errors)
        show_message(error.view());

      did_fail = true;
      continue;
    }

    /* at this point: formatted holds the formatted text of source. */
    for (let const &warning : errors)
      show_message(warning.view());

    if (context.runtime_state().show_ast() && !ast_output.is_empty()) {
      print(ast_output.view());
      print("\n");
    }

    if (should_apply) {
      ASSERT(snapshot.has_value());
      if (formatted->view() != source.view() &&
          !replace_file_contents(*snapshot, formatted->view(), invocation,
                                 context))
      {
        did_fail = true;
      }
    } else if (colors::stdout_wants_color()) {
      let highlighted = String{heap_allocator()};

      completion::append_highlighted_source(
          highlighted, formatted->view(), context,
          colors::printed_source_highlight_theme());
      print(highlighted);
    } else {
      print(formatted->view());
    }
  }

  return did_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}

static fn
run_lint_apply_operation(const ArrayList<String>         &file_names,
                         const ArrayList<SourceLocation> &operand_locations,
                         StringView                       invocation,
                         bool                             should_format,
                         EvalContext                     &context,
                         BumpArena                       &ast_arena) throws -> int
{
  bool did_fail        = false;
  let  final_totals    = analysis_diagnostic_totals{};
  let  applied_tallies = ArrayList<applied_fix_tally>{heap_allocator()};

  for (usize file_index = 0; file_index < file_names.count(); file_index++) {
    let const &file_name        = file_names[file_index];
    let const &operand_location = operand_locations[file_index];
    let const  path             = Path{file_name.view()};
    let const  snapshot =
        read_apply_file(path, operand_location, invocation, context);

    if (!snapshot.has_value()) {
      did_fail = true;
      continue;
    }

    /* at this point: snapshot holds the contents of a regular file that did
       not change while it was read. */
    let source = snapshot->contents.clone();

    source.normalize_crlf_line_endings();

    let  diagnostics          = ArrayList<source_diagnostic>{heap_allocator()};
    bool is_format_recognized = true;

    unused(run_lint_document_contents(
        source, context, ast_arena, file_name.view(), nullptr, &diagnostics,
        &is_format_recognized, false, None, &operand_location, invocation));
    if (!is_format_recognized) {
      did_fail = true;
      continue;
    }

    let normalized_fixes    = ArrayList<source_fix>{heap_allocator()};
    let reported_severities = ArrayList<applied_fix_tally>{heap_allocator()};

    for (let const &diagnostic : diagnostics) {
      if (!diagnostic.source_name.is_empty() &&
          diagnostic.source_name != file_name.view())
      {
        continue;
      }

      if (diagnostic.id.has_value()) {
        record_applied_fix(reported_severities, *diagnostic.id,
                           diagnostic.severity);
      }

      for (let const &fix : diagnostic.fixes) {
        let edits = ArrayList<source_edit>{heap_allocator()};

        for (let const &edit : fix.edits) {
          edits.push(source_edit{edit.start_position, edit.end_position,
                                 edit.expected.clone(),
                                 edit.replacement.clone()});
        }

        normalized_fixes.push(source_fix{fix.title.clone(), steal(edits),
                                         fix.is_preferred,
                                         fix.is_safe_for_fix_all, fix.origin});
      }
    }

    let const fixes = source_fixes_for_original_line_endings(
        snapshot->contents.view(), normalized_fixes);
    let applied_origins = ArrayList<diagnostic_id>{heap_allocator()};
    let fixed =
        apply_source_fixes(snapshot->contents.view(), fixes, &applied_origins);

    if (!fixed.has_value()) {
      show_operand_error(&operand_location, invocation, context,
                         "Unable to apply non-conflicting fixes to '" +
                             path.text() + "'");
      did_fail = true;
      continue;
    }

    let final_source = fixed.take();

    if (should_format) {
      let errors    = ArrayList<String>{heap_allocator()};
      let formatted = format_document_source(
          final_source.view(), file_name.view(), ast_arena, errors, nullptr,
          context.arena_store().function_arena(),
          context.runtime_state().get_mood(), &operand_location, invocation,
          &context);

      if (!formatted.has_value()) {
        for (let const &error : errors)
          show_message(error.view());

        did_fail = true;
        continue;
      }

      final_source = formatted.take();
    }

    let validation_source = final_source.clone();

    validation_source.normalize_crlf_line_endings();

    let validation_diagnostics = ArrayList<source_diagnostic>{heap_allocator()};

    unused(run_lint_document_contents(validation_source, context, ast_arena,
                                      file_name.view(), nullptr,
                                      &validation_diagnostics, nullptr, false));

    bool has_parse_error = false;

    for (let const &diagnostic : validation_diagnostics) {
      if (diagnostic.id.has_value()) continue;
      has_parse_error = true;
      show_message(
          ErrorWithLocation{diagnostic.location, diagnostic.message.view()}
              .to_string(validation_source.view(), &context));
    }

    if (has_parse_error) {
      did_fail = true;
      continue;
    }

    /* at this point: validation_source reported no diagnostic without an id,
       and final_source is the candidate replacement for file_name. */
    if (final_source.view() != snapshot->contents.view() &&
        !replace_file_contents(*snapshot, final_source.view(), invocation,
                               context))
    {
      did_fail = true;
      continue;
    }

    for (let const applied_id : applied_origins) {
      Maybe<error_severity> severity = None;

      for (let const &reported : reported_severities) {
        if (reported.id != applied_id) continue;
        severity = reported.severity;
        break;
      }

      if (!severity.has_value()) continue;
      record_applied_fix(applied_tallies, applied_id, *severity);
    }

    let analyzed_source = final_source.clone();

    analyzed_source.normalize_crlf_line_endings();

    let const final_status = run_lint_document_contents(
        analyzed_source, context, ast_arena, file_name.view(), &final_totals);

    if (final_status != EXIT_SUCCESS) did_fail = true;
  }

  print_applied_fix_summary(steal(applied_tallies));
  print_analysis_diagnostic_summary(final_totals);

  return did_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}

} /* namespace koshka */
