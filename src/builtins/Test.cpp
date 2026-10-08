/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the test and bracket expression grammar, including
 * string, integer, filesystem, option, logical, and parenthesized operators
 * with shell-specific argument-count rules.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("expression", "[ expression ]");

HELP_DESCRIPTION_DECL(
    "The test builtin evaluates an expression and reports true or false.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Test);

namespace koshka {

namespace {

fn parse_integer(StringView text) throws -> Maybe<i64>
{
  bool is_out_of_range = false;
  let const parsed = utils::parse_decimal_i64(text, &is_out_of_range);
  if (parsed.is_error() || is_out_of_range) return None;

  return parsed.value();
}

enum class binary_operator : u8
{
  StringEqual,
  StringEqualBashism,
  StringNotEqual,
  StringLess,
  StringGreater,
  SameFile,
  NewerFile,
  OlderFile,
  IntegerEqual,
  IntegerNotEqual,
  IntegerLess,
  IntegerLessOrEqual,
  IntegerGreater,
  IntegerGreaterOrEqual,
};

constexpr static_string_entry<binary_operator> BINARY_OPERATOR_ENTRIES[] = {
    {SSK("="),   binary_operator::StringEqual          },
    {SSK("=="),  binary_operator::StringEqualBashism   },
    {SSK("!="),  binary_operator::StringNotEqual       },
    {SSK("<"),   binary_operator::StringLess           },
    {SSK(">"),   binary_operator::StringGreater        },
    {SSK("-ef"), binary_operator::SameFile             },
    {SSK("-nt"), binary_operator::NewerFile            },
    {SSK("-ot"), binary_operator::OlderFile            },
    {SSK("-eq"), binary_operator::IntegerEqual         },
    {SSK("-ne"), binary_operator::IntegerNotEqual      },
    {SSK("-lt"), binary_operator::IntegerLess          },
    {SSK("-le"), binary_operator::IntegerLessOrEqual   },
    {SSK("-gt"), binary_operator::IntegerGreater       },
    {SSK("-ge"), binary_operator::IntegerGreaterOrEqual},
};
constexpr StaticStringMap BINARY_OPERATORS{BINARY_OPERATOR_ENTRIES};

class TestEvaluator
{
public:
  const ExecContext &ec;
  const EvalContext &cxt;
  const ArrayList<String> &args;
  usize pos;
  usize end;
  bool is_bash_compatible;
  bool has_name_reference_test;

  pure const String &current() const wontthrow
  {
    ASSERT(pos < end);
    return args[pos];
  }
  pure bool at_end() const wontthrow { return pos >= end; }

  void fail(StringView message) throws
  {
    let error = make_error_for_arg(ec, pos, message);
    error.set_command_status(2);
    throw error;
  }

  bool evaluate_unary(const String &op, const String &operand) throws
  {
    if (op == "-z") return operand.is_empty();
    if (op == "-n") return !operand.is_empty();
    if (op == "-R") return cxt.is_bound_nameref(operand.view());

    let const operand_path = Path{operand};

    if (op == "-s") {
      let const size = operand_path.file_size();
      return size.has_value() && size.value() > 0;
    }
    using file_predicate = bool (Path::*)() const;
    static constexpr static_string_entry<file_predicate> ENTRIES[] = {
        {SSK("-e"), &Path::exists                     },
        {SSK("-f"), &Path::is_regular_file            },
        {SSK("-d"), &Path::is_directory               },
        {SSK("-r"), &Path::is_readable                },
        {SSK("-w"), &Path::is_writable                },
        {SSK("-x"), &Path::is_executable              },
        {SSK("-L"), &Path::is_symbolic_link           },
        {SSK("-h"), &Path::is_symbolic_link           },
        {SSK("-b"), &Path::is_block_device            },
        {SSK("-c"), &Path::is_character_device        },
        {SSK("-p"), &Path::is_fifo                    },
        {SSK("-S"), &Path::is_socket                  },
        {SSK("-g"), &Path::has_setgid_bit             },
        {SSK("-u"), &Path::has_setuid_bit             },
        {SSK("-k"), &Path::has_sticky_bit             },
        {SSK("-O"), &Path::is_owned_by_effective_user },
        {SSK("-G"), &Path::is_owned_by_effective_group},
    };
    static constexpr StaticStringMap FILE_TESTS{ENTRIES};

    if (let const predicate = FILE_TESTS.find(op.view()); predicate.has_value())
      return (operand_path.*(*predicate))();

    if (op == "-t") {
      let const file_descriptor = parse_integer(operand.view());
      if (!file_descriptor.has_value()) return false;
      return os::shell_fd_is_a_tty(static_cast<int>(*file_descriptor));
    }
    fail(
        StringView{"'"} + op +
        "' is not a known unary operator, expected one of -z -n -e -f -d -s -r "
        "-w -x -L -h -b -c -p -S -g -u -k -O -G -t -R");
    return false;
  }

  bool evaluate_binary(const String &left, const String &op,
                       const String &right) throws
  {
    let const found = BINARY_OPERATORS.find(op.view());
    if (!found.has_value()) {
      fail(StringView{"'"} + op +
           "' is not a known binary operator, expected one of = != < > -eq "
           "-ne -lt -le -gt -ge -ef -nt -ot");
      return false;
    }

    switch (*found) {
    case binary_operator::StringEqualBashism:
      if (!is_bash_compatible) {
        fail("'==' is a bashism, use = for string equality in POSIX mode");
        return false;
      }
      return left == right;
    case binary_operator::StringEqual: return left == right;
    case binary_operator::StringNotEqual: return left != right;
    case binary_operator::StringLess: return left < right;
    case binary_operator::StringGreater: return right < left;
    case binary_operator::SameFile:
      return Path{left}.is_same_file_as(Path{right});
    case binary_operator::NewerFile:
      return Path{left}.is_newer_than(Path{right});
    case binary_operator::OlderFile:
      return Path{left}.is_older_than(Path{right});
    default: break;
    }

    let const left_number = parse_integer(left);
    let const right_number = parse_integer(right);
    if (!left_number.has_value() || !right_number.has_value()) {
      let const &not_a_number = left_number.has_value() ? right : left;
      fail(StringView{"Cannot compare with '"} + op + "', '" + not_a_number +
           "' is not an integer");
      return false;
    }

    switch (*found) {
    case binary_operator::IntegerEqual: return *left_number == *right_number;
    case binary_operator::IntegerNotEqual: return *left_number != *right_number;
    case binary_operator::IntegerLess: return *left_number < *right_number;
    case binary_operator::IntegerLessOrEqual:
      return *left_number <= *right_number;
    case binary_operator::IntegerGreater: return *left_number > *right_number;
    default: return *left_number >= *right_number;
    }
  }

  pure fn is_unary_operator(const String &s) const wontthrow -> bool
  {
    static constexpr PackedStringKey KEYS[] = {
        SSK("-z"), SSK("-n"), SSK("-e"), SSK("-f"), SSK("-d"), SSK("-s"),
        SSK("-r"), SSK("-w"), SSK("-x"), SSK("-L"), SSK("-h"), SSK("-b"),
        SSK("-c"), SSK("-p"), SSK("-S"), SSK("-g"), SSK("-u"), SSK("-k"),
        SSK("-O"), SSK("-G"), SSK("-t"),
    };
    static constexpr StaticStringSet UNARY_OPS{KEYS};
    if (has_name_reference_test && s == "-R") return true;

    return UNARY_OPS.contains(s.view());
  }

  pure fn is_binary_operator(const String &s) const wontthrow -> bool
  {
    return BINARY_OPERATORS.find(s.view()).has_value();
  }

  pure bool is_unary_in_operand_position(usize index) const wontthrow
  {
    if (index + 1 >= end) return true;
    if (index + 2 >= end) return false;
    return is_binary_operator(args[index + 1]);
  }

  pure bool is_open_paren_token(usize index) const wontthrow
  {
    return args[index] == "(" && index + 1 < end;
  }

  pure bool is_unary_operator_token(usize index) const wontthrow
  {
    return is_unary_operator(args[index]) &&
           !is_unary_in_operand_position(index);
  }

  bool parse_factor() throws
  {
    if (at_end()) {
      fail("An argument is expected");
      return false;
    }
    if (current() == "!") {
      if (pos + 1 >= end) {
        pos++;
        return true;
      }
      pos++;
      return !parse_factor();
    }
    if (is_open_paren_token(pos)) {
      pos++;
      let const result = parse_expression();
      if (at_end() || current() != ")") {
        fail("A ')' is expected");
      } else
        pos++;
      return result;
    }
    if (is_unary_operator_token(pos)) {
      let const &op = args[pos];
      let const &operand = args[pos + 1];
      pos += 2;
      return evaluate_unary(op, operand);
    }
    if (pos + 1 < end && is_binary_operator(args[pos + 1])) {
      if (pos + 2 >= end) {
        fail(StringView{"An argument is expected after '"} + args[pos + 1] +
             "'");
        pos = end;
        return false;
      }
      let const &left = args[pos];
      let const &op = args[pos + 1];
      let const &right = args[pos + 2];
      pos += 3;
      return evaluate_binary(left, op, right);
    }
    let const result = !current().is_empty();
    pos++;
    return result;
  }

  bool parse_term() throws
  {
    let is_true = parse_factor();
    while (!at_end() && current() == "-a") {
      pos++;
      let const right = parse_factor();
      is_true = is_true && right;
    }
    return is_true;
  }

  bool parse_expression() throws
  {
    let is_true = parse_term();
    while (!at_end() && current() == "-o") {
      pos++;
      let const right = parse_term();
      is_true = is_true || right;
    }
    return is_true;
  }

  bool evaluate_with_posix_argument_count_rules() throws
  {
    let should_negate = false;
    loop
    {
      let const count = end - pos;
      if (count < 1) return !should_negate;

      if (count == 3 && is_binary_operator(args[pos + 1])) {
        let const result =
            evaluate_binary(args[pos], args[pos + 1], args[pos + 2]);
        pos = end;
        return should_negate ? !result : result;
      }

      if (count == 3 || count == 4) {
        if (args[pos] == "(" && args[end - 1] == ")") {
          pos++;
          end--;
          continue;
        }
        if (args[pos] == "!") {
          should_negate = !should_negate;
          pos++;
          continue;
        }
      }
      break;
    }

    let const result = parse_expression();
    return should_negate ? !result : result;
  }
};

} /* namespace */

fn Test::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &arguments = ec.args();
  ASSERT(!arguments.is_empty());

  if (arguments.count() == 2 && arguments[1] == "--help" &&
      ec.program() != "[" && !cxt.runtime_state().is_posix_mode() &&
      !cxt.runtime_state().is_bash_compatible())
  {
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  }

  usize expression_end = arguments.count();
  if (ec.program() == "[") {
    if (arguments.count() < 2 || arguments[arguments.count() - 1] != "]") {
      let error = Error{"The closing ']' is missing"};
      error.set_command_status(2);
      throw error;
    }
    expression_end = arguments.count() - 1;
  }

  if (expression_end <= 1) return 1;

  LOG(All, "test evaluating %zu operands", expression_end - 1);

  let evaluator = TestEvaluator{ec,
                                cxt,
                                arguments,
                                1,
                                expression_end,
                                cxt.runtime_state().is_bash_compatible(),
                                cxt.runtime_state().bash_additions_enabled()};
  let const result = evaluator.evaluate_with_posix_argument_count_rules();
  if (evaluator.pos != evaluator.end) {
    ASSERT(evaluator.pos < evaluator.end);
    let error = make_error_for_arg(
        ec, evaluator.pos,
        StringView{"'"} + arguments[evaluator.pos] +
            "' is an unexpected argument",
        "A test takes an operator or an operand at this position");
    error.set_command_status(2);
    throw error;
  }
  return result ? 0 : 1;
}

} /* namespace koshka */
