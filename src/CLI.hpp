/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements command-line and builtin option parsing. It owns flag
 * declarations, help rendering, operand collection, validation, and located
 * usage errors. It also declares the ReportTable grid whose rendering rules are
 * described in CLI.cpp.
 */

#pragma once

#include "Errors.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"

#define FLAG_LIST T__FLAG_LIST

#define HELP_SYNOPSIS T__FLAG_HELP_SYNOPSIS

#define HELP_SYNOPSIS_DECL(...)                                                \
  static koshka::SynopsisList HELP_SYNOPSIS { __VA_ARGS__ }

#define HELP_DESCRIPTION T__FLAG_HELP_DESCRIPTION

#define HELP_DESCRIPTION_DECL(text)                                            \
  maybeunused static constexpr koshka::StringView HELP_DESCRIPTION { text }

#define FLAG_LIST_DECL() static koshka::FlagList FLAG_LIST

#define T__FLAG_SELECT(_1, _2, _3, _4, _5, _6, name, ...) name
#define FLAG(...)                                         T__FLAG_SELECT(__VA_ARGS__, T__FLAG6, T__FLAG5)(__VA_ARGS__)
#define T__FLAG5(var_name, kind, short_name, long_name, description)           \
  T__FLAG6(var_name, kind, short_name, long_name, NoSection, description)
#define T__FLAG6(var_name, kind, short_name, long_name, section, description)  \
  static koshka::Flag##kind concat_literal(FLAG_, var_name)                    \
  {                                                                            \
    FLAG_LIST, short_name, long_name, koshka::flag_section::section,           \
        description                                                            \
  }

#define T__FLAG_OPTIONAL_SELECT(_1, _2, _3, _4, _5, _6, _7, name, ...) name
#define FLAG_OPTIONAL(...)                                                     \
  T__FLAG_OPTIONAL_SELECT(__VA_ARGS__, T__FLAG_OPTIONAL7, T__FLAG_OPTIONAL6,   \
                          T__FLAG_OPTIONAL5)                                   \
  (__VA_ARGS__)
#define T__FLAG_OPTIONAL5(var_name, short_name, long_name, description,        \
                          acceptor)                                            \
  T__FLAG_OPTIONAL6(var_name, short_name, long_name, description, acceptor,    \
                    "...")
#define T__FLAG_OPTIONAL6(var_name, short_name, long_name, description,        \
                          acceptor, value_name)                                \
  T__FLAG_OPTIONAL7(var_name, short_name, long_name, NoSection, description,   \
                    acceptor, value_name)
#define T__FLAG_OPTIONAL7(var_name, short_name, long_name, section,            \
                          description, acceptor, value_name)                   \
  static koshka::FlagOptionalValue concat_literal(FLAG_, var_name)             \
  {                                                                            \
    FLAG_LIST, short_name, long_name, koshka::flag_section::section,           \
        description, acceptor, value_name                                      \
  }

namespace koshka {

class ExecContext;
class Flag;

class SynopsisList
{
public:
  SynopsisList(std::initializer_list<StringView> lines) wontthrow
  {
    if (lines.size() > countof(m_lines))
      TRAP("the help synopsis exceeds its fixed capacity");
    for (let const line : lines)
      m_lines[m_count++] = line;
  }

  pure fn count() const wontthrow -> usize { return m_count; }
  pure fn begin() const wontthrow -> const StringView * { return m_lines; }
  pure fn end() const wontthrow -> const StringView *
  {
    return m_lines + m_count;
  }
  pure fn operator[](usize index) const wontthrow->StringView
  {
    ASSERT(index < m_count);
    return m_lines[index];
  }

private:
  StringView m_lines[4]{};
  usize m_count{0};
};

class FlagList
{
public:
  fn push(Flag *flag) wontthrow -> void
  {
    if (m_count >= countof(m_flags))
      TRAP("the flag registry exceeds its fixed capacity");
    m_flags[m_count++] = flag;
  }

  pure fn count() const wontthrow -> usize { return m_count; }
  pure fn begin() const wontthrow -> Flag *const * { return m_flags; }
  pure fn end() const wontthrow -> Flag *const * { return m_flags + m_count; }
  pure fn operator[](usize index) const wontthrow->Flag *
  {
    ASSERT(index < m_count);
    return m_flags[index];
  }

private:
  Flag *m_flags[64]{};
  usize m_count{0};
};

enum class flag_section : u8
{
  NoSection,
  Posix,
  Bash,
  Compat,
  Auxiliary,
  Kosh,
  Live,
  Debug,
};

extern const usize HELP_WRAP_WIDTH;
extern const usize HELP_INDENT;

class Flag
{
public:
  enum class Kind : u8
  {
    Bool,
    RepeatedBool,
    String,
    ManyStrings,
    OptionalValue,
  };

