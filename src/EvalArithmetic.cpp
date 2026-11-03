/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file parses shell arithmetic expressions, variable operands,
 * assignments, calculator expressions, and bc expressions into trees and
 * evaluates those trees. It also owns constant arithmetic evaluation. The split
 * keeps arithmetic grammar separate from word expansion and promoted numeric
 * storage.
 */

#include "Errors.hpp"
#include "Eval.hpp"
#include "EvalExtendedArithmetic.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/ErrorOr.hpp"
#include "base/Maybe.hpp"
#include "base/Trace.hpp"
#include "util/Utils.hpp"

namespace koshka {

namespace {

/* Right operands of '**' and the branches of a conditional recurse, so both
   parsers stop such a chain at this depth. */
static constexpr usize  MAX_OPERATOR_CHAIN_DEPTH = 1024;
static const StringView OPERATOR_CHAIN_HINT =
    "An operator chain this long needs parentheses or a loop";

static fn
resolve_subscripted_array_name(const EvalContext &context,
                               StringView         name) throws -> String
{
  let resolved = context.resolve_nameref(name);

  return resolved.has_value() && !resolved->is_empty()
             ? steal(*resolved)
             : String{heap_allocator(), name};
}

/* a command substitution left in a subscript after the whole expression has
   been expanded came from data. It is a literal key of an associative array,
   reached directly or through a nameref, and an indexed array rejects it. */
static fn
associative_array_for_expanded_subscript(const EvalContext &context,
                                         StringView         name,
                                         StringView         subscript) throws
    -> String
{
  let array_name = resolve_subscripted_array_name(context, name);

  if (!context.is_associative_array(array_name.view())) {
    throw ErrorWithDetails{"The subscript '" + subscript +
                               "' runs a command substitution",
                           "A subscript taken from a variable's value is data "
                           "and cannot run a command substitution"};
  }

  return array_name;
}

using arithmetic_internal::ArithmeticValue;

pure fn
fold_leading_digits(StringView text, u32 radix) wontthrow -> u64
{
  u64 magnitude = 0;

  for (usize length = 0; length < text.length; length++) {
    let const digit = utils::hex_digit_value(text[length]);

    if (!digit.has_value() || *digit >= radix) {
      break;
    }

    magnitude = magnitude * radix + *digit;
  }

  return magnitude;
}

pure fn
parse_arithmetic_operand(StringView text) wontthrow -> i64
{
  let body        = text;
  let is_negative = false;

  if (body.length > 0 && (body[0] == '+' || body[0] == '-')) {
    is_negative = body[0] == '-';
    body        = body.substring(1);
  }

  let const detected  = arithmetic_internal::detect_radix_prefix(body);
  let const magnitude = fold_leading_digits(
      body.substring(detected.prefix_length), detected.radix);

  return static_cast<i64>(is_negative ? -magnitude : magnitude);
}

pure fn
is_number_continuation(char c) wontthrow -> bool
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') || c == '_' || c == '@';
}

pure alwaysinline fn
try_parse_single_integer_literal(StringView text) wontthrow -> Maybe<i64>
{
  /* e.g. "12345": a decimal literal with no sign or radix prefix, short
     enough that 18 digits cannot overflow, is folded directly. */
  if (text.length > 0 && text.length <= 18 && text[0] >= '1' && text[0] <= '9')
  {
    i64   value    = 0;
    usize position = 0;

    while (position < text.length && text[position] >= '0' &&
           text[position] <= '9')
    {
      value = value * 10 + (text[position] - '0');
      position++;
    }

    if (position == text.length) return value;
  }

  usize i           = 0;
  let   is_negative = false;

  if (i < text.length && (text[i] == '+' || text[i] == '-')) {
    is_negative = text[i] == '-';
    i++;
  }

  let const detected =
      arithmetic_internal::detect_radix_prefix(text.substring(i));

  i += detected.prefix_length;

  let const digits = text.substring(i);
  let const digit_count =
      arithmetic_internal::count_leading_digits(digits, detected.radix);

  if (digit_count == 0 || i + digit_count != text.length) {
    return None;
  }

  let const magnitude = fold_leading_digits(digits, detected.radix);

  return static_cast<i64>(is_negative ? -magnitude : magnitude);
}

pure fn
arithmetic_add(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) + static_cast<u64>(rhs));
}

pure fn
arithmetic_subtract(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) - static_cast<u64>(rhs));
}

pure fn
arithmetic_multiply(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) * static_cast<u64>(rhs));
}

pure fn
arithmetic_power(i64 base, i64 exponent) wontthrow -> i64
{
  let result    = static_cast<u64>(1);
  let factor    = static_cast<u64>(base);
  let remaining = static_cast<u64>(exponent);

  while (remaining > 0) {
    if ((remaining & 1u) != 0) result *= factor;
    factor *= factor;
    remaining >>= 1;
  }

  return static_cast<i64>(result);
}

pure fn
arithmetic_divide(i64 lhs, i64 rhs) wontthrow -> i64
{
  if (lhs == INT64_MIN && rhs == -1) {
    return INT64_MIN;
  }

  return lhs / rhs;
}

pure fn
arithmetic_modulo(i64 lhs, i64 rhs) wontthrow -> i64
{
  if (lhs == INT64_MIN && rhs == -1) {
    return 0;
  }

  return lhs % rhs;
}

pure fn
arithmetic_shift_left(i64 lhs, i64 rhs) wontthrow -> i64
{
  let const count = static_cast<u64>(rhs) & 63u;

  return static_cast<i64>(static_cast<u64>(lhs) << count);
}

pure fn
arithmetic_shift_right(i64 lhs, i64 rhs) wontthrow -> i64
{
  let const count = static_cast<u64>(rhs) & 63u;
  let       value = static_cast<u64>(lhs) >> count;

  if (lhs < 0) {
    value |= ~(~static_cast<u64>(0) >> count);
  }

  return static_cast<i64>(value);
}

wontreturn cold fn
throw_division_by_zero() throws -> void
{
  throw ErrorWithDetails{"Division by zero",
                         "The right operand evaluated to 0"};
}

static fn
lex_arith_number(StringView from, i64 *out_value) throws -> usize;
static fn
lex_exact_arith_number(StringView       from,
                       ArithmeticValue *out_value,
                       BumpArena       &arena) throws -> usize;

static pure fn
is_leading_decimal_number(StringView source, usize position) wontthrow -> bool
{
  return position + 1 < source.length && source[position] == '.' &&
         lexer::is_number(source[position + 1]);
}

hot static fn
arith_apply_binop(char                   kind,
                  const ArithmeticValue &lhs,
                  const ArithmeticValue &rhs,
                  bool                   is_exact,
                  BumpArena             &arena,
                  Maybe<u32>             bc_scale = {}) throws -> ArithmeticValue;

static pure fn
is_plain_decimal_operand(StringView value) wontthrow -> bool
{
  if (value.is_empty() || value.length > 18) {
    return false;
  }
  if (value[0] == '0') return value.length == 1;

  for (usize index = 0; index < value.length; index++) {
    if (value[index] < '0' || value[index] > '9') {
      return false;
    }
  }

  return true;
}

/* the brackets of a subscript in expanded arithmetic text, located from the
   source before its expansion. */
struct expanded_subscript_span
{
  usize open_position{0};
  usize close_position{0};
};

/* the position after the quoted run, backquoted command, or $( or ${ group
   that starts at position in arithmetic source, position itself when none
   starts there, or None when that group is not closed. */
static fn
skip_arithmetic_source_group(StringView source, usize position) throws
    -> Maybe<usize>
{
  let const byte = source[position];

  if (byte == '\\') {
    if (position + 2 > source.length) return None;

    return position + 2;
  }

  if (byte == '\'' || byte == '"') {
    let const closing = lexer::skip_quoted_run(source, position);

    if (closing == position) return None;

    return closing + 1;
  }

  if (byte == '`') {
    for (usize i = position + 1; i < source.length; i++) {
      if (source[i] == '\\') {
        i++;
      } else if (source[i] == '`') {
        return i + 1;
      }
    }

    return None;
  }

  if (byte != '$' || position + 1 >= source.length) return position;
  if (source[position + 1] == '(') {
    return lexer::scan_balanced_shell_region(source, position + 2, ')');
  }

  if (source[position + 1] == '{') {
    return lexer::scan_parameter_expansion(source, position + 2, false, false,
                                           lexer::shell_quoting_rules{}, 0,
                                           nullptr);
  }

  return position;
}

/* the position of the ']' that closes the subscript whose '[' is at
   open_position in arithmetic source. */
static fn
find_source_subscript_close(StringView source,
                            usize      open_position) throws -> Maybe<usize>
{
  usize depth    = 1;
  usize position = open_position + 1;

  while (position < source.length) {
    let const after_group = skip_arithmetic_source_group(source, position);

    if (!after_group.has_value()) return None;
    if (*after_group != position) {
      position = *after_group;
      continue;
    }

    if (source[position] == '[') {
      depth++;
    } else if (source[position] == ']' && --depth == 0) {
      return position;
    }

    position++;
  }

  return None;
}

/* expands arithmetic source with each subscript expanded on its own, so a
   ']' produced by an expansion stays inside its subscript. spans receives the
   brackets of each subscript in the returned text. */
static fn
expand_arithmetic_source(
    EvalContext                        *context,
    StringView                          expression,
    const SourceLocation               *expression_base,
    ArrayList<expanded_subscript_span> &spans) throws -> String
{
  let const should_remove_quotes =
      context->runtime_state().bash_additions_enabled();
  /* single quotes protect the text of a subscript, as Bash does. */
  let const do_expand_piece = [&](usize start, usize end,
                                  bool is_subscript) throws -> String {
    let piece_base = Maybe<SourceLocation>{};

    if (expression_base != nullptr) {
      piece_base = *expression_base;
      piece_base->position += static_cast<u32>(start);
      if (start != 0 || end != expression.length) {
        piece_base->length = static_cast<u32>(end - start);
      }
    }

    return context->expand_modifier_word(
        expression.substring_of_length(start, end - start),
        should_remove_quotes, true,
        piece_base.has_value() ? &*piece_base : nullptr, false,
        is_subscript && should_remove_quotes
            ? parameter_word_quoting::Unquoted
            : parameter_word_quoting::HereDocument);
  };

  if (!expression.find_character('[').has_value()) {
    return do_expand_piece(0, expression.length, false);
  }

  let   expanded    = String{heap_allocator()};
  usize piece_start = 0;
  usize position    = 0;

  while (position < expression.length) {
    let const after_group = skip_arithmetic_source_group(expression, position);

    if (!after_group.has_value()) break;
    if (*after_group != position) {
      position = *after_group;
      continue;
    }

    let const byte = expression[position];
    let const is_name_start =
        lexer::is_variable_name_start(byte) &&
        (position == 0 || (!lexer::is_variable_name(expression[position - 1]) &&
                           expression[position - 1] != '$'));

    if (!is_name_start) {
      position++;
      continue;
    }

    usize name_end = position;

    while (name_end < expression.length &&
           lexer::is_variable_name(expression[name_end]))
    {
      name_end++;
    }

    position = name_end;
    if (name_end >= expression.length || expression[name_end] != '[') continue;

    let const close_position =
        find_source_subscript_close(expression, name_end);

    if (!close_position.has_value()) continue;

    /* at this point: expression[name_end] is the '[' after a name, and
       close_position holds the ']' that closes it in the source. */
    expanded.append(do_expand_piece(piece_start, name_end + 1, false).view());

    let const open_position = expanded.count() - 1;

    expanded.append(
        do_expand_piece(name_end + 1, *close_position, true).view());
    spans.push(expanded_subscript_span{open_position, expanded.count()});
    expanded.push(']');
    piece_start = *close_position + 1;
    position    = piece_start;
  }

  expanded.append(
      do_expand_piece(piece_start, expression.length, false).view());

  return expanded;
}

} /* namespace */

fn
has_expanded_arithmetic_subscript(StringView source) throws -> bool
{
  usize position = 0;

  while (position < source.length) {
    let const after_group = skip_arithmetic_source_group(source, position);

    if (!after_group.has_value()) return false;
    if (*after_group != position) {
      position = *after_group;
      continue;
    }

    if (source[position] != '[' || position == 0 ||
        !lexer::is_variable_name(source[position - 1]))
    {
      position++;
      continue;
    }

    let const close_position = find_source_subscript_close(source, position);

    if (!close_position.has_value()) return false;

    let const subscript = source.substring_of_length(
        position + 1, *close_position - position - 1);

    if (subscript.find_character('$').has_value() ||
        subscript.find_character('`').has_value() ||
        subscript.find_character('"').has_value() ||
        subscript.find_character('\'').has_value() ||
        subscript.find_character('\\').has_value())
    {
      return true;
    }

    position++;
  }

  return false;
}

namespace {

static constexpr usize MAX_PRIMARY_DEPTH = 128;
static constexpr usize MAX_CHAIN_DEPTH   = 4096;

enum class CalculatorFunction : u8
{
  Abs,
  Atan,
  Ceil,
  Cmp,
  Cos,
  Exp,
  Fact,
  Fib,
  Floor,
  Frac,
  Gcd,
  Int,
  IsEven,
  IsInt,
  IsOdd,
  Length,
  Lcm,
  Ln,
  Max,
  Min,
  Scale,
  Sgn,
  Sin,
  Sqrt,
};

static constexpr static_string_entry<CalculatorFunction>
    CALCULATOR_FUNCTION_ENTRIES[] = {
        {SSK("abs"),    CalculatorFunction::Abs   },
        {SSK("atan"),   CalculatorFunction::Atan  },
        {SSK("ceil"),   CalculatorFunction::Ceil  },
        {SSK("cmp"),    CalculatorFunction::Cmp   },
        {SSK("cos"),    CalculatorFunction::Cos   },
        {SSK("exp"),    CalculatorFunction::Exp   },
        {SSK("fact"),   CalculatorFunction::Fact  },
        {SSK("fib"),    CalculatorFunction::Fib   },
        {SSK("floor"),  CalculatorFunction::Floor },
        {SSK("frac"),   CalculatorFunction::Frac  },
        {SSK("gcd"),    CalculatorFunction::Gcd   },
        {SSK("int"),    CalculatorFunction::Int   },
        {SSK("iseven"), CalculatorFunction::IsEven},
        {SSK("isint"),  CalculatorFunction::IsInt },
        {SSK("isodd"),  CalculatorFunction::IsOdd },
        {SSK("length"), CalculatorFunction::Length},
        {SSK("lcm"),    CalculatorFunction::Lcm   },
        {SSK("ln"),     CalculatorFunction::Ln    },
        {SSK("max"),    CalculatorFunction::Max   },
        {SSK("min"),    CalculatorFunction::Min   },
        {SSK("scale"),  CalculatorFunction::Scale },
        {SSK("sgn"),    CalculatorFunction::Sgn   },
        {SSK("sin"),    CalculatorFunction::Sin   },
        {SSK("sqrt"),   CalculatorFunction::Sqrt  },
};
static constexpr StaticStringMap CALCULATOR_FUNCTIONS{
    CALCULATOR_FUNCTION_ENTRIES};

struct arith_lvalue_syntax
{
  StringView        name;
  Maybe<StringView> subscript;
  usize             name_position{0};
  u32               error{ARITH_NO_NODE};
  bool              is_expanded_subscript{false};
};

struct arith_tree_storage
{
  explicit arith_tree_storage(Allocator allocator)
      : nodes{allocator}, text{allocator}
  {}

  ArrayList<arith_node> nodes;
  String                text;

  fn
  add_node(const arith_node &node) throws -> u32
  {
    nodes.push(node);

    return static_cast<u32>(nodes.count() - 1);
  }

  fn
  add_text(StringView value) throws -> u32
  {
    let const offset = text.count();

    text.append(value);

    return static_cast<u32>(offset);
  }

  pure fn
  get_view(u32 root) const wontthrow -> arith_tree
  {
    arith_tree tree{};

    tree.nodes       = nodes.begin();
    tree.node_count  = static_cast<u32>(nodes.count());
    tree.text        = text.view().data;
    tree.text_length = static_cast<u32>(text.count());
    tree.root        = root;
    tree.exact_root  = root;

    return tree;
  }
};

class ArithmeticTreeBuilder
{
public:
  ArithmeticTreeBuilder(StringView          source_value,
                        bool                is_exact_value,
                        bool                is_calculator_value,
                        arith_tree_storage &storage_value,
                        BumpArena          &arena_value)
      : source{source_value}, is_exact{is_exact_value},
        is_calculator{is_calculator_value}, storage{storage_value},
        nodes{storage_value.nodes}, arena{arena_value}
  {}

