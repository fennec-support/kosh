/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines source locations and the owned error, warning, note, trace,
 * and located-diagnostic hierarchy shared by parsing and evaluation. Errors.cpp
 * owns source interning and source-aware rendering.
 */

#pragma once

#include "base/Common.hpp"
#include "base/Maybe.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka {

class EvalContext;

enum class error_severity : u8
{
  error,
  warning,
  note,
  details,
  trace,
};

pure inline fn get_error_severity_word(error_severity severity) wontthrow
    -> StringView
{
  switch (severity) {
  case error_severity::error: return "error";
  case error_severity::warning: return "warning";
  case error_severity::note: return "note";
  case error_severity::details: return "details";
  case error_severity::trace: return "trace";
  }
  unreachable("invalid error severity %d", ENUM(severity));
}

fn intern_source_name(StringView name) throws -> u32;
fn source_name_at(u32 source_name_index) wontthrow -> Maybe<StringView>;

enum class source_identity_kind : u8
{
  File,
  CommandString,
};

fn source_identity_kind_at(u32 source_name_index) wontthrow
    -> source_identity_kind;

inline constexpr i32 SYNTAX_ERROR_STATUS = 2;
inline constexpr i32 BASH_COMMAND_STRING_FATAL_STATUS = 127;

inline constexpr char COMMAND_STRING_SOURCE_TEXT[] = "-c";
inline constexpr StringView COMMAND_STRING_SOURCE_NAME{
    COMMAND_STRING_SOURCE_TEXT, 2};

struct SourceLocation
{
  u32 position{0};
  u32 length{0};
  u32 source_name_index{0};

  SourceLocation() = default;

  SourceLocation(usize position, usize length,
                 u32 source_name_index = 0) wontthrow
      : position{static_cast<u32>(position)},
        length{static_cast<u32>(length)},
        source_name_index{source_name_index}
  {}

  pure fn get_filename() const wontthrow -> Maybe<StringView>
  {
    return source_name_at(source_name_index);
  }

  pure fn has_same_source_as(const SourceLocation &other) const wontthrow
      -> bool
  {
    return source_name_index == other.source_name_index;
  }

  pure fn get_source_text(StringView source) const wontthrow
      -> Maybe<StringView>
  {
    if (position > source.length || length > source.length - position)
      return None;
    return source.substring_of_length(position, length);
  }

  pure fn subspan(usize relative_position,
                  usize relative_length) const wontthrow -> SourceLocation
  {
    ASSERT(relative_position <= length);
    ASSERT(relative_length <= length - relative_position);
    return SourceLocation{position + relative_position, relative_length,
                          source_name_index};
  }

  fn subspan_for_view(StringView source, StringView part,
                      SourceLocation &storage,
                      usize source_offset = 0) const wontthrow
      -> const SourceLocation *
  {
    ASSERT(part.data >= source.data &&
           part.data + part.length <= source.data + source.length);
    let const part_offset = static_cast<usize>(part.data - source.data);
    if (part_offset < source_offset) return nullptr;
    let const mapped_offset = part_offset - source_offset;
    if (part.length == 0) return nullptr;
    if (mapped_offset > length || part.length > length - mapped_offset)
      return nullptr;
    storage = subspan(mapped_offset, part.length);
    return &storage;
  }
};

struct rendered_site
{
  StringView source;
  SourceLocation location;
  isize line_offset;
};

fn resolve_rendered_site(StringView source, const SourceLocation &location,
                         isize line_offset, bool is_rebased,
                         const EvalContext *context) wontthrow -> rendered_site;

class ErrorBase
{
public:
  virtual ~ErrorBase();

  virtual pure fn message() const wontthrow -> const String & = 0;
  virtual pure fn detail_message() const wontthrow -> StringView { return {}; }

  virtual fn get_severity() const wontthrow -> error_severity;

  virtual fn to_string(StringView source,
                       EvalContext *context = nullptr) const throws -> String;

  fn set_script_fatal() wontthrow -> void { m_is_script_fatal = true; }
  pure fn is_script_fatal() const wontthrow -> bool
  {
    return m_is_script_fatal;
  }
  fn set_line_discarding() wontthrow -> void { m_is_line_discarding = true; }
  pure fn is_line_discarding() const wontthrow -> bool
  {
    return m_is_line_discarding;
  }
  fn set_top_level_line_discarding() wontthrow -> void
  {
    m_is_line_discarding = true;
    m_is_top_level_line_discarding = true;
  }
  pure fn is_top_level_line_discarding() const wontthrow -> bool
  {
    return m_is_top_level_line_discarding;
  }
  fn take_line_discard_marks(const ErrorBase &source) wontthrow -> void
  {
    m_is_line_discarding = source.m_is_line_discarding;
    m_is_top_level_line_discarding = source.m_is_top_level_line_discarding;
  }
  fn set_command_status(i64 status) wontthrow -> void
  {
    m_command_status = status;
  }
  pure fn command_status() const wontthrow -> i64 { return m_command_status; }

  fn set_rendered() wontthrow -> void { m_was_rendered = true; }
  pure fn was_rendered() const wontthrow -> bool { return m_was_rendered; }

protected:
  bool m_was_rendered{false};
  fn trailing_details_to_string() const throws -> String;

  bool m_is_script_fatal{false};
  bool m_is_line_discarding{false};
  bool m_is_top_level_line_discarding{false};
  i64 m_command_status{1};
};

class Error : public ErrorBase
{
public:
  Error(StringView message);

  pure fn message() const wontthrow -> const String & override
  {
    return m_message;
  }

