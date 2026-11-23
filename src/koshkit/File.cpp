/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the file utility. It parses magic databases, inspects
 * file types and contents, controls symbolic-link traversal, and formats
 * matching descriptions.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Bitset.hpp"
#include "../base/Path.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL("[-dhiL] [-m file] [-M file] [file ...]",
                  "The file utility can classify file operands.");

FLAG(FILE_DEFAULT_TESTS, Bool, 'd', "default-tests", "Apply default tests.");
FLAG(FILE_NO_FOLLOW, Bool, 'h', "no-dereference", "Classify symbolic links.");
FLAG(FILE_REGULAR_ONLY,
     Bool,
     'i',
     "regular-only",
     "Identify regular files without content tests.");
FLAG(FILE_FOLLOW, Bool, 'L', "dereference", "Follow symbolic links.");
FLAG(FILE_MAGIC,
     ManyStrings,
     'm',
     "magic-file",
     "Add position-sensitive tests from this file.");
FLAG(FILE_MAGIC_ONLY,
     ManyStrings,
     'M',
     "magic-only",
     "Use position-sensitive tests from this file.");

REGISTER_KOSHKIT_UTIL_FLAGS(File);

namespace koshka::koshkit {

enum class file_magic_kind : u8
{
  Signed,
  Unsigned,
  String,
};

struct FileMagicRule
{
  explicit FileMagicRule(Allocator allocator)
      : expected_text(allocator), message(allocator)
  {}

