/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the shell-facing editor interface for input, history,
 * prompts, completion, highlighting, terminal modes, and display-width
 * conversion. Toiletline.cpp adapts this interface to the vendored editor,
 * while ToiletlineStubs.cpp supplies noninteractive builds.
 */

#pragma once

#include "Completion.hpp"
#include "ToiletlineHistory.hpp"
#include "base/ArrayList.hpp"
#include "base/Common.hpp"
#include "base/ErrorOr.hpp"
#include "base/Path.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

#define TL_HISTORY_MAX_SIZE (1024 * 4)
#include "toiletline/toiletline.h"

namespace koshka {
class EvalContext;
} /* namespace koshka */

namespace toiletline {

using koshka::String;
using koshka::StringView;

fn
get_default_prompt_template() -> String;

fn
get_default_right_prompt_template() -> String;

fn
decode_prompt_escapes(StringView                 prompt,
                      const koshka::EvalContext &context) throws
    -> String;

fn
build_prompt(koshka::EvalContext &context) -> String;

fn
build_right_prompt(koshka::EvalContext &context) -> String;

fn
build_transient_prompt(koshka::EvalContext &context) -> koshka::Maybe<String>;

fn
build_transient_right_prompt(koshka::EvalContext &context)
    -> koshka::Maybe<String>;

fn
expand_prompt_template(StringView prompt, koshka::EvalContext &context)
    -> String;

fn
render_ps0(koshka::EvalContext &context) -> String;

fn
emit_command_end_mark(koshka::EvalContext &context, i32 exit_status) -> void;

fn
emit_prompt_start_marks(koshka::EvalContext &context) -> void;

fn
append_prompt_end_mark(koshka::EvalContext &context, String &prompt) -> void;

fn
emit_command_start_marks(koshka::EvalContext &context, StringView command_line)
    -> void;

fn
enable_editor_commands(koshka::EvalContext &context) -> void;
fn
enable_completion(koshka::EvalContext &context) -> void;
fn
disable_completion() -> void;
/* turns off completion, highlighting, the formatter, and the other
   shell-syntax callbacks, and keeps the external editor and the option
   commands. */
fn
disable_shell_completion() -> void;

fn
is_completion_enabled() -> bool;

fn
enter_calc_history() -> void;
fn
leave_calc_history() -> void;

/* hides the shell history from the editor until leave_empty_history(), so Up
   and Down cannot replace a buffer with a command. */
fn
enter_empty_history() -> void;

fn
leave_empty_history() -> void;

/* saves the edited file through callback for Ctrl-X s and the vi write
   commands, or turns saving off when callback is null. */
fn
set_save_callback(int (*callback)(const char *text, const char **out_message))
    -> void;

/* keeps text as the saved file contents that quit keys compare the buffer
   against, or forgets them when text is null. text must outlive its use. */
fn
set_saved_text(const char *text) -> void;

/* shows title on the top row of the full-screen layout, or no title when
   title is null. title must outlive its use. */
fn
set_editor_title(const char *title) -> void;

/* keeps status, one line about the edited file, for the rows under the
   full-screen input. status must outlive its use. */
fn
set_status(const char *status) -> void;

/* the prompt of the last line read outside the full-screen layout. */
fn
get_last_prompt() -> koshka::StringView;

/* lays the editor out over the whole terminal for editing a file, with the
   status and hints on the last rows and the menu over the input. */
fn
set_full_screen(bool is_enabled) -> void;

/* sets the longest line in bytes the editor holds, never below the default. */
fn
set_line_max(usize byte_count) -> void;

/* places the caret of the next get_input at the start of a line counted from
   1, or at the end of the input for 0. */
fn
set_start_line(usize line_number) -> void;

fn
get_history_path() -> koshka::Maybe<koshka::Path>;
fn
is_history_contents_valid(StringView contents) -> bool;
fn
encode_history_record(String &output, StringView command) -> void;

fn
write_history() -> koshka::ErrorOr<koshka::Ok>;
fn
read_history() -> koshka::ErrorOr<koshka::Ok>;
fn
sync_history() -> koshka::ErrorOr<koshka::Ok>;
fn
clear_history() -> koshka::ErrorOr<koshka::Ok>;
fn
import_history(StringView contents) -> koshka::ErrorOr<koshka::Ok>;
fn
append_unwritten_history() -> koshka::ErrorOr<koshka::Ok>;

fn
set_history_persistent(bool should_persist) -> void;
fn
set_history_enabled(bool is_enabled) -> void;
fn
set_history_limit(usize entry_count) -> void;

struct history_event
{
  usize  number;
  String command;
};

fn
get_history_events(koshka::Allocator    allocator,
                   koshka::Maybe<usize> after_event_number = koshka::None)
    -> koshka::ErrorOr<koshka::ArrayList<history_event>>;
fn
get_newest_history_event_number() -> koshka::ErrorOr<koshka::Maybe<usize>>;

fn
get_relative_history_event(koshka::Allocator    allocator,
                           usize                distance,
                           koshka::Maybe<usize> before_event_number =
                               koshka::None) -> koshka::Maybe<history_event>;
fn
get_numbered_history_event(koshka::Allocator    allocator,
                           usize                number,
                           koshka::Maybe<usize> before_event_number =
                               koshka::None) -> koshka::Maybe<history_event>;
fn
get_prefixed_history_event(koshka::Allocator    allocator,
                           StringView           prefix,
                           koshka::Maybe<usize> before_event_number =
                               koshka::None) -> koshka::Maybe<history_event>;
fn
get_containing_history_event(koshka::Allocator    allocator,
                             StringView           text,
                             koshka::Maybe<usize> before_event_number =
                                 koshka::None) -> koshka::Maybe<history_event>;

fn
append_history_event(StringView command) -> koshka::Maybe<usize>;
fn
rewrite_history_event(usize number, StringView expected, StringView replacement)
    -> bool;
fn
rewrite_history_event(usize                            number,
                      StringView                       expected,
                      const koshka::ArrayList<String> &replacements) -> bool;
fn
rewrite_history_events(usize                                        number,
                       const koshka::ArrayList<koshka::StringView> &expected,
                       const koshka::ArrayList<String> &replacements) -> bool;

fn
enable_job_notifications(koshka::EvalContext &context) -> void;

fn
set_ghost_enabled(bool is_enabled) -> void;

fn
set_space_after_completion(u8 mode) -> void;

fn
set_history_prefix_search(bool is_enabled) -> void;

fn
set_hint_row(bool should_show_hints, bool should_show_diagnostics) -> void;

fn
set_auto_pair(bool is_enabled) -> void;

fn
set_wrap_width(usize column_count) -> void;

fn
set_extended_keys(bool is_enabled) -> void;

fn
set_highlight_enabled(bool is_enabled) -> void;

fn
set_colors_enabled(bool is_enabled) -> void;

enum class edit_mode : u8
{
  Emacs,
  Vi,
  ViNormal,
};

fn
set_edit_mode(edit_mode mode) -> void;

fn
set_tab_selector(koshka::tab_selector_mode selector) -> void;

fn
get_utf8_length(const String &string, usize byte_count = static_cast<usize>(-1))
    -> usize;

fn
get_utf8_length(const char *bytes, usize byte_count) -> usize;

fn
get_codepoint_byte_offset(const char *bytes,
                          usize       byte_count,
                          usize       codepoint_index) -> usize;

fn
get_escape_sequence_end(StringView text, usize byte_offset) -> usize;

fn
get_display_width(StringView text) -> usize;

fn
get_byte_offset_at_or_before_display_cell(StringView text,
                                          usize      cell_position,
                                          usize &actual_cell_position) -> usize;

fn
is_active() -> bool;

fn
initialize() -> void;

fn
exit(usize history_size_limit = 4096) -> void;

fn
set_title(StringView title) -> void;
fn
set_idle_title() -> void;

struct input_result
{
  i32                  code;
  String               text{koshka::heap_allocator()};
  koshka::Maybe<usize> history_event_number{koshka::None};
};

fn
get_input(const String &prompt) -> input_result;
fn
get_input(const String                &prompt,
          const String                &right_prompt,
          const koshka::Maybe<String> &transient_prompt,
          const koshka::Maybe<String> &transient_right_prompt) -> input_result;

fn
set_input(const String &input) -> void;

fn
enter_raw_mode() -> void;

fn
exit_raw_mode() -> void;

fn
restore_terminal_for_exit() wontthrow -> void;

fn
emit_newlines(StringView buffer) -> void;

} /* namespace toiletline */
