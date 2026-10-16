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
build_prompt(koshka::EvalContext &context) -> String;

fn
build_right_prompt(koshka::EvalContext &context) -> String;

fn
build_transient_prompt(koshka::EvalContext &context) -> String;

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
enable_completion(koshka::EvalContext &context) -> void;
fn
disable_completion() -> void;

fn
is_completion_enabled() -> bool;

fn
enter_calc_history() -> void;
fn
leave_calc_history() -> void;

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
set_extended_keys(bool is_enabled) -> void;

fn
set_highlight_enabled(bool is_enabled) -> void;

fn
set_colors_enabled(bool is_enabled) -> void;

enum class edit_mode : u8
{
  Emacs,
  Vi,
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
                          usize       byte_length,
                          usize       codepoint_index) -> usize;

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
get_input(const String &prompt,
          const String &right_prompt,
          const String &transient_prompt) -> input_result;

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