  u64             offset{0};
  u64             expected_number{0};
  u64             mask{~static_cast<u64>(0)};
  usize           byte_count{0};
  char            comparison{'='};
  bool            is_continuation{false};
  file_magic_kind kind{file_magic_kind::String};
  String          expected_text;
  String          message;
};

struct file_magic_path
{
  usize          position;
  StringView     path;
  SourceLocation location;
};

static fn
parse_magic_number(StringView text) wontthrow -> Maybe<u64>
{
  if (text.is_empty()) return None;

  usize position    = 0;
  bool  is_negative = false;

  if (text[position] == '-') {
    is_negative = true;
    position++;
  }

  if (position == text.length) return None;

  let const base =
      static_cast<u8>(utils::detect_c_integer_base(text.substring(position)));

  if (base == 16) position += 2;
  if (position == text.length) return None;

  /* at this point: text is not empty, and position is less than
     text.length, so the digit loop stays inside text. */
  u64 parsed = 0;

  for (; position < text.length; position++) {
    let const digit = utils::hex_digit_value(text[position]).value_or(0xff);

    if (digit >= base || parsed > (UINT64_MAX - digit) / base) {
      return None;
    }
    parsed = parsed * base + digit;
  }

  return is_negative ? 0 - parsed : parsed;
}

static fn
decode_magic_text(StringView encoded, String &decoded) throws -> void
{
  for (usize position = 0; position < encoded.length; position++) {
    let const byte = encoded[position];

    if (byte != '\\') {
      let const remaining   = encoded.substring(position);
      let const next_escape = remaining.find_character('\\');
      let const run_length =
          next_escape.has_value() ? *next_escape : remaining.length;

      decoded.append(remaining.substring_of_length(0, run_length));
      position += run_length - 1;
      continue;
    }

    if (position + 1 == encoded.length) {
      decoded.push(byte);
      continue;
    }

    let const escaped = encoded[++position];

    switch (escaped) {
    case '\\': decoded.push('\\'); break;

    case 'a': decoded.push('\a'); break;

    case 'b': decoded.push('\b'); break;

    case 'f': decoded.push('\f'); break;

    case 'n': decoded.push('\n'); break;

    case 'r': decoded.push('\r'); break;

    case 't': decoded.push('\t'); break;

    case 'v': decoded.push('\v'); break;

    case ' ': decoded.push(' '); break;

    case 'x': {
      let const scanned =
          utils::scan_escape_digits(encoded, position + 1, 16, 2);

      position += scanned.digit_count;
      if (scanned.digit_count == 0) {
        decoded += "\\x";
      } else {
        decoded.push(static_cast<char>(scanned.value));
      }

      break;
    }
    default:
      if (escaped >= '0' && escaped <= '7') {
        let const scanned = utils::scan_escape_digits(
            encoded, position + 1, 8, 2, static_cast<u32>(escaped - '0'));

        position += scanned.digit_count;
        decoded.push(static_cast<char>(scanned.value));
      } else {
        decoded.push('\\');
        decoded.push(escaped);
      }

      break;
    }
  }
}

static fn
next_magic_field(StringView line, usize &position) wontthrow -> StringView
{
  while (position < line.length &&
         (line[position] == ' ' || line[position] == '\t'))
  {
    position++;
  }

  let const start = position;

  while (position < line.length && line[position] != ' ' &&
         line[position] != '\t')
  {
    /* a backslash keeps the next byte in the field, so an escaped space
       stays in a value. */
    position += line[position] == '\\' && position + 1 < line.length ? 2 : 1;
  }

  return line.substring_of_length(start, position - start);
}

static fn
parse_magic_type(StringView text, FileMagicRule &rule) throws -> bool
{
  usize position = 0;

  switch (text[0]) {
  case 's':
    if (text.starts_with("string")) {
      rule.kind = file_magic_kind::String;
      position  = 6;
    } else if (text.starts_with("short")) {
      rule.kind       = file_magic_kind::Signed;
      rule.byte_count = 2;
      position        = 5;
    } else {
      rule.kind = file_magic_kind::String;
      position  = 1;
    }

    break;

  case 'b':
    if (!text.starts_with("byte")) return false;

    rule.kind       = file_magic_kind::Signed;
    rule.byte_count = 1;
    position        = 4;
    break;

  case 'l':
    if (!text.starts_with("long")) return false;

    rule.kind       = file_magic_kind::Signed;
    rule.byte_count = 4;
    position        = 4;
    break;

  case 'd':
  case 'u': {
    rule.kind =
        text[0] == 'd' ? file_magic_kind::Signed : file_magic_kind::Unsigned;
    position = 1;
    if (position == text.length || text[position] == '&') {
      rule.byte_count = 4;
    } else {
      switch (text[position]) {
      case 'C':
        rule.byte_count = 1;
        position++;
        break;

      case 'S':
        rule.byte_count = 2;
        position++;
        break;

      case 'I':
        rule.byte_count = 4;
        position++;
        break;

      case 'L':
        rule.byte_count = 8;
        position++;
        break;

      default: {
        let const size_start = position;

        while (position < text.length && text[position] >= '0' &&
               text[position] <= '9')
        {
          position++;
        }

        if (size_start == position) return false;

        let const byte_count = parse_magic_number(
            text.substring_of_length(size_start, position - size_start));

        if (!byte_count.has_value() || *byte_count == 0 || *byte_count > 8) {
          return false;
        }

        rule.byte_count = static_cast<usize>(*byte_count);
        break;
      }
      }
    }
  } break;

  default: return false;
  }

  if (rule.kind == file_magic_kind::String) return position == text.length;
  if (position < text.length && text[position] == '&') {
    position++;

    let const mask = parse_magic_number(text.substring(position));

    if (!mask.has_value()) return false;
    rule.mask = *mask;

    return true;
  }

  return position == text.length;
}

static fn
parse_magic_rule(StringView     line,
                 Allocator      allocator,
                 FileMagicRule &rule) throws -> bool
{
  line = line.trim_blanks();
  if (line.is_empty() || line[0] == '#') {
    return false;
  }

  usize     position = 0;
  let       offset   = next_magic_field(line, position);
  let const type     = next_magic_field(line, position);
  let       value    = next_magic_field(line, position);

  while (position < line.length &&
         (line[position] == ' ' || line[position] == '\t'))
  {
    position++;
  }

  let const message = line.substring(position);

  if (offset.is_empty() || type.is_empty() || value.is_empty() ||
      message.is_empty())
  {
    return false;
  }

  if (offset[0] == '>') {
    rule.is_continuation = true;
    offset               = offset.substring(1);
  }

  if (offset.is_empty() || offset[0] == '-') {
    return false;
  }

  let const parsed_offset = parse_magic_number(offset);

  if (!parsed_offset.has_value() || !parse_magic_type(type, rule)) {
    return false;
  }

  rule.offset = *parsed_offset;

  if (rule.kind == file_magic_kind::String) {
    decode_magic_text(value, rule.expected_text);
  } else {
    if (value[0] == '=' || value[0] == '<' || value[0] == '>' ||
        value[0] == '&' || value[0] == '^')
    {
      rule.comparison = value[0];
      value           = value.substring(1);
    } else if (value == "x") {
      rule.comparison = 'x';
      value           = {};
    }

    if (rule.comparison != 'x') {
      let const expected_number = parse_magic_number(value);

      if (!expected_number.has_value()) return false;
      rule.expected_number = *expected_number;
    }
  }

  rule.message = String{allocator, message};

  return true;
}

static fn
append_magic_database(StringView                path,
                      ArrayList<FileMagicRule> &rules,
                      Allocator                 allocator) throws -> bool
{
  let contents = Path{path, allocator}.read_entire_file();

  if (!contents.has_value()) return false;

  /* a CRLF pair would leave a carriage return in the rule messages. */
  contents->normalize_crlf_line_endings();

  usize position = 0;

  while (position < contents->count()) {
    let const     line = contents->view().next_line(position);
    FileMagicRule rule{allocator};

    if (parse_magic_rule(line, allocator, rule)) rules.push(steal(rule));
  }

  return true;
}

static fn
read_magic_number(const FileMagicRule &rule,
                  StringView           bytes,
                  u64                 &value) wontthrow -> bool
{
  if (rule.offset > bytes.length ||
      rule.byte_count > bytes.length - rule.offset)
  {
    return false;
  }

  /* at this point: rule.byte_count is from 1 to 8, and the bytes it selects
     at rule.offset lie inside bytes. */
  value =
      os::read_native_endian_bytes(bytes.data + rule.offset, rule.byte_count);
  value &= rule.mask;

  if (rule.kind == file_magic_kind::Signed && rule.byte_count < 8) {
    let const sign_bit = static_cast<u64>(1) << (rule.byte_count * 8 - 1);

    if ((value & sign_bit) != 0) value |= UINT64_MAX << (rule.byte_count * 8);
  }

  switch (rule.comparison) {
  case '=': return value == rule.expected_number;
  case '<':
    return rule.kind == file_magic_kind::Signed
               ? static_cast<i64>(value) <
                     static_cast<i64>(rule.expected_number)
               : value < rule.expected_number;
  case '>':
    return rule.kind == file_magic_kind::Signed
               ? static_cast<i64>(value) >
                     static_cast<i64>(rule.expected_number)
               : value > rule.expected_number;
  case '&': return (value & rule.expected_number) == rule.expected_number;
  case '^': return (rule.expected_number & ~value) != 0;
  case 'x': return true;
  default:  return false;
  }
}

static fn
magic_rule_matches(const FileMagicRule &rule,
                   StringView           bytes,
                   u64                 &number) wontthrow -> bool
{
  if (rule.kind != file_magic_kind::String)
    return read_magic_number(rule, bytes, number);
  if (rule.offset > bytes.length ||
      rule.expected_text.count() > bytes.length - rule.offset)
  {
    return false;
  }

  /* at this point: rule.kind is String, and the expected text lies
     inside bytes at rule.offset. */
  return __builtin_memcmp(bytes.data + rule.offset, rule.expected_text.data(),
                          rule.expected_text.count()) == 0;
}

static fn
format_magic_message(const FileMagicRule &rule,
                     u64                  number,
                     Allocator            allocator) throws -> String
{
  let       output              = String{allocator};
  let const message             = rule.message.view();
  bool      has_formatted_value = false;

  for (usize position = 0; position < message.length; position++) {
    if (message[position] != '%' || position + 1 == message.length) {
      output.push(message[position]);
      continue;
    }

    let const conversion = message[++position];

    if (conversion == '%') {
      output.push('%');
      continue;
    }

    if (has_formatted_value) {
      output.push('%');
      output.push(conversion);
      continue;
    }

    /* at this point: conversion is not '%', and no earlier conversion
       in message has formatted a value. */
    has_formatted_value = true;
    switch (conversion) {
    case 'd':
    case 'i':
      output += String::from(static_cast<i64>(number), allocator);
      break;

    case 'u': output += String::from(number, allocator); break;

    case 'x':
      output += String::from_in_base(number, false, int_base::hex, allocator);
      break;

    case 'X': {
      let formatted =
          String::from_in_base(number, false, int_base::hex, allocator);
      formatted.uppercase_ascii();
      output += formatted.view();
      break;
    }
    case 'o':
      output += String::from_in_base(number, false, int_base::octal, allocator);
      break;

    case 'c': output.push(static_cast<char>(number)); break;

    case 's': output += rule.expected_text.view(); break;

    default:
      output.push('%');
      output.push(conversion);
      has_formatted_value = false;
      break;
    }
  }

  return output;
}

static fn
match_magic_rules(const ArrayList<FileMagicRule> &rules,
                  StringView                      bytes,
                  Allocator                       allocator) throws -> Maybe<String>
{
  for (usize index = 0; index < rules.count(); index++) {
    let const &rule = rules[index];

    if (rule.is_continuation) continue;

    u64 number = 0;

    if (!magic_rule_matches(rule, bytes, number)) continue;

    /* at this point: rule is not a continuation rule, and rule matches
       bytes. */
    let result = format_magic_message(rule, number, allocator);

    for (index++; index < rules.count() && rules[index].is_continuation;
         index++)
    {
      u64 child_number = 0;

      if (!magic_rule_matches(rules[index], bytes, child_number)) continue;
      result.push(' ');
      result += format_magic_message(rules[index], child_number, allocator);
    }

    return result;
  }

  return {};
}

/* Callers validate the enclosing header before reading a field. */
static pure fn
read_header_field(StringView bytes,
                  usize      offset,
                  usize      width,
                  bool       is_little_endian = true) wontthrow -> u32
{
  u32 value = 0;

  for (usize i = 0; i < width; ++i) {
    let const index = is_little_endian ? width - 1 - i : i;
    value           = (value << 8) | static_cast<u8>(bytes[offset + index]);
  }

  return value;
}

static fn
append_detail_label(String &text, StringView label) throws -> void
{
  if (label.is_empty()) return;

  text.append(", ");
  text.append(label);
}

static fn
append_detail_number(String &text, u32 value) throws -> void
{
  text.append(String::from(static_cast<u64>(value), text.allocator()).view());
}

static fn
append_detail_dimensions(String &text, u32 width, u32 height) throws
    -> void
{
  if (width == 0 || height == 0) return;

  text.append(", ");
  append_detail_number(text, width);
  text.push('x');
  append_detail_number(text, height);
}

static fn
append_png_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 33 || read_header_field(bytes, 8, 4, false) != 13 ||
      bytes.substring_of_length(12, 4) != "IHDR")
  {
    return;
  }

  append_detail_dimensions(text, read_header_field(bytes, 16, 4, false),
                           read_header_field(bytes, 20, 4, false));
  text.append(", ");
  append_detail_number(text, static_cast<u8>(bytes[24]));
  text.append("-bit/channel");
}