  const ArrayList<expanded_subscript_span> *expanded_subscripts{nullptr};

  fn
  build() throws -> u32
  {
    return parse();
  }

  pure fn
  get_has_failed() const wontthrow -> bool
  {
    return has_failed;
  }

  pure fn
  get_failed_node() const wontthrow -> u32
  {
    return failed_node;
  }

private:
  StringView             source;
  usize                  pos{0};
  bool                   is_exact;
  bool                   is_calculator;
  arith_tree_storage    &storage;
  ArrayList<arith_node> &nodes;
  BumpArena             &arena;
  usize                  depth{0};
  usize                  binary_depth{0};
  usize                  chain_depth{0};
  u32                    failed_node{ARITH_NO_NODE};
  bool                   has_failed{false};

  fn
  add_node(const arith_node &node) throws -> u32
  {
    return storage.add_node(node);
  }

  fn
  add_text(StringView text) throws -> u32
  {
    return storage.add_text(text);
  }

  fn
  fail_at(usize      start_position,
          usize      length,
          StringView message,
          StringView note = {}) throws -> u32
  {
    arith_node node{};

    node.kind   = arith_node_kind::Error;
    node.start  = static_cast<u32>(start_position);
    node.length = static_cast<u32>(length);
    node.first  = add_text(message);
    node.second = static_cast<u32>(message.length);
    node.third  = add_text(note);
    node.end    = static_cast<u32>(note.length);
    has_failed  = true;
    failed_node = add_node(node);

    return failed_node;
  }

  fn
  fail(StringView message, StringView note = {}) throws -> u32
  {
    let const error_position = is_calculator         ? pos
                               : pos < source.length ? pos
                               : source.is_empty()   ? 0
                                                     : source.length - 1;

    return fail_at(error_position, 1, message, note);
  }

  fn
  fail_span(usize      start_position,
            usize      end_position,
            StringView message,
            StringView note = {}) throws -> u32
  {
    while (end_position > start_position && (source[end_position - 1] == ' ' ||
                                             source[end_position - 1] == '\t' ||
                                             source[end_position - 1] == '\n' ||
                                             source[end_position - 1] == '\r'))
    {
      end_position--;
    }

    return fail_at(start_position, end_position - start_position, message,
                   note);
  }

  fn
  mark_primary_error(u32 error, usize primary_position) wontthrow -> u32
  {
    nodes[error].flags |= arith_node::PRIMARY_ERROR_FLAG;
    nodes[error].middle = static_cast<u32>(primary_position);

    return error;
  }

  fn
  sequence(u32 first_index, u32 second_index) throws -> u32
  {
    arith_node node{};

    nodes[first_index].next = second_index;
    node.kind               = arith_node_kind::Comma;
    node.first              = first_index;

    return add_node(node);
  }

  fn
  add_unary(arith_node_kind kind, u32 operand) throws -> u32
  {
    arith_node node{};

    node.kind  = kind;
    node.first = operand;

    return add_node(node);
  }

  fn
  add_lvalue(const arith_lvalue_syntax &target) throws -> u32
  {
    arith_node node{};

    node.kind   = arith_node_kind::Lvalue;
    node.start  = static_cast<u32>(target.name_position);
    node.length = static_cast<u32>(target.name.length);

    if (target.subscript.has_value()) {
      node.flags |= arith_node::SUBSCRIPT_FLAG;
      node.middle = static_cast<u32>(target.subscript->data - source.data);
      node.end    = static_cast<u32>(target.subscript->length);
    }

    if (target.is_expanded_subscript)
      node.flags |= arith_node::EXPANDED_SUBSCRIPT_FLAG;

    return add_node(node);
  }

  fn
  add_step(arith_node_kind            kind,
           const arith_lvalue_syntax &target,
           i64                        delta,
           usize                      primary_position) throws -> u32
  {
    arith_node node{};

    node.first  = add_lvalue(target);
    node.kind   = kind;
    node.value  = delta;
    node.start  = static_cast<u32>(pos);
    node.middle = static_cast<u32>(primary_position);

    return add_node(node);
  }

  fn
  enter_chain() throws -> u32
  {
    chain_depth++;
    if (chain_depth > MAX_CHAIN_DEPTH) {
      chain_depth = 0;

      return fail("Expression nested too deeply",
                  "Split a long chain of unary operators or assignments");
    }

    return ARITH_NO_NODE;
  }

  fn
  enter_binary_chain() throws -> u32
  {
    binary_depth++;
    if (binary_depth > MAX_OPERATOR_CHAIN_DEPTH) {
      return fail("Expression nested too deeply", OPERATOR_CHAIN_HINT);
    }

    return ARITH_NO_NODE;
  }

  fn
  skip_spaces() wontthrow -> void
  {
    arithmetic_internal::skip_spaces(source, pos);
  }

  fn
  starts_with(StringView op) wontthrow -> bool
  {
    return arithmetic_internal::starts_with(source, pos, op);
  }

  fn
  consume(StringView op) wontthrow -> bool
  {
    return arithmetic_internal::consume(source, pos, op);
  }

  fn
  read_lvalue_name() wontthrow -> StringView
  {
    skip_spaces();
    if (pos >= source.length || !lexer::is_variable_name_start(source[pos])) {
      return StringView{};
    }

    let const name_start = pos;

    while (pos < source.length && lexer::is_variable_name(source[pos])) {
      pos++;
    }

    return source.substring_of_length(name_start, pos - name_start);
  }

  fn
  read_optional_subscript(arith_lvalue_syntax &target) throws
      -> Maybe<StringView>
  {
    if (pos >= source.length || source[pos] != '[') {
      return None;
    }

    if (expanded_subscripts != nullptr) {
      for (let const &span : *expanded_subscripts) {
        if (span.open_position != pos) continue;

        let const subscript =
            source.substring_of_length(pos + 1, span.close_position - pos - 1);

        pos                          = span.close_position + 1;
        target.is_expanded_subscript = true;

        return subscript;
      }
    }

    pos++;

    let const inner_start   = pos;
    usize     bracket_depth = 1;

    while (pos < source.length && bracket_depth > 0) {
      if (source[pos] == '[') {
        bracket_depth++;
      } else if (source[pos] == ']' && --bracket_depth == 0) {
        break;
      } else if (source[pos] == '\'' || source[pos] == '"' ||
                 source[pos] == '\\')
      {
        pos = lexer::skip_quoted_run(source, pos);
      }

      pos++;
    }

    if (bracket_depth != 0) {
      target.error = fail("Expected ']' after an array subscript",
                          "The subscript '[' was never closed");

      return None;
    }

    let const subscript =
        source.substring_of_length(inner_start, pos - inner_start);

    pos++;

    return subscript;
  }

  fn
  read_lvalue() throws -> arith_lvalue_syntax
  {
    skip_spaces();

    arith_lvalue_syntax target{};

    target.name_position = pos;
    target.name          = read_lvalue_name();
    if (target.name.is_empty()) return target;

    target.subscript = read_optional_subscript(target);

    return target;
  }

  fn
  parse() throws -> u32
  {
    skip_spaces();
    if (pos == source.length) return ARITH_NO_NODE;

    let const result = parse_comma();

    if (has_failed) return result;

    skip_spaces();
    if (pos != source.length) {
      let const error = fail_at(pos, is_calculator ? source.length - pos : 1,
                                "Unexpected '" + source.substring(pos) +
                                    "' after the expression",
                                "An operator is missing between two values");

      return sequence(result, error);
    }

    return result;
  }

  fn
  parse_comma() throws -> u32
  {
    let const first = parse_assignment();

    if (has_failed || !consume(",")) return first;

    arith_node node{};

    node.kind  = arith_node_kind::Comma;
    node.first = first;

    let const comma = add_node(node);
    let       last  = first;

    loop {
      let const next = parse_assignment();

      nodes[last].next = next;
      last             = next;
      if (has_failed || !consume(",")) return comma;
    }
  }

  fn
  parse_assignment() throws -> u32
  {
    if (let const error = enter_chain(); error != ARITH_NO_NODE) return error;
    defer { chain_depth = chain_depth > 0 ? chain_depth - 1 : 0; };

    let const save   = pos;
    let const target = read_lvalue();

    if (has_failed) return target.error;

    if (!target.name.is_empty()) {
      struct compound_operator
      {
        StringView token;
        char       kind;
      };
      static const compound_operator compound_operators[] = {
          {"<<=", 'L'},
          {">>=", 'R'},
          {"**=", 'P'},
          {"+=",  '+'},
          {"-=",  '-'},
          {"*=",  '*'},
          {"/=",  '/'},
          {"%=",  '%'},
          {"&=",  '&'},
          {"|=",  '|'},
          {"^=",  '^'},
      };

      skip_spaces();

      let const next_byte = pos < source.length ? source[pos] : '\0';

      if (next_byte == '<' || next_byte == '>' || next_byte == '+' ||
          next_byte == '-' || next_byte == '*' || next_byte == '/' ||
          next_byte == '%' || next_byte == '&' || next_byte == '|' ||
          next_byte == '^')
      {
        for (let const &[ op, kind ] : compound_operators) {
          if (consume(op)) {
            skip_spaces();

            arith_node node{};

            node.kind   = arith_node_kind::CompoundAssign;
            node.op     = kind;
            node.first  = add_lvalue(target);
            node.middle = static_cast<u32>(pos);
            node.second = parse_assignment();
            node.end    = static_cast<u32>(pos);

            return add_node(node);
          }
        }
      }

      if (starts_with("=") && !starts_with("==")) {
        consume("=");

        arith_node node{};

        node.kind   = arith_node_kind::Assign;
        node.first  = add_lvalue(target);
        node.second = parse_assignment();

        return add_node(node);
      }

      pos = save;
    }

    return parse_ternary();
  }

  fn
  add_ternary(u32 condition, u32 if_true, u32 if_false) throws -> u32
  {
    arith_node node{};

    node.kind   = arith_node_kind::Ternary;
    node.first  = condition;
    node.second = if_true;
    node.third  = if_false;

    return add_node(node);
  }

  fn
  parse_ternary() throws -> u32
  {
    defer { binary_depth--; };
    if (let const error = enter_binary_chain(); error != ARITH_NO_NODE) {
      return error;
    }

    let const condition = parse_binary(1);

    if (has_failed || !consume("?")) return condition;

    let const if_true = parse_comma();

    if (has_failed) return add_ternary(condition, if_true, ARITH_NO_NODE);

    if (!consume(":")) {
      let const error =
          fail("Expected ':' in a conditional", "A '?' needs a matching ':'");

      return add_ternary(condition, if_true, error);
    }

    let const if_false = parse_ternary();

    return add_ternary(condition, if_true, if_false);
  }

  struct BinaryOperator
  {
    char kind;
    u8   precedence;
    u8   length;
  };

  fn
  peek_binary_operator() wontthrow -> BinaryOperator
  {
    skip_spaces();
    if (pos >= source.length) return {0, 0, 0};

    let const first_byte  = source[pos];
    let const second_byte = pos + 1 < source.length ? source[pos + 1] : '\0';
    let const third_byte  = pos + 2 < source.length ? source[pos + 2] : '\0';

    switch (first_byte) {
    case '*':
      if (second_byte == '*') return {'P', 11, 2};

      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'*', 10, 1};
    case '/':
      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'/', 10, 1};
    case '%':
      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'%', 10, 1};
    case '+':
      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'+', 9, 1};
    case '-':
      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'-', 9, 1};
    case '<':
      if (second_byte == '<')
        return third_byte == '=' ? BinaryOperator{0, 0, 0}
                                 : BinaryOperator{'L', 8, 2};
      if (second_byte == '=') return {'l', 7, 2};

      return {'<', 7, 1};
    case '>':
      if (second_byte == '>')
        return third_byte == '=' ? BinaryOperator{0, 0, 0}
                                 : BinaryOperator{'R', 8, 2};
      if (second_byte == '=') return {'g', 7, 2};

      return {'>', 7, 1};
    case '=':
      return second_byte == '=' ? BinaryOperator{'e', 6, 2}
                                : BinaryOperator{0, 0, 0};
    case '!':
      return second_byte == '=' ? BinaryOperator{'n', 6, 2}
                                : BinaryOperator{0, 0, 0};
    case '&':
      if (second_byte == '&') return {'A', 2, 2};

      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'&', 5, 1};
    case '^':
      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'^', 4, 1};
    case '|':
      if (second_byte == '|') return {'O', 1, 2};

      return second_byte == '=' ? BinaryOperator{0, 0, 0}
                                : BinaryOperator{'|', 3, 1};
    default: return {0, 0, 0};
    }
  }

  fn
  parse_binary(u8 min_precedence) throws -> u32
  {
    defer { binary_depth--; };
    if (let const error = enter_binary_chain(); error != ARITH_NO_NODE) {
      return error;
    }

    skip_spaces();

    let const lhs_start = pos;
    let const lhs       = parse_unary();

    if (has_failed) return lhs;

    let chain     = ARITH_NO_NODE;
    let last_step = ARITH_NO_NODE;

    loop {
      let const op = peek_binary_operator();

      if (op.precedence < min_precedence) {
        return chain == ARITH_NO_NODE ? lhs : chain;
      }

      pos += op.length;
      skip_spaces();

      let const is_logical = op.kind == 'A' || op.kind == 'O';

      arith_node step{};

      step.kind   = arith_node_kind::ChainStep;
      step.op     = op.kind;
      step.middle = static_cast<u32>(pos);
      step.first  = parse_binary(is_logical || op.kind != 'P'
                                     ? static_cast<u8>(op.precedence + 1)
                                     : op.precedence);
      step.end    = static_cast<u32>(pos);

      let const step_index = add_node(step);

      if (chain == ARITH_NO_NODE) {
        arith_node node{};

        node.kind   = arith_node_kind::Chain;
        node.first  = lhs;
        node.second = step_index;
        node.start  = static_cast<u32>(lhs_start);
        chain       = add_node(node);
      } else {
        nodes[last_step].next = step_index;
      }

      last_step = step_index;
      if (has_failed) return chain;
    }
  }

  fn
  prefix_step(i64 delta, usize operator_position) throws -> u32
  {
    let const target = read_lvalue();

    if (has_failed) return target.error;
    if (target.name.is_empty() && pos < source.length) {
      return parse_unary();
    }

    if (target.name.is_empty()) {
      return fail_span(operator_position, operator_position + 2,
                       "Expected a variable after '++' or '--'",
                       "'++' and '--' apply only to a variable");
    }

    return add_step(arith_node_kind::PrefixStep, target, delta, pos);
  }

  fn
  parse_unary() throws -> u32
  {
    if (let const error = enter_chain(); error != ARITH_NO_NODE) return error;
    defer { chain_depth = chain_depth > 0 ? chain_depth - 1 : 0; };

    skip_spaces();

    let const first = pos < source.length ? source[pos] : '\0';

    if (first == '+') {
      let const operator_position = pos;

      if (consume("++")) return prefix_step(1, operator_position);
      pos++;

      return parse_unary();
    }

    if (first == '-') {
      let const operator_position = pos;

      if (consume("--")) return prefix_step(-1, operator_position);
      pos++;

      return add_unary(arith_node_kind::Negate, parse_unary());
    }

    if (first == '!') {
      pos++;

      return add_unary(arith_node_kind::Not, parse_unary());
    }

    if (first == '~') {
      pos++;

      return add_unary(arith_node_kind::BitNot, parse_unary());
    }

    return parse_primary();
  }

  fn
  calculator_call(const arith_lvalue_syntax &target,
                  usize                      primary_position) throws -> u32
  {
    let const name     = target.name;
    let const function = CALCULATOR_FUNCTIONS.find(name);

    if (!function.has_value()) {
      return mark_primary_error(
          fail_span(target.name_position, target.name_position + name.length,
                    "Unknown calculator function '" + name + "'",
                    "Check the spelling, or use a function that the "
                    "calculator supports"),
          primary_position);
    }

    arith_node node{};

    node.kind   = arith_node_kind::Call;
    node.start  = static_cast<u32>(target.name_position);
    node.length = static_cast<u32>(name.length);
    node.middle = static_cast<u32>(primary_position);
    node.value  = static_cast<i64>(*function);

    let const call = add_node(node);

    if (consume(")")) {
      nodes[call].end = static_cast<u32>(pos);

      return call;
    }

    let last = ARITH_NO_NODE;

    let const do_link = [&](u32 index) wontthrow {
      if (last == ARITH_NO_NODE) {
        nodes[call].first = index;
      } else {
        nodes[last].next = index;
      }

      last = index;
    };

    loop {
      skip_spaces();

      arith_node argument{};

      argument.kind  = arith_node_kind::Argument;
      argument.start = static_cast<u32>(pos);
      argument.first = parse_assignment();
      argument.end   = static_cast<u32>(pos);
      do_link(add_node(argument));
      if (has_failed) return call;

      if (consume(")")) {
        nodes[call].end = static_cast<u32>(pos);

        return call;
      }

      if (!consume(",")) {
        do_link(fail("Expected ',' or ')' after an argument",
                     "The call to '" + name + "' is unfinished"));

        return call;
      }
    }
  }

  pure fn
  get_radix_prefix_length(usize number_start) const wontthrow -> usize
  {
    return arithmetic_internal::count_leading_digits(
               source.substring(number_start), 10) +
           1;
  }

  fn
  reject_invalid_digit(usize number_start) throws -> u32
  {
    if (pos >= source.length || !is_number_continuation(source[pos])) {
      return ARITH_NO_NODE;
    }

    usize end = pos;

    while (end < source.length && is_number_continuation(source[end])) {
      end++;
    }

    let const token =
        source.substring_of_length(number_start, end - number_start);

    usize base = arithmetic_internal::detect_radix_prefix(token).radix;

    if (let const hash = token.find_character('#'); hash.has_value()) {
      base = static_cast<usize>(
          parse_arithmetic_operand(token.substring_of_length(0, *hash)));
    }

    return fail_span(
        number_start, end,
        "Invalid digit '" + source.substring_of_length(pos, 1) +
            "' in the number '" +
            source.substring_of_length(number_start, end - number_start) + "'",
        "The digit is not valid in base " +
            String::from(base, bump_allocator(arena)));
  }

  fn
  parse_number(usize primary_position) throws -> u32
  {
    let const number_start = pos;

    arith_node node{};

    node.kind   = arith_node_kind::Number;
    node.start  = static_cast<u32>(number_start);
    node.middle = static_cast<u32>(primary_position);

    try {
      if (is_exact) {
        let value = ArithmeticValue{};

        pos += lex_exact_arith_number(source.substring(pos), &value, arena);

        if (is_plain_decimal_operand(
                source.substring_of_length(number_start, pos - number_start)))
        {
          node.flags |= arith_node::PLAIN_NUMBER_FLAG;
          node.value = value.wrapped_i64();
        }
      } else {
        pos += lex_arith_number(source.substring(pos), &node.value);

        if (is_plain_decimal_operand(
                source.substring_of_length(number_start, pos - number_start)))
        {
          node.flags |= arith_node::PLAIN_NUMBER_FLAG;
        }
      }
    } catch (const ErrorWithLocation &) {
      throw;
    } catch (const ErrorBase &error) {
      return mark_primary_error(
          fail_at(number_start, get_radix_prefix_length(number_start),
                  error.message().view(), error.detail_message()),
          primary_position);
    }

    if (let const error = reject_invalid_digit(number_start);
        error != ARITH_NO_NODE)
    {
      return mark_primary_error(error, primary_position);
    }

    node.length = static_cast<u32>(pos - number_start);

    return add_node(node);
  }

  fn
  parse_primary() throws -> u32
  {
    depth++;
    defer { depth--; };

    let const primary_position = pos;

    if (depth > MAX_PRIMARY_DEPTH) {
      arith_node node{};

      node.kind   = arith_node_kind::DepthLimit;
      node.middle = static_cast<u32>(primary_position);
      has_failed  = true;
      failed_node = add_node(node);

      return failed_node;
    }

    skip_spaces();
    if (consume("(")) {
      arith_node node{};

      node.kind   = arith_node_kind::Group;
      node.middle = static_cast<u32>(primary_position);
      node.first  = parse_comma();

      if (!has_failed && !consume(")")) {
        let const error = fail("Expected ')'", "An opening '(' is unmatched");

        node.first = sequence(node.first, error);
      }

      return add_node(node);
    }

    let const has_leading_decimal_point =
        is_exact && is_leading_decimal_number(source, pos);

    if (pos < source.length &&
        (lexer::is_number(source[pos]) || has_leading_decimal_point))
    {
      return parse_number(primary_position);
    }

    if (pos < source.length && lexer::is_variable_name_start(source[pos])) {
      let const target = read_lvalue();

      if (has_failed) return mark_primary_error(target.error, primary_position);

      if (is_calculator && !target.subscript.has_value() && consume("(")) {
        return calculator_call(target, primary_position);
      }

      if (consume("++")) {
        return add_step(arith_node_kind::PostfixStep, target, 1,
                        primary_position);
      }

      if (consume("--")) {
        return add_step(arith_node_kind::PostfixStep, target, -1,
                        primary_position);
      }

      return add_step(arith_node_kind::Read, target, 0, primary_position);
    }

    if (pos >= source.length) {
      return mark_primary_error(
          fail("Unfinished expression", "An operand is missing"),
          primary_position);
    }

    return mark_primary_error(
        fail("Unexpected character in the arithmetic expression",
             "This is not a valid operator or operand"),
        primary_position);
  }
};

