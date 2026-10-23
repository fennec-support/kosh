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
#include "../util/Utils.hpp"

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
  /* zero for an indefinite [c*] repeat until set2 is sized. */
  usize repeat_count;
};

struct tr_error
{
  String     message;
  StringView note;
};

/* count copies of byte, from the element at element_index of its set. a
   [c*n] repeat is one run, so a large count costs no memory. */
struct tr_run
{
  u8    byte;
  usize count;
  usize element_index;
};

struct tr_set
{
  ArrayList<tr_element> elements;
  ArrayList<tr_run>     runs;
  usize                 length{0};

  fn
  push_run(u8 byte, usize count, usize element_index) throws -> void
  {
    if (count == 0) return;
    runs.push(tr_run{byte, count, element_index});
    length = count > SIZE_MAX - length ? SIZE_MAX : length + count;
  }

  fn
  clear_runs() wontthrow -> void
  {
    runs.clear();
    length = 0;
  }
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
                             tr_case_class::None, 0});
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
                               0};

      if (!digits.is_empty()) {
        let const base = digits[0] == '0' ? int_base::octal : int_base::decimal;
        let const parsed = utils::parse_integer_in_base_u64(digits, base);

        if (parsed.is_error() || parsed.value() > SIZE_MAX / 2) {
          repeat_error = tr_error{
              "Invalid repeat count '" + String{allocator, digits}
                + "'",
              "Write the count in decimal, or in octal with a leading 0"
          };

          return None;
        }

        element.repeat_count = static_cast<usize>(parsed.value());
      }

      elements.push(element);

      return close + 1;
    }

    return None;
  };

  let const do_is_star_digits_close = [&](usize position) wontthrow -> bool {
    if (!do_match(position, '*')) return false;

    for (usize i = position + 1; i < length; i++) {
      if (!lexer::is_number(p[i]) || is_escaped[i]) {
        return do_match(i, ']');
      }
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
                           ? "The character class '[::]' is empty"
                           : "The equivalence class '[==]' is empty"},
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
                                       case_class, 0});
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
            "The range '" +
                String{allocator, p.view().substring_of_length(i, 3)}
                +
                "' is in reverse order",
            "Order the endpoints from the lower byte to the higher byte"
        };
      }

      elements.push(tr_element{tr_element_kind::Range, first_byte, last_byte,
                               nullptr, tr_case_class::None, 0});
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

static fn
expand_tr_set(tr_set &set) throws -> void
{
  set.clear_runs();

  for (usize index = 0; index < set.elements.count(); index++) {
    let const &element = set.elements[index];

    switch (element.kind) {
    case tr_element_kind::Byte:
    case tr_element_kind::Equivalence:
      set.push_run(element.first_byte, 1, index);
      break;
    case tr_element_kind::Range:
      for (int byte = element.first_byte; byte <= element.last_byte; byte++)
        set.push_run(static_cast<u8>(byte), 1, index);
      break;
    case tr_element_kind::Class:
      for (int byte = 0; byte < 256; byte++) {
        if (element.test(static_cast<u8>(byte)))
          set.push_run(static_cast<u8>(byte), 1, index);
      }
      break;
    case tr_element_kind::Repeat:
      set.push_run(element.first_byte, element.repeat_count, index);
      break;
    }
  }
}

/* a position in the runs of a set: the run and the offset into it. */
struct tr_cursor
{
  usize run_index{0};
  usize offset{0};
};

/* an [:upper:] or [:lower:] class in set2 must start where an [:upper:] or
   [:lower:] class starts in set1. the two sets are walked one byte at a
   time, and a pair of case classes is passed over as one step. */