static fn
append_gif_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 13) return;

  append_detail_dimensions(text, read_header_field(bytes, 6, 2),
                           read_header_field(bytes, 8, 2));
  append_detail_label(text, bytes.substring_of_length(3, 3));
}

static fn
append_jpeg_details(String &text, StringView bytes) throws -> void
{
  usize offset = 2;

  while (offset + 4 <= bytes.length && static_cast<u8>(bytes[offset]) == 255) {
    let const marker = static_cast<u8>(bytes[offset + 1]);
    if (marker == 255) {
      ++offset;
      continue;
    }
    if (marker == 0 || marker == 0xd9 || marker == 0xda) break;
    let const size = read_header_field(bytes, offset + 2, 2, false);
    if (size < 2 || size > bytes.length - offset - 2) break;
    if (size >= 8 && marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 &&
        marker != 0xc8 && marker != 0xcc)
    {
      append_detail_dimensions(text,
                               read_header_field(bytes, offset + 7, 2, false),
                               read_header_field(bytes, offset + 5, 2, false));
      if (marker == 0xc2) append_detail_label(text, "progressive");
      break;
    }
    offset += 2 + size;
  }
}

static fn
append_webp_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 25) return;

  let const chunk = bytes.substring_of_length(12, 4);
  let const size  = read_header_field(bytes, 16, 4);

  if (chunk == "VP8X" && size == 10 && bytes.length >= 30) {
    append_detail_dimensions(text, 1 + read_header_field(bytes, 24, 3),
                             1 + read_header_field(bytes, 27, 3));
    if ((bytes[20] & 2) != 0) append_detail_label(text, "animated");
    if ((bytes[20] & 16) != 0) append_detail_label(text, "alpha");
  } else if (chunk == "VP8L" && size >= 5 && static_cast<u8>(bytes[20]) == 0x2f)
  {
    let const dimensions = read_header_field(bytes, 21, 4);
    append_detail_dimensions(text, 1 + (dimensions & 0x3fff),
                             1 + ((dimensions >> 14) & 0x3fff));
    append_detail_label(text, "lossless");
  } else if (chunk == "VP8 " && size >= 10 && bytes.length >= 30 &&
             bytes.substring_of_length(23, 3) == "\x9d\x01\x2a")
  {
    append_detail_dimensions(text, read_header_field(bytes, 26, 2) & 0x3fff,
                             read_header_field(bytes, 28, 2) & 0x3fff);
  }
}

static fn
append_bmp_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 26) return;

  let const header_size = read_header_field(bytes, 14, 4);

  if (header_size == 12) {
    append_detail_dimensions(text, read_header_field(bytes, 18, 2),
                             read_header_field(bytes, 20, 2));
  } else if (header_size >= 40) {
    let const width  = read_header_field(bytes, 18, 4);
    let const height = read_header_field(bytes, 22, 4);
    if ((width & 0x80000000u) == 0)
      append_detail_dimensions(text, width,
                               height & 0x80000000u ? 0u - height : height);
  }
}

static fn
append_elf_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 20 || (bytes[4] != 1 && bytes[4] != 2) ||
      (bytes[5] != 1 && bytes[5] != 2))
  {
    return;
  }

  let const is_little = bytes[5] == 1;
  let const type      = read_header_field(bytes, 16, 2, is_little);

  if (type == 1) text = String{text.allocator(), "ELF relocatable object"};
  if (type == 3) text = String{text.allocator(), "ELF shared object / PIE"};
  if (type == 4) text = String{text.allocator(), "ELF core dump"};
  append_detail_label(text, bytes[4] == 2 ? "64-bit" : "32-bit");
  switch (read_header_field(bytes, 18, 2, is_little)) {
  case 3:   append_detail_label(text, "x86"); break;
  case 20:
  case 21:  append_detail_label(text, "PowerPC"); break;
  case 40:  append_detail_label(text, "ARM"); break;
  case 62:  append_detail_label(text, "x86-64"); break;
  case 183: append_detail_label(text, "ARM64"); break;
  case 243: append_detail_label(text, "RISC-V"); break;
  case 258: append_detail_label(text, "LoongArch"); break;
  default:  break;
  }
  append_detail_label(text, is_little ? "little-endian" : "big-endian");
}

static fn
append_mach_o_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 16) return;

  let const first     = static_cast<u8>(bytes[0]);
  let const is_little = first == 0xce || first == 0xcf;
  let const is_wide   = first == 0xcf || static_cast<u8>(bytes[3]) == 0xcf;
  let const cpu       = read_header_field(bytes, 4, 4, is_little);
  let const type      = read_header_field(bytes, 12, 4, is_little);

  if (type == 1) text = String{text.allocator(), "Mach-O object"};
  if (type == 6) text = String{text.allocator(), "Mach-O dynamic library"};
  if (type == 8) text = String{text.allocator(), "Mach-O bundle"};
  append_detail_label(text, is_wide ? "64-bit" : "32-bit");
  switch (cpu & 0xffffffu) {
  case 7:  append_detail_label(text, is_wide ? "x86-64" : "x86"); break;
  case 12: append_detail_label(text, is_wide ? "ARM64" : "ARM"); break;
  case 18: append_detail_label(text, "PowerPC"); break;
  default: break;
  }
}

static fn
append_pe_details(String &text, StringView bytes) throws -> void
{
  let const offset = read_header_field(bytes, 0x3c, 4);

  if (bytes.length - offset >= 24) {
    if ((read_header_field(bytes, offset + 22, 2) & 0x2000) != 0)
      text = String{text.allocator(), "PE DLL"};
    if (bytes.length - offset >= 26 &&
        read_header_field(bytes, offset + 20, 2) >= 2)
    {
      let const magic = read_header_field(bytes, offset + 24, 2);
      if (magic == 0x10b || magic == 0x20b)
        append_detail_label(text, magic == 0x20b ? "64-bit" : "32-bit");
    }
    switch (read_header_field(bytes, offset + 4, 2)) {
    case 0x14c:  append_detail_label(text, "x86"); break;
    case 0x1c0:
    case 0x1c4:  append_detail_label(text, "ARM"); break;
    case 0x8664: append_detail_label(text, "x86-64"); break;
    case 0xaa64: append_detail_label(text, "ARM64"); break;
    default:     break;
    }
  }
}

static fn
append_wave_details(String &text, StringView bytes) throws -> void
{
  usize offset = 12;

  while (offset + 8 <= bytes.length) {
    let const size = read_header_field(bytes, offset + 4, 4);
    if (size > bytes.length - offset - 8) break;
    if (size >= 16 && bytes.substring_of_length(offset, 4) == "fmt ") {
      text.append(", ");
      append_detail_number(text, read_header_field(bytes, offset + 12, 4));
      text.append(" Hz, ");
      append_detail_number(text, read_header_field(bytes, offset + 10, 2));
      text.append(" ch");
      break;
    }
    offset += 8 + size + (size & 1);
  }
}

static fn
append_script_details(String &text, StringView bytes) throws -> void
{
  let const start  = bytes.substring_of_length(2, 128);
  usize     offset = 0;

  while (offset < start.length &&
         (start[offset] == ' ' || start[offset] == '\t'))
  {
    ++offset;
  }
  usize end = offset;
  while (end < start.length && static_cast<u8>(start[end]) > 32 &&
         static_cast<u8>(start[end]) < 127)
  {
    ++end;
  }
  append_detail_label(text, start.substring_of_length(offset, end - offset));
}