struct arith_text_options
{
  usize                                     depth{0};
  Maybe<u32>                                bc_scale{};
  const SourceLocation                     *precise_base{nullptr};
  const ArrayList<expanded_subscript_span> *expanded_subscripts{nullptr};
  bool                                      is_skipping{false};
  bool                                      should_error_unset{false};
  bool                                      is_variable_value{false};
};

static fn
evaluate_text_tree(EvalContext              *context,
                   StringView                source,
                   bool                      is_exact,
                   BumpArena                &arena,
                   const arith_text_options &options) throws
    -> ArithmeticValue;

class ArithmeticTreeEvaluator
{
public:
  ArithmeticTreeEvaluator(EvalContext      *context_value,
                          StringView        source_value,
                          const arith_tree &tree_value,
                          bool              is_exact_value,
                          BumpArena        &arena_value,
                          usize             depth_value    = 0,
                          bool              is_skipping    = false,
                          Maybe<u32>        bc_scale_value = {})
      : context{context_value}, source{source_value}, tree{tree_value},
        is_exact{is_exact_value}, depth{depth_value},
        m_is_skipping{is_skipping}, bc_scale{bc_scale_value}, arena{arena_value}
  {}

  EvalContext      *context;
  StringView        source;
  const arith_tree &tree;
  usize             pos{0};
  bool              is_exact{false};

  usize depth{0};
  bool  is_variable_value{false};

  bool       m_is_skipping{false};
  bool       should_error_unset{false};
  Maybe<u32> bc_scale{};

  Maybe<SourceLocation> precise_base{};
  BumpArena            &arena;
  u32                   cached_pi_scale{0};
  ArithmeticValue       cached_pi{};
  usize                 calculator_function_position{0};

  wontreturn cold fn
  fail_at(usize      start_position,
          usize      length,
          StringView message,
          StringView note = {}) const throws -> void
  {
    if (precise_base.has_value()) {
      const SourceLocation location{precise_base->position + start_position,
                                    length, precise_base->source_name_index};
      if (note.is_empty()) throw ErrorWithLocation{location, message};
      throw ErrorWithLocationAndDetails{location, message, note};
    }

    if (note.is_empty()) throw Error{message};
    throw ErrorWithDetails{message, note};
  }

  wontreturn cold fn
  fail(StringView message, StringView note = {}) throws -> void
  {
    let const error_position = should_error_unset    ? pos
                               : pos < source.length ? pos
                               : source.is_empty()   ? 0
                                                     : source.length - 1;

    fail_at(error_position, 1, message, note);
  }

  wontreturn cold fn
  fail_span(usize      start_position,
            usize      end_position,
            StringView message,
            StringView note = {}) throws -> void
  {
    while (end_position > start_position && (source[end_position - 1] == ' ' ||
                                             source[end_position - 1] == '\t' ||
                                             source[end_position - 1] == '\n' ||
                                             source[end_position - 1] == '\r'))
    {
      end_position--;
    }

    fail_at(start_position, end_position - start_position, message, note);
  }

  fn
  read_variable_value(StringView name, usize name_position) throws
      -> ArithmeticValue
  {
    if (m_is_skipping) return ArithmeticValue{};

    ASSERT(context != nullptr);
    if (let const stored = context->variable_store().find_plain_scalar(name);
        stored.has_value())
    {
      return evaluate_operand_value(stored->view());
    }

    let const value = context->get_variable_value(name);

    if (!value.has_value()) {
      if (should_error_unset)
        fail_at(name_position, name.length,
                "The variable '" + name + "' is not set");
      context->report_unset_reference(name);

      return ArithmeticValue{};
    }

    /* at this point: m_is_skipping is false, and value holds the variable's
       value. */
    return evaluate_operand_value(value->view());
  }

  fn
  evaluate_operand_value(StringView value) throws -> ArithmeticValue
  {
    if (value.is_empty()) return ArithmeticValue{};
    if (!is_exact) {
      if (let const literal = try_parse_single_integer_literal(value);
          literal.has_value())
        return ArithmeticValue{literal.value()};
    } else if (is_plain_decimal_operand(value)) {
      return ArithmeticValue::parse(value, 10, arena);
    }

    if (depth >= MAX_PRIMARY_DEPTH)
      fail("The variable value recurses too deeply",
           "A variable value refers back to itself");

    let const stable_value = String{bump_allocator(arena), value};
    let       options      = arith_text_options{};

    options.depth              = depth + 1;
    options.is_skipping        = m_is_skipping;
    options.bc_scale           = bc_scale;
    options.should_error_unset = should_error_unset;
    options.is_variable_value  = true;

    return evaluate_text_tree(context, stable_value.view(), is_exact, arena,
                              options);
  }

  fn
  write_variable(StringView name, const ArithmeticValue &value) const throws -> void
  {
    if (m_is_skipping) return;
    ASSERT(context != nullptr);
    if (context->variable_store().attributes().is_nameref(name) &&
        context->is_circular_nameref(name))
    {
      context->warn_circular_nameref(name);

      return;
    }

    /* at this point: m_is_skipping is false, and the variable is not a
       circular nameref. */
    let const text = value.to_string(context->scratch_allocator());

    context->set_shell_variable(name, text.view());
  }

  struct Lvalue
  {
    StringView        name;
    Maybe<StringView> subscript;
    usize             name_position;
    char              resolved_subscript[24]{};
    bool              is_expanded_subscript{false};
    bool              was_base_checked{false};
  };

  /* an element read of a variable without any value reports the variable,
     before its subscript is evaluated. */
  fn
  check_array_base_set(const Lvalue &target) const throws -> void
  {
    let const array_name =
        resolve_subscripted_array_name(*context, target.name);

    if (!context->has_array_base_value(array_name.view()))
      context->report_unset_reference(target.name);
  }

  /* the associative array that a subscript expanded with the expression
     addresses by its literal key, reached directly or through a nameref. */
  fn
  find_literal_key_array(const Lvalue &target) const throws
      -> Maybe<String>
  {
    if (!target.is_expanded_subscript || *target.subscript == "@" ||
        *target.subscript == "*")
    {
      return None;
    }

    let array_name = resolve_subscripted_array_name(*context, target.name);

    if (!context->is_associative_array(array_name.view())) return None;

    return array_name;
  }

  fn
  get_associative_array_of(const Lvalue &target) const throws -> String
  {
    try {
      return associative_array_for_expanded_subscript(*context, target.name,
                                                      *target.subscript);
    } catch (const ErrorWithLocation &) {
      throw;
    } catch (const ErrorBase &error) {
      fail_at(target.name_position, target.name.length, error.message().view(),
              error.detail_message());
    }
  }

  /* evaluates an indexed subscript once so that a read and a write of the
     same lvalue use one index. */
  fn
  resolve_lvalue_subscript(Lvalue &target) throws -> void
  {
    if (m_is_skipping || !target.subscript.has_value()) {
      return;
    }

    ASSERT(context != nullptr);

    check_array_base_set(target);
    target.was_base_checked = true;

    if (*target.subscript == "@" || *target.subscript == "*") {
      return;
    }
    if (utils::has_command_substitution(*target.subscript)) return;
    if (context->is_associative_array(target.name)) return;
    if (context->variable_store().attributes().is_nameref(target.name)) return;

    /* at this point: m_is_skipping is false, the subscript is set and is
       neither "@" nor "*", it has no command substitution, and target.name
       is neither an associative array nor a nameref. */
    let const data_scope = DataExpansionScope{*context};
    let const index =
        context->evaluate_arithmetic(*target.subscript, nullptr, true);
    let const text = utils::int_to_text_into(index, target.resolved_subscript,
                                             sizeof(target.resolved_subscript));

    target.subscript = text;
  }

  fn
  get_lvalue(u32 index) const wontthrow -> Lvalue
  {
    let const &node = tree.nodes[index];
    let target = Lvalue{source.substring_of_length(node.start, node.length),
                        None, node.start};

    if ((node.flags & arith_node::SUBSCRIPT_FLAG) != 0) {
      target.subscript = source.substring_of_length(node.middle, node.end);
    }

    target.is_expanded_subscript =
        (node.flags & arith_node::EXPANDED_SUBSCRIPT_FLAG) != 0;

    return target;
  }

  fn
  read_lvalue_value(const Lvalue &target) throws -> ArithmeticValue
  {
    if (m_is_skipping) return ArithmeticValue{};

    if (target.subscript.has_value()) {
      ASSERT(context != nullptr);

      if (!target.was_base_checked) check_array_base_set(target);

      if (let const literal_array = find_literal_key_array(target);
          literal_array.has_value())
      {
        let const value = context->lookup_associative_element(
            literal_array->view(), *target.subscript);

        return evaluate_operand_value(value.has_value() ? value->view()
                                                        : StringView{});
      }

      if (utils::has_command_substitution(*target.subscript)) {
        let const array_name = get_associative_array_of(target);
        let const value      = context->lookup_associative_element(
            array_name.view(), *target.subscript);

        return evaluate_operand_value(value.has_value() ? value->view()
                                                        : StringView{});
      }

      /* at this point: m_is_skipping is false, the subscript is set, and it
         has no command substitution. */
      let const data_scope = DataExpansionScope{*context};
      let const value      = context->read_array_element_arithmetic_text(
          target.name, *target.subscript);

      return evaluate_operand_value(value.view());
    }

    return read_variable_value(target.name, target.name_position);
  }

  fn
  write_lvalue(const Lvalue &target, const ArithmeticValue &value) const throws -> void
  {
    if (m_is_skipping) return;
    if (target.subscript.has_value()) {
      ASSERT(context != nullptr);

      let const text = value.to_string(context->scratch_allocator());

      if (let const literal_array = find_literal_key_array(target);
          literal_array.has_value())
      {
        context->set_associative_element(literal_array->view(),
                                         *target.subscript, text.view());

        return;
      }

      if (utils::has_command_substitution(*target.subscript)) {
        let const array_name = get_associative_array_of(target);

        context->set_associative_element(array_name.view(), *target.subscript,
                                         text.view());

        return;
      }

      let const data_scope = DataExpansionScope{*context};

      context->assign_array_element(target.name, *target.subscript, text.view(),
                                    assignment_update_mode::Replace);

      return;
    }

    write_variable(target.name, value);
  }

  fn
  evaluate_step(const arith_node &node, bool is_prefix) throws
      -> ArithmeticValue
  {
    if (tree.is_simple) return evaluate_simple_step(node, is_prefix);

    let target = get_lvalue(node.first);

    resolve_lvalue_subscript(target);
    pos = node.start;

    let const original = read_lvalue_value(target);
    let const updated  = arith_apply_binop(
        '+', original, ArithmeticValue{node.value}, is_exact, arena, bc_scale);

    write_lvalue(target, updated);

    return is_prefix ? updated : original;
  }

  fn
  fail_on_unexpected_tail() throws -> void
  {
    let tail_position = pos;

    while (tail_position < source.length &&
           (source[tail_position] == ' ' || source[tail_position] == '\t' ||
            source[tail_position] == '\n'))
      tail_position++;

    if (tail_position == source.length) return;

    static constexpr StringView CONTINUATIONS = "+-*/%<>=!&|^,?:)]";

    if (CONTINUATIONS.find_character(source[tail_position]).has_value()) return;

    fail_at(tail_position,
            should_error_unset ? source.length - tail_position : 1,
            "Unexpected '" + source.substring(tail_position) +
                "' after the expression",
            "An operator is missing between two values");
  }

  fn
  run(u32 root) throws -> ArithmeticValue
  {
    if (root == ARITH_NO_NODE) return ArithmeticValue{};

    return evaluate(root);
  }

