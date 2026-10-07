/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file reads and writes the koshconf files, builds the preset files, and
 * encodes and decodes the binary KOSHCONF form. A malformed line or record
 * becomes a warning and never stops the shell.
 */

#include "Koshconf.hpp"

#include "Errors.hpp"
#include "Eval.hpp"
#include "Koshkit.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace {

constexpr u32 KOSHCONF_DIRECTORY_MODE = 0755;

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
  case option_type::String: return String{bytes};
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

fn read_koshconf_text(StringView text, StringView origin_name,
                      koshconf_reading &reading) throws -> void
{
  let const source_index = intern_source_name(origin_name);
  let const do_warn = [&](StringView span, StringView message) throws {
    let const offset = static_cast<usize>(span.data - text.data);
    reading.warnings.push(WarningWithLocation{
        SourceLocation{offset, span.count(), source_index},
        message
    }
                              .to_string(text));
  };

  usize position = 0;
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
      do_warn(name.is_empty() ? trimmed : name,
              StringView{"Unknown option '"} + name + "', skipping it");
      continue;
    }

    let const raw_value = line.substring(*equals + 1);
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
    if (option->type != option_type::String &&
        !parse_option_number(*option, value).has_value())
    {
      do_warn(raw_value.is_empty() ? name : raw_value,
              StringView{"Invalid value '"} + value + "' for '" + name +
                  "', expected " + describe_option_values(*option) +
                  ", skipping it");
      continue;
    }
    reading.settings.push(koshconf_setting{option, String{value}});
  }
}

fn read_koshconf_file(const Path &path, koshconf_reading &reading) throws
    -> bool
{
  if (!os::path_exists(path.text().view())) return false;
  let const contents = path.read_entire_file();
  if (!contents.has_value()) {
    reading.warnings.push(Warning{"Unable to read '" + path.text() +
                                  "': " + os::last_system_error_message()}
                              .to_string());
    return false;
  }
  LOG(Info, "reading the koshconf file '%s'", path.text().c_str());
  read_koshconf_text(contents->view(), path.text().view(), reading);
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
    -> bool
{
  let const records = utils::decode_base64(encoded);
  if (!records.has_value()) return false;

  let decoded = ArrayList<koshconf_setting>{heap_allocator()};
  let warnings = ArrayList<String>{heap_allocator()};
  let const bytes = records->view();
  usize position = 0;
  while (position < bytes.count()) {
    let const id = read_varint(bytes, position);
    if (!id.has_value()) return false;
    let const length = read_varint(bytes, position);
    if (!length.has_value() || *length > bytes.count() - position) {
      return false;
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
      warnings.push(Warning{StringView{"Skipping an unknown value of '"} +
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
  return true;
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
  contents += ".\n# Each line is name=value. kosh(5) describes the format.\n";
  for (let const &option : get_option_registry()) {
    if (!option.is_serialized() || option.type == option_type::String) {
      continue;
    }
    let const value =
        option.storage == option_storage::Mood ? static_cast<u32>(preset)
        : preset == mimic_mood::Default        ? option.default_value
                                               : option.bash_default_value;
    contents += format_koshconf_line(
        option, format_option_number(option, value).view());
    contents += '\n';
  }
  return contents;
}

fn write_koshconf_file(const Path &path, StringView contents) throws -> void
{
  let const directory = path.parent_or_current();
  if (!koshkit::make_directories(directory, KOSHCONF_DIRECTORY_MODE))
    throw Error{"Unable to create the directory '" + directory.text() +
                "': " + os::last_system_error_message()};
  let const temporary =
      os::write_to_named_temp_file(directory, ".kosh.conf", contents);
  if (!temporary.has_value())
    throw Error{"Unable to write a file in '" + directory.text() +
                "': " + os::last_system_error_message()};
  if (!os::rename_path(temporary->text().view(), path.text().view())) {
    let const message = os::last_system_error_message();
    unused(os::remove_file(temporary->text().view()));
    throw Error{"Unable to replace '" + path.text() + "': " + message};
  }
  LOG(Info, "wrote the koshconf file '%s'", path.text().c_str());
}

fn persist_koshconf_setting(const Path &path, const option_descriptor &option,
                            StringView value) throws -> void
{
  let const replacement = format_koshconf_line(option, value);
  let const existing = os::path_exists(path.text().view())
                           ? path.read_entire_file()
                           : Maybe<String>{String{heap_allocator()}};
  if (!existing.has_value())
    throw Error{"Unable to read '" + path.text() +
                "': " + os::last_system_error_message()};

  let contents = String{heap_allocator()};
  let did_replace = false;
  let const text = existing->view();
  usize position = 0;
  while (position < text.count()) {
    let const line = text.next_line(position);
    let const name = declared_name(line.without_trailing_newline());
    let const is_setting_line =
        name.has_value() && *name == StringView{option.koshconf_name};
    if (is_setting_line && did_replace) {
      continue;
    }
    if (is_setting_line) {
      contents += replacement.view();
      did_replace = true;
    } else {
      contents += line;
    }
    contents += '\n';
  }
  if (!did_replace) {
    contents += replacement.view();
    contents += '\n';
  }
  write_koshconf_file(path, contents.view());
}

} /* namespace koshka */