static fn
append_java_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 8) return;

  text.append(", version ");
  append_detail_number(text, read_header_field(bytes, 6, 2, false));
  text.push('.');
  append_detail_number(text, read_header_field(bytes, 4, 2, false));
}

static fn
append_pdf_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 8 || bytes[5] < '0' || bytes[5] > '9' || bytes[6] != '.' ||
      bytes[7] < '0' || bytes[7] > '9')
  {
    return;
  }

  append_detail_label(text, bytes.substring_of_length(5, 3));
}

static fn
append_sqlite_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 100) return;

  let const page_byte_count = read_header_field(bytes, 16, 2, false);
  let const page_count      = read_header_field(bytes, 28, 4, false);

  if (page_byte_count == 1 || (page_byte_count >= 512 &&
                               (page_byte_count & (page_byte_count - 1)) == 0))
  {
    text.append(", ");
    append_detail_number(text, page_byte_count == 1 ? 65536 : page_byte_count);
    text.append(" B/page");
    if (page_count > 0 && read_header_field(bytes, 24, 4, false) ==
                              read_header_field(bytes, 92, 4, false))
    {
      text.append(", ");
      append_detail_number(text, page_count);
      text.append(page_count == 1 ? " page" : " pages");
    }
  }
}

static fn
append_flac_details(String &text, StringView bytes) throws -> void
{
  if (bytes.length < 42 || (bytes[4] & 127) != 0 ||
      read_header_field(bytes, 5, 3, false) != 34)
  {
    return;
  }

  let const format      = read_header_field(bytes, 18, 4, false);
  let const sample_rate = format >> 12;

  if (sample_rate > 0) {
    text.append(", ");
    append_detail_number(text, sample_rate);
    text.append(" Hz, ");
    append_detail_number(text, 1 + ((format >> 9) & 7));
    text.append(" ch, ");
    append_detail_number(text, 1 + ((format >> 4) & 31));
    text.append("-bit");
  }
}

using file_detail_parser = void (*)(String &, StringView);

struct file_content_kind
{
  StringView         description;
  file_detail_parser append_details{nullptr};
};

struct builtin_file_signature
{
  usize             offset;
  StringView        bytes;
  file_content_kind kind;
};

template <usize byte_count>
consteval fn
make_builtin_file_signature(usize offset,
                            const char (&bytes)[byte_count],
                            StringView         description,
                            file_detail_parser append_details = nullptr)
    -> builtin_file_signature
{
  return {
      offset, StringView{bytes,       byte_count - 1},
       {description, append_details}
  };
}

static pure fn
matches_builtin_file_signature(StringView                    bytes,
                               const builtin_file_signature &signature)
    wontthrow
    -> bool
{
  if (signature.offset > bytes.length ||
      signature.bytes.length > bytes.length - signature.offset)
  {
    return false;
  }

  let const candidate = bytes.data + signature.offset;

  if (candidate[0] != signature.bytes[0]) return false;

  /* at this point: signature.bytes lies inside bytes at signature.offset,
     and candidate[0] equals signature.bytes[0]. */

  return signature.bytes.length == 1 ||
         byte_scan::are_bytes_equal(candidate + 1, signature.bytes.data + 1,
                                    signature.bytes.length - 1);
}