  fn
  evaluate_skipped(u32 index, bool should_skip) throws
      -> ArithmeticValue
  {
    let const was_skipping = m_is_skipping;

    m_is_skipping = was_skipping || should_skip;
    defer { m_is_skipping = was_skipping; };

    return evaluate(index);
  }

  wontreturn cold fn
  throw_error_node(const arith_node &node) throws -> void
  {
    fail_at(node.start, node.length, tree.get_text(node.first, node.second),
            tree.get_text(node.third, node.end));
  }

  wontreturn cold fn
  throw_failure(u32 index) throws -> void
  {
    let const &node = tree.nodes[index];

    if (node.kind == arith_node_kind::DepthLimit) {
      fail_nested_too_deeply(node.middle);
    }

    throw_error_node(node);
  }

  wontreturn cold fn
  fail_nested_too_deeply(u32 primary_position) throws -> void
  {
    pos = primary_position;
    fail("Expression nested too deeply",
         is_variable_value
             ? StringView{"A variable may reference itself, like `x=x`"}
             : StringView{});
  }

  fn
  read_simple_variable(StringView name) throws -> ArithmeticValue
  {
    ASSERT(context != nullptr);
    if (let const stored = context->variable_store().find_plain_scalar(name);
        stored.has_value())
    {
      return evaluate_named_value(stored->view());
    }

    let const value = context->get_variable_value(name);

    if (!value.has_value()) {
      context->report_unset_reference(name);

      return ArithmeticValue{};
    }

    return evaluate_named_value(value->view());
  }

  fn
  evaluate_named_value(StringView value) throws -> ArithmeticValue
  {
    if (value.is_empty()) return ArithmeticValue{};
    if (!is_exact) {
      if (let const literal = try_parse_single_integer_literal(value);
          literal.has_value())
        return ArithmeticValue{literal.value()};
    } else if (is_plain_decimal_operand(value)) {
      return ArithmeticValue::parse(value, 10, arena);
    }

    let const stable_value = String{bump_allocator(arena), value};
    let       options      = arith_text_options{};

    options.is_variable_value = true;

    return evaluate_text_tree(context, stable_value.view(), is_exact, arena,
                              options);
  }

  fn
  evaluate_simple_step(const arith_node &node, bool is_prefix) throws
      -> ArithmeticValue
  {
    let const &target = tree.nodes[node.first];
    let const  name   = source.substring_of_length(target.start, target.length);
    let const  original = read_simple_variable(name);
    let const  updated  = arith_apply_binop(
        '+', original, ArithmeticValue{node.value}, is_exact, arena);

    if (context->variable_store().attributes().is_nameref(name) &&
        context->is_circular_nameref(name))
    {
      context->warn_circular_nameref(name);

      return is_prefix ? updated : original;
    }

    let const text = updated.to_string(context->scratch_allocator());

    context->set_shell_variable(name, text.view());

    return is_prefix ? updated : original;
  }

  fn
  evaluate_number(const arith_node &node) throws -> ArithmeticValue
  {
    if (!is_exact || (node.flags & arith_node::PLAIN_NUMBER_FLAG) != 0) {
      return ArithmeticValue{node.value};
    }

    let value = ArithmeticValue{};

    try {
      lex_exact_arith_number(source.substring(node.start), &value, arena);
    } catch (const ErrorWithLocation &) {
      throw;
    } catch (const ErrorBase &error) {
      fail_at(node.start,
              arithmetic_internal::count_leading_digits(
                  source.substring(node.start), 10) +
                  1,
              error.message().view(), error.detail_message());
    }

    return value;
  }

  fn
  evaluate_call(const arith_node &node) throws -> ArithmeticValue
  {
    let arguments = ArrayList<CalculatorArgument>{bump_allocator(arena)};

    for (let index = node.first; index != ARITH_NO_NODE;
         index     = tree.nodes[index].next)
    {
      let const &argument = tree.nodes[index];

      if (argument.kind == arith_node_kind::Error) throw_error_node(argument);
      arguments.push(CalculatorArgument{evaluate(argument.first),
                                        argument.start, argument.end});
    }

    pos = node.end;

    return apply_calculator_function(
        source.substring_of_length(node.start, node.length), node.start,
        static_cast<CalculatorFunction>(node.value), arguments);
  }

  noinline fn
  evaluate_primary(const arith_node &node) throws -> ArithmeticValue
  {
    depth++;
    defer { depth--; };

    if (depth > MAX_PRIMARY_DEPTH && !tree.is_simple) {
      fail_nested_too_deeply(node.middle);
    }

    switch (node.kind) {
    case arith_node_kind::Number: return evaluate_number(node);
    case arith_node_kind::Read:   {
      if (tree.is_simple) {
        let const &target = tree.nodes[node.first];

        return read_simple_variable(
            source.substring_of_length(target.start, target.length));
      }

      let const target = get_lvalue(node.first);

      pos = node.start;

      return read_lvalue_value(target);
    }
    case arith_node_kind::PostfixStep: return evaluate_step(node, false);
    case arith_node_kind::Group:       return evaluate(node.first);
    case arith_node_kind::Call:        return evaluate_call(node);
    case arith_node_kind::Error:       throw_error_node(node);
    default:                           break;
    }

    fail_nested_too_deeply(node.middle);
  }

  fn
  apply_chain_step(const arith_node &chain,
                   const arith_node &step,
                   ArithmeticValue   lhs,
                   ArithmeticValue   rhs) throws -> ArithmeticValue
  {
    if (tree.is_simple) {
      try {
        return arith_apply_binop(step.op, lhs, rhs, is_exact, arena);
      } catch (const ErrorWithLocation &) {
        throw;
      } catch (const ErrorBase &error) {
        if ((step.flags & arith_node::TAIL_FLAG) != 0) {
          fail_at(step.middle, step.end,
                  "Unexpected '" +
                      source.substring_of_length(step.middle, step.end) +
                      "' after the expression",
                  "An operator is missing between two values");
        }

        if (!precise_base.has_value()) throw;
        fail_at(step.start, step.length, error.message().view(),
                error.detail_message());
      }
    }

    try {
      if (step.op == 'P' && rhs.is_negative() && !bc_scale.has_value()) {
        fail_span(step.middle, step.end, "Exponent less than 0",
                  "'**' requires a non-negative exponent");
      }

      return arith_apply_binop(step.op, lhs, rhs, is_exact, arena, bc_scale);
    } catch (const ErrorWithLocation &) {
      throw;
    } catch (const ErrorBase &error) {
      pos = step.end;
      fail_on_unexpected_tail();
      fail_span(step.op == 'P' || step.op == '/' || step.op == '%'
                    ? step.middle
                    : chain.start,
                step.end, error.message().view(), error.detail_message());
    }
  }

  noinline hot fn
  evaluate_chain(const arith_node &chain) throws -> ArithmeticValue
  {
    let lhs = evaluate(chain.first);

    for (let index = chain.second; index != ARITH_NO_NODE;
         index     = tree.nodes[index].next)
    {
      let const &step = tree.nodes[index];

      if (step.op == 'A' || step.op == 'O') {
        let const is_decided_by_lhs = (step.op == 'A') == lhs.is_zero();
        let const rhs = evaluate_skipped(step.first, is_decided_by_lhs);

        if (step.op == 'A') {
          lhs = ArithmeticValue{!lhs.is_zero() && !rhs.is_zero() ? 1 : 0};
        } else {
          lhs = ArithmeticValue{!lhs.is_zero() || !rhs.is_zero() ? 1 : 0};
        }

        continue;
      }

      let const rhs = evaluate(step.first);

      if (m_is_skipping) {
        lhs = ArithmeticValue{};
        continue;
      }

      lhs = apply_chain_step(chain, step, lhs, rhs);
    }

    return lhs;
  }

  fn
  evaluate_compound_assignment(const arith_node &node) throws
      -> ArithmeticValue
  {
    let target = get_lvalue(node.first);

    resolve_lvalue_subscript(target);
    pos = node.middle;

    let const lhs = read_lvalue_value(target);
    let const rhs = evaluate(node.second);

    if (m_is_skipping) return ArithmeticValue{};

    let result = ArithmeticValue{};

    try {
      result = arith_apply_binop(node.op, lhs, rhs, is_exact, arena, bc_scale);
    } catch (const ErrorWithLocation &) {
      throw;
    } catch (const ErrorBase &error) {
      fail_span(node.middle, node.end, error.message().view(),
                error.detail_message());
    }

    write_lvalue(target, result);

    return result;
  }

  hot fn
  evaluate(u32 index) throws -> ArithmeticValue
  {
    let const &node = tree.nodes[index];

    switch (node.kind) {
    case arith_node_kind::Number:
      if (depth < MAX_PRIMARY_DEPTH &&
          (!is_exact || (node.flags & arith_node::PLAIN_NUMBER_FLAG) != 0))
      {
        return ArithmeticValue{node.value};
      }

      return evaluate_primary(node);
    case arith_node_kind::Read:
      if (tree.is_simple) {
        let const &target = tree.nodes[node.first];

        return read_simple_variable(
            source.substring_of_length(target.start, target.length));
      }

      return evaluate_primary(node);
    case arith_node_kind::Chain: return evaluate_chain(node);
    case arith_node_kind::PostfixStep:
      if (tree.is_simple) return evaluate_simple_step(node, false);

      return evaluate_primary(node);
    case arith_node_kind::Group:
    case arith_node_kind::Call:
    case arith_node_kind::DepthLimit: return evaluate_primary(node);
    case arith_node_kind::PrefixStep: return evaluate_step(node, true);
    default:                          return evaluate_other(node);
    }
  }

  noinline fn
  evaluate_other(const arith_node &node) throws -> ArithmeticValue
  {
    switch (node.kind) {
    case arith_node_kind::Negate:
      return arith_apply_binop('-', ArithmeticValue{}, evaluate(node.first),
                               is_exact, arena, bc_scale);
    case arith_node_kind::Not:
      return ArithmeticValue{evaluate(node.first).is_zero() ? 1 : 0};
    case arith_node_kind::BitNot: {
      let const value = evaluate(node.first);

      return is_exact ? ArithmeticValue::bit_not(value, arena)
                      : ArithmeticValue{~value.wrapped_i64()};
    }
    case arith_node_kind::Ternary: {
      let const condition = evaluate(node.first);

      if (!condition.is_zero()) {
        let const if_true = evaluate(node.second);

        if (node.third != ARITH_NO_NODE)
          unused(evaluate_skipped(node.third, true));

        return if_true;
      }

      unused(evaluate_skipped(node.second, true));
      if (node.third == ARITH_NO_NODE) return ArithmeticValue{};

      return evaluate(node.third);
    }
    case arith_node_kind::Assign: {
      let const target = get_lvalue(node.first);
      let const rhs    = evaluate(node.second);

      write_lvalue(target, rhs);

      return rhs;
    }
    case arith_node_kind::CompoundAssign:
      return evaluate_compound_assignment(node);
    case arith_node_kind::Comma: {
      let result = ArithmeticValue{};

      for (let child = node.first; child != ARITH_NO_NODE;
           child     = tree.nodes[child].next)
        result = evaluate(child);

      return result;
    }
    case arith_node_kind::Error:
      if ((node.flags & arith_node::PRIMARY_ERROR_FLAG) != 0) {
        return evaluate_primary(node);
      }

      throw_error_node(node);
    case arith_node_kind::Number:
    case arith_node_kind::Read:
    case arith_node_kind::Chain:
    case arith_node_kind::PostfixStep:
    case arith_node_kind::Group:
    case arith_node_kind::Call:
    case arith_node_kind::DepthLimit:
    case arith_node_kind::PrefixStep:
    case arith_node_kind::Lvalue:
    case arith_node_kind::ChainStep:
    case arith_node_kind::Argument:    break;
    }

    unreachable("an arithmetic tree node of kind %d was evaluated",
                static_cast<int>(node.kind));
  }

  struct CalculatorArgument : ArithmeticValue
  {
    CalculatorArgument(ArithmeticValue value,
                       usize           start_position,
                       usize           end_position)
        : ArithmeticValue(steal(value)), start(start_position),
          end(end_position)
    {}

    usize start;
    usize end;
  };

  fn
  require_argument_count(StringView                           name,
                         const ArrayList<CalculatorArgument> &arguments,
                         usize                                minimum_count,
                         usize maximum_count) throws -> void
  {
    if (arguments.count() >= minimum_count &&
        arguments.count() <= maximum_count)
    {
      return;
    }

    let const allocator = bump_allocator(arena);
    let       detail    = String{allocator, "The function accepts "};
    if (maximum_count == ~usize{0}) {
      detail += "at least ";
      detail += String::from(minimum_count, allocator);
      detail += minimum_count == 1 ? " argument" : " arguments";
    } else if (minimum_count == maximum_count) {
      detail += String::from(minimum_count, allocator);
      detail += minimum_count == 1 ? " argument" : " arguments";
    } else {
      detail += String::from(minimum_count, allocator);
      detail += " to ";
      detail += String::from(maximum_count, allocator);
      detail += " arguments";
    }

    fail_span(calculator_function_position,
              calculator_function_position + name.length,
              "Wrong number of arguments for '" + name + "'", detail);
  }

  fn
  require_integer_argument(StringView                name,
                           const CalculatorArgument &argument) throws
      -> i64
  {
    if (!argument.has_integer_value(arena))
      fail_span(argument.start, argument.end,
                "The argument to '" + name + "' is not an integer",
                "This function requires an integer value");
    try {
      return ArithmeticValue::integer_part(argument, arena).checked_i64();
    } catch (const ErrorWithLocation &) {
      throw;
    } catch (const ErrorBase &error) {
      fail_span(argument.start, argument.end, error.message().view(),
                error.detail_message());
    }
  }

  fn
  calculator_gcd(ArithmeticValue left, ArithmeticValue right) throws
      -> ArithmeticValue
  {
    left  = ArithmeticValue::absolute(left, arena);
    right = ArithmeticValue::absolute(right, arena);

    while (!right.is_zero()) {
      let const remainder = ArithmeticValue::modulo(left, right, arena);

      left  = right;
      right = remainder;
    }

    return left;
  }

  /* memory: each intermediate value is allocated in arena and kept until the
             owner of arena resets it. values reach about 0.7 * index bits.
     time:   O(log index) loop steps, each with a constant number of additions
             and multiplications of values up to about 0.7 * index bits. */
  fn
  calculator_fibonacci(u64 index) throws -> ArithmeticValue
  {
    let current = ArithmeticValue{};
    let next    = ArithmeticValue{1};

    if (index == 0) return current;

    /* at this point: index is nonzero, so __builtin_clzll() is defined. */

    let bit = u64{1} << (63u - static_cast<u32>(__builtin_clzll(index)));

    /* e.g. index = 5 (binary 101), so bit starts at 4. (current, next) is
       (0, 1) before the loop; bit 4 gives (1, 1), bit 2 gives (1, 2), and bit 1
       gives (5, 8), so the loop returns 5. */
    while (bit != 0) {
      let const doubled_next = ArithmeticValue::add(next, next, arena);
      let const c            = ArithmeticValue::multiply(
          current, ArithmeticValue::subtract(doubled_next, current, arena),
          arena);
      let const d = ArithmeticValue::add(
          ArithmeticValue::multiply(current, current, arena),
          ArithmeticValue::multiply(next, next, arena), arena);

      if ((index & bit) == 0) {
        current = c;
        next    = d;
      } else {
        current = d;
        next    = ArithmeticValue::add(c, d, arena);
      }

      bit >>= 1u;
    }

    return current;
  }

  fn
  calculator_requested_scale(
      StringView                           name,
      const ArrayList<CalculatorArgument> &arguments) throws -> u32
  {
    u32 requested_scale = 20;

    if (arguments.count() == 2) {
      let const parsed = require_integer_argument(name, arguments[1]);

      if (parsed < 0 || parsed > 100000) {
        fail_span(arguments[1].start, arguments[1].end,
                  "Invalid scale for '" + name + "'",
                  "The scale must be between 0 and 100000");
      }

      requested_scale = static_cast<u32>(parsed);
    }

    return requested_scale;
  }

  fn
  calculator_work_scale(u32 requested_scale,
                        u32 magnitude_digit_count = 0) throws -> u32
  {
    if (magnitude_digit_count > ~u32{0} - 12 ||
        requested_scale > ~u32{0} - 12 - magnitude_digit_count)
    {
      throw std::bad_alloc{};
    }

    return requested_scale + magnitude_digit_count + 12;
  }