static pure fn
are_case_classes_aligned(const tr_set &set1, const tr_set &set2)
    wontthrow -> bool
{
  let const do_is_at_end = [](const tr_set    &set,
                              const tr_cursor &cursor) wontthrow -> bool {
    return cursor.run_index >= set.runs.count();
  };
  let const do_get_case =
      [&](const tr_set &set, const tr_cursor &cursor)
          wontthrow -> tr_case_class {
    return do_is_at_end(set, cursor)
               ? tr_case_class::None
               : set.elements[set.runs[cursor.run_index].element_index]
                     .case_class;
  };
  let const do_skip_element = [&](const tr_set &set,
                                  tr_cursor    &cursor) wontthrow {
    if (do_is_at_end(set, cursor)) return;

    let const index = set.runs[cursor.run_index].element_index;

    while (!do_is_at_end(set, cursor) &&
           set.runs[cursor.run_index].element_index == index)
    {
      cursor.run_index++;
    }
    cursor.offset = 0;
  };
  let const do_is_element_start = [&](const tr_set    &set,
                                      const tr_cursor &cursor)
                                      wontthrow -> bool {
    return do_is_at_end(set, cursor) ||
           (cursor.offset == 0 &&
            (cursor.run_index == 0 ||
             set.runs[cursor.run_index].element_index !=
                 set.runs[cursor.run_index - 1].element_index));
  };
  let const do_remaining = [&](const tr_set    &set,
                               const tr_cursor &cursor) wontthrow -> usize {
    return do_is_at_end(set, cursor)
               ? 1
               : set.runs[cursor.run_index].count - cursor.offset;
  };
  let const do_advance = [&](const tr_set &set, tr_cursor &cursor,
                             usize count) wontthrow {
    if (do_is_at_end(set, cursor)) return;

    cursor.offset += count;
    if (cursor.offset == set.runs[cursor.run_index].count) {
      cursor.run_index++;
      cursor.offset = 0;
    }
  };

  tr_cursor cursor1;
  tr_cursor cursor2;
  let       case1     = tr_case_class::None;
  let       case2     = tr_case_class::None;
  bool      has_ended = false;

  while (!has_ended) {
    let const is_new1 = do_is_element_start(set1, cursor1);
    let const is_new2 = do_is_element_start(set2, cursor2);

    has_ended = do_is_at_end(set1, cursor1) || do_is_at_end(set2, cursor2);
    if (is_new1) case1 = do_get_case(set1, cursor1);
    if (is_new2) case2 = do_get_case(set2, cursor2);

    if (is_new2 && case2 != tr_case_class::None &&
        !(is_new1 && case1 != tr_case_class::None))
    {
      return false;
    }

    if (case2 != tr_case_class::None) {
      do_skip_element(set1, cursor1);
      do_skip_element(set2, cursor2);
      continue;
    }

    /* no element starts inside a run, so both sets move to the nearer run
       end at once. */
    let const remaining1 = do_remaining(set1, cursor1);
    let const remaining2 = do_remaining(set2, cursor2);
    let const step       = remaining1 < remaining2 ? remaining1 : remaining2;

    do_advance(set1, cursor1, step);
    do_advance(set2, cursor2, step);
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
  let       set1 =
      tr_set{ArrayList<tr_element>{allocator}, ArrayList<tr_run>{allocator}};
  let set2 =
      tr_set{ArrayList<tr_element>{allocator}, ArrayList<tr_run>{allocator}};

  for (usize index = 0; index < operands.count(); index++) {
    let const error =
        parse_tr_set(operands[index].view(),
                     index == 0 ? set1.elements : set2.elements, allocator);

    if (error.has_value()) {
      return do_fail(operand_locations[index], error->message, error->note);
    }
  }

  for (let const &element : set1.elements) {
    if (element.kind == tr_element_kind::Repeat && element.repeat_count == 0) {
      return do_fail(operand_locations[0],
                     "The [c*] repeat cannot appear in set1",
                     "Give the repeat a count, such as `[c*3]`");
    }
  }

  expand_tr_set(set1);

  bool has_class_in_set1 = false;

  for (let const &element : set1.elements) {
    if (element.kind == tr_element_kind::Class) has_class_in_set1 = true;
  }

  bool is_listed[256] = {};

  for (let const &run : set1.runs)
    is_listed[run.byte] = true;

  if (is_complementing) {
    set1.clear_runs();
    for (int byte = 0; byte < 256; byte++) {
      if (!is_listed[byte]) set1.push_run(static_cast<u8>(byte), 1, SIZE_MAX);
    }
  }

  if (operands.count() == 2) {
    let const location         = operand_locations[1];
    usize     indefinite_count = 0;
    usize     fixed_length     = 0;

    bool has_equivalence      = false;
    bool has_restricted_class = false;

    expand_tr_set(set2);
    for (let const &element : set2.elements) {
      if (element.kind == tr_element_kind::Repeat && element.repeat_count == 0)
      {
        indefinite_count++;
      }

      if (element.kind == tr_element_kind::Equivalence) has_equivalence = true;
      if (element.kind == tr_element_kind::Class &&
          element.case_class == tr_case_class::None)
      {
        has_restricted_class = true;
      }
    }

    fixed_length = set2.length;
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

    if (indefinite_count == 1 && set1.length > fixed_length) {
      for (let &element : set2.elements) {
        if (element.kind == tr_element_kind::Repeat &&
            element.repeat_count == 0)
        {
          element.repeat_count = set1.length - fixed_length;
        }
      }

      expand_tr_set(set2);
    }

    if (is_translating) {
      if (!is_complementing && !are_case_classes_aligned(set1, set2)) {
        return do_fail(location, "A [:upper:] or [:lower:] class in SET2 does "
                                 "not start at a [:upper:] or [:lower:] class "
                                 "in SET1");
      }

      if (set1.length > set2.length && !is_truncating) {
        if (set2.runs.is_empty()) {
          return do_fail(location, "Set2 must not be empty unless -t is given",
                         "Supply SET2, or use `-t` to truncate SET1");
        }

        if (set2.elements.back().kind == tr_element_kind::Class) {
          return do_fail(location,
                         "Set2 must not end with a class when set1 is longer",
                         "End SET2 with a byte to repeat");
        }

        set2.push_run(set2.runs.back().byte, set1.length - set2.length,
                      set2.runs.back().element_index);
      }

      bool is_homogeneous = set2.length == set1.length;

      for (let const &run : set2.runs) {
        if (run.byte != set2.runs[0].byte) is_homogeneous = false;
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

  /* at this point: set1.runs holds the first set, complemented when -c or
     -C is given, and set2.runs holds the second set, extended to the
     length of set1 unless -t is given, or is empty. */
  static constexpr usize BYTE_VALUE_COUNT             = 256;
  bool                   is_in_set1[BYTE_VALUE_COUNT] = {};
  unsigned char          translation[BYTE_VALUE_COUNT];

  for (usize i = 0; i < BYTE_VALUE_COUNT; i++)
    translation[i] = static_cast<unsigned char>(i);

  for (let const &run : set1.runs)
    is_in_set1[run.byte] = true;

  /* the runs of both sets are walked by position, and a later position
     overrides an earlier mapping of the same byte. */
  if (is_translating) {
    usize index1  = 0;
    usize index2  = 0;
    usize offset1 = 0;
    usize offset2 = 0;

    while (index1 < set1.runs.count() && index2 < set2.runs.count()) {
      let const &run1       = set1.runs[index1];
      let const &run2       = set2.runs[index2];
      let const  remaining1 = run1.count - offset1;
      let const  remaining2 = run2.count - offset2;
      let const  step       = remaining1 < remaining2 ? remaining1 : remaining2;

      translation[run1.byte] = run2.byte;
      offset1 += step;
      offset2 += step;
      if (offset1 == run1.count) {
        index1++;
        offset1 = 0;
      }
      if (offset2 == run2.count) {
        index2++;
        offset2 = 0;
      }
    }
  }

  bool is_squeezed[BYTE_VALUE_COUNT] = {};

  if (is_squeezing) {
    let const &squeeze_source = operands.count() >= 2 ? set2.runs : set1.runs;

    for (let const &run : squeeze_source)
      is_squeezed[run.byte] = true;
  }

  int last_byte = -1;

  char input[65536];
  char output[sizeof(input)];

  loop {
    let const read_count =
        os::read_fd(ec.in_fd.value_or(KOSH_STDIN), input, sizeof(input));

    if (!read_count.has_value()) {
      if (os::INTERRUPT_REQUESTED) return 130;
      report_soft_koshkit_error(
          ec, cxt, "Read failed: " + os::last_system_error_message());

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
