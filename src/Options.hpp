/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the option registry that set, shopt, koshconf, and the
 * -o test read and write. Each entry carries a stable numeric id, its koshconf
 * name, its Bash spellings, its letter, its type, its class, and its kosh mood
 * value.
 */

#pragma once

#include "base/Common.hpp"
#include "base/Maybe.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka {

class EvalContext;
enum class shell_option_id : u8;

enum class option_type : u8
{
  Boolean,
  Enum,
  String,
};

enum class option_class : u8
{
  Interactive,
  Semantic,
};

enum class option_storage : u8
{
  ShellOption,
  Shopt,
  Failglob,
  Posix,
  Vi,
  Emacs,
  Mood,
  TabSelector,
  WarningLevel,
  AnnoyingDiagnostics,
  Analysis,
  Login,
  RestrictedShell,
  Variable,
};

enum class option_origin : u8
{
  Set,
  Shopt,
  Koshconf,
  Startup,
};

struct option_text
{
  const char *data{nullptr};
  usize length{0};

  constexpr option_text() = default;
  template <usize Count>
  consteval option_text(const char (&text)[Count])
      : data(text), length(Count - 1)
  {}
  constexpr operator StringView() const wontthrow
  {
    return StringView{data, length};
  }
  constexpr fn is_empty() const wontthrow -> bool { return length == 0; }
};

struct option_enum_values
{
  const option_text *names{nullptr};
  u8 count{0};
};

struct option_descriptor
{
  option_text koshconf_name;
  option_text set_name;
  option_text shopt_name;
  option_text variable_name;
  option_text help;
  option_enum_values enum_values;
  u16 id;
  option_type type;
  option_class category;
  option_storage storage;
  shell_option_id shell_option;
  char letter;
  u8 default_value;
  u8 bash_default_value;
  u8 strict_value;
  bool is_fixed_in_kosh_mood;
  bool is_read_only;
  bool is_session_dependent;
  bool is_listed_by_set;

  pure fn is_bash_option() const wontthrow -> bool
  {
    return !set_name.is_empty() || !shopt_name.is_empty();
  }
  pure fn is_legacy() const wontthrow -> bool
  {
    return StringView{koshconf_name}.starts_with(StringView{"legacy."});
  }
  pure fn is_serialized() const wontthrow -> bool
  {
    return !is_read_only && !is_session_dependent &&
           (category == option_class::Interactive ||
            storage == option_storage::Mood);
  }
};

struct option_registry_view
{
  const option_descriptor *entries;
  usize entry_count;

  pure fn begin() const wontthrow -> const option_descriptor *
  {
    return entries;
  }
  pure fn end() const wontthrow -> const option_descriptor *
  {
    return entries + entry_count;
  }
  pure fn count() const wontthrow -> usize { return entry_count; }
  pure fn operator[](usize index) const wontthrow->const option_descriptor &
  {
    return entries[index];
  }
};

pure fn get_option_registry() wontthrow -> option_registry_view;
pure fn find_option_by_id(u16 id) wontthrow -> const option_descriptor *;
fn find_option_by_koshconf_name(StringView name) wontthrow
    -> const option_descriptor *;
fn find_option_by_set_name(StringView name) wontthrow
    -> const option_descriptor *;
fn find_option_by_shopt_name(StringView name) wontthrow
    -> const option_descriptor *;
pure fn find_option_by_letter(char letter) wontthrow
    -> const option_descriptor *;
pure fn get_set_listing_order() wontthrow -> option_registry_view;
pure fn get_shell_flag_letter_order() wontthrow -> StringView;

fn option_is_available(const EvalContext &cxt,
                       const option_descriptor &option) wontthrow -> bool;
fn read_option_number(const EvalContext &cxt,
                      const option_descriptor &option) throws -> u32;
fn read_option_text(const EvalContext &cxt,
                    const option_descriptor &option) throws -> String;
fn format_option_number(const option_descriptor &option, u32 value) throws
    -> String;
fn parse_option_number(const option_descriptor &option, StringView text) throws
    -> Maybe<u32>;
fn describe_option_values(const option_descriptor &option) throws -> String;
fn write_option_number(EvalContext &cxt, const option_descriptor &option,
                       u32 value, option_origin origin) throws -> void;
fn write_option_text(EvalContext &cxt, const option_descriptor &option,
                     StringView text, option_origin origin) throws -> void;
fn step_warning_level(EvalContext &cxt, bool should_raise) throws -> void;

} /* namespace koshka */