  /* e.g. value = -123.45 has 3 digits before the point. */
  fn
  calculator_integer_digit_count(const ArithmeticValue &value) throws
      -> u32
  {
    let const text = value.to_string(bump_allocator(arena));
    let const digits =
        text.view().starts_with("-") ? text.view().substring(1) : text.view();
    let const point = digits.find_character('.');

    return static_cast<u32>(point.has_value() ? *point : digits.length);
  }

  /* memory: cached_pi is allocated in arena and lives until the owner of arena
             resets it.
     time:   one calculator_atan() call when cached_pi_scale is below
             decimal_scale. otherwise one ArithmeticValue::rescale() call, or no
             work when the scales match. */
  fn
  calculator_pi(u32 decimal_scale) throws -> ArithmeticValue
  {
    if (cached_pi_scale < decimal_scale) {
      let const quarter_pi = calculator_atan(ArithmeticValue{1}, decimal_scale);
      let const half_pi = ArithmeticValue::add(quarter_pi, quarter_pi, arena);

      cached_pi       = ArithmeticValue::add(half_pi, half_pi, arena);
      cached_pi_scale = decimal_scale;
    }

    if (cached_pi_scale == decimal_scale) return cached_pi;

    return ArithmeticValue::rescale(cached_pi, decimal_scale, arena);
  }

  fn
  calculator_fixed_multiply(const ArithmeticValue &left,
                            const ArithmeticValue &right,
                            u32                    decimal_scale,
                            BumpArena             &result_arena) throws
      -> ArithmeticValue
  {
    return ArithmeticValue::rescale(
        ArithmeticValue::multiply(left, right, result_arena), decimal_scale,
        result_arena);
  }

  fn
  calculator_fixed_divide(const ArithmeticValue &left,
                          const ArithmeticValue &right,
                          u32                    decimal_scale,
                          BumpArena             &result_arena) throws
      -> ArithmeticValue
  {
    if (right.is_zero())
      fail("Division by zero", "The right operand evaluated to 0");

    u32 prepared_scale = left.get_decimal_scale();

    if (decimal_scale > prepared_scale) prepared_scale = decimal_scale;

    let const prepared =
        ArithmeticValue::rescale(left, prepared_scale, result_arena);

    return ArithmeticValue::rescale(
        ArithmeticValue::divide(prepared, right, result_arena), decimal_scale,
        result_arena);
  }

  fn
  calculator_atan(ArithmeticValue value, u32 requested_scale) throws
      -> ArithmeticValue
  {
    let const work_scale  = calculator_work_scale(requested_scale);
    let const is_negative = value.is_negative();

    value = ArithmeticValue::rescale(ArithmeticValue::absolute(value, arena),
                                     work_scale, arena);

    let const threshold = ArithmeticValue::rescale(
        ArithmeticValue::parse_decimal("0.1", arena), work_scale, arena);

    BumpArena iteration_arenas[2];
    usize     state_position = 0;

    value = ArithmeticValue::add(value, ArithmeticValue{}, iteration_arenas[0]);

    u32 doubling_count = 0;

    while (value.compare(threshold, bump_allocator(arena)) > 0) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();

      let const square = calculator_fixed_multiply(
          value, value, work_scale, iteration_arenas[next_position]);
      let const root = ArithmeticValue::square_root(
          ArithmeticValue::add(ArithmeticValue{1}, square,
                               iteration_arenas[next_position]),
          work_scale, iteration_arenas[next_position]);

      value = calculator_fixed_divide(
          value,
          ArithmeticValue::add(ArithmeticValue{1}, root,
                               iteration_arenas[next_position]),
          work_scale, iteration_arenas[next_position]);
      state_position = next_position;
      doubling_count++;
      if (doubling_count > 64) throw std::bad_alloc{};
    }

    value = ArithmeticValue::add(value, ArithmeticValue{}, arena);
    iteration_arenas[0].reset();
    iteration_arenas[1].reset();

    let const square =
        calculator_fixed_multiply(value, value, work_scale, arena);
    let term =
        ArithmeticValue::add(value, ArithmeticValue{}, iteration_arenas[0]);
    let sum = term;

    state_position = 0;

    let const iteration_count = static_cast<usize>(work_scale) * 2 + 128;

    for (usize index = 1; index < iteration_count; index++) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();

      let const next_term = calculator_fixed_multiply(
          term, square, work_scale, iteration_arenas[next_position]);

      let const denominator  = ArithmeticValue{static_cast<i64>(index * 2 + 1)};
      let const contribution = calculator_fixed_divide(
          next_term, denominator, work_scale, iteration_arenas[next_position]);

      if (contribution.is_zero()) break;

      let const next_sum =
          (index & 1u) != 0
              ? ArithmeticValue::subtract(sum, contribution,
                                          iteration_arenas[next_position])
              : ArithmeticValue::add(sum, contribution,
                                     iteration_arenas[next_position]);

      term           = next_term;
      sum            = next_sum;
      state_position = next_position;
    }

    sum = ArithmeticValue::add(sum, ArithmeticValue{}, arena);

    for (u32 count = 0; count < doubling_count; count++)
      sum = ArithmeticValue::add(sum, sum, arena);
    if (is_negative)
      sum = ArithmeticValue::subtract(ArithmeticValue{}, sum, arena);

    return ArithmeticValue::rescale(sum, requested_scale, arena);
  }

  /* memory: iteration_arenas[0] and iteration_arenas[1] hold the series terms
             and are reset before reuse. the result is allocated in arena.
     time:   one modulo(), a fixed number of comparisons, and one
             calculator_pi() call. then at most 2 * work_scale + 128 series
             steps, each a multiply and a divide. the series stops early when
             a term is zero. */
  fn
  calculator_sin_cos(ArithmeticValue value,
                     u32             requested_scale,
                     bool            is_cosine) throws -> ArithmeticValue
  {
    /* the reduction by two pi cancels one digit of pi per integer digit. */
    let const work_scale = calculator_work_scale(
        requested_scale, calculator_integer_digit_count(value));

    value = ArithmeticValue::rescale(value, work_scale, arena);

    let const pi = calculator_pi(work_scale);
    let const half_pi =
        calculator_fixed_divide(pi, ArithmeticValue{2}, work_scale, arena);
    let const two_pi = ArithmeticValue::add(pi, pi, arena);

    value = ArithmeticValue::modulo(value, two_pi, arena);
    if (value.compare(pi, bump_allocator(arena)) > 0)
      value = ArithmeticValue::subtract(value, two_pi, arena);

    let const negative_pi =
        ArithmeticValue::subtract(ArithmeticValue{}, pi, arena);

    if (value.compare(negative_pi, bump_allocator(arena)) < 0)
      value = ArithmeticValue::add(value, two_pi, arena);

    let should_negate = false;

    if (value.compare(half_pi, bump_allocator(arena)) > 0) {
      value         = ArithmeticValue::subtract(pi, value, arena);
      should_negate = is_cosine;
    } else {
      let const negative_half_pi =
          ArithmeticValue::subtract(ArithmeticValue{}, half_pi, arena);

      if (value.compare(negative_half_pi, bump_allocator(arena)) < 0) {
        value         = ArithmeticValue::subtract(negative_pi, value, arena);
        should_negate = is_cosine;
      }
    }

    let const square =
        calculator_fixed_multiply(value, value, work_scale, arena);
    BumpArena iteration_arenas[2];
    let term = ArithmeticValue::add(is_cosine ? ArithmeticValue{1} : value,
                                    ArithmeticValue{}, iteration_arenas[0]);
    let       sum             = term;
    usize     state_position  = 0;
    let const iteration_count = static_cast<usize>(work_scale) * 2 + 128;

    for (usize index = 1; index < iteration_count; index++) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();

      let const first       = is_cosine ? index * 2 - 1 : index * 2;
      let const second      = is_cosine ? index * 2 : index * 2 + 1;
      let const denominator = ArithmeticValue{static_cast<i64>(first * second)};
      let const next_term   = calculator_fixed_divide(
          calculator_fixed_multiply(term, square, work_scale,
                                      iteration_arenas[next_position]),
          denominator, work_scale, iteration_arenas[next_position]);

      if (next_term.is_zero()) break;

      let const next_sum =
          (index & 1u) != 0
              ? ArithmeticValue::subtract(sum, next_term,
                                          iteration_arenas[next_position])
              : ArithmeticValue::add(sum, next_term,
                                     iteration_arenas[next_position]);

      term           = next_term;
      sum            = next_sum;
      state_position = next_position;
    }

    sum = ArithmeticValue::add(sum, ArithmeticValue{}, arena);

    if (should_negate)
      sum = ArithmeticValue::subtract(ArithmeticValue{}, sum, arena);

    return ArithmeticValue::rescale(sum, requested_scale, arena);
  }

  /* memory: iteration_arenas[0] and iteration_arenas[1] hold the series terms
             and are reset before reuse. the result is allocated in arena.
     time:   up to 4096 halvings of the operand while it exceeds 0.5, each a
             divide by 2. then at most 4 * work_scale + 256 series steps, each
             a multiply and a divide by index. then squaring_count squarings.
             the series stops early when a term is zero. */
  fn
  calculator_exp(ArithmeticValue value, u32 requested_scale) throws
      -> ArithmeticValue
  {
    static constexpr u32 MAX_OPERAND_DIGIT_COUNT = 9;

    if (calculator_integer_digit_count(value) > MAX_OPERAND_DIGIT_COUNT) {
      fail("The operand to 'exp' is too large",
           "Use an absolute value below 10 ** 9");
    }

    /* the result has log10(e) digits per unit of the operand before its point,
       and each of at most 32 squarings doubles the error of the series, so 16
       digits cover the squarings. */
    let const operand_whole =
        ArithmeticValue::integer_part(ArithmeticValue::absolute(value, arena),
                                      arena)
            .checked_i64();
    let const magnitude_digit_count =
        static_cast<u32>(operand_whole * 4343 / 10000) + 16;
    let const work_scale =
        calculator_work_scale(requested_scale, magnitude_digit_count);
    let const is_negative = value.is_negative();

    value = ArithmeticValue::rescale(ArithmeticValue::absolute(value, arena),
                                     work_scale, arena);

    let const reduction_limit = ArithmeticValue::rescale(
        ArithmeticValue::parse_decimal("0.5", arena), work_scale, arena);
    BumpArena iteration_arenas[2];
    usize     state_position = 0;

    value = ArithmeticValue::add(value, ArithmeticValue{}, iteration_arenas[0]);

    u32 squaring_count = 0;

    while (value.compare(reduction_limit, bump_allocator(arena)) > 0) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();
      value = calculator_fixed_divide(value, ArithmeticValue{2}, work_scale,
                                      iteration_arenas[next_position]);
      state_position = next_position;
      squaring_count++;
      if (squaring_count > 4096)
        fail("The operand to 'exp' is too large",
             "Use an absolute value below 2 ** 4096");
    }

    value = ArithmeticValue::add(value, ArithmeticValue{}, arena);
    iteration_arenas[0].reset();
    iteration_arenas[1].reset();

    let term = ArithmeticValue::rescale(ArithmeticValue{1}, work_scale,
                                        iteration_arenas[0]);
    let sum  = term;

    state_position = 0;

    let const iteration_count = static_cast<usize>(work_scale) * 4 + 256;

    for (usize index = 1; index < iteration_count; index++) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();

      let const next_term = calculator_fixed_divide(
          calculator_fixed_multiply(term, value, work_scale,
                                    iteration_arenas[next_position]),
          ArithmeticValue{static_cast<i64>(index)}, work_scale,
          iteration_arenas[next_position]);

      if (next_term.is_zero()) break;

      let const next_sum =
          ArithmeticValue::add(sum, next_term, iteration_arenas[next_position]);

      term           = next_term;
      sum            = next_sum;
      state_position = next_position;
    }

    for (u32 count = 0; count < squaring_count; count++) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();
      sum            = calculator_fixed_multiply(sum, sum, work_scale,
                                                 iteration_arenas[next_position]);
      state_position = next_position;
    }

    if (is_negative)
      sum = calculator_fixed_divide(ArithmeticValue{1}, sum, work_scale, arena);

    return ArithmeticValue::rescale(sum, requested_scale, arena);
  }

  fn
  calculator_ln(ArithmeticValue value, u32 requested_scale) throws
      -> ArithmeticValue
  {
    let const work_scale = calculator_work_scale(requested_scale);

    value = ArithmeticValue::rescale(value, work_scale, arena);

    if (value.is_zero())
      fail("Logarithm operand is too small",
           "ln requires an operand of at least 10 ** -(scale + 12)");

    let const lower = ArithmeticValue::rescale(
        ArithmeticValue::parse_decimal("0.8", arena), work_scale, arena);
    let const upper = ArithmeticValue::rescale(
        ArithmeticValue::parse_decimal("1.25", arena), work_scale, arena);
    BumpArena iteration_arenas[2];
    usize     state_position = 0;

    value = ArithmeticValue::add(value, ArithmeticValue{}, iteration_arenas[0]);

    u32 root_count = 0;

    while (value.compare(lower, bump_allocator(arena)) < 0 ||
           value.compare(upper, bump_allocator(arena)) > 0)
    {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();
      value          = ArithmeticValue::square_root(value, work_scale,
                                                    iteration_arenas[next_position]);
      state_position = next_position;
      root_count++;
      if (root_count > 64) throw std::bad_alloc{};
    }

    value = ArithmeticValue::add(value, ArithmeticValue{}, arena);
    iteration_arenas[0].reset();
    iteration_arenas[1].reset();

    /* at this point: value lies in [0.8, 1.25] at work_scale, so reduced is
       (value - 1) / (value + 1) with magnitude at most about 0.112. */
    let const numerator =
        ArithmeticValue::subtract(value, ArithmeticValue{1}, arena);
    let const denominator =
        ArithmeticValue::add(value, ArithmeticValue{1}, arena);
    let const reduced =
        calculator_fixed_divide(numerator, denominator, work_scale, arena);
    let const square =
        calculator_fixed_multiply(reduced, reduced, work_scale, arena);
    let term =
        ArithmeticValue::add(reduced, ArithmeticValue{}, iteration_arenas[0]);
    let sum = term;

    state_position = 0;

    let const iteration_count = static_cast<usize>(work_scale) * 2 + 128;

    for (usize index = 1; index < iteration_count; index++) {
      let const next_position = 1 - state_position;

      iteration_arenas[next_position].reset();

      let const next_term = calculator_fixed_multiply(
          term, square, work_scale, iteration_arenas[next_position]);
      let const contribution = calculator_fixed_divide(
          next_term, ArithmeticValue{static_cast<i64>(index * 2 + 1)},
          work_scale, iteration_arenas[next_position]);

      if (contribution.is_zero()) break;

      let const next_sum = ArithmeticValue::add(
          sum, contribution, iteration_arenas[next_position]);

      term           = next_term;
      sum            = next_sum;
      state_position = next_position;
    }

    sum = ArithmeticValue::add(sum, ArithmeticValue{}, arena);
    sum = ArithmeticValue::add(sum, sum, arena);

    for (u32 count = 0; count < root_count; count++)
      sum = ArithmeticValue::add(sum, sum, arena);

    return ArithmeticValue::rescale(sum, requested_scale, arena);
  }

  fn
  apply_calculator_function(
      StringView                           name,
      usize                                name_position,
      CalculatorFunction                   function_value,
      const ArrayList<CalculatorArgument> &arguments) throws
      -> ArithmeticValue
  {
    let const previous_function_position = calculator_function_position;

    calculator_function_position = name_position;
    defer {
      calculator_function_position = previous_function_position;
    };

    let const *const function = &function_value;

    switch (*function) {
    case CalculatorFunction::Abs:
      require_argument_count(name, arguments, 1, 1);

      return ArithmeticValue::absolute(arguments[0], arena);
    case CalculatorFunction::Atan:
      require_argument_count(name, arguments, 1, 2);

      return calculator_atan(arguments[0],
                             calculator_requested_scale(name, arguments));
    case CalculatorFunction::Ceil:
    case CalculatorFunction::Floor:
    case CalculatorFunction::Frac:
    case CalculatorFunction::Int:   {
      require_argument_count(name, arguments, 1, 1);

      let const integer = ArithmeticValue::integer_part(arguments[0], arena);

      if (*function == CalculatorFunction::Int) return integer;

      let const fraction =
          ArithmeticValue::subtract(arguments[0], integer, arena);
      if (*function == CalculatorFunction::Frac) return fraction;
      if (fraction.is_zero()) return integer;
      if (*function == CalculatorFunction::Floor)
        return arguments[0].is_negative()
                   ? ArithmeticValue::subtract(integer, ArithmeticValue{1},
                                               arena)
                   : integer;

      return arguments[0].is_negative()
                 ? integer
                 : ArithmeticValue::add(integer, ArithmeticValue{1}, arena);
    }
    case CalculatorFunction::Cmp:
      require_argument_count(name, arguments, 2, 2);

      return ArithmeticValue{
          arguments[0].compare(arguments[1], bump_allocator(arena))};
    case CalculatorFunction::Cos:
      require_argument_count(name, arguments, 1, 2);

      return calculator_sin_cos(
          arguments[0], calculator_requested_scale(name, arguments), true);
    case CalculatorFunction::Exp:
      require_argument_count(name, arguments, 1, 2);

      return calculator_exp(arguments[0],
                            calculator_requested_scale(name, arguments));
    case CalculatorFunction::Fact: {
      require_argument_count(name, arguments, 1, 1);

      let const count = require_integer_argument(name, arguments[0]);
      if (count < 0)
        fail_span(arguments[0].start, arguments[0].end,
                  "The argument to 'fact' is negative",
                  "Factorial requires a non-negative integer");

      /* memory: each product is allocated in arena and kept until the owner of
                 arena resets it.
         time:   count - 1 multiplications. the product grows to about
                 count * log2(count) bits. */
      let result = ArithmeticValue{1};

      for (i64 factor = 2; factor <= count; factor++) {
        arithmetic_internal::throw_if_arithmetic_interrupted();
        result =
            ArithmeticValue::multiply(result, ArithmeticValue{factor}, arena);
      }

      return result;
    }
    case CalculatorFunction::Fib: {
      require_argument_count(name, arguments, 1, 1);

      let const index = require_integer_argument(name, arguments[0]);

      if (index < 0)
        fail_span(arguments[0].start, arguments[0].end,
                  "The argument to 'fib' is negative",
                  "Fibonacci requires a non-negative integer");

      return calculator_fibonacci(static_cast<u64>(index));
    }
    case CalculatorFunction::Gcd:
    case CalculatorFunction::Lcm: {
      require_argument_count(name, arguments, 1, ~usize{0});
      if (!arguments[0].has_integer_value(arena))
        fail_span(arguments[0].start, arguments[0].end,
                  "An argument to '" + name + "' is not an integer",
                  "This function requires integer values");

      let result = ArithmeticValue::absolute(
          ArithmeticValue::integer_part(arguments[0], arena), arena);

      for (usize index = 1; index < arguments.count(); index++) {
        if (!arguments[index].has_integer_value(arena))
          fail_span(arguments[index].start, arguments[index].end,
                    "An argument to '" + name + "' is not an integer",
                    "This function requires integer values");

        let const argument =
            ArithmeticValue::integer_part(arguments[index], arena);
        let const divisor = calculator_gcd(result, argument);

        if (*function == CalculatorFunction::Gcd) {
          result = divisor;
        } else if (result.is_zero() || argument.is_zero()) {
          result = ArithmeticValue{};
        } else {
          result = ArithmeticValue::absolute(
              ArithmeticValue::multiply(
                  ArithmeticValue::divide(result, divisor, arena), argument,
                  arena),
              arena);
        }
      }

      return result;
    }
    case CalculatorFunction::IsEven:
    case CalculatorFunction::IsOdd:  {
      require_argument_count(name, arguments, 1, 1);
      require_integer_argument(name, arguments[0]);

      let const remainder = ArithmeticValue::modulo(
          ArithmeticValue::absolute(
              ArithmeticValue::integer_part(arguments[0], arena), arena),
          ArithmeticValue{2}, arena);
      let const is_odd = !remainder.is_zero();

      return ArithmeticValue{
          (*function == CalculatorFunction::IsOdd) == is_odd ? 1 : 0};
    }
    case CalculatorFunction::IsInt:
      require_argument_count(name, arguments, 1, 1);

      return ArithmeticValue{arguments[0].has_integer_value(arena) ? 1 : 0};
    case CalculatorFunction::Length: {
      require_argument_count(name, arguments, 1, 1);

      let const text        = arguments[0].to_string(bump_allocator(arena));
      usize     digit_count = 0;

      for (usize position = 0; position < text.count(); position++) {
        if (text[position] >= '0' && text[position] <= '9') {
          digit_count++;
        }
      }

      let const body_start = text[0] == '-' ? usize{1} : usize{0};

      if (text.count() > body_start + 1 && text[body_start] == '0' &&
          text[body_start + 1] == '.')
      {
        digit_count--;
      }

      return ArithmeticValue{static_cast<i64>(digit_count)};
    }
    case CalculatorFunction::Ln:
      require_argument_count(name, arguments, 1, 2);
      if (arguments[0].is_zero() || arguments[0].is_negative()) {
        fail_span(arguments[0].start, arguments[0].end,
                  "Logarithm operand is not positive",
                  "ln requires a value greater than 0");
      }

      return calculator_ln(arguments[0],
                           calculator_requested_scale(name, arguments));
    case CalculatorFunction::Max:
    case CalculatorFunction::Min: {
      require_argument_count(name, arguments, 1, ~usize{0});

      let result = arguments[0];

      for (usize index = 1; index < arguments.count(); index++) {
        let const ordering =
            result.compare(arguments[index], bump_allocator(arena));

        if ((*function == CalculatorFunction::Max && ordering < 0) ||
            (*function == CalculatorFunction::Min && ordering > 0))
        {
          result = arguments[index];
        }
      }

      return result;
    }
    case CalculatorFunction::Scale:
      require_argument_count(name, arguments, 1, 1);

      return ArithmeticValue{
          static_cast<i64>(arguments[0].get_decimal_scale())};
    case CalculatorFunction::Sgn:
      require_argument_count(name, arguments, 1, 1);

      return ArithmeticValue{
          arguments[0].is_zero() ? 0 : (arguments[0].is_negative() ? -1 : 1)};
    case CalculatorFunction::Sin:
      require_argument_count(name, arguments, 1, 2);

      return calculator_sin_cos(
          arguments[0], calculator_requested_scale(name, arguments), false);
    case CalculatorFunction::Sqrt: {
      require_argument_count(name, arguments, 1, 2);
      if (arguments[0].is_negative())
        fail_span(arguments[0].start, arguments[0].end,
                  "Square root operand is negative",
                  "sqrt requires a value greater than or equal to 0");

      u32 decimal_scale = arguments[0].get_decimal_scale() > 20
                              ? arguments[0].get_decimal_scale()
                              : 20;

      if (arguments.count() == 2)
        decimal_scale = calculator_requested_scale(name, arguments);

      return ArithmeticValue::square_root(arguments[0], decimal_scale, arena);
    }
    }

    unreachable();
  }
};

