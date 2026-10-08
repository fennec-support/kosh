/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the tr utility. It expands escapes, ranges, repetitions,
 * and POSIX character classes, then translates, deletes, or squeezes streamed
 * input bytes, optionally over the complement of the first set.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL(
    "[-cCds] set1 [set2]",
    "The tr utility translates the bytes in set1 to the matching bytes in "
    "set2.");

FLAG(TR_DELETE, Bool, 'd', "", "Delete the bytes in set1.");
FLAG(TR_COMPLEMENT, Bool, 'c', "",
     "Use every byte that is not in set1, in byte order.");
FLAG(TR_COMPLEMENT_VALUES, Bool, 'C', "",
     "Use every byte that is not in set1, in byte order.");
FLAG(TR_SQUEEZE, Bool, 's', "",
     "Collapse each run of one repeated byte from the final set into one.");

REGISTER_KOSHKIT_UTIL_FLAGS(Tr);

namespace koshka::koshkit {

using posix_class_test = bool (*)(u8 byte);

constexpr static_string_entry<posix_class_test> TR_POSIX_CLASS_ENTRIES[] = {
    {SSK("alnum"),  [](u8 byte) { return std::isalnum(byte) != 0; } },
    {SSK("alpha"),  [](u8 byte) { return std::isalpha(byte) != 0; } },
    {SSK("blank"),  [](u8 byte) { return std::isblank(byte) != 0; } },
    {SSK("cntrl"),  [](u8 byte) { return std::iscntrl(byte) != 0; } },
    {SSK("digit"),  [](u8 byte) { return std::isdigit(byte) != 0; } },
    {SSK("graph"),  [](u8 byte) { return std::isgraph(byte) != 0; } },
    {SSK("lower"),  [](u8 byte) { return std::islower(byte) != 0; } },
    {SSK("print"),  [](u8 byte) { return std::isprint(byte) != 0; } },
    {SSK("punct"),  [](u8 byte) { return std::ispunct(byte) != 0; } },
    {SSK("space"),  [](u8 byte) { return std::isspace(byte) != 0; } },
    {SSK("upper"),  [](u8 byte) { return std::isupper(byte) != 0; } },
    {SSK("xdigit"), [](u8 byte) { return std::isxdigit(byte) != 0; }},
};

constexpr StaticStringMap TR_POSIX_CLASSES{TR_POSIX_CLASS_ENTRIES};

struct decoded_char
{
  unsigned char byte;
  usize width_count;
};

static fn decode_escaped_char(StringView set, usize position) wontthrow
    -> decoded_char
{
  if (set[position] != '\\' || position + 1 >= set.length) {
    return {static_cast<unsigned char>(set[position]), 1};
  }

  let const escaped = set[position + 1];
  switch (escaped) {
  case 'a': return {'\a', 2};
  case 'b': return {'\b', 2};
  case 'f': return {'\f', 2};
  case 'n': return {'\n', 2};
  case 'r': return {'\r', 2};
  case 't': return {'\t', 2};
  case 'v': return {'\v', 2};
  case '\\': return {'\\', 2};
  }

  if (escaped >= '0' && escaped <= '7') {
    int value = 0;
    usize digit_count = 0;
    while (digit_count < 3 && position + 1 + digit_count < set.length) {
      let const digit = set[position + 1 + digit_count];
      if (digit < '0' || digit > '7') {
        break;
      }
      value = value * 8 + (digit - '0');
      digit_count++;
    }
    return {static_cast<unsigned char>(value), 1 + digit_count};
  }

  return {static_cast<unsigned char>(escaped), 2};
}

static fn expand_posix_class(StringView set, usize position,
                             String &expanded) throws -> usize
{
  if (set[position] != '[' || position + 1 >= set.length ||
      set[position + 1] != ':')
  {
    return 0;
  }

  usize scan = position + 2;
  while (scan + 1 < set.length && !(set[scan] == ':' && set[scan + 1] == ']'))
    scan++;

  if (scan + 1 >= set.length || set[scan] != ':' || set[scan + 1] != ']') {
    return 0;
  }

  let const name_start = position + 2;
  let const class_name = set.substring_of_length(name_start, scan - name_start);
  let const test = TR_POSIX_CLASSES.find(class_name);
  if (!test.has_value()) return 0;

  for (int byte = 0; byte < 256; byte++) {
    if ((*test)(static_cast<u8>(byte))) expanded.push(static_cast<char>(byte));
  }

  return scan + 2 - position;
}

static fn expand_set(StringView set, Allocator allocator) throws
    -> Maybe<String>
{
  String expanded{allocator};
  usize i = 0;
  while (i < set.length) {
    if (let const class_width = expand_posix_class(set, i, expanded);
        class_width > 0)
    {
      i += class_width;
      continue;
    }

    let const first = decode_escaped_char(set, i);
    let const after = i + first.width_count;

    if (after < set.length && set[after] == '-' && after + 1 < set.length) {
      let const second = decode_escaped_char(set, after + 1);
      if (first.byte > second.byte) {
        return None;
      }

      for (int c = first.byte; c <= second.byte; c++)
        expanded.push(static_cast<char>(c));

      i = after + 1 + second.width_count;
      continue;
    }

    expanded.push(static_cast<char>(first.byte));
    i = after;
  }
  return expanded;
}

fn Tr::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const is_deleting = FLAG_TR_DELETE.is_enabled();
  let const is_squeezing = FLAG_TR_SQUEEZE.is_enabled();
  let const is_complementing =
      FLAG_TR_COMPLEMENT.is_enabled() || FLAG_TR_COMPLEMENT_VALUES.is_enabled();
  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  if (!is_deleting && !is_squeezing && operands.count() < 2) {
    throw ErrorWithDetails{"tr expects two sets unless -d is given",
                           "Supply SET1 and SET2, or use `-d` with one set"};
  }