  pure fn kind() const wontthrow -> Kind;
  pure fn position() const wontthrow -> usize;
  fn set_position(u32 position) throws -> void;
  pure fn value_location() const wontthrow -> SourceLocation;
  fn set_value_location(SourceLocation location) wontthrow -> void;
  pure fn short_name() const wontthrow -> char;
  pure fn long_name() const wontthrow -> StringView;
  pure fn section() const wontthrow -> flag_section;
  pure fn description() const wontthrow -> StringView;

protected:
  Flag(Kind type, char short_name, StringView long_name, flag_section section,
       StringView description);

  Kind m_kind;
  usize m_position{0};
  SourceLocation m_value_location{};
  char m_short_name;
  flag_section m_section;
  StringView m_long_name;
  StringView m_description;
};

class FlagBool : public Flag
{
public:
  FlagBool(FlagList &flags, char short_name, StringView long_name,
           flag_section section, StringView description);
  FlagBool(char short_name, StringView long_name, flag_section section,
           StringView description);

  fn enable() wontthrow -> void;
  fn disable() wontthrow -> void;
  fn toggle() throws -> void;
  pure fn is_enabled() const wontthrow -> bool;

  fn reset() throws -> void;

private:
  bool m_value{false};
};

class FlagRepeatedBool : public Flag
{
public:
  FlagRepeatedBool(FlagList &flags, char short_name, StringView long_name,
                   flag_section section, StringView description);
  FlagRepeatedBool(char short_name, StringView long_name, flag_section section,
                   StringView description);

  fn increment() throws -> void;
  pure fn count() const wontthrow -> usize;

  fn reset() throws -> void;

private:
  usize m_count{0};
};

class FlagString : public Flag
{
public:
  FlagString(FlagList &flags, char short_name, StringView long_name,
             flag_section section, StringView description);
  FlagString(char short_name, StringView long_name, flag_section section,
             StringView description);

  fn set(StringView v) throws -> void;
  pure fn is_set() const wontthrow -> bool;
  pure fn value() const wontthrow -> StringView;

  fn reset() throws -> void;

private:
  bool m_is_set{false};
  String m_value{heap_allocator()};
};

class FlagManyStrings : public Flag
{
public:
  FlagManyStrings(FlagList &flags, char short_name, StringView long_name,
                  flag_section section, StringView description);
  FlagManyStrings(char short_name, StringView long_name, flag_section section,
                  StringView description);

  fn append(StringView v, usize position = 0, SourceLocation location = {},
            bool was_given_after_plus = false) throws -> void;
  pure fn count() const wontthrow -> usize;
  pure fn is_empty() const wontthrow -> bool;

  pure fn get(usize i) const wontthrow -> StringView;
  pure fn get_position(usize i) const wontthrow -> usize;
  pure fn get_location(usize i) const wontthrow -> SourceLocation;
  pure fn was_given_after_plus(usize i) const wontthrow -> bool;

  fn take_next() wontthrow -> String;
  pure fn at_end() const wontthrow -> bool;
  pure fn value_position() const wontthrow -> usize { return m_value_position; }

  fn reset() throws -> void;

private:
  ArrayList<String> m_values{heap_allocator()};
  ArrayList<usize> m_positions{heap_allocator()};
  ArrayList<SourceLocation> m_locations{heap_allocator()};
  ArrayList<bool> m_plus_markers{heap_allocator()};
  usize m_value_position{0};
};

class FlagOptionalValue : public Flag
{
public:
  using value_acceptor = bool (*)(StringView);

  FlagOptionalValue(FlagList &flags, char short_name, StringView long_name,
                    flag_section section, StringView description,
                    value_acceptor should_accept_value,
                    StringView value_name = "...");
  FlagOptionalValue(char short_name, StringView long_name, flag_section section,
                    StringView description, value_acceptor should_accept_value,
                    StringView value_name = "...");

  fn enable() wontthrow -> void;
  fn set(StringView value) throws -> void;
  pure fn is_enabled() const wontthrow -> bool;
  pure fn has_value() const wontthrow -> bool;
  pure fn value() const wontthrow -> StringView;
  pure fn value_name() const wontthrow -> StringView;
  pure fn should_accept_value(StringView value) const wontthrow -> bool;