struct explicit_radix
{
  u32   radix;
  usize digit_start;
};

alwaysinline static fn
lex_explicit_radix(StringView from) throws -> Maybe<explicit_radix>
{
  let const base_digit_count =
      arithmetic_internal::count_leading_digits(from, 10);

  if (base_digit_count == 0 || base_digit_count >= from.length ||
      from[base_digit_count] != '#')
  {
    return None;
  }

  let const base =
      parse_arithmetic_operand(from.substring_of_length(0, base_digit_count));

  if (base < 2 || base > 64) {
    throw ErrorWithDetails{"The arithmetic base must be between 2 and 64",
                           "Use `base#digits` with a base from 2 to 64"};
  }

  /* at this point: from starts with base_digit_count digits and a '#', and
     base is between 2 and 64. */
  return explicit_radix{static_cast<u32>(base), base_digit_count + 1};
}

/* a binary or based number needs a digit after its prefix. 0x alone stays 0. */
static fn
reject_missing_radix_digits(StringView from,
                            usize      digit_start,
                            u32        radix) throws -> void
{
  if (radix != 16 || from[digit_start - 1] == '#') {
    if (arithmetic_internal::count_leading_digits(from.substring(digit_start),
                                                  radix) == 0)
    {
      throw ErrorWithDetails{"The number '" +
                                 from.substring_of_length(0, digit_start) +
                                 "' has no digits",
                             "Write at least one digit after the prefix"};
    }
  }
}

static fn
lex_arith_number(StringView from, i64 *out_value) throws -> usize
{
  if (let const explicit_base = lex_explicit_radix(from);
      explicit_base.has_value())
  {
    let const radix = explicit_base->radix;
    u64       value = 0;
    usize     i     = explicit_base->digit_start;

    reject_missing_radix_digits(from, i, radix);

    while (i < from.length) {
      let const digit = arithmetic_internal::radix_digit_value(from[i], radix);

      if (digit >= radix) break;
      value = value * radix + digit;
      i++;
    }

    *out_value = static_cast<i64>(value);
    return i;
  }

  let const detected = arithmetic_internal::detect_radix_prefix(from);

  if (detected.prefix_length > 0) {
    reject_missing_radix_digits(from, detected.prefix_length, detected.radix);
  }

  usize consumed = detected.prefix_length +
                   arithmetic_internal::count_leading_digits(
                       from.substring(detected.prefix_length), detected.radix);

  if (consumed == 0) consumed = 1;
  *out_value = parse_arithmetic_operand(from.substring_of_length(0, consumed));
  return consumed;
}

static fn
lex_exact_arith_number(StringView       from,
                       ArithmeticValue *out_value,
                       BumpArena       &arena) throws -> usize
{
  if (let const explicit_base = lex_explicit_radix(from);
      explicit_base.has_value())
  {
    reject_missing_radix_digits(from, explicit_base->digit_start,
                                explicit_base->radix);

    let const digits = from.substring(explicit_base->digit_start);
    let const digit_count =
        arithmetic_internal::count_leading_digits(digits, explicit_base->radix);

    *out_value =
        ArithmeticValue::parse(digits.substring_of_length(0, digit_count),
                               explicit_base->radix, arena);

    return explicit_base->digit_start + digit_count;
  }

  let const decimal_integer_count =
      arithmetic_internal::count_leading_digits(from, 10);
  if (decimal_integer_count < from.length && from[decimal_integer_count] == '.')
  {
    let const fractional_count = arithmetic_internal::count_leading_digits(
        from.substring(decimal_integer_count + 1), 10);

    if (decimal_integer_count == 0 && fractional_count == 0) {
      *out_value = ArithmeticValue{};
      return 0;
    }

    let const consumed = decimal_integer_count + 1 + fractional_count;

    *out_value = ArithmeticValue::parse_decimal(
        from.substring_of_length(0, consumed), arena);

    return consumed;
  }

  let const detected = arithmetic_internal::detect_radix_prefix(from);

  if (detected.prefix_length > 0) {
    reject_missing_radix_digits(from, detected.prefix_length, detected.radix);
  }

  let digit_count = arithmetic_internal::count_leading_digits(
      from.substring(detected.prefix_length), detected.radix);

  if (digit_count == 0 && detected.prefix_length == 0) {
    digit_count = 1;
  }

  let const consumed = detected.prefix_length + digit_count;

  *out_value = ArithmeticValue::parse(
      from.substring_of_length(detected.prefix_length, digit_count),
      detected.radix, arena);

  return consumed;
}

struct arith_token
{
  enum class kind : u8
  {
    number,
    name,
    op,
    subscript,
  };
  kind       k;
  i64        value{0};
  StringView text{};
};

static pure fn
arith_operator_length(StringView src, usize i) wontthrow -> usize
{
  let const first  = src[i];
  let const second = i + 1 < src.length ? src[i + 1] : '\0';
  let const third  = i + 2 < src.length ? src[i + 2] : '\0';

  switch (first) {
  case '<':
  case '>':
    if (second == first) return third == '=' ? 3 : 2;

    return second == '=' ? 2 : 1;
  case '*':
  case '&':
  case '|':
  case '+':
  case '-': return second == first || second == '=' ? 2 : 1;
  case '=':
  case '!':
  case '/':
  case '%':
  case '^': return second == '=' ? 2 : 1;
  default:  return 1;
  }
}

struct arith_binop
{
  char kind;
  u8   precedence;
};

static pure fn
arith_classify_binop(StringView t) wontthrow -> arith_binop
{
  static constexpr static_string_entry<arith_binop> ENTRIES[] = {
      {SSK("**"), {'P', 11}},
      {SSK("*"),  {'*', 10}},
      {SSK("/"),  {'/', 10}},
      {SSK("%"),  {'%', 10}},
      {SSK("+"),  {'+', 9} },
      {SSK("-"),  {'-', 9} },
      {SSK("<<"), {'L', 8} },
      {SSK(">>"), {'R', 8} },
      {SSK("<"),  {'<', 7} },
      {SSK("<="), {'l', 7} },
      {SSK(">"),  {'>', 7} },
      {SSK(">="), {'g', 7} },
      {SSK("=="), {'e', 6} },
      {SSK("!="), {'n', 6} },
      {SSK("&"),  {'&', 5} },
      {SSK("^"),  {'^', 4} },
      {SSK("|"),  {'|', 3} },
  };
  static constexpr StaticStringMap BINOPS{ENTRIES};

  if (let const found = BINOPS.find(t); found.has_value()) return *found;

  return {0, 0};
}

/* the binary operator class of an operator token, packed into its value as
   precedence << 8 | kind. */
static pure fn
arith_packed_binop(StringView t) wontthrow -> i64
{
  let const op = arith_classify_binop(t);

  return (static_cast<i64>(op.precedence) << 8) | static_cast<u8>(op.kind);
}

static pure fn
arith_unpacked_binop(i64 value) wontthrow -> arith_binop
{
  return {static_cast<char>(value & 0xff), static_cast<u8>(value >> 8)};
}

/* memory: out grows by one arith_token per token; the caller owns out and
           clears it before a retokenize. token texts are views into src.
   time:   O(n) in the bytes of src. */
static fn
tokenize_arithmetic(StringView src, ArrayList<arith_token> &out) throws
    -> void
{
  usize i = 0;

  while (i < src.length) {
    let const current_byte = src[i];

    if (current_byte == ' ' || current_byte == '\t' || current_byte == '\n' ||
        current_byte == '\r')
    {
      i++;
      continue;
    }

    let const has_leading_decimal_point = is_leading_decimal_number(src, i);

    if (lexer::is_number(current_byte) || has_leading_decimal_point) {
      i64   value = 0;
      usize consumed;

      if (has_leading_decimal_point) {
        consumed = 1 + arithmetic_internal::count_leading_digits(
                           src.substring(i + 1), 10);
      } else {
        consumed = lex_arith_number(src.substring(i), &value);
        if (i + consumed < src.length &&
            is_number_continuation(src[i + consumed]))
        {
          throw Error{"Invalid digit in the number"};
        }

        if (i + consumed < src.length && src[i + consumed] == '.') {
          consumed++;
          consumed += arithmetic_internal::count_leading_digits(
              src.substring(i + consumed), 10);
        }
      }

      out.push(arith_token{arith_token::kind::number, value,
                           src.substring_of_length(i, consumed)});
      i += consumed;
      continue;
    }

    if (lexer::is_variable_name_start(current_byte)) {
      let const name_start = i;

      i++;

      while (i < src.length && lexer::is_variable_name(src[i])) {
        i++;
      }

      out.push(
          arith_token{arith_token::kind::name, 0,
                      src.substring_of_length(name_start, i - name_start)});
      if (i < src.length && src[i] == '[') {
        i++;

        let const inner_start = i;
        usize     depth       = 1;

        /* e.g. src = "a[b[1]]+2" and i = 2 after the '[' at index 1
           -> the loop visits index 2 (depth 1), index 3 (depth 2), index 5
              (depth 1), index 6 (depth 0, break with i = 6)
           -> subscript "b[1]" from inner_start 2, length 4; i = 7 at '+' */
        while (i < src.length && depth > 0) {
          if (src[i] == '[') {
            depth++;
          } else if (src[i] == ']' && --depth == 0) {
            break;
          }

          i++;
        }

        if (depth != 0) {
          throw ErrorWithDetails{"Expected ']' after an array subscript",
                                 "The subscript '[' was never closed"};
        }

        out.push(
            arith_token{arith_token::kind::subscript, 0,
                        src.substring_of_length(inner_start, i - inner_start)});
        i++;
      }

      continue;
    }

    let const op = src.substring_of_length(i, arith_operator_length(src, i));

    out.push(arith_token{arith_token::kind::op, arith_packed_binop(op), op});
    i += op.length;
  }
}