static pure fn
classify_file_content(StringView bytes) wontthrow -> file_content_kind
{
  /* time:   O(bytes.length) for the control-byte scan at the end. the other
             checks compare a constant number of bytes each. memory: none, since
             every result is a static string. */
  if (bytes.is_empty()) return {"empty"};

  if (bytes.length >= 0x40 && bytes.starts_with("MZ")) {
    let const pe_offset =
        static_cast<usize>(static_cast<u8>(bytes[0x3c])) |
        static_cast<usize>(static_cast<u8>(bytes[0x3d])) << 8 |
        static_cast<usize>(static_cast<u8>(bytes[0x3e])) << 16 |
        static_cast<usize>(static_cast<u8>(bytes[0x3f])) << 24;

    if (pe_offset <= bytes.length && 4 <= bytes.length - pe_offset &&
        bytes.substring_of_length(pe_offset, 4) == StringView{"PE\0\0", 4})
    {
      return {"PE executable", append_pe_details};
    }
  }

  if (bytes.length >= 8 &&
      bytes.substring_of_length(0, 4) == StringView{"\xca\xfe\xba\xbe", 4})
  {
    let const major_version = static_cast<u16>(static_cast<u8>(bytes[6]) << 8) |
                              static_cast<u8>(bytes[7]);

    return major_version >= 45
               ? file_content_kind{"Java class", append_java_details}
               : file_content_kind{"Mach-O universal binary"};
  }

  if (bytes.length >= 12 && bytes.substring_of_length(0, 4) == "RIFF") {
    let const kind = bytes.substring_of_length(8, 4);

    if (kind == "WAVE") return {"WAVE audio", append_wave_details};
    if (kind == "AVI ") return {"AVI video"};
    if (kind == "WEBP") return {"WebP image", append_webp_details};
    if (kind == "ACON") return {"Windows animated cursor"};
  }

  if (bytes.length >= 12 && bytes.substring_of_length(0, 4) == "FORM") {
    let const kind = bytes.substring_of_length(8, 4);

    if (kind == "AIFF" || kind == "AIFC") {
      return {"AIFF audio"};
    }
  }

  if (bytes.length >= 16 && bytes.substring_of_length(0, 8) == "AT&TFORM") {
    static constexpr PackedStringKey DJVU_FORM_KEYS[] = {
        SSK("DJVU"), SSK("DJVM"), SSK("DJVI"), SSK("THUM")};
    static constexpr StaticStringSet DJVU_FORMS{DJVU_FORM_KEYS};

    if (DJVU_FORMS.contains(bytes.substring_of_length(12, 4)))
      return {"DjVu document"};
  }

  if (bytes.length >= 12 && bytes.substring_of_length(4, 4) == "ftyp") {
    let const brand = bytes.substring_of_length(8, 4);

    if (brand == "avif" || brand == "avis") {
      return {"AVIF image"};
    }

    static constexpr PackedStringKey HEIF_BRAND_KEYS[] = {
        SSK("heic"), SSK("heix"), SSK("hevc"),
        SSK("hevx"), SSK("mif1"), SSK("msf1")};
    static constexpr StaticStringSet HEIF_BRANDS{HEIF_BRAND_KEYS};

    if (HEIF_BRANDS.contains(brand)) return {"HEIF image"};
  }

  if (bytes.length >= 4 &&
      bytes.substring_of_length(0, 4) == StringView{"\x1a\x45\xdf\xa3", 4})
  {
    let const header = bytes.substring_of_length(4, 256 - 4);

    if (header.find_substring("matroska").has_value())
      return {"Matroska video"};
    if (header.find_substring("webm").has_value()) return {"WebM video"};
  }

  if (bytes.length > 376 && bytes[0] == 'G' && bytes[188] == 'G' &&
      bytes[376] == 'G')
  {
    return {"MPEG transport stream"};
  }

  constexpr builtin_file_signature SIGNATURES[] = {
      make_builtin_file_signature(0, "\x89PNG\r\n\x1a\n", "PNG image",
                                  append_png_details),
      make_builtin_file_signature(0, "\xff\xd8\xff", "JPEG image",
                                  append_jpeg_details),
      make_builtin_file_signature(0, "GIF87a", "GIF image", append_gif_details),
      make_builtin_file_signature(0, "GIF89a", "GIF image", append_gif_details),
      make_builtin_file_signature(0, "BM", "BMP image", append_bmp_details),
      make_builtin_file_signature(0, "II\x2a\0", "TIFF image"),
      make_builtin_file_signature(0, "MM\0\x2a", "TIFF image"),
      make_builtin_file_signature(0, "II\x2b\0", "BigTIFF image"),
      make_builtin_file_signature(0, "MM\0\x2b", "BigTIFF image"),
      make_builtin_file_signature(0, "\xff\x0a", "JPEG XL image"),
      make_builtin_file_signature(0, "\xff\x4f\xff\x51", "JPEG 2000 image"),
      make_builtin_file_signature(0, "\0\0\0\x0cJXL \r\n\x87\n",
                                  "JPEG XL image"),
      make_builtin_file_signature(0, "\0\0\0\x0cjP  \r\n\x87\n",
                                  "JPEG 2000 image"),
      make_builtin_file_signature(0, "\0\0\x01\0", "Windows icon"),
      make_builtin_file_signature(0, "\0\0\x02\0", "Windows cursor"),
      make_builtin_file_signature(0, "8BPS\0\x01", "Photoshop document"),
      make_builtin_file_signature(0, "\x76\x2f\x31\x01", "OpenEXR image"),
      make_builtin_file_signature(0, "#?RADIANCE", "Radiance HDR image"),
      make_builtin_file_signature(0, "qoif", "QOI image"),
      make_builtin_file_signature(0, "DDS ", "DDS image"),
      make_builtin_file_signature(0, "\xabKTX 20\xbb\r\n\x1a\n", "KTX2 image"),
      make_builtin_file_signature(0, "\xabKTX 11\xbb\r\n\x1a\n", "KTX image"),
      make_builtin_file_signature(0, "PVR\x03", "PVR image"),
      make_builtin_file_signature(0, "\x03RVP", "PVR image"),
      make_builtin_file_signature(0, "\x13\xab\xa1\x5c", "ASTC image"),
      make_builtin_file_signature(0, "BPG\xfb", "BPG image"),
      make_builtin_file_signature(0, "FLIF", "FLIF image"),
      make_builtin_file_signature(0, "gimp xcf ", "GIMP XCF image"),
      make_builtin_file_signature(0, "farbfeld", "farbfeld image"),
      make_builtin_file_signature(0, "\x59\xa6\x6a\x95", "Sun raster image"),
      make_builtin_file_signature(0,
                                  "\x7f"
                                  "ELF",
                                  "ELF executable", append_elf_details),
      make_builtin_file_signature(0, "#!AMR-WB\n", "AMR-WB audio"),
      make_builtin_file_signature(0, "#!AMR\n", "AMR audio"),
      make_builtin_file_signature(0, "#!", "script text executable",
                                  append_script_details),
      make_builtin_file_signature(0, "MZ", "DOS executable"),
      make_builtin_file_signature(0, "\xfe\xed\xfa\xce", "Mach-O executable",
                                  append_mach_o_details),
      make_builtin_file_signature(0, "\xce\xfa\xed\xfe", "Mach-O executable",
                                  append_mach_o_details),
      make_builtin_file_signature(0, "\xfe\xed\xfa\xcf", "Mach-O executable",
                                  append_mach_o_details),
      make_builtin_file_signature(0, "\xcf\xfa\xed\xfe", "Mach-O executable",
                                  append_mach_o_details),
      make_builtin_file_signature(0, "\xbe\xba\xfe\xca",
                                  "Mach-O universal binary"),
      make_builtin_file_signature(0, "\xca\xfe\xba\xbf",
                                  "Mach-O universal binary"),
      make_builtin_file_signature(0, "\xbf\xba\xfe\xca",
                                  "Mach-O universal binary"),
      make_builtin_file_signature(0, "\0asm", "WebAssembly module"),
      make_builtin_file_signature(0, "BC\xc0\xde", "LLVM bitcode"),
      make_builtin_file_signature(0, "!<arch>\n", "Unix archive"),
      make_builtin_file_signature(0, "dex\n", "Dalvik executable"),
      make_builtin_file_signature(0, "oat\n", "Android OAT file"),
      make_builtin_file_signature(0, "vdex", "Android VDEX file"),
      make_builtin_file_signature(0, "ANDROID!", "Android boot image"),
      make_builtin_file_signature(0, "VNDRBOOT", "Android vendor boot image"),
      make_builtin_file_signature(0, "\x27\x05\x19\x56", "U-Boot image"),
      make_builtin_file_signature(
          0, "L\0\0\0\x01\x14\x02\0\0\0\0\0\xc0\0\0\0\0\0\0F",
          "Windows shortcut"),
      make_builtin_file_signature(0, "MDMP", "Windows minidump"),
      make_builtin_file_signature(0, "\xac\xed\0\x05", "Java serialization"),
      make_builtin_file_signature(0, "\x1bLua", "Lua bytecode"),
      make_builtin_file_signature(0, "PK\x03\x04", "ZIP archive"),
      make_builtin_file_signature(0, "PK\x05\x06", "ZIP archive"),
      make_builtin_file_signature(0, "PK\x07\x08", "ZIP archive"),
      make_builtin_file_signature(0, "Rar!\x1a\x07", "RAR archive"),
      make_builtin_file_signature(0, "7z\xbc\xaf\x27\x1c", "7-zip archive"),
      make_builtin_file_signature(0, "\x1f\x8b", "gzip compressed data"),
      make_builtin_file_signature(0, "BZh", "bzip2 compressed data"),
      make_builtin_file_signature(0,
                                  "\xfd"
                                  "7zXZ\0",
                                  "XZ compressed data"),
      make_builtin_file_signature(0, "\x28\xb5\x2f\xfd",
                                  "Zstandard compressed data"),
      make_builtin_file_signature(0, "\x04\x22\x4d\x18", "LZ4 frame"),
      make_builtin_file_signature(0, "LZIP", "lzip compressed data"),
      make_builtin_file_signature(0, "\x1f\x9d", "Unix compressed data"),
      make_builtin_file_signature(0, "MSCF", "Microsoft Cabinet archive"),
      make_builtin_file_signature(0, "MSWIM\0\0\0", "Windows Imaging file"),
      make_builtin_file_signature(0, "070701", "cpio archive"),
      make_builtin_file_signature(0, "070702", "cpio archive"),
      make_builtin_file_signature(0, "070707", "cpio archive"),
      make_builtin_file_signature(257, "ustar", "tar archive"),
      make_builtin_file_signature(0, "\xed\xab\xee\xdb", "RPM package"),
      make_builtin_file_signature(0, "hsqs", "SquashFS filesystem"),
      make_builtin_file_signature(0, "\x45\x3d\xcd\x28", "cramfs filesystem"),
      make_builtin_file_signature(0, "xar!", "XAR archive"),
      make_builtin_file_signature(0, "\x60\xea", "ARJ archive"),
      make_builtin_file_signature(7, "**ACE**", "ACE archive"),
      make_builtin_file_signature(2, "-lh", "LHA archive"),
      make_builtin_file_signature(0, "bvx2", "LZFSE compressed data"),
      make_builtin_file_signature(
          0, "7kSt\xa0\x31\x83\xd3\x8c\xb2\x28\xb0\xd3zPQ", "ZPAQ archive"),
      make_builtin_file_signature(0, "\xff\x06\0\0sNaPpY",
                                  "Snappy framed data"),
      make_builtin_file_signature(0, "%PDF-", "PDF document",
                                  append_pdf_details),
      make_builtin_file_signature(0, "%!PS", "PostScript document"),
      make_builtin_file_signature(0, "{\\rtf", "RTF document"),
      make_builtin_file_signature(0, "<!DOCTYPE html", "HTML document"),
      make_builtin_file_signature(0, "<html", "HTML document"),
      make_builtin_file_signature(0, "<?xml", "XML document"),
      make_builtin_file_signature(0, "SQLite format 3\0", "SQLite database",
                                  append_sqlite_details),
      make_builtin_file_signature(0, "\x37\x7f\x06\x82",
                                  "SQLite write-ahead log"),
      make_builtin_file_signature(0, "\x37\x7f\x06\x83",
                                  "SQLite write-ahead log"),
      make_builtin_file_signature(0, "\xd9\xd5\x05\xf9 \xa1\x63\xd7",
                                  "SQLite rollback journal"),
      make_builtin_file_signature(0, "PAR1", "Apache Parquet data"),
      make_builtin_file_signature(0, "ORC", "Apache ORC data"),
      make_builtin_file_signature(0, "Obj\x01", "Apache Avro object"),
      make_builtin_file_signature(0, "ARROW1\0\0", "Apache Arrow file"),
      make_builtin_file_signature(0, "\x89HDF\r\n\x1a\n", "HDF5 data"),
      make_builtin_file_signature(0, "CDF\x01", "NetCDF data"),
      make_builtin_file_signature(0, "CDF\x02", "NetCDF data"),
      make_builtin_file_signature(0, "CDF\x05", "NetCDF data"),
      make_builtin_file_signature(0, "SIMPLE  =", "FITS data"),
      make_builtin_file_signature(0, "\xa1\xb2\xc3\xd4", "pcap capture"),
      make_builtin_file_signature(0, "\xd4\xc3\xb2\xa1", "pcap capture"),
      make_builtin_file_signature(0, "\xa1\xb2\x3c\x4d", "pcap capture"),
      make_builtin_file_signature(0, "\x4d\x3c\xb2\xa1", "pcap capture"),
      make_builtin_file_signature(0, "\x0a\x0d\x0d\x0a", "pcapng capture"),
      make_builtin_file_signature(0, "REDIS", "Redis database"),
      make_builtin_file_signature(0, "PGDMP", "PostgreSQL custom dump"),
      make_builtin_file_signature(0, "PACK", "Git pack"),
      make_builtin_file_signature(0, "DIRC", "Git index"),
      make_builtin_file_signature(0, "MIDX", "Git multi-pack index"),
      make_builtin_file_signature(0, "CGPH", "Git commit graph"),
      make_builtin_file_signature(128, "DICM", "DICOM medical image"),
      make_builtin_file_signature(0, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1",
                                  "OLE compound document"),
      make_builtin_file_signature(0, "ITSF", "Compiled HTML help"),
      make_builtin_file_signature(0, "fLaC", "FLAC audio", append_flac_details),
      make_builtin_file_signature(0, "OggS", "Ogg data"),
      make_builtin_file_signature(0, "ID3", "MP3 audio"),
      make_builtin_file_signature(0, "MThd\0\0\0\x06", "MIDI audio"),
      make_builtin_file_signature(0, ".snd", "Sun audio"),
      make_builtin_file_signature(0, "MAC ", "Monkey's Audio"),
      make_builtin_file_signature(0, "MPCK", "Musepack audio"),
      make_builtin_file_signature(0, "wvpk", "WavPack audio"),
      make_builtin_file_signature(0, "DSD ", "DSF audio"),
      make_builtin_file_signature(0, "FRM8", "DSDIFF audio"),
      make_builtin_file_signature(0, "Creative Voice File\x1a",
                                  "Creative Voice audio"),
      make_builtin_file_signature(0, "Extended Module: ", "FastTracker module"),
      make_builtin_file_signature(0, "IMPM", "Impulse Tracker module"),
      make_builtin_file_signature(44, "SCRM", "Scream Tracker module"),
      make_builtin_file_signature(1080, "M.K.", "ProTracker module"),
      make_builtin_file_signature(0, "FLV\x01", "Flash video"),
      make_builtin_file_signature(0, "\0\0\x01\xba", "MPEG program stream"),
      make_builtin_file_signature(
          0, "\x30\x26\xb2\x75\x8e\x66\xcf\x11\xa6\xd9\0\xaa\0b\xce\x6c",
          "ASF media"),
      make_builtin_file_signature(0, ".RMF", "RealMedia file"),
      make_builtin_file_signature(0, "FWS", "Flash file"),
      make_builtin_file_signature(0, "CWS", "Flash file"),
      make_builtin_file_signature(0, "ZWS", "Flash file"),
  };

  static_assert(sizeof(SIGNATURES) / sizeof(*SIGNATURES) >= 100);

  for (let const &signature : SIGNATURES) {
    if (matches_builtin_file_signature(bytes, signature)) {
      return signature.kind;
    }
  }

  /* at this point: bytes is not empty, and no earlier test or builtin
     signature matched bytes, so the text or data test decides the result. */
  static constexpr u32 TEXT_CONTROL_MASK =
      (1U << '\b') | (1U << '\t') | (1U << '\n') | (1U << '\f') | (1U << '\r');

  bool is_text = true;

  for (usize position = 0; position < bytes.length; position++) {
    let const byte = static_cast<u8>(bytes[position]);

    if (byte < 0x20 && ((TEXT_CONTROL_MASK >> byte) & 1U) == 0) {
      is_text = false;
      break;
    }
  }

  return {is_text ? StringView{"text"} : StringView{"data"}};
}

