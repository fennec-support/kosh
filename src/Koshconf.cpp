/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file reads and writes the koshconf files, builds the preset files, and
 * encodes and decodes the binary KOSHCONF form. A malformed line or record
 * becomes a warning and never stops the shell.
 */

#include "Koshconf.hpp"

#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Koshkit.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace {

constexpr u32 KOSHCONF_DIRECTORY_MODE = 0755;
constexpr usize KOSHCONF_COMMENT_WIDTH = 80;
constexpr u32 KOSHCONF_FILE_MODE = 0644;
constexpr u32 KOSHCONF_PERMISSION_BITS = 07777;
constexpr usize KOSHCONF_LINK_LIMIT = 40;
constexpr u32 MAX_CODEPOINT = 0x10ffff;
constexpr u32 INVALID_CODEPOINT = MAX_CODEPOINT + 1;
constexpr StringView UTF8_BYTE_ORDER_MARK{"\xef\xbb\xbf"};
constexpr StringView HISTORY_MAX_ENTRIES_NAME{"history.max_entries"};
constexpr u64 HISTORY_MAX_ENTRIES_LIMIT = 2147483647;

enum class escape_style : u8
{
  Message,
  AnsiC,
};

fn quote_for_value(StringView value) wontthrow -> Maybe<char>
{
  if (value.is_empty()) return None;
  let const first = value[0];
  let const last = value[value.count() - 1];
  let const needs_quotes = first == '"' || first == '\'' || first == ' ' ||
                           first == '\t' || last == ' ' || last == '\t';
  if (!needs_quotes) return None;
  return value.find_character('"').has_value() ? '\'' : '"';
}

fn decode_strict_utf8(StringView text, usize position) wontthrow
    -> utils::decoded_codepoint
{
  let const decoded = utils::decode_utf8(text, position, INVALID_CODEPOINT);
  let const is_overlong = (decoded.length == 2 && decoded.value < 0x80) ||
                          (decoded.length == 3 && decoded.value < 0x800) ||
                          (decoded.length == 4 && decoded.value < 0x10000);
  let const is_surrogate = decoded.value >= 0xd800 && decoded.value <= 0xdfff;
  if (is_overlong || is_surrogate || decoded.value > MAX_CODEPOINT) {
    return {INVALID_CODEPOINT, 1};
  }

  return decoded;
}

fn is_valid_utf8(StringView text) wontthrow -> bool
{
  for (usize position = 0; position < text.count();) {
    let const decoded = decode_strict_utf8(text, position);
    if (decoded.value == INVALID_CODEPOINT) return false;
    position += decoded.length;
  }

  return true;
}

fn is_decimal_count(StringView text) wontthrow -> bool
{
  if (text.is_empty()) return false;
  u64 value = 0;
  for (usize position = 0; position < text.count(); position++) {
    if (text[position] < '0' || text[position] > '9') {
      return false;
    }
    value = value * 10 + static_cast<u64>(text[position] - '0');
    if (value > HISTORY_MAX_ENTRIES_LIMIT) return false;
  }

  return true;
}

fn append_escaped_text(String &out, StringView text, escape_style style) throws
    -> void
{
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  for (usize position = 0; position < text.count();) {
    let const byte = static_cast<u8>(text[position]);
    let const decoded = decode_strict_utf8(text, position);
    if (byte >= 0x80 && decoded.value != INVALID_CODEPOINT) {
      out += text.substring_of_length(position, decoded.length);
      position += decoded.length;
      continue;
    }

    position++;
    let const is_quoting_byte = byte == '\\' || byte == '\'';
    if (style == escape_style::AnsiC && is_quoting_byte) {
      out.push('\\');
      out.push(static_cast<char>(byte));
      continue;
    }
    switch (byte) {
    case '\n': out += "\\n"; continue;
    case '\r': out += "\\r"; continue;
    case '\t': out += "\\t"; continue;
    default: break;
    }
    if (byte >= 0x20 && byte < 0x7f) {
      out.push(static_cast<char>(byte));
      continue;
    }

    out += "\\x";
    out.push(HEX_DIGITS[byte >> 4]);
    out.push(HEX_DIGITS[byte & 0xf]);
  }
}

