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
#include "../Lexer.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-cCdst] set1 [set2]",
                  "Translate the bytes in set1 to the matching bytes in "
                  "set2.");

FLAG(TR_DELETE, Bool, 'd', "", "Delete the bytes in set1.");
FLAG(TR_COMPLEMENT,
     Bool,
     'c',
     "",
     "Use every byte that is not in set1, in byte order.");
FLAG(TR_COMPLEMENT_VALUES,
     Bool,
     'C',
     "",
     "Use every byte that is not in set1, in byte order.");
FLAG(TR_SQUEEZE,
     Bool,
     's',
     "",
     "Collapse each run of one repeated byte from the final set into one.");

FLAG(TR_TRUNCATE_SET1,
     Bool,
     't',
     "truncate-set1",
     "Translate only the bytes of set1 that have a byte in set2.");

REGISTER_KOSHKIT_UTIL_FLAGS(Tr);

namespace koshka::koshkit {

using posix_class_test = bool (*)(u8 byte);

constexpr static_string_entry<posix_class_test> TR_POSIX_CLASS_ENTRIES[] = {
    {SSK("alnum"),  [](u8 byte) { return isalnum(byte) != 0; } },
    {SSK("alpha"),  [](u8 byte) { return isalpha(byte) != 0; } },
    {SSK("blank"),  [](u8 byte) { return isblank(byte) != 0; } },
    {SSK("cntrl"),  [](u8 byte) { return iscntrl(byte) != 0; } },
    {SSK("digit"),  [](u8 byte) { return isdigit(byte) != 0; } },
    {SSK("graph"),  [](u8 byte) { return isgraph(byte) != 0; } },
    {SSK("lower"),  [](u8 byte) { return islower(byte) != 0; } },
    {SSK("print"),  [](u8 byte) { return isprint(byte) != 0; } },
    {SSK("punct"),  [](u8 byte) { return ispunct(byte) != 0; } },
    {SSK("space"),  [](u8 byte) { return isspace(byte) != 0; } },
    {SSK("upper"),  [](u8 byte) { return isupper(byte) != 0; } },
    {SSK("xdigit"), [](u8 byte) { return isxdigit(byte) != 0; }},
};

constexpr StaticStringMap TR_POSIX_CLASSES{TR_POSIX_CLASS_ENTRIES};

enum class tr_element_kind : u8
{
  Byte,
  Range,
  Class,
  Equivalence,
  Repeat,
};

enum class tr_case_class : u8
{
  None,
  Upper,
  Lower,
};

struct tr_element
{
  tr_element_kind  kind;
  u8               first_byte;
  u8               last_byte;
  posix_class_test test;
  tr_case_class    case_class;
  usize            repeat_count;
  bool             is_indefinite;
};

struct tr_error
{
  String     message;
  StringView note;
};

struct tr_set
{
  ArrayList<tr_element> elements;
  String                bytes;
  ArrayList<usize>      element_indices;
};

/* e.g. set = "a\\n\\101" gives the bytes "a\nA", and the last two are
   escaped. an octal escape takes a third digit only when the value stays
   below 256. */
static fn
unescape_tr_set(StringView       set,
                String          &bytes,
                ArrayList<bool> &is_escaped) throws -> void
{
  for (usize i = 0; i < set.length; i++) {
    if (set[i] != '\\') {
      bytes.push(set[i]);
      is_escaped.push(false);
      continue;
    }

    if (i + 1 == set.length) {
      bytes.push('\\');
      is_escaped.push(true);
      continue;
    }

    let  escaped = set[++i];
    char byte    = escaped;

    switch (escaped) {
    case 'a': byte = '\a'; break;
    case 'b': byte = '\b'; break;
    case 'f': byte = '\f'; break;
    case 'n': byte = '\n'; break;
    case 'r': byte = '\r'; break;
    case 't': byte = '\t'; break;
    case 'v': byte = '\v'; break;
    }

    if (escaped >= '0' && escaped <= '7') {
      int value = escaped - '0';

      for (int digit_count = 1; digit_count < 3 && i + 1 < set.length;
           digit_count++)
      {
        let const digit = set[i + 1];

        if (digit < '0' || digit > '7' || value * 8 + (digit - '0') > 255) {
          break;
        }

        value = value * 8 + (digit - '0');
        i++;
      }

      byte = static_cast<char>(value);
    }

    bytes.push(byte);
    is_escaped.push(true);
  }
}

/* memory: the unescaped bytes and their escape markers use allocator.
   time:   O(set length) plus O(n) for each [c*n] count and class name. */
static fn
parse_tr_set(StringView             set,
             ArrayList<tr_element> &elements,
             Allocator              allocator) throws -> Maybe<tr_error>
{
  let  bytes      = String{allocator};
  let  is_escaped = ArrayList<bool>{allocator};
  let &p          = bytes;

  unescape_tr_set(set, bytes, is_escaped);

  let const length   = bytes.count();
  let const do_match = [&](usize position, char wanted) wontthrow -> bool {
    return position < length && p[position] == wanted && !is_escaped[position];
  };
  let const do_append = [&](tr_element_kind kind,
                            u8              first_byte) throws {
    elements.push(tr_element{kind, first_byte, first_byte, nullptr,
                             tr_case_class::None, 0, false});
  };

  /* e.g. "[x*5]" at position gives a repeat of 'x' five times, "[x*]" an
     indefinite repeat, and "[x*y]" an invalid count. */
  Maybe<tr_error> repeat_error;

  let const do_parse_repeat =
      [&](usize position) throws -> Maybe<usize> {
    if (!do_match(position + 2, '*')) return None;

    for (usize close = position + 3; close < length && !is_escaped[close];
         close++)
    {
      if (p[close] != ']') continue;

      let const digits =
          p.view().substring_of_length(position + 3, close - position - 3);
      let element = tr_element{tr_element_kind::Repeat,
                               static_cast<u8>(p[position + 1]),
                               static_cast<u8>(p[position + 1]),
                               nullptr,
                               tr_case_class::None,
                               0,
                               true};

      if (!digits.is_empty()) {
        let const base = digits[0] == '0' ? int_base::octal : int_base::decimal;
        let const parsed = utils::parse_integer_in_base_u64(digits, base);

        if (parsed.is_error() || parsed.value() > SIZE_MAX / 2) {
          repeat_error = tr_error{
              "Invalid repeat count '" + String{allocator, digits}
                +
                  "' in [c*n] construct",
              "Write the count in decimal, or in octal with a leading 0"
          };

          return None;
        }

        element.repeat_count  = static_cast<usize>(parsed.value());
        element.is_indefinite = element.repeat_count == 0;
      }

      elements.push(element);

      return close + 1;
    }

    return None;
  };

  let const do_is_star_digits_close = [&](usize position) wontthrow -> bool {
    if (!do_match(position, '*')) return false;

    for (usize i = position + 1; i < length; i++) {
      if (!lexer::is_number(p[i]) || is_escaped[i]) return do_match(i, ']');
    }

    return false;
  };

  usize i = 0;

  while (i + 2 < length) {
    if (do_match(i, '[')) {
      let const delimiter = p[i + 1];

      if (do_match(i + 1, ':') || do_match(i + 1, '=')) {
        Maybe<usize> close;

        for (usize scan = i + 2; scan + 1 < length; scan++) {
          if (do_match(scan, delimiter) && do_match(scan + 1, ']')) {
            close = scan;
            break;
          }
        }

        if (close.has_value()) {
          let const operand =
              p.view().substring_of_length(i + 2, *close - i - 2);

          if (operand.is_empty()) {
            return tr_error{
                String{allocator,
                       delimiter == ':'
                           ? "Missing character class name '[::]'"
                           : "Missing equivalence class character '[==]'"},
                {}
            };
          }

          if (delimiter == ':') {
            let const test = TR_POSIX_CLASSES.find(operand);

            if (test.has_value()) {
              let const case_class = operand == "upper"   ? tr_case_class::Upper
                                     : operand == "lower" ? tr_case_class::Lower
                                                          : tr_case_class::None;

              elements.push(tr_element{tr_element_kind::Class, 0, 0, *test,
                                       case_class, 0, false});
              i = *close + 2;
              continue;
            }

            if (!do_is_star_digits_close(i + 2)) {
              return tr_error{
                  "Invalid character class '" + String{allocator, operand}
                    +
                      "'",
                  "Use alnum, alpha, blank, cntrl, digit, graph, lower, "
                  "print, punct, space, upper, or xdigit"
              };
            }
          } else if (operand.length == 1) {
            do_append(tr_element_kind::Equivalence,
                      static_cast<u8>(operand[0]));
            i = *close + 2;
            continue;
          } else if (!do_is_star_digits_close(i + 2)) {
            return tr_error{
                "'" + String{allocator, operand}
                  +
                    "': An equivalence class operand must be a "
                    "single character",
                {}
            };
          }
        }
      }

      if (let const next = do_parse_repeat(i); next.has_value()) {
        i = *next;
        continue;
      }

      if (repeat_error.has_value()) return repeat_error;
    }

    if (do_match(i + 1, '-')) {
      let const first_byte = static_cast<u8>(p[i]);
      let const last_byte  = static_cast<u8>(p[i + 2]);

      if (first_byte > last_byte) {
        return tr_error{
            "Range endpoints of '" +
                String{allocator, p.view().substring_of_length(i, 3)}
                +
                "' are in reverse order",
            "Order the endpoints from the lower byte to the higher byte"
        };
      }

      elements.push(tr_element{tr_element_kind::Range, first_byte, last_byte,
                               nullptr, tr_case_class::None, 0, false});
      i += 3;
      continue;
    }

    do_append(tr_element_kind::Byte, static_cast<u8>(p[i]));
    i++;
  }

  for (; i < length; i++)
    do_append(tr_element_kind::Byte, static_cast<u8>(p[i]));

  return None;
}

/* a [c*n] repeat in set1 expands to at most 65536 bytes. a repeat in set2
   needs at most one byte more than set1, which keeps every mapping and every
   length comparison with set1. */
static constexpr usize SET1_REPEAT_LIMIT = 65536;

static fn
expand_tr_set(tr_set &set, usize repeat_limit) throws -> void
{
  set.bytes.clear();
  set.element_indices.clear();

  for (usize index = 0; index < set.elements.count(); index++) {
    let const &element = set.elements[index];

    let const do_push = [&](int byte) throws {
      set.bytes.push(static_cast<char>(byte));
      set.element_indices.push(index);
    };

    switch (element.kind) {
    case tr_element_kind::Byte:
    case tr_element_kind::Equivalence: do_push(element.first_byte); break;
    case tr_element_kind::Range:
      for (int byte = element.first_byte; byte <= element.last_byte; byte++)
        do_push(byte);
      break;
    case tr_element_kind::Class:
      for (int byte = 0; byte < 256; byte++) {
        if (element.test(static_cast<u8>(byte))) do_push(byte);
      }
      break;
    case tr_element_kind::Repeat:
      for (usize n = 0; n < std::min(element.repeat_count, repeat_limit); n++)
        do_push(element.first_byte);
      break;
    }
  }
}

/* an [:upper:] or [:lower:] class in set2 must start where an [:upper:] or
   [:lower:] class starts in set1. the two sets are walked one byte at a
   time, and a pair of case classes is passed over as one step. */
static pure fn
are_case_classes_aligned(const tr_set &set1, const tr_set &set2)
    wontthrow -> bool
{
  let const do_get_case = [](const tr_set &set,
                             usize         position) wontthrow -> tr_case_class {
    return position < set.bytes.count()
               ? set.elements[set.element_indices[position]].case_class
               : tr_case_class::None;
  };
  let const do_skip_element = [](const tr_set &set, usize &position) wontthrow {
    let const index = set.element_indices[position];

    while (position < set.bytes.count() &&
           set.element_indices[position] == index)
    {
      position++;
    }
  };
  let const do_is_element_start = [](const tr_set &set,
                                     usize         position) wontthrow -> bool {
    return position == 0 || position >= set.bytes.count() ||
           set.element_indices[position] != set.element_indices[position - 1];
  };

  usize position1 = 0;
  usize position2 = 0;
  let   case1     = tr_case_class::None;
  let   case2     = tr_case_class::None;
  bool  has_ended = false;

  while (!has_ended) {
    let const is_new1 = do_is_element_start(set1, position1);
    let const is_new2 = do_is_element_start(set2, position2);

    has_ended =
        position1 >= set1.bytes.count() || position2 >= set2.bytes.count();
    if (is_new1) case1 = do_get_case(set1, position1);
    if (is_new2) case2 = do_get_case(set2, position2);

    if (is_new2 && case2 != tr_case_class::None &&
        !(is_new1 && case1 != tr_case_class::None))
    {
      return false;
    }

    if (case2 != tr_case_class::None) {
      do_skip_element(set1, position1);
      do_skip_element(set2, position2);
      continue;
    }

    position1++;
    position2++;
  }

  return true;
}

/* memory: input and output are 64 KiB stack arrays, 128 KiB together. set1
           and set2, including the complement of set1, use
           cxt.scratch_allocator().
   time:   O(input bytes) for the read loop. table setup is O(256) plus
           O(set1 + set2) entries. one read_fd() call and at most one
           print_to_stdout() call per chunk. */
fn
Tr::execute(const ExecContext               &ec,
            EvalContext                     &cxt,
            const ArrayList<String>         &args,
            const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const is_deleting   = FLAG_TR_DELETE.is_enabled();
  let const is_squeezing  = FLAG_TR_SQUEEZE.is_enabled();
  let const is_truncating = FLAG_TR_TRUNCATE_SET1.is_enabled();
  let const is_complementing =
      FLAG_TR_COMPLEMENT.is_enabled() || FLAG_TR_COMPLEMENT_VALUES.is_enabled();

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const do_fail = [&](SourceLocation location, StringView message,
                          StringView note = {}) throws -> i32 {
    if (note.is_empty()) {
      KOSHKIT_REPORT_ERROR_AT(location, message);
    } else {
      KOSHKIT_REPORT_ERROR_AT(location, message, note);
    }

    return 1;
  };

  let const minimum_operand_count =
      usize{is_deleting == is_squeezing ? 2u : 1u};
  let const maximum_operand_count =
      usize{is_deleting && !is_squeezing ? 1u : 2u};

  if (operands.count() < minimum_operand_count) {
    return do_fail(operand_locations[0],
                   is_squeezing ? "Expects two sets when deleting and squeezing"
                                : "Expects two sets unless -d is given",
                   is_squeezing
                       ? "Supply SET1 to delete and SET2 to squeeze"
                       : "Supply SET1 and SET2, or use `-d` with one set");
  }

  if (operands.count() > maximum_operand_count) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[maximum_operand_count],
                            "Extra operand '" +
                                operands[maximum_operand_count] + "'",
                            maximum_operand_count == 1
                                ? "One set is accepted when deleting without -s"
                                : "Supply at most two sets");