constexpr usize FILE_CONTENT_SAMPLE_BYTE_COUNT  = 8192;
constexpr usize FILE_CONTENT_SAMPLE_BATCH_COUNT = 32;
fn
describe_file_type(StringView             path,
                   const os::file_status &status,
                   Allocator              allocator,
                   bool should_include_details) throws -> Maybe<String>
{
  /* memory: the returned String comes from allocator, and the caller owns it.
     time:   for a regular file, one nonblocking open, one descriptor stat,
             one read of at most 8192 bytes, and one close. other file types
             make no system calls. */
  switch (os::file_type_letter(status.mode)) {
  case 'd': return String{allocator, "directory"};
  case 'l': return String{allocator, "symbolic link"};
  case 'p': return String{allocator, "fifo"};
  case 's': return String{allocator, "socket"};
  case 'b': return String{allocator, "block special file"};
  case 'c': return String{allocator, "character special file"};
  default:  break;
  }

  let const descriptor =
      os::open_file_descriptor(path, os::file_open_mode::ReadNonblocking);

  if (!descriptor.has_value()) return None;
  defer { os::close_fd(*descriptor); };

  os::file_status opened_status{};

  if (!os::stat_descriptor(*descriptor, opened_status) ||
      os::file_type_letter(opened_status.mode) != '-')
  {
    return None;
  }

  char      buffer[FILE_CONTENT_SAMPLE_BYTE_COUNT];
  let const read_byte_count = os::read_fd(*descriptor, buffer, sizeof(buffer));

  if (!read_byte_count.has_value()) return None;

  /* at this point: buffer holds *read_byte_count bytes read from the
     descriptor, which closes on return. */

  let const bytes       = StringView{buffer, *read_byte_count};
  let const kind        = classify_file_content(bytes);
  let       description = String{allocator, kind.description};

  if (should_include_details && kind.append_details != nullptr) {
    kind.append_details(description, bytes);
  }

  return description;
}