fn is_crlf_carriage_return(StringView text, usize position) wontthrow -> bool
{
  return text[position] == '\r' && position + 1 < text.count() &&
         text[position + 1] == '\n';
}

fn printable_offset(StringView text, usize offset) wontthrow -> usize
{
  usize removed_count = 0;
  for (usize position = 0; position < offset; position++) {
    if (is_crlf_carriage_return(text, position)) removed_count++;
  }

  return offset - removed_count;
}

fn make_printable_copy(StringView text) throws -> String
{
  let copy = String{heap_allocator()};
  copy.reserve(text.count());
  for (usize position = 0; position < text.count();) {
    let const byte = static_cast<u8>(text[position]);
    let const decoded = decode_strict_utf8(text, position);
    let const is_control =
        (byte < 0x20 && byte != '\t' && byte != '\n') || byte == 0x7f;
    if (is_crlf_carriage_return(text, position)) {
      position++;
      continue;
    }
    if (decoded.value == INVALID_CODEPOINT || is_control) {
      copy.push('?');
    } else {
      copy += text.substring_of_length(position, decoded.length);
    }
    position += decoded.length;
  }

  return copy;
}

fn escape_for_message(StringView text) throws -> String
{
  let escaped = String{heap_allocator()};
  append_escaped_text(escaped, text, escape_style::Message);
  return escaped;
}