  fn to_string() const throws -> String;
  using ErrorBase::to_string;

protected:
  String m_message{heap_allocator()};
};

class ErrorWithDetails : public Error
{
public:
  ErrorWithDetails(StringView message, StringView note);

  pure fn detail_message() const wontthrow -> StringView override
  {
    return m_note.view();
  }

private:
  String m_note{heap_allocator()};
};

class Warning : public Error
{
public:
  Warning(StringView message);

  fn get_severity() const wontthrow -> error_severity override;
};

class WarningWithDetails : public Warning
{
public:
  WarningWithDetails(StringView message, StringView note);

  pure fn detail_message() const wontthrow -> StringView override
  {
    return m_note.view();
  }

private:
  String m_note{heap_allocator()};
};

class Note : public Error
{
public:
  Note(StringView message);

  fn get_severity() const wontthrow -> error_severity override;
};

class BrokenPipeExit : public Error
{
public:
  BrokenPipeExit();
};

class TrapAbandonedRedirection : public Error
{
public:
  TrapAbandonedRedirection();
};

class ErrorWithLocation : public Error
{
public:
  ErrorWithLocation(SourceLocation location, StringView message);

  fn to_string(StringView source, EvalContext *context = nullptr) const throws
      -> String override;

  fn set_line_offset(isize offset) wontthrow -> void
  {
    m_line_offset = offset;
    m_is_rebased = true;
  }
  fn hide_filename() wontthrow -> void { m_is_filename_hidden = true; }

  pure fn location() const wontthrow -> SourceLocation { return m_location; }
  fn set_location(SourceLocation location) wontthrow -> void
  {
    m_location = steal(location);
  }

protected:
  SourceLocation m_location;
  isize m_line_offset{0};
  bool m_is_rebased{false};
  bool m_is_filename_hidden{false};
};

class InterruptErrorWithLocation : public ErrorWithLocation
{
public:
  explicit InterruptErrorWithLocation(SourceLocation location);
};

class CommandResolutionErrorWithLocation : public ErrorWithLocation
{
public:
  CommandResolutionErrorWithLocation(SourceLocation location,
                                     StringView message,
                                     i64 command_status = 127);
};

class CommandResolutionErrorWithLocationAndDetails
    : public CommandResolutionErrorWithLocation
{
public:
  CommandResolutionErrorWithLocationAndDetails(SourceLocation location,
                                               StringView message,
                                               StringView note,
                                               i64 command_status = 127);

  pure fn detail_message() const wontthrow -> StringView override
  {
    return m_note.view();
  }

private:
  String m_note{heap_allocator()};
};

class WarningWithLocation : public ErrorWithLocation
{
public:
  WarningWithLocation(SourceLocation location, StringView message);

  fn get_severity() const wontthrow -> error_severity override;
};

class WarningWithLocationAndDetails : public WarningWithLocation
{
public:
  WarningWithLocationAndDetails(SourceLocation location, StringView message,
                                StringView note);

  pure fn detail_message() const wontthrow -> StringView override
  {
    return m_note.view();
  }

private:
  String m_note{heap_allocator()};
};

class TraceWithLocation : public ErrorWithLocation
{
public:
  TraceWithLocation(SourceLocation location, StringView message = {});

  fn get_severity() const wontthrow -> error_severity override;
};

class DetailsWithLocation : public ErrorWithLocation
{
public:
  DetailsWithLocation(SourceLocation location, StringView message);

  fn get_severity() const wontthrow -> error_severity override;
  fn to_string(StringView source, EvalContext *context = nullptr) const throws
      -> String override;
};

class ErrorWithLocationAndDetails : public ErrorWithLocation
{
public:
  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              SourceLocation details_location,
                              StringView details_message, StringView note = {});
  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              StringView note);

  pure fn detail_message() const wontthrow -> StringView override
  {
    return m_note.view();
  }

  pure fn details_location() const wontthrow -> SourceLocation
  {
    return m_details_location;
  }

  pure fn details_message() const wontthrow -> StringView
  {
    return m_details_message.view();
  }

  fn details_to_string(StringView source,
                       EvalContext *context = nullptr) const throws -> String;

protected:
  SourceLocation m_details_location;
  String m_details_message;
  String m_note{heap_allocator()};
};

static_assert(std::is_abstract_v<ErrorBase>);
static_assert(std::is_base_of_v<Error, ErrorWithLocation>);
static_assert(!std::is_same_v<ErrorWithDetails, Error>);
static_assert(!std::is_same_v<WarningWithDetails, Warning>);
static_assert(
    !std::is_same_v<WarningWithLocationAndDetails, WarningWithLocation>);

wontreturn inline fn relocate_error(const ErrorBase &error,
                                    const SourceLocation &location) throws
    -> void
{
  if (!error.detail_message().is_empty()) {
    let relocated = ErrorWithLocationAndDetails{
        location, error.message().view(), error.detail_message()};
    if (error.is_script_fatal()) relocated.set_script_fatal();
    relocated.take_line_discard_marks(error);
    relocated.set_command_status(error.command_status());
    throw relocated;
  }

  let relocated = ErrorWithLocation{location, error.message().view()};
  if (error.is_script_fatal()) relocated.set_script_fatal();
  relocated.take_line_discard_marks(error);
  relocated.set_command_status(error.command_status());
  throw relocated;
}

wontreturn inline fn
relocate_if_unlocated(const ErrorBase &error,
                      const SourceLocation &location) throws -> void
{
  try {
    throw;
  } catch (const ErrorWithLocation &) {
    throw;
  } catch (const ErrorBase &) {
    relocate_error(error, location);
  }
}

} /* namespace koshka */