    return 1;
  }

  let const allocator      = cxt.scratch_allocator();
  let const is_translating = operands.count() == 2 && !is_deleting;
  let       set1 = tr_set{ArrayList<tr_element>{allocator}, String{allocator},
                    ArrayList<usize>{allocator}};
  let       set2 = tr_set{ArrayList<tr_element>{allocator}, String{allocator},
                    ArrayList<usize>{allocator}};

  for (usize index = 0; index < operands.count(); index++) {
    let const error =
        parse_tr_set(operands[index].view(),
                     index == 0 ? set1.elements : set2.elements, allocator);

    if (error.has_value()) {
      return do_fail(operand_locations[index], error->message, error->note);
    }
  }

  for (let const &element : set1.elements) {
    if (element.kind == tr_element_kind::Repeat && element.is_indefinite) {
      return do_fail(operand_locations[0],
                     "The [c*] repeat cannot appear in set1",
                     "Give the repeat a count, such as `[c*3]`");
    }
  }

  expand_tr_set(set1, SET1_REPEAT_LIMIT);

  bool has_class_in_set1 = false;

  for (let const &element : set1.elements) {
    if (element.kind == tr_element_kind::Class) has_class_in_set1 = true;
  }

  bool is_listed[256] = {};

  for (usize i = 0; i < set1.bytes.count(); i++)
    is_listed[static_cast<u8>(set1.bytes[i])] = true;

  if (is_complementing) {
    set1.bytes.clear();
    for (int byte = 0; byte < 256; byte++) {
      if (!is_listed[byte]) set1.bytes.push(static_cast<char>(byte));
    }
  }

  if (operands.count() == 2) {
    let const location         = operand_locations[1];
    usize     indefinite_count = 0;
    usize     fixed_length     = 0;

    bool has_equivalence      = false;
    bool has_restricted_class = false;

    expand_tr_set(set2, set1.bytes.count() + 1);
    for (let const &element : set2.elements) {
      if (element.kind == tr_element_kind::Repeat && element.is_indefinite) {
        indefinite_count++;
      }

      if (element.kind == tr_element_kind::Equivalence) has_equivalence = true;
      if (element.kind == tr_element_kind::Class &&
          element.case_class == tr_case_class::None)
      {
        has_restricted_class = true;
      }
    }

    fixed_length = set2.bytes.count();
    if (indefinite_count > 1) {
      return do_fail(location, "One [c*] repeat is allowed in set2",
                     "Remove the extra `[c*]` repeat from "
                     "SET2");
    }

    if (is_translating && has_equivalence) {
      return do_fail(location, "[=c=] cannot appear in set2 when translating");
    }

    if (is_translating && has_restricted_class) {
      return do_fail(location,
                     "Only the upper and lower classes can appear in set2 when "
                     "translating",
                     "Use `[:upper:]` or `[:lower:]` in SET2");
    }

    if (indefinite_count == 1 && !is_translating) {
      return do_fail(location, "The [c*] repeat can appear in set2 only when "
                               "translating");
    }

    if (indefinite_count == 1 && set1.bytes.count() > fixed_length) {
      for (let &element : set2.elements) {
        if (element.kind == tr_element_kind::Repeat && element.is_indefinite) {
          element.repeat_count = set1.bytes.count() - fixed_length;
        }
      }

      expand_tr_set(set2, set1.bytes.count() + 1);
    }

    if (is_translating) {
      if (!is_complementing && !are_case_classes_aligned(set1, set2)) {
        return do_fail(location, "Misaligned [:upper:] or [:lower:] construct");
      }

      if (set1.bytes.count() > set2.bytes.count() && !is_truncating) {
        if (set2.bytes.is_empty()) {
          return do_fail(location, "Set2 must not be empty unless -t is given",
                         "Supply SET2, or use `-t` to truncate SET1");
        }

        if (set2.elements.back().kind == tr_element_kind::Class) {
          return do_fail(location,
                         "Set2 must not end with a class when set1 is longer",
                         "End SET2 with a byte to repeat");
        }

        set2.bytes.append_repeated(set2.bytes.back(),
                                   set1.bytes.count() - set2.bytes.count());
      }

      bool is_homogeneous = set2.bytes.count() == set1.bytes.count();

      for (usize i = 1; i < set2.bytes.count(); i++) {
        if (set2.bytes[i] != set2.bytes[0]) is_homogeneous = false;
      }

      if (is_complementing && has_class_in_set1 && !is_homogeneous) {
        return do_fail(
            location,
            "Set2 must map every byte to one byte when complementing a "
            "class",
            "Use one byte or `[c*]` in SET2");
      }
    }
  }

  /* at this point: set1.bytes holds the first set, complemented when -c or
     -C is given, and set2.bytes holds the second set, extended to the
     length of set1 unless -t is given, or is empty. */
  static constexpr usize BYTE_VALUE_COUNT             = 256;
  bool                   is_in_set1[BYTE_VALUE_COUNT] = {};
  unsigned char          translation[BYTE_VALUE_COUNT];

  for (usize i = 0; i < BYTE_VALUE_COUNT; i++)
    translation[i] = static_cast<unsigned char>(i);

  for (usize i = 0; i < set1.bytes.count(); i++) {
    let const from = static_cast<unsigned char>(set1.bytes[i]);

    is_in_set1[from] = true;
    if (is_translating && i < set2.bytes.count()) {
      translation[from] = static_cast<unsigned char>(set2.bytes[i]);
    }
  }

  bool is_squeezed[BYTE_VALUE_COUNT] = {};

  if (is_squeezing) {
    let const &squeeze_source = operands.count() >= 2 ? set2.bytes : set1.bytes;

    for (usize i = 0; i < squeeze_source.count(); i++)
      is_squeezed[static_cast<unsigned char>(squeeze_source[i])] = true;
  }

  int last_byte = -1;

  char input[65536];
  char output[sizeof(input)];

  loop {
    let const read_count =
        os::read_fd(ec.in_fd.value_or(KOSH_STDIN), input, sizeof(input));

    if (!read_count.has_value()) {
      if (os::INTERRUPT_REQUESTED) return 130;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "Read failed: " +
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

        last_byte            = byte;
        output[kept_count++] = static_cast<char>(byte);
      }

      output_count = kept_count;
    }

    if (output_count > 0) ec.print_to_stdout(StringView{output, output_count});
  }

  return 0;
}

} /* namespace koshka::koshkit */