fn append_varint(String &output, u32 value) throws -> void
{
  while (value >= 0x80) {
    output.push(static_cast<char>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  output.push(static_cast<char>(value));
}

fn read_varint(StringView bytes, usize &position) wontthrow -> Maybe<u32>
{
  u32 value = 0;
  for (u32 shift = 0; shift < 35; shift += 7) {
    if (position >= bytes.count()) return None;
    let const byte = static_cast<u8>(bytes[position++]);
    if (shift == 28 && (byte & 0x70) != 0) return None;
    if (shift > 0 && byte == 0) {
      return None;
    }
    value |= static_cast<u32>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) return value;
  }
  return None;
}

fn encode_value(const EvalContext &cxt, const option_descriptor &option) throws
    -> String
{
  let value = String{heap_allocator()};
  switch (option.type) {
  case option_type::Boolean:
    value.push(read_option_number(cxt, option) != 0 ? '\1' : '\0');
    break;
  case option_type::Enum:
    append_varint(value, read_option_number(cxt, option));
    break;
  case option_type::String: value = read_option_text(cxt, option); break;
  }
  return value;
}

fn decode_value(const option_descriptor &option, StringView bytes) throws
    -> Maybe<String>
{
  switch (option.type) {
  case option_type::Boolean:
    if (bytes.count() != 1 || static_cast<u8>(bytes[0]) > 1) return None;
    return format_option_number(option, static_cast<u8>(bytes[0]));
  case option_type::Enum: {
    usize position = 0;
    let const number = read_varint(bytes, position);
    if (!number.has_value() || position != bytes.count() ||
        *number >= option.enum_values.name_count)
    {
      return None;
    }
    return format_option_number(option, *number);
  }
  case option_type::String:
    if (find_koshconf_value_problem(option, bytes).has_value()) return None;
    return String{bytes};
  }
  return None;
}

fn declared_name(StringView line) wontthrow -> Maybe<StringView>
{
  let const trimmed = line.trim_blanks();
  if (trimmed.is_empty() || trimmed[0] == '#') return None;
  let const equals = line.find_character('=');
  if (!equals.has_value()) return None;
  return line.substring_of_length(0, *equals).trim_blanks();
}

fn preset_group_of(const option_descriptor &option) wontthrow -> StringView
{
  let const name = StringView{option.koshconf_name};
  if (!option.is_legacy()) {
    let const dot = name.find_character('.');
    return dot.has_value() ? name.substring_of_length(0, *dot) : name;
  }

  let const topic_start = StringView{"legacy."}.count();
  let const topic = name.substring(topic_start);
  let const underscore = topic.find_character('_');
  return name.substring_of_length(
      0, topic_start + (underscore.has_value() ? *underscore : topic.count()));
}

fn get_preset_group_first_id(const option_descriptor &option) wontthrow -> u16
{
  let const group = preset_group_of(option);
  let first_id = option.id;
  for (let const &other : get_option_registry()) {
    if (other.is_legacy() || other.id >= first_id) continue;
    if (preset_group_of(other) == group) first_id = other.id;
  }

  return first_id;
}

fn preset_sorts_before(const option_descriptor &left,
                       const option_descriptor &right) wontthrow -> bool
{
  if (left.is_legacy() != right.is_legacy()) return right.is_legacy();
  if (!left.is_legacy()) {
    let const left_group_id = get_preset_group_first_id(left);
    let const right_group_id = get_preset_group_first_id(right);
    if (left_group_id != right_group_id) return left_group_id < right_group_id;

    return left.id < right.id;
  }

  return StringView{left.koshconf_name} < StringView{right.koshconf_name};
}

fn resolve_koshconf_target(const Path &path) throws -> Path
{
  if (let resolved = os::canonical_path(path); resolved.has_value()) {
    return steal(*resolved);
  }

  let target = Path{path.view()};
  for (usize depth = 0;
       depth < KOSHCONF_LINK_LIMIT && os::path_is_symbolic_link(target.view());
       depth++)
  {
    let const link = os::read_symlink(target.view(), heap_allocator());
    if (!link.has_value()) break;

    let next = Path{link->view()};
    if (!next.is_absolute()) {
      next = target.parent_or_current();
      next.append(link->view());
    }
    target = steal(next);
  }

  return target;
}

fn prepare_koshconf_target(const Path &path) throws -> Path
{
  let target = resolve_koshconf_target(path);
  let const directory = target.parent_or_current();
  if (!koshkit::make_directories(directory, KOSHCONF_DIRECTORY_MODE)) {
    throw Error{"Unable to create the directory '" + directory.text() +
                "': " + os::last_system_error_message()};
  }

  return target;
}

fn lock_koshconf_directory(const Path &target) throws -> os::descriptor
{
  let const directory = target.parent_or_current();
  let const lock = os::acquire_process_lock(directory.view());
  if (!lock.has_value()) {
    throw Error{"Unable to lock the directory '" + directory.text() +
                "': " + os::last_system_error_message()};
  }

  return *lock;
}

fn replace_koshconf_target(const Path &target, StringView contents) throws
    -> void
{
  let status = os::file_status{};
  let const mode = os::stat_path_following(target.view(), status)
                       ? status.mode & KOSHCONF_PERMISSION_BITS
                       : KOSHCONF_FILE_MODE & ~os::get_file_creation_mask();
  let const directory = target.parent_or_current();
  let const temporary =
      os::write_to_named_temp_file(directory, ".kosh.conf", contents);
  if (!temporary.has_value()) {
    throw Error{"Unable to write a file in '" + directory.text() +
                "': " + os::last_system_error_message()};
  }

  let const do_discard = [&](StringView action) throws {
    let const message = os::last_system_error_message();
    unused(os::remove_file(temporary->view()));
    throw Error{StringView{"Unable to "} + action + " '" + target.text() +
                "': " + message};
  };
  if (!os::set_file_mode(temporary->view(), mode))
    do_discard("set the mode of");
  if (!os::sync_path(temporary->view(), os::sync_mode::All)) {
    do_discard("flush the replacement of");
  }
  if (!os::rename_path(temporary->view(), target.view())) {
    do_discard("replace");
  }

  LOG(Info, "wrote the koshconf file '%s'", target.text().c_str());
}

} /* namespace */