static pure fn
arith_op_is_complex(StringView t) wontthrow -> bool
{
  static constexpr PackedStringKey KEYS[] = {
      SSK("="),  SSK("+="), SSK("-="), SSK("*="),  SSK("/="),  SSK("%="),
      SSK("&="), SSK("|="), SSK("^="), SSK("<<="), SSK(">>="), SSK("?"),
      SSK(":"),  SSK(","),  SSK("++"), SSK("--"),  SSK("&&"),  SSK("||"),
  };
  static constexpr StaticStringSet COMPLEX_OPS{KEYS};

  return COMPLEX_OPS.contains(t);
}

static pure fn
arith_tokens_are_single_step(const ArrayList<arith_token> &toks)
    wontthrow -> bool
{
  if (toks.count() != 2) return false;

  let const &first  = toks[0];
  let const &second = toks[1];

  let const do_is_step = [](const arith_token &token) {
    return token.k == arith_token::kind::op &&
           (token.text == "++" || token.text == "--");
  };

  if (do_is_step(first)) return second.k == arith_token::kind::name;

  /* at this point: toks holds two tokens, and first is not a '++' or '--'
     operator. */
  return first.k == arith_token::kind::name && do_is_step(second) &&
         first.text.data + first.text.length == second.text.data;
}

static pure fn
arith_tokens_are_simple(const ArrayList<arith_token> &toks) wontthrow -> bool
{
  if (arith_tokens_are_single_step(toks)) return true;

  for (let const &t : toks) {
    if (t.k == arith_token::kind::subscript) return false;
    if (t.k == arith_token::kind::number &&
        t.text.find_character('.').has_value())
    {
      return false;
    }

    if (t.k == arith_token::kind::op && arith_op_is_complex(t.text)) {
      return false;
    }
  }

  return true;
}

static fn
bc_apply_binop(char                   kind,
               const ArithmeticValue &lhs,
               const ArithmeticValue &rhs,
               u32                    scale,
               BumpArena             &arena) throws -> ArithmeticValue
{
  if ((kind == '/' || kind == '%') && rhs.is_zero()) {
    throw_division_by_zero();
  }

  switch (kind) {
  case '*': {
    let const combined_scale =
        static_cast<u64>(lhs.get_decimal_scale()) + rhs.get_decimal_scale();
    let desired_scale = ArithmeticValue::wider_decimal_scale(lhs, rhs);

    if (scale > desired_scale) desired_scale = scale;
    if (combined_scale < desired_scale)
      desired_scale = static_cast<u32>(combined_scale);

    return ArithmeticValue::rescale(ArithmeticValue::multiply(lhs, rhs, arena),
                                    desired_scale, arena);
  }
  case '/': {
    u32 prepared_scale = lhs.get_decimal_scale();

    if (scale > prepared_scale) prepared_scale = scale;

    let const prepared = ArithmeticValue::rescale(lhs, prepared_scale, arena);

    return ArithmeticValue::rescale(
        ArithmeticValue::divide(prepared, rhs, arena), scale, arena);
  }
  case '%': {
    let const quotient  = bc_apply_binop('/', lhs, rhs, scale, arena);
    let const product   = ArithmeticValue::multiply(quotient, rhs, arena);
    let const remainder = ArithmeticValue::subtract(lhs, product, arena);
    let const desired_scale_wide =
        static_cast<u64>(scale) + rhs.get_decimal_scale();

    if (desired_scale_wide > ~u32{0}) throw std::bad_alloc{};

    let desired_scale = static_cast<u32>(desired_scale_wide);

    if (lhs.get_decimal_scale() > desired_scale)
      desired_scale = lhs.get_decimal_scale();

    return ArithmeticValue::rescale(remainder, desired_scale, arena);
  }
  case 'P': {
    if (!rhs.has_integer_value(arena))
      throw ErrorWithDetails{"Exponent is not an integer",
                             "'^' requires an integer exponent"};

    let const exponent    = ArithmeticValue::integer_part(rhs, arena);
    let const is_negative = exponent.is_negative();
    let const magnitude   = ArithmeticValue::absolute(exponent, arena);
    let const powered     = ArithmeticValue::power(lhs, magnitude, arena);

    if (is_negative)
      return bc_apply_binop('/', ArithmeticValue{1}, powered, scale, arena);

    /* at this point: magnitude holds the absolute value of an integer
       exponent, and is_negative is false. */
    let const exponent_count = magnitude.checked_i64();
    let const combined_scale = static_cast<u64>(lhs.get_decimal_scale()) *
                               static_cast<u64>(exponent_count);
    let desired_scale =
        lhs.get_decimal_scale() > scale ? lhs.get_decimal_scale() : scale;

    if (combined_scale < desired_scale)
      desired_scale = static_cast<u32>(combined_scale);

    return ArithmeticValue::rescale(powered, desired_scale, arena);
  }
  default: return arith_apply_binop(kind, lhs, rhs, true, arena);
  }
}

hot static fn
arith_apply_binop(char                   kind,
                  const ArithmeticValue &lhs,
                  const ArithmeticValue &rhs,
                  bool                   is_exact,
                  BumpArena             &arena,
                  Maybe<u32>             bc_scale) throws -> ArithmeticValue
{
  if (!is_exact) {
    let const left  = lhs.wrapped_i64();
    let const right = rhs.wrapped_i64();

    switch (kind) {
    case 'P':
      if (right < 0) {
        throw ErrorWithDetails{"Exponent less than 0",
                               "'**' requires a non-negative exponent"};
      }

      return ArithmeticValue{arithmetic_power(left, right)};
    case '*': return ArithmeticValue{arithmetic_multiply(left, right)};
    case '/':
      if (right == 0) throw_division_by_zero();

      return ArithmeticValue{arithmetic_divide(left, right)};
    case '%':
      if (right == 0) throw_division_by_zero();

      return ArithmeticValue{arithmetic_modulo(left, right)};
    case '+': return ArithmeticValue{arithmetic_add(left, right)};
    case '-': return ArithmeticValue{arithmetic_subtract(left, right)};
    case 'L': return ArithmeticValue{arithmetic_shift_left(left, right)};
    case 'R': return ArithmeticValue{arithmetic_shift_right(left, right)};
    case '<': return ArithmeticValue{left < right ? 1 : 0};
    case 'l': return ArithmeticValue{left <= right ? 1 : 0};
    case '>': return ArithmeticValue{left > right ? 1 : 0};
    case 'g': return ArithmeticValue{left >= right ? 1 : 0};
    case 'e': return ArithmeticValue{left == right ? 1 : 0};
    case 'n': return ArithmeticValue{left != right ? 1 : 0};
    case '&': return ArithmeticValue{left & right};
    case '^': return ArithmeticValue{left ^ right};
    case '|': return ArithmeticValue{left | right};
    default:
      unreachable("the cached arithmetic evaluator received invalid binary "
                  "operator '%c'",
                  kind);
    }
  }

  if (bc_scale.has_value() &&
      (kind == '*' || kind == '/' || kind == '%' || kind == 'P'))
  {
    return bc_apply_binop(kind, lhs, rhs, *bc_scale, arena);
  }

  if ((kind == '/' || kind == '%') && rhs.is_zero()) {
    throw_division_by_zero();
  }

  /* at this point: is_exact is true, bc_scale is absent or kind is not
     '*', '/', '%', or 'P', and rhs is nonzero when kind is '/' or '%'. */
  let const allocator = bump_allocator(arena);

  try {
    switch (kind) {
    case 'P': return ArithmeticValue::power(lhs, rhs, arena);
    case '*': return ArithmeticValue::multiply(lhs, rhs, arena);
    case '/': return ArithmeticValue::divide(lhs, rhs, arena);
    case '%': return ArithmeticValue::modulo(lhs, rhs, arena);
    case '+': return ArithmeticValue::add(lhs, rhs, arena);
    case '-': return ArithmeticValue::subtract(lhs, rhs, arena);
    case 'L': return ArithmeticValue::shift_left(lhs, rhs, arena);
    case 'R': return ArithmeticValue::shift_right(lhs, rhs, arena);
    case '<': return ArithmeticValue{lhs.compare(rhs, allocator) < 0 ? 1 : 0};
    case 'l': return ArithmeticValue{lhs.compare(rhs, allocator) <= 0 ? 1 : 0};
    case '>': return ArithmeticValue{lhs.compare(rhs, allocator) > 0 ? 1 : 0};
    case 'g': return ArithmeticValue{lhs.compare(rhs, allocator) >= 0 ? 1 : 0};
    case 'e': return ArithmeticValue{lhs.compare(rhs, allocator) == 0 ? 1 : 0};
    case 'n': return ArithmeticValue{lhs.compare(rhs, allocator) != 0 ? 1 : 0};
    case '&':
    case '^':
    case '|': return ArithmeticValue::bitwise(kind, lhs, rhs, arena);
    default:
      unreachable("the cached arithmetic evaluator received invalid binary "
                  "operator '%c'",
                  kind);
    }
  } catch (const std::bad_alloc &) {
    throw ErrorWithDetails{"The arithmetic result is too large",
                           "The result cannot fit in addressable memory"};
  }
}

class ArithmeticTokenTreeBuilder
{
public:
  ArithmeticTokenTreeBuilder(StringView                    source_value,
                             const ArrayList<arith_token> &tokens_value,
                             arith_tree_storage           &storage_value)
      : source{source_value}, toks{tokens_value}, storage{storage_value}
  {}

  fn
  build() throws -> u32
  {
    if (toks.is_empty()) return ARITH_NO_NODE;
    if (arith_tokens_are_single_step(toks)) return build_single_step();

    let const result = parse_binary(1);

    if (has_failed || ti == toks.count()) return result;

    let const error = fail_at_token(
        ti, "Unexpected '" + toks[ti].text + "' after the expression",
        "An operator is missing between two values");

    return sequence(result, error);
  }

private:
  StringView                    source;
  const ArrayList<arith_token> &toks;
  arith_tree_storage           &storage;
  usize                         ti{0};
  usize                         depth{0};
  usize                         binary_depth{0};
  bool                          has_failed{false};

  pure fn
  get_offset(const arith_token &token) const wontthrow -> u32
  {
    return static_cast<u32>(token.text.data - source.data);
  }

  fn
  fail_at_token(usize      token_index,
                StringView message,
                StringView note = {}) throws -> u32
  {
    let const &token =
        toks[token_index < toks.count() ? token_index : toks.count() - 1];

    arith_node node{};

    node.kind   = arith_node_kind::Error;
    node.start  = get_offset(token);
    node.length = static_cast<u32>(token.text.length);
    node.first  = storage.add_text(message);
    node.second = static_cast<u32>(message.length);
    node.third  = storage.add_text(note);
    node.end    = static_cast<u32>(note.length);
    has_failed  = true;

    return storage.add_node(node);
  }

  fn
  sequence(u32 first_index, u32 second_index) throws -> u32
  {
    arith_node node{};

    storage.nodes[first_index].next = second_index;
    node.kind                       = arith_node_kind::Comma;
    node.first                      = first_index;

    return storage.add_node(node);
  }

  fn
  add_unary(arith_node_kind kind, u32 operand) throws -> u32
  {
    arith_node node{};

    node.kind  = kind;
    node.first = operand;

    return storage.add_node(node);
  }

  fn
  add_name(const arith_token &token) throws -> u32
  {
    arith_node node{};

    node.kind   = arith_node_kind::Lvalue;
    node.start  = get_offset(token);
    node.length = static_cast<u32>(token.text.length);

    return storage.add_node(node);
  }

  pure fn
  at_op(StringView s) const wontthrow -> bool
  {
    return ti < toks.count() && toks[ti].k == arith_token::kind::op &&
           toks[ti].text == s;
  }

  fn
  build_single_step() throws -> u32
  {
    let const  is_prefix = toks[0].k == arith_token::kind::op;
    let const &step      = is_prefix ? toks[0] : toks[1];

    arith_node node{};

    node.first = add_name(is_prefix ? toks[1] : toks[0]);
    node.kind =
        is_prefix ? arith_node_kind::PrefixStep : arith_node_kind::PostfixStep;
    node.value = step.text[0] == '+' ? i64{1} : i64{-1};

    return storage.add_node(node);
  }

  fn
  parse_operand() throws -> u32
  {
    depth++;
    defer { depth--; };

    if (depth > MAX_PRIMARY_DEPTH) {
      return fail_at_token(ti, "Expression nested too deeply");
    }

    if (at_op("+")) {
      ti++;

      return parse_operand();
    }

    if (at_op("-")) {
      ti++;

      return add_unary(arith_node_kind::Negate, parse_operand());
    }

    if (at_op("!")) {
      ti++;

      return add_unary(arith_node_kind::Not, parse_operand());
    }

    if (at_op("~")) {
      ti++;

      return add_unary(arith_node_kind::BitNot, parse_operand());
    }

    if (at_op("(")) {
      ti++;

      let const value = parse_binary(1);

      if (has_failed) return value;
      if (!at_op(")")) {
        return sequence(value, fail_at_token(ti, "Expected ')'",
                                             "An opening '(' is unmatched"));
      }

      ti++;

      return value;
    }

    if (ti < toks.count() && toks[ti].k == arith_token::kind::number) {
      arith_node node{};

      node.kind   = arith_node_kind::Number;
      node.start  = get_offset(toks[ti]);
      node.length = static_cast<u32>(toks[ti].text.length);
      node.value  = toks[ti].value;
      if (is_plain_decimal_operand(toks[ti].text))
        node.flags |= arith_node::PLAIN_NUMBER_FLAG;
      ti++;

      return storage.add_node(node);
    }

    if (ti < toks.count() && toks[ti].k == arith_token::kind::name) {
      arith_node node{};

      node.first = add_name(toks[ti]);
      node.kind  = arith_node_kind::Read;
      ti++;

      return storage.add_node(node);
    }

    if (ti >= toks.count()) {
      return fail_at_token(ti, "Unfinished expression",
                           "An operand is missing");
    }

    return fail_at_token(ti,
                         "Unexpected character in the arithmetic expression",
                         "This is not a valid operator or operand");
  }

  fn
  parse_binary(u8 min_precedence) throws -> u32
  {
    binary_depth++;
    defer { binary_depth--; };

    if (binary_depth > MAX_OPERATOR_CHAIN_DEPTH) {
      return fail_at_token(ti, "Expression nested too deeply",
                           OPERATOR_CHAIN_HINT);
    }

    let const lhs = parse_operand();

    if (has_failed) return lhs;

    let chain     = ARITH_NO_NODE;
    let last_step = ARITH_NO_NODE;

    loop {
      if (ti >= toks.count() || toks[ti].k != arith_token::kind::op) {
        return chain == ARITH_NO_NODE ? lhs : chain;
      }

      let const op = arith_unpacked_binop(toks[ti].value);

      if (op.precedence < min_precedence) {
        return chain == ARITH_NO_NODE ? lhs : chain;
      }

      arith_node step{};

      step.kind   = arith_node_kind::ChainStep;
      step.op     = op.kind;
      step.start  = get_offset(toks[ti]);
      step.length = static_cast<u32>(toks[ti].text.length);
      ti++;
      step.first = parse_binary(
          op.kind == 'P' ? op.precedence : static_cast<u8>(op.precedence + 1));

      if (ti < toks.count() &&
          (toks[ti].k != arith_token::kind::op ||
           (toks[ti].text != ")" &&
            arith_unpacked_binop(toks[ti].value).precedence == 0)))
      {
        step.flags |= arith_node::TAIL_FLAG;
        step.middle = get_offset(toks[ti]);
        step.end    = static_cast<u32>(toks[ti].text.length);
      }

      let const step_index = storage.add_node(step);

      if (chain == ARITH_NO_NODE) {
        arith_node node{};

        node.kind   = arith_node_kind::Chain;
        node.first  = lhs;
        node.second = step_index;
        chain       = storage.add_node(node);
      } else {
        storage.nodes[last_step].next = step_index;
      }

      last_step = step_index;
      if (has_failed) return chain;
    }
  }
};

pure fn
get_arith_tree_slot_count(u32 node_count, u32 text_length) wontthrow -> usize
{
  return 1 + usize{node_count} +
         (usize{text_length} + sizeof(arith_node) - 1) / sizeof(arith_node);
}

fn
get_constant_arithmetic_arena() wontthrow -> BumpArena &
{
  static thread_local BumpArena arena;

  return arena;
}