struct file_content_probe
{
  os::descriptor descriptor{KOSH_INVALID_FD};
  usize          operand_position{0};
  char           bytes[FILE_CONTENT_SAMPLE_BYTE_COUNT]{};
};

fn
File::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  /* memory: the operand paths, file statuses, magic paths, and magic rules are
             bump-allocated in cxt.scratch_allocator(). the probes array holds
             32 buffers of 8192 bytes on the stack. sample Strings in
             result.content use heap_allocator() and are released by reset() or
             when source_results is destroyed.
     time:   O(P log P) sort over P magic paths. one lstat() per operand
             in one batch. the default samples read one 8192-byte buffer per
             regular operand. the custom samples read at most sample_byte_count
             bytes per regular operand. numeric rule checks take constant
             time. string rule checks compare up to expected_text.count()
             bytes each. */
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const                  allocator = cxt.scratch_allocator();
  i32                        status    = 0;
  ArrayList<file_magic_path> magic_paths{allocator};

  for (usize index = 0; index < FLAG_FILE_MAGIC.count(); index++)
    magic_paths.push(file_magic_path{FLAG_FILE_MAGIC.get_position(index),
                                     FLAG_FILE_MAGIC.get(index),
                                     FLAG_FILE_MAGIC.get_location(index)});

  for (usize index = 0; index < FLAG_FILE_MAGIC_ONLY.count(); index++)
    magic_paths.push(file_magic_path{FLAG_FILE_MAGIC_ONLY.get_position(index),
                                     FLAG_FILE_MAGIC_ONLY.get(index),
                                     FLAG_FILE_MAGIC_ONLY.get_location(index)});

  /* sorts the -m and -M paths by command-line position. match_magic_rules()
     returns the first matching top-level rule, so this order selects the
     magic file that wins. */
  magic_paths.sort(
      [](const file_magic_path &left, const file_magic_path &right) {
        return left.position < right.position;
      });

  ArrayList<FileMagicRule> magic_rules{allocator};

  for (let const &magic_path : magic_paths) {
    if (!append_magic_database(magic_path.path, magic_rules, allocator)) {
      KOSHKIT_REPORT_PATH_ERROR_AT(magic_path.location, "open",
                                   magic_path.path);
      status = 1;
    }
  }

  let const should_apply_default_tests =
      FLAG_FILE_DEFAULT_TESTS.is_enabled() || FLAG_FILE_MAGIC_ONLY.is_empty();
  let const should_follow =
      !FLAG_FILE_NO_FOLLOW.is_enabled() ||
      FLAG_FILE_FOLLOW.position() > FLAG_FILE_NO_FOLLOW.position();

  let operand_paths  = ArrayList<Path>{allocator};
  let file_statuses  = ArrayList<os::file_status>{allocator};
  let metadata_batch = os::Batch{allocator};

  operand_paths.reserve(operands.count());
  file_statuses.reserve(operands.count());
  metadata_batch.reserve(operands.count());

  for (let const &operand : operands) {
    operand_paths.push(Path{operand.view(), allocator});
    file_statuses.push({});
  }

  for (usize operand_position = 0; operand_position < operands.count();
       operand_position++)
  {
    metadata_batch.add(os::BatchOperation::lstat(
        operand_paths[operand_position], file_statuses[operand_position]));
  }

  let const metadata_results = metadata_batch.execute();

  let       default_samples       = ArrayList<String>{allocator};
  let       default_sample_errors = Bitset{allocator};
  let const should_batch_default_samples =
      !FLAG_FILE_REGULAR_ONLY.is_enabled() && magic_rules.is_empty();

  if (should_batch_default_samples) {
    default_samples.reserve(operands.count());

    for (usize operand_position = 0; operand_position < operands.count();
         operand_position++)
    {
      default_samples.push_managed();
    }

    default_sample_errors.reset(operands.count());

    file_content_probe probes[FILE_CONTENT_SAMPLE_BATCH_COUNT]{};
    usize              probe_count     = 0;
    let                content_batch   = os::Batch{allocator};
    let                content_results = ArrayList<os::batch_result>{allocator};

    content_batch.reserve(FILE_CONTENT_SAMPLE_BATCH_COUNT);
    content_results.reserve(FILE_CONTENT_SAMPLE_BATCH_COUNT);
    defer {
      for (usize index = 0; index < probe_count; index++)
        if (probes[index].descriptor != KOSH_INVALID_FD)
          unused(os::close_fd(probes[index].descriptor));
    };

    let const do_flush_probes = [&]() throws -> void {
      content_batch.clear();

      for (usize index = 0; index < probe_count; index++) {
        content_batch.add(os::BatchOperation::read(
            probes[index].descriptor, probes[index].bytes,
            FILE_CONTENT_SAMPLE_BYTE_COUNT));
      }

      content_batch.execute(content_results);

      for (usize index = 0; index < probe_count; index++) {
        let const  operand_position = probes[index].operand_position;
        let const &result           = content_results[index];

        if (result.error_number == 0) {
          default_samples[operand_position] =
              String{allocator,
                     classify_file_content(StringView{
                         probes[index].bytes, result.transferred_byte_count})
                         .description};
        } else {
          os::set_last_system_error(result.error_number);
          default_samples[operand_position] = os::last_system_error_message();
          default_sample_errors.set(operand_position);
        }

        unused(os::close_fd(probes[index].descriptor));
        probes[index].descriptor = KOSH_INVALID_FD;
      }

      probe_count = 0;
    };

    for (usize operand_position = 0; operand_position < operands.count();
         operand_position++)
    {
      if (metadata_results[operand_position].error_number != 0 ||
          os::file_type_letter(file_statuses[operand_position].mode) != '-')
      {
        continue;
      }

      let const descriptor = os::open_file_descriptor(
          operands[operand_position].view(), os::file_open_mode::Read);

      if (!descriptor.has_value()) {
        default_samples[operand_position] = os::last_system_error_message();
        default_sample_errors.set(operand_position);
        continue;
      }

      probes[probe_count].descriptor       = *descriptor;
      probes[probe_count].operand_position = operand_position;
      probe_count++;
      if (probe_count == FILE_CONTENT_SAMPLE_BATCH_COUNT) do_flush_probes();
    }

    if (probe_count != 0) do_flush_probes();
  }

  let       custom_sample_descriptions      = ArrayList<String>{allocator};
  let       custom_sample_error_numbers     = ArrayList<i32>{allocator};
  let       custom_sample_ready             = Bitset{allocator};
  let       custom_sample_errors            = Bitset{allocator};
  let       custom_sample_open_errors       = Bitset{allocator};
  bool      was_custom_sampling_interrupted = false;
  let const should_batch_custom_samples =
      !FLAG_FILE_REGULAR_ONLY.is_enabled() && !magic_rules.is_empty();

  if (should_batch_custom_samples) {
    custom_sample_descriptions.reserve(operands.count());
    custom_sample_error_numbers.reserve(operands.count());

    for (usize operand_position = 0; operand_position < operands.count();
         operand_position++)
    {
      custom_sample_descriptions.push_managed();
      custom_sample_error_numbers.push(0);
    }

    custom_sample_ready.reset(operands.count());
    custom_sample_errors.reset(operands.count());
    custom_sample_open_errors.reset(operands.count());

    usize sample_byte_count = 1;

    for (let const &rule : magic_rules) {
      if (rule.offset > SIZE_MAX) continue;

      let const offset = static_cast<usize>(rule.offset);
      let const width  = rule.kind == file_magic_kind::String
                             ? rule.expected_text.count()
                             : rule.byte_count;

      if (width > SIZE_MAX - offset) continue;

      let const rule_end = offset + width;

      if (rule_end > sample_byte_count) sample_byte_count = rule_end;
    }

    if (should_apply_default_tests &&
        sample_byte_count < FILE_CONTENT_SAMPLE_BYTE_COUNT)
    {
      sample_byte_count = FILE_CONTENT_SAMPLE_BYTE_COUNT;
    }

    metadata_batch.clear();

    for (usize operand_position = 0; operand_position < operands.count();
         operand_position++)
    {
      if (metadata_results[operand_position].error_number != 0 ||
          os::file_type_letter(file_statuses[operand_position].mode) != 'l' ||
          !should_follow)
      {
        continue;
      }

      metadata_batch.add(os::BatchOperation::stat(
          operand_paths[operand_position], file_statuses[operand_position]));
    }

    if (metadata_batch.count() != 0) metadata_batch.execute();

    let sample_sources           = ArrayList<StringView>{allocator};
    let sample_operand_positions = ArrayList<usize>{allocator};

    for (usize operand_position = 0; operand_position < operands.count();
         operand_position++)
    {
      if (metadata_results[operand_position].error_number != 0 ||
          os::file_type_letter(file_statuses[operand_position].mode) != '-')
      {
        continue;
      }

      sample_sources.push(operands[operand_position].view());
      sample_operand_positions.push(operand_position);
    }

    let source_results = ArrayList<source_read_result>{allocator};

    source_results.reserve(sample_sources.count());

    for (usize source_index = 0; source_index < sample_sources.count();
         source_index++)
    {
      source_results.push({None, 0, source_completion_state::Pending});
    }

    let const read_byte_count =
        sample_byte_count < 64 * 1024 ? sample_byte_count : usize{64 * 1024};
    let reader =
        SourceBatchReader{ec,
                          sample_sources,
                          allocator,
                          read_byte_count,
                          SourceBatchReader::SourceDashMode::TreatAsPath,
                          SourceBatchReader::SourceKindMode::KnownRegular};
    let chunks = ArrayList<SourceBatchReader::chunk>{allocator};

    loop {
      let const read_result        = reader.read_next(chunks);
      bool      is_reader_complete = false;

      switch (read_result) {
      case SourceBatchReader::ReadResult::Chunks: break;

      case SourceBatchReader::ReadResult::Complete:
        is_reader_complete = true;
        break;

      case SourceBatchReader::ReadResult::Interrupted:
        was_custom_sampling_interrupted = true;
        break;
      }

      for (let const &chunk : chunks) {
        let &result = source_results[chunk.source_index];

        result.completion = chunk.completion;
        if (chunk.error_number != 0) {
          result.content.reset();
          result.error_number = chunk.error_number;
        } else {
          if (!result.content.has_value())
            result.content = String{heap_allocator()};
          result.content->append(chunk.content);

          if (result.content->length() == sample_byte_count &&
              result.completion != source_completion_state::Complete)
          {
            reader.finish_source(chunk.source_index);
            result.completion = source_completion_state::Complete;
          } else if (result.completion != source_completion_state::Complete) {
            let const remaining_byte_count =
                sample_byte_count - result.content->length();

            reader.set_source_read_byte_count(
                chunk.source_index, remaining_byte_count < read_byte_count
                                        ? remaining_byte_count
                                        : read_byte_count);
          }
        }

        if (result.completion != source_completion_state::Complete) continue;

        let const operand_position =
            sample_operand_positions[chunk.source_index];

        custom_sample_ready.set(operand_position);
        if (!result.content.has_value()) {
          custom_sample_errors.set(operand_position);
          if (chunk.open_state == source_open_state::Failed)
            custom_sample_open_errors.set(operand_position);
          custom_sample_error_numbers[operand_position] = result.error_number;
          continue;
        }

        let const sample = result.content->view();

        if (sample.is_empty()) {
          custom_sample_descriptions[operand_position] =
              String{allocator, "empty"};
        } else if (let magic_description =
                       match_magic_rules(magic_rules, sample, allocator);
                   magic_description.has_value())
        {
          custom_sample_descriptions[operand_position] =
              magic_description.take();
        } else if (should_apply_default_tests) {
          let const fallback_sample =
              sample.substring_of_length(0, FILE_CONTENT_SAMPLE_BYTE_COUNT);

          custom_sample_descriptions[operand_position] =
              String{allocator, classify_file_content(fallback_sample).description};
        } else {
          custom_sample_descriptions[operand_position] =
              String{allocator, "data"};
        }

        result.content.reset();
      }

      if (is_reader_complete || was_custom_sampling_interrupted) {
        break;
      }
    }
  }

  let const should_color = koshkit_should_color();

  for (usize operand_position = 0; operand_position < operands.count();
       operand_position++)
  {
    let const &operand     = operands[operand_position];
    let       &file_status = file_statuses[operand_position];

    if (metadata_results[operand_position].error_number != 0) {
      os::set_last_system_error(
          metadata_results[operand_position].error_number);
      KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[operand_position], "open",
                                   operand);
      status = 1;
      continue;
    }

    let const is_symbolic_link = os::file_type_letter(file_status.mode) == 'l';

    if (is_symbolic_link) {
      if (!should_follow || should_batch_custom_samples ||
          !os::stat_path_following(operand.view(), file_status))
      {
        let const target = os::read_symlink(operand.view(), allocator);
        let       line   = String{allocator};

        append_report_text(line, operand.view(), colors::color::Bold,
                           should_color);
        line += ": symbolic link";
        if (target.has_value()) line += " to " + *target;
        line += '\n';
        ec.print_to_stdout(line);
        continue;
      }
    }

    /* at this point: operand has no metadata error, and when operand is a
       symbolic link, file_status holds the status of the target that was
       followed. */
    let       description = String{allocator};
    let const is_regular  = os::file_type_letter(file_status.mode) == '-';

    if (is_regular && FLAG_FILE_REGULAR_ONLY.is_enabled()) {
      description += "regular file";
    } else if (is_regular && magic_rules.is_empty() &&
               !default_samples[operand_position].is_empty())
    {
      if (default_sample_errors[operand_position]) {
        report_soft_koshkit_error(ec, cxt, operand_locations[operand_position],
                                  "Cannot read '" + operand + "': " +
                                      default_samples[operand_position]);
        status = 1;
        continue;
      }

      description += default_samples[operand_position].view();
    } else if (!is_regular || magic_rules.is_empty()) {
      let const default_description =
          describe_file_type(operand.view(), file_status, allocator);

      if (!default_description.has_value()) {
        KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[operand_position],
                                     "read", operand);
        status = 1;
        continue;
      }

      description += default_description->view();
    } else {
      if (custom_sample_errors[operand_position]) {
        os::set_last_system_error(
            custom_sample_error_numbers[operand_position]);
        report_soft_koshkit_error(
            ec, cxt, operand_locations[operand_position],
            StringView{custom_sample_open_errors[operand_position]
                           ? "Cannot open '"
                           : "Cannot read '"} +
                operand + "': " + os::last_system_error_message());
        status = 1;
        continue;
      }

      if (!custom_sample_ready[operand_position]) {
        ASSERT(was_custom_sampling_interrupted,
               "custom file sample completed without a result");

        return 130;
      }

      description += custom_sample_descriptions[operand_position].view();
    }

    let line = String{allocator};

    append_report_text(line, operand.view(), colors::color::Bold, should_color);
    line += ": ";
    line += description;
    line += '\n';
    ec.print_to_stdout(line);
  }

  return was_custom_sampling_interrupted ? 130 : status;
}

} /* namespace koshka::koshkit */