fn get_user_koshconf_path() throws -> Maybe<Path>
{
  let config_home = Maybe<Path>{};
  if (let const xdg = os::get_environment_variable("XDG_CONFIG_HOME");
      xdg.has_value() && !xdg->is_empty() && Path{xdg->view()}.is_absolute())
  {
    config_home = Path{xdg->view()};
  } else if (let home = os::get_home_directory(); home.has_value()) {
    home->append(".config");
    config_home = steal(*home);
  }
  if (!config_home.has_value()) return None;

  config_home->append("kosh");
  config_home->append("kosh.conf");
  return config_home;
}

fn parse_mood_list(StringView list, ArrayList<mimic_mood> &moods) throws
    -> Maybe<StringView>
{
  usize name_start = 0;
  for (usize position = 0; position <= list.count(); position++) {
    if (position != list.count() && list[position] != ',') continue;

    let const name =
        list.substring_of_length(name_start, position - name_start);
    name_start = position + 1;
    if (name.is_empty()) continue;

    let const mood = parse_mood_name(name);
    if (!mood.has_value()) return name;

    moods.push(*mood);
  }

  return None;
}

pure fn koshconf_option_takes_count(const option_descriptor &option) wontthrow
    -> bool
{
  return StringView{option.koshconf_name} == HISTORY_MAX_ENTRIES_NAME;
}

fn suggest_koshconf_option_name(StringView name) throws -> Maybe<String>
{
  if (name.is_empty()) return None;

  constexpr usize ENDING_EDIT_COUNT = 2;
  let suggestion = utils::NameSuggestion{name};
  Maybe<String> topic_match{};
  Maybe<String> ending_match{};
  usize ending_match_length = 0;
  for (let const &option : get_option_registry()) {
    if (option.is_set_alias) continue;

    let const candidate = StringView{option.koshconf_name};
    suggestion.consider(candidate);
    let const dot = candidate.find_character('.');
    if (dot.has_value() && candidate.substring(*dot + 1) == name &&
        !topic_match.has_value())
    {
      topic_match = String{candidate};
    }

    usize shared_length = 0;
    while (shared_length < name.length && shared_length < candidate.length &&
           name[shared_length] == candidate[shared_length])
    {
      shared_length++;
    }
    if (shared_length + ENDING_EDIT_COUNT >= name.length && dot.has_value() &&
        shared_length > *dot + 1 && shared_length > ending_match_length)
    {
      ending_match = String{candidate};
      ending_match_length = shared_length;
    }
  }

  if (let close_match = suggestion.take_suggestion(); close_match.has_value())
    return close_match;
  if (topic_match.has_value()) return topic_match;

  return ending_match;
}

fn describe_kosh_mood_hold(const option_descriptor &option) throws -> String
{
  return StringView{"The kosh mood keeps '"} + option.koshconf_name + "' " +
         format_option_number(option, option.strict_value) +
         ", so the configured value is skipped; set mood=bash or run "
         "`koshconf set mood bash` to change it";
}

fn find_koshconf_value_problem(const option_descriptor &option,
                               StringView value) throws -> Maybe<String>
{
  let const do_describe = [&](StringView expectation) throws -> String {
    let message = String{"Invalid value '"};
    append_escaped_text(message, value, escape_style::Message);
    message += "' for '";
    message += option.koshconf_name;
    message += "', expected ";
    message += expectation;
    return message;
  };

  if (option.type != option_type::String) {
    if (parse_option_number(option, value).has_value()) return None;
    return do_describe(describe_option_values(option).view());
  }
  if (value.find_character('\0').has_value()) {
    return do_describe("text without a NUL byte");
  }
  if (!is_valid_utf8(value)) return do_describe("valid UTF-8 text");
  if (koshconf_option_takes_count(option) && !is_decimal_count(value)) {
    return do_describe("a decimal integer from 0 to 2147483647");
  }
  if (option.storage == option_storage::InitMoods) {
    let moods = ArrayList<mimic_mood>{heap_allocator()};
    if (let const bad_name = parse_mood_list(value, moods);
        bad_name.has_value())
    {
      let message = String{"Unknown mood '"};
      append_escaped_text(message, *bad_name, escape_style::Message);
      message += "' in the value of '";
      message += option.koshconf_name;
      message += "', expected a comma-separated list of 'kosh', 'sh', 'bash', "
                 "and 'bash-posix'";
      return message;
    }
  }

  return None;
}