  if (operands.count() > 2) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  if (is_deleting && is_squeezing && operands.count() < 2) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  if (is_deleting && !is_squeezing && operands.count() > 1) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  let set1 = expand_set(operands[0].view(), cxt.scratch_allocator());
  if (!set1.has_value()) {
    KOSHKIT_REPORT_ERROR_AT(
        operand_locations[0], "reverse range in set '" + operands[0] + "'",
        "order the endpoints from the lower byte to the higher byte");
    return 1;
  }

  if (is_complementing) {
    bool is_listed[256] = {};
    for (usize i = 0; i < set1->count(); i++)
      is_listed[static_cast<unsigned char>(set1->view()[i])] = true;

    let complement = String{cxt.scratch_allocator()};
    for (int byte = 0; byte < 256; byte++) {
      if (!is_listed[byte]) complement.push(static_cast<char>(byte));
    }
    set1 = steal(complement);
  }

  let set2 = String{cxt.scratch_allocator()};
  if (operands.count() >= 2) {
    let expanded_set2 = expand_set(operands[1].view(), cxt.scratch_allocator());
    if (!expanded_set2.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          operand_locations[1], "reverse range in set '" + operands[1] + "'",
          "order the endpoints from the lower byte to the higher byte");
      return 1;
    }
    set2 = steal(*expanded_set2);
  }

  static constexpr usize BYTE_VALUE_COUNT = 256;
  bool is_in_set1[BYTE_VALUE_COUNT] = {};
  unsigned char translation[BYTE_VALUE_COUNT];
  for (usize i = 0; i < BYTE_VALUE_COUNT; i++)
    translation[i] = static_cast<unsigned char>(i);

  for (usize i = 0; i < set1->count(); i++) {
    let const from = static_cast<unsigned char>(set1->view()[i]);
    is_in_set1[from] = true;
    if (!is_deleting && set2.count() > 0) {
      let const index = i < set2.count() ? i : set2.count() - 1;
      translation[from] = static_cast<unsigned char>(set2.view()[index]);
    }
  }

  bool is_squeezed[BYTE_VALUE_COUNT] = {};
  if (is_squeezing) {
    let const &squeeze_source = operands.count() >= 2 ? set2 : *set1;
    for (usize i = 0; i < squeeze_source.count(); i++)
      is_squeezed[static_cast<unsigned char>(squeeze_source.view()[i])] = true;
  }
  int last_byte = -1;

  char input[65536];
  char output[sizeof(input)];
  loop
  {
    let const read_count =
        os::read_fd(ec.in_fd.value_or(KOSH_STDIN), input, sizeof(input));
    if (!read_count.has_value()) {
      if (os::INTERRUPT_REQUESTED) return 130;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "read failed: " +
                                         os::last_system_error_message());
      return 1;
    }
    if (*read_count == 0) break;

    usize output_count = 0;
    if (is_deleting) {
      for (usize i = 0; i < *read_count; i++) {
        let const byte = static_cast<unsigned char>(input[i]);
        if (!is_in_set1[byte]) output[output_count++] = static_cast<char>(byte);
      }
    } else {
      for (usize i = 0; i < *read_count; i++)
        output[i] = static_cast<char>(
            translation[static_cast<unsigned char>(input[i])]);

      output_count = *read_count;
    }

    if (is_squeezing) {
      usize kept_count = 0;
      for (usize i = 0; i < output_count; i++) {
        let const byte = static_cast<unsigned char>(output[i]);
        if (is_squeezed[byte]) {
          if (byte == last_byte) continue;
        }

        last_byte = byte;
        output[kept_count++] = static_cast<char>(byte);
      }
      output_count = kept_count;
    }

    if (output_count > 0) ec.print_to_stdout(StringView{output, output_count});
  }

  return 0;
}

}