fn
build_tree_into(StringView          expression,
                arith_tree_storage &storage,
                BumpArena          &arena) throws -> arith_tree
{
  let tree = arith_tree{};

  tree.needs_expansion = expression.find_character('$').has_value() ||
                         expression.find_character('`').has_value() ||
                         expression.find_character('\\').has_value() ||
                         expression.find_character('"').has_value();
  if (tree.needs_expansion) return tree;

  let tokens = ArrayList<arith_token>{bump_allocator(arena)};
  let root   = ARITH_NO_NODE;

  try {
    tokenize_arithmetic(expression, tokens);
    tree.is_simple = arith_tokens_are_simple(tokens);
  } catch (...) {
    tree.is_simple = false;
  }

  if (tree.is_simple) {
    root = ArithmeticTokenTreeBuilder{expression, tokens, storage}.build();
  } else {
    root =
        ArithmeticTreeBuilder{expression, false, false, storage, arena}.build();
  }

  let exact_root = root;

  if (!tree.is_simple && expression.find_character('.').has_value()) {
    exact_root =
        ArithmeticTreeBuilder{expression, true, false, storage, arena}.build();
  }

  let const is_simple = tree.is_simple;
  tree                = storage.get_view(root);
  tree.exact_root     = exact_root;
  tree.is_simple      = is_simple;

  return tree;
}

static fn
evaluate_text_tree(EvalContext              *context,
                   StringView                source,
                   bool                      is_exact,
                   BumpArena                &arena,
                   const arith_text_options &options) throws
    -> ArithmeticValue
{
  let storage = arith_tree_storage{bump_allocator(arena)};

  let builder = ArithmeticTreeBuilder{
      source, is_exact, options.should_error_unset, storage, arena};

  builder.expanded_subscripts = options.expanded_subscripts;

  let const root = builder.build();
  let const tree = storage.get_view(root);

  ArithmeticTreeEvaluator evaluator{context,
                                    source,
                                    tree,
                                    is_exact,
                                    arena,
                                    options.depth,
                                    options.is_skipping,
                                    options.bc_scale};

  evaluator.should_error_unset = options.should_error_unset;
  evaluator.is_variable_value  = options.is_variable_value;
  if (options.precise_base != nullptr) {
    evaluator.precise_base = *options.precise_base;
  }

  return evaluator.run(root);
}

} /* namespace */

fn
arith_tree::copy_to(Allocator allocator) const throws
    -> const arith_tree *
{
  static_assert(sizeof(arith_tree) <= sizeof(arith_node));
  static_assert(alignof(arith_tree) <= alignof(arith_node));

  let const slot_count = get_arith_tree_slot_count(node_count, text_length);
  let const block      = allocator.alloc_array<arith_node>(slot_count);
  let const copy       = new (static_cast<void *>(block)) arith_tree{*this};
  let const node_block = block + 1;
  let const text_block = reinterpret_cast<char *>(node_block + node_count);

  copy->is_heap = allocator.get_kind() == Allocator::Kind::Heap;
  copy->nodes   = node_count > 0 ? node_block : nullptr;
  copy->text    = text_length > 0 ? text_block : nullptr;

  if (node_count > 0) {
    memcpy(static_cast<void *>(node_block), nodes,
           node_count * sizeof(arith_node));
  }

  if (text_length > 0) memcpy(text_block, text, text_length);

  return copy;
}

fn
arith_tree::release(const arith_tree *tree) wontthrow -> void
{
  if (tree == nullptr || !tree->is_heap) return;

  heap_allocator().free_array(
      reinterpret_cast<arith_node *>(const_cast<arith_tree *>(tree)),
      get_arith_tree_slot_count(tree->node_count, tree->text_length));
}

fn
build_arithmetic_tree(StringView expression, Allocator allocator) throws
    -> const arith_tree *
{
  static thread_local arith_tree_storage storage{heap_allocator()};

  let      &arena   = get_constant_arithmetic_arena();
  let const scratch = arena.mark();

  defer { arena.release(scratch); };

  storage.nodes.clear();
  storage.text.clear();

  let const tree = build_tree_into(expression, storage, arena);

  return tree.copy_to(allocator);
}

fn
EvalContext::read_array_element_arithmetic_text(
    StringView name, StringView subscript) throws -> String
{
  let const array_name = resolve_subscripted_array_name(*this, name);

  return apply_array_subscript(array_name.view(), subscript);
}

namespace {

fn
evaluate_arithmetic_value(
    EvalContext          *context,
    StringView            expression,
    const SourceLocation *expression_base,
    bool                  is_exact,
    BumpArena            &arena,
    arithmetic_text_kind  text_kind          = arithmetic_text_kind::Value,
    bool                  should_error_unset = false,
    Maybe<u32>            bc_scale           = {}) throws -> ArithmeticValue
{
  LOG(All, "evaluating the arithmetic expression of %zu bytes",
      expression.length);

  if (text_kind == arithmetic_text_kind::ShellSource &&
      expression.find_character('\\').has_value())
  {
    let joined = String{bump_allocator(arena)};

    for (usize position = 0; position < expression.length; position++) {
      if (expression[position] == '\\' && position + 1 < expression.length &&
          expression[position + 1] == '\n')
      {
        position++;
      } else {
        joined.push(expression[position]);
      }
    }

    if (joined.count() != expression.length) {
      return evaluate_arithmetic_value(context, joined.view(), nullptr,
                                       is_exact, arena, text_kind,
                                       should_error_unset, bc_scale);
    }
  }

  let const has_removable_quote =
      text_kind == arithmetic_text_kind::ShellSource &&
      context->runtime_state().bash_additions_enabled() &&
      expression.find_character('"').has_value();

  /* a variable's value is data and is parsed without expansion, and a
     '$' or '`' in an operand taken from a value is rejected. */
  let const is_plain_text = !expression.find_character('$').has_value() &&
                            !expression.find_character('`').has_value() &&
                            !has_removable_quote;

  let options = arith_text_options{};

  options.bc_scale           = bc_scale;
  options.should_error_unset = should_error_unset;

  if (text_kind == arithmetic_text_kind::Value || is_plain_text) {
    options.precise_base = expression_base;

    return evaluate_text_tree(context, expression, is_exact, arena, options);
  }

  /* at this point: text_kind is ShellSource and expression has a '$', a
     '`', or a removable double quote, so the expansion runs before the
     parse. */
  LOG(All, "expanding parameters inside the arithmetic before the parse");

  let subscript_spans = ArrayList<expanded_subscript_span>{heap_allocator()};
  let const expanded_word = expand_arithmetic_source(
      context, expression, expression_base, subscript_spans);

  options.expanded_subscripts = &subscript_spans;

  return evaluate_text_tree(context, expanded_word.view(), is_exact, arena,
                            options);
}

fn
evaluate_stored_tree(EvalContext          *context,
                     StringView            expression,
                     const arith_tree     *stored_tree,
                     const SourceLocation *source_location,
                     bool                  is_exact,
                     BumpArena            &arena) throws -> ArithmeticValue;

cold fn
evaluate_unstored_tree(EvalContext          *context,
                       StringView            expression,
                       const SourceLocation *source_location,
                       bool                  is_exact,
                       BumpArena            &arena) throws -> ArithmeticValue
{
  let       storage = arith_tree_storage{bump_allocator(arena)};
  let const tree    = build_tree_into(expression, storage, arena);

  return evaluate_stored_tree(context, expression, &tree, source_location,
                              is_exact, arena);
}

hot fn
evaluate_stored_tree(EvalContext          *context,
                     StringView            expression,
                     const arith_tree     *stored_tree,
                     const SourceLocation *source_location,
                     bool                  is_exact,
                     BumpArena            &arena) throws -> ArithmeticValue
{
  if (stored_tree == nullptr) rarely {
    return evaluate_unstored_tree(context, expression, source_location,
                                  is_exact, arena);
  }

  if (stored_tree->needs_expansion) {
    return evaluate_arithmetic_value(context, expression, source_location,
                                     is_exact, arena,
                                     arithmetic_text_kind::ShellSource);
  }

  ArithmeticTreeEvaluator evaluator{context, expression, *stored_tree, is_exact,
                                    arena};

  if (source_location != nullptr) evaluator.precise_base = *source_location;

  return evaluator.run(stored_tree->get_root(is_exact));
}

} /* namespace */

fn
EvalContext::evaluate_arithmetic(
    StringView            expression,
    const SourceLocation *expression_base,
    bool                  should_discard_top_level_line) throws -> i64
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  let const is_exact = runtime_state().is_extended_arithmetic_enabled();

  try {
    let const value = evaluate_arithmetic_value(
        this, expression, expression_base, is_exact,
        expansion_store().scratch_arena(), arithmetic_text_kind::ShellSource);

    return is_exact ? value.checked_i64() : value.wrapped_i64();
  } catch (ErrorBase &error) {
    let const was_marked =
        error.is_line_discarding() || error.is_script_fatal();
    mark_expansion_error(error, expansion_error_reach::Line);
    if (should_discard_top_level_line && !was_marked &&
        error.is_line_discarding())
    {
      error.set_top_level_line_discarding();
    }

    throw;
  }
}

fn
EvalContext::evaluate_arithmetic_text(
    StringView            expression,
    const SourceLocation *expression_base,
    arithmetic_text_kind  text_kind) throws -> String
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  let const is_exact = runtime_state().is_extended_arithmetic_enabled();

  let const value =
      evaluate_arithmetic_value(this, expression, expression_base, is_exact,
                                expansion_store().scratch_arena(), text_kind);

  return value.to_string(heap_allocator());
}

fn
EvalContext::evaluate_calculator_arithmetic_text(
    StringView expression, const SourceLocation *expression_base) throws
    -> String
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  const SourceLocation synthetic_base{0, 0};
  let const            base =
      expression_base != nullptr ? expression_base : &synthetic_base;
  let const value = evaluate_arithmetic_value(
      this, expression, base, true, expansion_store().scratch_arena(),
      arithmetic_text_kind::Value, true);

  return value.to_string(heap_allocator());
}

fn
EvalContext::evaluate_bc_arithmetic_text(StringView expression,
                                         u32        scale) throws -> String
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  const SourceLocation expression_base{0, 0};
  let const            value =
      evaluate_arithmetic_value(this, expression, &expression_base, true,
                                expansion_store().scratch_arena(),
                                arithmetic_text_kind::Value, true, scale);

  return value.to_string(heap_allocator());
}

fn
EvalContext::evaluate_arithmetic_nonzero(
    StringView            expression,
    const SourceLocation *expression_base,
    arithmetic_text_kind  text_kind) throws -> bool
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  let const is_exact = runtime_state().is_extended_arithmetic_enabled();

  return !evaluate_arithmetic_value(this, expression, expression_base, is_exact,
                                    expansion_store().scratch_arena(),
                                    text_kind)
              .is_zero();
}

fn
EvalContext::compare_arithmetic(
    StringView            left,
    const SourceLocation *left_source_base,
    StringView            right,
    const SourceLocation *right_source_base) throws -> i32
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  let const is_exact = runtime_state().is_extended_arithmetic_enabled();
  let const do_kind  = [](const SourceLocation *source_base) {
    return source_base != nullptr ? arithmetic_text_kind::ShellSource
                                   : arithmetic_text_kind::Value;
  };
  let const left_value = evaluate_arithmetic_value(
      this, left, left_source_base, is_exact, expansion_store().scratch_arena(),
      do_kind(left_source_base));
  let const right_value = evaluate_arithmetic_value(
      this, right, right_source_base, is_exact,
      expansion_store().scratch_arena(), do_kind(right_source_base));

  return left_value.compare(right_value, scratch_allocator());
}

fn
EvalContext::evaluate_arithmetic_segment_text(
    const WordSegment &segment) throws -> String
{
  let const source_location = segment.get_source_location(
      source_store().current_location().source_name_index);
  let cache_arena = segment.is_substitution_cache_in_function_arena
                        ? arena_store().function_arena()
                        : arena_store().parse_arena();

  if (cache_arena == nullptr) {
    try {
      return evaluate_arithmetic_text(
          segment.text.view(),
          source_location.has_value() ? &*source_location : nullptr,
          arithmetic_text_kind::ShellSource);
    } catch (ErrorBase &error) {
      mark_expansion_error(error, expansion_error_reach::LineOrPosixScript);
      throw;
    }
  }

  let const is_exact = runtime_state().is_extended_arithmetic_enabled();

  if (is_exact) {
    if (let const constant_text =
            segment.get_exact_constant_arithmetic_text(*cache_arena);
        constant_text.has_value())
    {
      return String{scratch_allocator(), *constant_text};
    }
  }

  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  try {
    return evaluate_stored_tree(
               this, segment.text.view(), segment.get_arithmetic_tree(),
               source_location.has_value() ? &*source_location : nullptr,
               is_exact, expansion_store().scratch_arena())
        .to_string(heap_allocator());
  } catch (ErrorBase &error) {
    mark_expansion_error(error, expansion_error_reach::LineOrPosixScript);
    throw;
  }
}

fn
EvalContext::evaluate_arithmetic_tree_nonzero(
    StringView            expression,
    const arith_tree     *tree,
    const SourceLocation *source_location) throws -> bool
{
  let const scratch = expansion_store().scratch_arena().mark();

  defer { expansion_store().scratch_arena().release(scratch); };

  let const is_exact = runtime_state().is_extended_arithmetic_enabled();

  return !evaluate_stored_tree(this, expression, tree, source_location,
                               is_exact, expansion_store().scratch_arena())
              .is_zero();
}

fn
evaluate_constant_arithmetic(StringView expression) throws -> i64
{
  let      &arena   = get_constant_arithmetic_arena();
  let const scratch = arena.mark();

  defer { arena.release(scratch); };

  return evaluate_text_tree(nullptr, expression, false, arena,
                            arith_text_options{})
      .wrapped_i64();
}

fn
find_arithmetic_syntax_problem(StringView expression,
                               bool       is_exact) throws
    -> Maybe<arithmetic_syntax_problem>
{
  let      &arena   = get_constant_arithmetic_arena();
  let const scratch = arena.mark();

  defer { arena.release(scratch); };

  const SourceLocation base{0, 0};
  let                  problem = arithmetic_syntax_problem{};

  let storage = arith_tree_storage{bump_allocator(arena)};
  let builder =
      ArithmeticTreeBuilder{expression, is_exact, false, storage, arena};

  unused(builder.build());
  if (!builder.get_has_failed()) return None;

  try {
    let const tree = storage.get_view(ARITH_NO_NODE);

    ArithmeticTreeEvaluator evaluator{nullptr, expression, tree, is_exact,
                                      arena,   0,          true};

    evaluator.precise_base = base;
    evaluator.throw_failure(builder.get_failed_node());
  } catch (const ErrorWithLocation &error) {
    problem.position = error.location().position;
    problem.length   = error.location().length;
    problem.message  = String{heap_allocator(), error.message().view()};
    problem.note     = String{heap_allocator(), error.detail_message()};
  } catch (const ErrorBase &error) {
    problem.length  = expression.length;
    problem.message = String{heap_allocator(), error.message().view()};
    problem.note    = String{heap_allocator(), error.detail_message()};
  }

  return problem;
}

fn
evaluate_constant_arithmetic_nonzero(StringView expression,
                                     bool       is_exact) throws -> bool
{
  let      &arena   = get_constant_arithmetic_arena();
  let const scratch = arena.mark();

  defer { arena.release(scratch); };

  return !evaluate_text_tree(nullptr, expression, is_exact, arena,
                             arith_text_options{})
              .is_zero();
}

fn
evaluate_constant_arithmetic_text(StringView expression,
                                  Allocator  allocator) throws -> String
{
  let      &arena   = get_constant_arithmetic_arena();
  let const scratch = arena.mark();

  defer { arena.release(scratch); };

  return evaluate_text_tree(nullptr, expression, true, arena,
                            arith_text_options{})
      .to_string(allocator);
}

pure fn
obvious_xor_power_operator_position(StringView expression) wontthrow
    -> Maybe<usize>
{
  usize position = 0;

  let const do_skip = [&](bool (*is_member)(char)) wontthrow -> usize {
    let const start_position = position;

    while (position < expression.length && is_member(expression[position])) {
      position++;
    }

    return position - start_position;
  };

  do_skip(lexer::is_whitespace);
  if (do_skip(lexer::is_number) == 0) return None;
  do_skip(lexer::is_whitespace);
  if (position >= expression.length || expression[position] != '^') {
    return None;
  }

  let const operator_position = position++;

  if (position < expression.length && expression[position] == '=') {
    return None;
  }

  do_skip(lexer::is_whitespace);
  if (do_skip(lexer::is_number) == 0) return None;
  do_skip(lexer::is_whitespace);
  if (position != expression.length) return None;

  /* at this point: expression holds a number, '^', and a number, with
     optional whitespace around them, and operator_position indexes the '^'. */
  return operator_position;
}

} /* namespace koshka */