fn format_koshconf_display_line(const option_descriptor &option,
                                StringView value) throws -> String
{
  let const quote = quote_for_value(value);
  let const has_both_quotes = quote.has_value() && *quote == '\'' &&
                              value.find_character('\'').has_value();
  let is_plain = !has_both_quotes && is_valid_utf8(value);
  for (usize position = 0; is_plain && position < value.count(); position++) {
    let const byte = static_cast<u8>(value[position]);
    if (byte < 0x20 || byte == 0x7f) {
      is_plain = false;
    }
  }
  if (is_plain) return format_koshconf_line(option, value);

  let line = String{StringView{option.koshconf_name}};
  line += "=$'";
  append_escaped_text(line, value, escape_style::AnsiC);
  line += '\'';
  return line;
}

fn read_koshconf_text(StringView text, StringView origin_name,
                      koshconf_reading &reading) throws -> void
{
  let source_index = Maybe<u32>{};
  let printable_text = Maybe<String>{};
  let const do_render_warning = [&](StringView span, StringView message,
                                    StringView note = {}) throws -> String {
    if (!source_index.has_value()) {
      source_index = intern_source_name(origin_name);
      printable_text = make_printable_copy(text);
    }
    let const offset =
        printable_offset(text, static_cast<usize>(span.data - text.data));
    let const location = SourceLocation{offset, span.count(), *source_index};
    if (note.is_empty()) {
      return WarningWithLocation{location, message}.to_string(
          printable_text->view());
    }

    return WarningWithLocationAndDetails{location, message, note}.to_string(
        printable_text->view());
  };
  let const do_warn = [&](StringView span, StringView message,
                          StringView note = {}) throws {
    reading.warnings.push(do_render_warning(span, message, note));
  };

  usize position =
      text.starts_with(UTF8_BYTE_ORDER_MARK) ? UTF8_BYTE_ORDER_MARK.count() : 0;
  while (position < text.count()) {
    let line = text.next_line(position);
    if (!line.is_empty() && line[line.count() - 1] == '\r') {
      line = line.substring_of_length(0, line.count() - 1);
    }
    let const trimmed = line.trim_blanks();
    if (trimmed.is_empty() || trimmed[0] == '#') continue;

    let const equals = line.find_character('=');
    if (!equals.has_value()) {
      do_warn(trimmed, "Expected a 'name=value' line, skipping it");
      continue;
    }
    let const name = line.substring_of_length(0, *equals).trim_blanks();
    let const *option = find_option_by_koshconf_name(name);
    if (option == nullptr) {
      let const suggestion = suggest_koshconf_option_name(name);
      do_warn(name.is_empty() ? trimmed : name,
              StringView{"Unknown option '"} + escape_for_message(name) +
                  "', skipping it",
              suggestion.has_value()
                  ? StringView{"Did you mean '"} + *suggestion + "'?"
                  : String{heap_allocator()});
      continue;
    }

    let const raw_value = line.substring(*equals + 1).trim_blanks();
    let value = raw_value;
    if (!raw_value.is_empty() && (raw_value[0] == '"' || raw_value[0] == '\''))
    {
      let const closing = raw_value.substring(1).find_character(raw_value[0]);
      if (!closing.has_value()) {
        do_warn(raw_value,
                "The quoted value has no closing quote, skipping it");
        continue;
      }
      let const rest = raw_value.substring(*closing + 2);
      if (!rest.trim_blanks().is_empty()) {
        do_warn(rest.trim_blanks(),
                "Unexpected text after the closing quote, skipping the line");
        continue;
      }
      value = raw_value.substring_of_length(1, *closing);
    }

    if (!option->is_configurable()) {
      do_warn(name, StringView{"The '"} + name +
                        "' option cannot be set from a configuration file, "
                        "skipping it");
      continue;
    }
    if (let const problem = find_koshconf_value_problem(*option, value);
        problem.has_value())
    {
      do_warn(raw_value.is_empty() ? name : raw_value,
              *problem + ", skipping it");
      continue;
    }

    let setting = koshconf_setting{option, String{value}};
    if (option->is_fixed_in_kosh_mood && option->type != option_type::String) {
      if (let const number = parse_option_number(*option, value);
          number.has_value() && *number != option->strict_value)
      {
        setting.kosh_mood_warning =
            do_render_warning(raw_value, describe_kosh_mood_hold(*option));
      }
    }
    reading.settings.push(steal(setting));
  }
}