  fn reset() throws -> void;

private:
  bool m_is_enabled{false};
  bool m_has_value{false};
  String m_value{heap_allocator()};
  value_acceptor m_should_accept_value;
  StringView m_value_name;
};

struct flag_parse_options
{
  bool should_accept_negative_number_operand{false};
  bool should_allow_options_after_operands{false};
  bool should_accept_unknown_flag_operand{false};
  bool should_omit_program_name{false};
  StringView plus_letters{};
};

fn parse_flags_vec(const FlagList &flags, const ArrayList<String> &args,
                   usize base_position = 0,
                   const Flag *operand_value_flag = nullptr,
                   const ArrayList<SourceLocation> *arg_locations = nullptr,
                   ArrayList<SourceLocation> *operand_locations = nullptr,
                   StringView program_name = StringView{},
                   flag_parse_options parse_options = {},
                   Allocator allocator = heap_allocator()) throws
    -> ArrayList<String>;
fn parse_flags(const FlagList &flags, int argc, const char *const *argv,
               usize base_position = 0,
               const Flag *operand_value_flag = nullptr,
               const ArrayList<SourceLocation> *arg_locations = nullptr,
               ArrayList<SourceLocation> *operand_locations = nullptr,
               StringView program_name = StringView{},
               flag_parse_options parse_options = {},
               Allocator allocator = heap_allocator()) throws
    -> ArrayList<String>;
struct util_operands_result
{
  ArrayList<String> operands;
  ArrayList<SourceLocation> operand_locations;
};

fn parse_util_operands(const FlagList &flags, const ArrayList<String> &args,
                       Allocator allocator,
                       const ArrayList<SourceLocation> *arg_locations = nullptr,
                       flag_parse_options parse_options = {}) throws
    -> util_operands_result;
fn parse_until_subcommand(
    const FlagList &flags, const ArrayList<String> &args,
    const ArrayList<SourceLocation> *arg_locations = nullptr,
    ArrayList<SourceLocation> *operand_locations = nullptr,
    StringView program_name = StringView{}) throws -> usize;

fn join_command_line(int argc, const char *const *argv) throws -> String;

pure fn arg_needs_shell_quoting(StringView arg) wontthrow -> bool;
pure fn shell_quoted_arg_length(StringView arg) wontthrow -> usize;
fn append_shell_quoted_arg(String &out, StringView arg,
                           bool should_always_quote = false) throws -> void;

fn reset_flags(const FlagList &flags) throws -> void;

fn show_version() throws -> void;
fn short_version_string(Allocator allocator) throws -> String;
fn show_short_version() throws -> void;

fn make_synopsis(StringView program_name, const SynopsisList &lines) throws
    -> String;
fn make_flag_help(const FlagList &flags, bool should_color = false) throws
    -> String;

fn wrap_text(StringView text, usize indent, usize width,
             const Maybe<usize> &continuation_indent = {}) throws -> String;

enum class cli_color_mode : u8
{
  Auto,
  Always,
  Never,
};

fn parse_cli_color_mode(StringView text) wontthrow -> Maybe<cli_color_mode>;
fn stdout_wants_color(cli_color_mode mode) throws -> bool;

struct tree_connector
{
  StringView branch;
  StringView continuation;
};

pure fn get_tree_connector(bool is_last) wontthrow -> tree_connector;

fn append_report_text(String &output, StringView text, StringView style,
                      bool should_color) throws -> void;
enum class report_table_alignment : u8
{
  Left,
  Right,
};
struct report_table_column
{
  String heading;
  report_table_alignment alignment;
  StringView style;
  usize min_width{0};
};
struct report_table_cell_view
{
  StringView text;
  StringView style;
};
struct report_table_cell
{
  String text;
  StringView style;
};
class ReportTable
{
public:
  explicit ReportTable(Allocator allocator)
      : m_columns(allocator), m_grid_rows(allocator)
  {}