fn read_koshconf_file(const Path &path, koshconf_reading &reading) throws
    -> bool
{
  let const contents = path.read_entire_file();
  if (!contents.has_value()) {
    if (os::last_system_error_is_missing_file()) return false;

    reading.warnings.push(Warning{"Unable to read '" + path.text() +
                                  "': " + os::last_system_error_message()}
                              .to_string());
    return false;
  }
  LOG(Info, "reading the koshconf file '%s'", path.text().c_str());
  read_koshconf_text(contents->view(), path.text().view(), reading);
  return true;
}

fn read_system_koshconf_file(const Path &path, koshconf_reading &reading) throws
    -> bool
{
  let system_file = os::read_system_owned_file(path);
  if (!system_file.rejection.is_empty()) {
    reading.warnings.push(Warning{system_file.rejection.view()}.to_string());
  }
  if (!system_file.contents.has_value()) return false;

  LOG(Info, "reading the system koshconf file '%s'", path.text().c_str());
  read_koshconf_text(system_file.contents->view(), path.text().view(), reading);
  return true;
}

fn encode_koshconf_blob(const EvalContext &cxt) throws -> String
{
  let records = String{heap_allocator()};
  for (let const &option : get_option_registry()) {
    if (!option.is_serialized()) continue;
    let const value = encode_value(cxt, option);
    append_varint(records, option.id);
    append_varint(records, static_cast<u32>(value.count()));
    records += value.view();
  }
  return utils::encode_base64(records.view());
}

fn read_koshconf_blob(StringView encoded, koshconf_reading &reading) throws
    -> Maybe<StringView>
{
  let const records = utils::decode_base64(encoded);
  if (!records.has_value()) {
    return StringView{"it is not padded standard base64"};
  }

  let decoded = ArrayList<koshconf_setting>{heap_allocator()};
  let warnings = ArrayList<String>{heap_allocator()};
  let const bytes = records->view();
  let const do_describe_bad_varint = [&](usize start) -> StringView {
    for (usize scanned = start; scanned < bytes.count(); scanned++) {
      if ((static_cast<u8>(bytes[scanned]) & 0x80) == 0) {
        return "a varint is not in its shortest form";
      }
    }

    return "a record is truncated";
  };
  usize position = 0;
  u32 previous_id = 0;
  while (position < bytes.count()) {
    let const id_start = position;
    let const id = read_varint(bytes, position);
    if (!id.has_value()) return do_describe_bad_varint(id_start);
    if (*id <= previous_id)
      return StringView{"its option ids are out of order"};

    previous_id = *id;
    let const length_start = position;
    let const length = read_varint(bytes, position);
    if (!length.has_value()) return do_describe_bad_varint(length_start);
    if (*length > bytes.count() - position) {
      return StringView{"a record is truncated"};
    }
    let const value_bytes = bytes.substring_of_length(position, *length);
    position += *length;

    let const *option =
        *id <= 0xffff ? find_option_by_id(static_cast<u16>(*id)) : nullptr;
    if (option == nullptr) {
      warnings.push(Warning{"Skipping the unknown option id " +
                            String::from(*id, heap_allocator()) +
                            " in KOSHCONF"}
                        .to_string());
      continue;
    }
    if (!option->is_serialized()) {
      warnings.push(Warning{StringView{"Skipping '"} + option->koshconf_name +
                            "' in KOSHCONF, which never carries it"}
                        .to_string());
      continue;
    }
    let value = decode_value(*option, value_bytes);
    if (!value.has_value()) {
      warnings.push(Warning{StringView{"Skipping an invalid value of '"} +
                            option->koshconf_name + "' in KOSHCONF"}
                        .to_string());
      continue;
    }
    decoded.push(koshconf_setting{option, steal(*value)});
  }

  for (let &setting : decoded)
    reading.settings.push(steal(setting));
  for (let &warning : warnings)
    reading.warnings.push(steal(warning));
  return None;
}

fn apply_koshconf_settings(EvalContext &cxt,
                           const ArrayList<koshconf_setting> &settings,
                           option_origin origin,
                           ArrayList<String> &warnings) throws -> void
{
  for (let const &setting : settings) {
    try {
      write_option_text(cxt, *setting.option, setting.value.view(), origin);
    } catch (const Error &error) {
      warnings.push(Warning{error.message().view()}.to_string());
    }
  }
}

fn format_koshconf_line(const option_descriptor &option,
                        StringView value) throws -> String
{
  if (value.find_character('\n').has_value() ||
      value.find_character('\r').has_value())
  {
    throw Error{StringView{"A value of '"} + option.koshconf_name +
                "' cannot span lines in the configuration file"};
  }
  let const quote = quote_for_value(value);
  if (quote.has_value() && *quote == '\'' &&
      value.find_character('\'').has_value())
  {
    throw Error{StringView{"A value of '"} + option.koshconf_name +
                "' cannot hold both quote characters in the configuration "
                "file"};
  }

  let line = String{StringView{option.koshconf_name}};
  line += '=';
  if (quote.has_value()) line += *quote;
  line += value;
  if (quote.has_value()) line += *quote;
  return line;
}

fn make_koshconf_preset(mimic_mood preset) throws -> String
{
  let contents = String{"# Koshka settings written by koshconf create "};
  contents += mood_name(preset);
  contents += ".\n# Each line is name=value. kosh(5) describes the format.\n"
              "# Every shell reads this file, including the ones that run "
              "scripts.\n";

  let ordered = ArrayList<const option_descriptor *>{heap_allocator()};
  for (let const &option : get_option_registry()) {
    if (!option.is_configurable()) continue;

    ordered.push(&option);
  }
  for (usize position = 1; position < ordered.count(); position++) {
    let const *moved = ordered[position];
    usize slot = position;
    while (slot > 0 && preset_sorts_before(*moved, *ordered[slot - 1])) {
      ordered[slot] = ordered[slot - 1];
      slot--;
    }
    ordered[slot] = moved;
  }

  let previous_group = StringView{};
  for (let const *option : ordered) {
    let const group = preset_group_of(*option);
    if (group != previous_group) {
      contents += '\n';
      previous_group = group;
    }

    let spelling = String{heap_allocator()};
    if (!option->set_name.is_empty()) {
      spelling += "(set -o ";
      spelling += option->set_name;
      spelling += ')';
    } else if (!option->shopt_name.is_empty()) {
      spelling += "(shopt ";
      spelling += option->shopt_name;
      spelling += ')';
    } else if (option->storage == option_storage::EditorMode) {
      usize alias_count = 0;
      for (let const &alias : get_option_registry()) {
        if (!alias.is_set_alias || alias.storage != option_storage::EditorMode)
          continue;

        spelling += alias_count == 0 ? "(set -o " : " or set -o ";
        spelling += alias.set_name;
        alias_count++;
      }
      if (alias_count != 0) spelling += ')';
    }

    let comment = String{StringView{option->help}};
    if (preset == mimic_mood::Bash && option->is_bash_default_session_dependent)
      comment += " Left unset, it is on in an interactive shell and off in a "
                 "script.";

    constexpr usize COMMENT_TEXT_WIDTH = KOSHCONF_COMMENT_WIDTH - 2;
    let const wrapped = wrap_text(comment.view(), 0, COMMENT_TEXT_WIDTH);
    usize position = 0;
    while (position < wrapped.count()) {
      let const row =
          wrapped.view().next_line(position).without_trailing_newline();
      contents += "# ";
      contents += row;
      let const is_last_row = position >= wrapped.count();
      if (is_last_row && !spelling.is_empty()) {
        if (row.count() + 1 + spelling.count() <= COMMENT_TEXT_WIDTH) {
          contents += ' ';
        } else {
          contents += "\n# ";
        }
        contents += spelling.view();
      }
      contents += '\n';
    }

    if (option->type == option_type::String) {
      if (option->default_text.is_empty()) {
        contents += "# ";
        contents += option->koshconf_name;
        contents += "=\n";
        continue;
      }

      contents += format_koshconf_line(*option, option->default_text);
      contents += '\n';
      continue;
    }

    if (preset == mimic_mood::Bash && option->is_bash_default_session_dependent)
    {
      contents += "# ";
      contents += option->koshconf_name;
      contents += "=\n";
      continue;
    }

    let const value =
        option->storage == option_storage::Mood ? static_cast<u32>(preset)
        : preset == mimic_mood::Posix           ? option->posix_default_value
        : preset != mimic_mood::Default         ? option->bash_default_value
        : option->is_fixed_in_kosh_mood         ? option->strict_value
                                                : option->default_value;
    contents += format_koshconf_line(
        *option, format_option_number(*option, value).view());
    contents += '\n';
  }

  return contents;
}