  fn add_column(StringView heading,
                report_table_alignment alignment = report_table_alignment::Left,
                StringView style = {}) throws -> void;
  fn add_heading(StringView heading, report_table_alignment alignment =
                                         report_table_alignment::Left) throws
      -> void;
  fn set_header_visible(bool should_show_header) wontthrow -> void
  {
    m_should_show_header = should_show_header;
  }
  fn clear_rows() wontthrow -> void { m_grid_rows.clear(); }
  fn set_column_gap(usize space_count) wontthrow -> void
  {
    m_column_gap_space_count = space_count;
  }
  pure fn get_row_count() const wontthrow -> usize
  {
    return m_grid_rows.count();
  }
  fn add_row(const ArrayList<report_table_cell_view> &cells) throws -> void;
  fn add_field_row(StringView name, StringView value,
                   StringView name_style) throws -> void;
  fn to_string() const throws -> String;
  fn to_string(bool should_color, StringView indentation = "  ") const throws
      -> String;

private:
  ArrayList<report_table_column> m_columns;
  ArrayList<ArrayList<report_table_cell>> m_grid_rows;
  bool m_should_show_header{true};
  usize m_column_gap_space_count{2};
};
fn append_titled_report_table(String &output, StringView title,
                              const ReportTable &table,
                              bool should_color) throws -> void;
fn append_report_name_section(String &output, StringView title,
                              const ArrayList<StringView> &names,
                              bool should_color,
                              StringView indentation = {}) throws -> void;
fn format_cli_help(StringView text, bool should_color) throws -> String;
fn format_cli_help(StringView text) throws -> String;
fn enter_alternate_screen(const ExecContext &ec) wontthrow -> bool;
fn leave_alternate_screen(const ExecContext &ec) wontthrow -> void;
fn hide_cursor(const ExecContext &ec) wontthrow -> bool;
fn show_cursor(const ExecContext &ec) wontthrow -> void;
fn format_live_duration(f64 seconds, Allocator allocator) throws -> String;

struct rolling_window_boundary
{
  usize before_index{0};
  usize after_index{0};
  u64 timestamp{0};
};

pure fn rolling_window_start(u64 now_nanoseconds,
                             u64 window_nanoseconds) wontthrow -> u64;

template <class T>
struct rolling_history
{
  ArrayList<T> samples;
  ArrayList<u64> timestamps;
  u64 last_seen_nanoseconds{0};

  rolling_history() : rolling_history(heap_allocator()) {}

  explicit rolling_history(Allocator allocator)
      : samples(allocator), timestamps(allocator)
  {}

  fn push(const T &value, u64 now_nanoseconds) throws -> void
  {
    samples.push(value);
    timestamps.push(now_nanoseconds);
  }

  fn clear() wontthrow -> void
  {
    samples.clear();
    timestamps.clear();
  }

  pure fn get_newest() const wontthrow -> const T & { return samples.back(); }

  pure fn get_newest_timestamp() const wontthrow -> u64
  {
    return timestamps.back();
  }

  fn trim(u64 window_start_nanoseconds) throws -> void
  {
    ASSERT(samples.count() == timestamps.count());
    while (timestamps.count() > 2 && timestamps[1] <= window_start_nanoseconds)
    {
      samples.remove(0);
      timestamps.remove(0);
    }
  }

  pure fn get_boundary(u64 window_start_nanoseconds) const wontthrow
      -> rolling_window_boundary
  {
    ASSERT(!timestamps.is_empty());
    if (window_start_nanoseconds <= timestamps[0]) return {0, 0, timestamps[0]};

    usize before = 0;
    while (before + 1 < timestamps.count() &&
           timestamps[before + 1] <= window_start_nanoseconds)
      before++;

    if (before + 1 == timestamps.count())
      return {before, before, timestamps[before]};

    return {before, before + 1, window_start_nanoseconds};
  }

  pure fn interpolate(const rolling_window_boundary &boundary, u64 before_value,
                      u64 after_value) const wontthrow -> Maybe<u64>
  {
    if (after_value < before_value) return None;

    let const before_timestamp = timestamps[boundary.before_index];
    let const after_timestamp = timestamps[boundary.after_index];
    if (boundary.timestamp <= before_timestamp) return before_value;
    if (boundary.timestamp >= after_timestamp) return after_value;
    if (after_timestamp <= before_timestamp) return before_value;

    let const elapsed = after_timestamp - before_timestamp;
    let const passed = boundary.timestamp - before_timestamp;
    return before_value +
           static_cast<u64>(static_cast<u128>(after_value - before_value) *
                            passed / elapsed);
  }
};

fn show_message(StringView err) throws -> void;
fn show_warning(StringView warning) throws -> void;
fn show_report_warning(StringView warning) throws -> void;
fn show_report_warnings(const ArrayList<String> &warnings) throws -> void;

fn arm_message_leading_newline(bool armed) wontthrow -> void;

fn print(StringView text) throws -> void;
fn print_error(StringView text) throws -> void;
fn flush() throws -> void;

}