fn write_koshconf_file(const Path &path, StringView contents) throws -> void
{
  let const target = prepare_koshconf_target(path);
  let const lock = lock_koshconf_directory(target);
  defer { os::release_process_lock(lock); };

  replace_koshconf_target(target, contents);
}

fn persist_koshconf_setting(const Path &path, const option_descriptor &option,
                            StringView value) throws -> void
{
  let const replacement = format_koshconf_line(option, value);
  let const target = prepare_koshconf_target(path);
  let const lock = lock_koshconf_directory(target);
  defer { os::release_process_lock(lock); };

  let existing = target.read_entire_file();
  if (!existing.has_value()) {
    if (!os::last_system_error_is_missing_file()) {
      throw Error{"Unable to read '" + target.text() +
                  "': " + os::last_system_error_message()};
    }
    existing = String{heap_allocator()};
  }

  let contents = String{heap_allocator()};
  let text = existing->view();
  if (text.starts_with(UTF8_BYTE_ORDER_MARK)) {
    contents += UTF8_BYTE_ORDER_MARK;
    text = text.substring(UTF8_BYTE_ORDER_MARK.count());
  }
  let const first_line_end = text.find_character('\n');
  let const line_end = first_line_end.has_value() && *first_line_end > 0 &&
                               text[*first_line_end - 1] == '\r'
                           ? StringView{"\r\n"}
                           : StringView{"\n"};
  let const option_name = StringView{option.koshconf_name};
  let const do_find_name = [](StringView line, bool is_commented) {
    let const has_return = !line.is_empty() && line[line.count() - 1] == '\r';
    let const without_return =
        has_return ? line.substring_of_length(0, line.count() - 1) : line;
    if (!is_commented) return declared_name(without_return);

    let const trimmed = without_return.trim_blanks();
    if (trimmed.is_empty() || trimmed[0] != '#') return Maybe<StringView>{};
    return declared_name(trimmed.substring(1));
  };

  let has_setting_line = false;
  for (usize position = 0; position < text.count();) {
    let const name = do_find_name(text.next_line(position), false);
    if (name.has_value() && *name == option_name) has_setting_line = true;
  }

  let did_replace = false;
  for (usize position = 0; position < text.count();) {
    let const line = text.next_line(position);
    let const name = did_replace && !has_setting_line
                         ? Maybe<StringView>{}
                         : do_find_name(line, !has_setting_line);
    let const is_setting_line = name.has_value() && *name == option_name;
    if (is_setting_line && did_replace) continue;

    if (is_setting_line) {
      contents += replacement.view();
      contents += line_end;
      did_replace = true;
      continue;
    }
    contents += line;
    contents += '\n';
  }
  if (!did_replace) {
    contents += replacement.view();
    contents += line_end;
  }
  replace_koshconf_target(target, contents.view());
}

} /* namespace koshka */
