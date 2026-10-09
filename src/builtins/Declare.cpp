/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements variable and function declaration, attribute changes,
 * array creation, reusable declaration output, virtual-array printing, and
 * function-name queries. These operations stay together because declare uses
 * one attribute grammar for mutation, filtering, and reusable output.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Formatter.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAfFgilnprtux] [+i] [name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The declare builtin declares variables and sets their attributes.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(DECLARE_INDEXED, Bool, 'a', "", "Declare an indexed array.");
FLAG(DECLARE_ASSOCIATIVE, Bool, 'A', "", "Declare an associative array.");
FLAG(DECLARE_FUNCTIONS, Bool, 'f', "",
     "Restrict to functions and print their recorded definitions.");
FLAG(DECLARE_FUNCTION_NAMES, Bool, 'F', "",
     "Print only the names of defined functions and report their existence "
     "through the exit status.");
FLAG(DECLARE_GLOBAL, Bool, 'g', "", "Accepted without effect.");
FLAG(DECLARE_INTEGER, Bool, 'i', "",
     "Mark the variable as integer. Evaluate each assignment as arithmetic. "
     "The +i form removes the mark.");
FLAG(DECLARE_LOWERCASE, Bool, 'l', "",
     "Convert every assigned value to lowercase. The +l form removes the "
     "attribute.");
FLAG(DECLARE_NAMEREF, Bool, 'n', "",
     "Make the variable a reference to the variable its value selects. The +n "
     "form removes the reference and keeps the value.");
FLAG(DECLARE_PRINT, Bool, 'p', "", "Print the matching declarations.");
FLAG(DECLARE_READONLY, Bool, 'r', "", "Accepted without effect.");
FLAG(DECLARE_TRACE, Bool, 't', "", "Accepted without effect.");
FLAG(DECLARE_UPPERCASE, Bool, 'u', "",
     "Convert every assigned value to uppercase. The +u form removes the "
     "attribute.");
FLAG(DECLARE_EXPORT, Bool, 'x', "", "Mark the variable for the environment.");

REGISTER_BUILTIN_FLAGS(Declare);

namespace koshka {

fn Declare::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  let should_make_associative = false;
  let should_make_indexed = false;
  let should_export = false;
  let should_unexport = false;
  let should_print = false;
  let should_mark_integer_attribute = false;
  let should_unmark_integer_attribute = false;
  let should_mark_lowercase_attribute = false;
  let should_unmark_lowercase_attribute = false;
  let should_mark_uppercase_attribute = false;
  let should_unmark_uppercase_attribute = false;
  let should_mark_readonly = false;
  let should_mark_nameref = false;
  let should_unmark_nameref = false;
  let should_restrict_to_functions = false;
  let should_print_function_names_only = false;
  let should_be_global = false;

  usize i = 1;
  i32 status = 0;
  for (; i < args.count(); i++) {
    let const arg = args[i].view();
    if (arg.length < 1 || (arg[0] != '-' && arg[0] != '+')) {
      break;
    }
    if (arg == "--") {
      i++;
      break;
    }
    let const is_remove_form = arg[0] == '+';
    for (usize c = 1; c < arg.length; c++) {
      switch (arg[c]) {
      case 'A': should_make_associative = true; break;
      case 'a': should_make_indexed = true; break;
      case 'x':
        if (is_remove_form)
          should_unexport = true;
        else
          should_export = true;
        break;
      case 'p': should_print = true; break;
      case 'i':
        if (is_remove_form)
          should_unmark_integer_attribute = true;
        else
          should_mark_integer_attribute = true;
        break;
      case 'l':
        if (is_remove_form)
          should_unmark_lowercase_attribute = true;
        else
          should_mark_lowercase_attribute = true;
        break;
      case 'u':
        if (is_remove_form)
          should_unmark_uppercase_attribute = true;
        else
          should_mark_uppercase_attribute = true;
        break;
      case 'f': should_restrict_to_functions = true; break;
      case 'F':
        should_restrict_to_functions = true;
        should_print_function_names_only = true;
        break;
      case 'r':
        if (!is_remove_form) should_mark_readonly = true;
        break;
      case 'g': should_be_global = true; break;
      case 'n':
        if (is_remove_form)
          should_unmark_nameref = true;
        else
          should_mark_nameref = true;
        break;
      case 't': break;
      default: {
        let invalid = String{cxt.scratch_allocator()};
        invalid += arg[0];
        invalid += arg[c];
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                  "'" + invalid +
                                      "' is not a valid declare option");
        return 2;
      }
      }
    }
  }

  if (should_make_associative && should_make_indexed) {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              "'-a' and '-A' cannot be used together");
    return 2;
  }

  if (should_mark_lowercase_attribute && should_mark_uppercase_attribute) {
    should_mark_lowercase_attribute = false;
    should_mark_uppercase_attribute = false;
    should_unmark_lowercase_attribute = true;
    should_unmark_uppercase_attribute = true;
  }

  if (should_restrict_to_functions) {
    i32 status = 0;
    if (i >= args.count()) {
      for (let const &name : cxt.function_store().sorted_names()) {
        let line = String{cxt.scratch_allocator()};
        if (should_print_function_names_only) {
          line += "declare -f ";
          line.append(name.view());
        } else if (const String *source =
                       cxt.function_store().find_source(name.view());
                   source != nullptr)
        {
          line.append(format_bash_function_source(source->view()).view());
        }
        line += '\n';
        ec.print_to_stdout(line.view());
      }
      return 0;
    }
    for (; i < args.count(); i++) {
      let const name = args[i].view();
      if (!cxt.function_store().find_function(name).has_value()) {
        if (!should_print_function_names_only)
          report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                    StringView{"'"} + name +
                                        "' is not a function");
        status = 1;
        continue;
      }
      if (should_print_function_names_only) {
        let line = String{cxt.scratch_allocator(), name};
        line += '\n';
        ec.print_to_stdout(line.view());
      } else if (const String *source = cxt.function_store().find_source(name);
                 source != nullptr)
      {
        if (!source->is_empty()) {
          let line = String{cxt.scratch_allocator(),
                            format_bash_function_source(source->view()).view()};
          line += '\n';
          ec.print_to_stdout(line.view());
        }
      }
    }
    return status;
  }

  let const do_print_declaration = [&](StringView name) throws -> bool {
    let line = String{cxt.scratch_allocator()};
    if (!append_variable_declaration(cxt, name, line)) return false;

    ec.print_to_stdout(line.view());

    return true;
  };

  let const has_attribute_filter =
      should_make_indexed || should_make_associative || should_export ||
      should_mark_integer_attribute || should_mark_lowercase_attribute ||
      should_mark_uppercase_attribute || should_mark_readonly ||
      should_mark_nameref;
  if ((should_print || has_attribute_filter) && i >= args.count() &&
      !ec.has_stripped_array_operands)
  {
    let const do_matches_attribute_filter = [&](StringView name) -> bool {
      if (should_export && !os::get_environment_variable(name).has_value() &&
          !cxt.is_exported(name))
      {
        return false;
      }
      if (should_mark_integer_attribute && !cxt.is_integer_variable(name)) {
        return false;
      }
      if (should_mark_lowercase_attribute &&
          !cxt.variable_store().attributes().is_lowercase(name))
      {
        return false;
      }
      if (should_mark_uppercase_attribute &&
          !cxt.variable_store().attributes().is_uppercase(name))
      {
        return false;
      }
      if (should_make_indexed &&
          !cxt.variable_store().indexed_arrays().find(name).has_value() &&
          !cxt.is_bash_directory_stack_special(name) &&
          !cxt.is_bash_argument_array(name))
      {
        return false;
      }
      if (should_make_associative && !cxt.is_associative_array(name)) {
        return false;
      }
      if (should_mark_readonly && !cxt.is_readonly(name)) {
        return false;
      }
      if (should_mark_nameref &&
          !cxt.variable_store().attributes().is_nameref(name))
      {
        return false;
      }
      return true;
    };

    let names = cxt.variable_names(cxt.scratch_allocator());
    for (let const &environment_name : os::environment_names())
      names.add(environment_name.view());
    cxt.variable_store().attributes().for_each_name(
        [&](StringView attributed_name) { names.add(attributed_name); });

    let collected_names = ArrayList<String>{cxt.scratch_allocator()};
    names.for_each(
        [&](StringView name) { collected_names.push_managed(name); });
    let const sorted_names =
        steal(collected_names).make_sorted(sort_order::ascending);

    for (let const &name : sorted_names) {
      if (do_matches_attribute_filter(name.view()))
        do_print_declaration(name.view());
    }

    return 0;
  }

  if (should_print) {
    i32 status = 0;
    for (; i < args.count(); i++) {
      let const name = args[i].view();
      if (!do_print_declaration(name)) {
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                  StringView{"'"} + name + "' is not defined");
        status = 1;
      }
    }
    return status;
  }

  for (; i < args.count(); i++) {
    let const operand = args[i].view();
    let const equals = operand.find_character('=');
    let name =
        equals.has_value() ? operand.substring_of_length(0, *equals) : operand;
    let const value =
        equals.has_value() ? operand.substring(*equals + 1) : StringView{};

    let const update_mode =
        equals.has_value() && !name.is_empty() && name[name.count() - 1] == '+'
            ? assignment_update_mode::Append
            : assignment_update_mode::Replace;
    if (update_mode == assignment_update_mode::Append)
      name = name.substring_of_length(0, name.count() - 1);

    let const bracket = name.find_character('[');
    let const has_subscript =
        bracket.has_value() && name[name.count() - 1] == ']';
    let const subscript =
        has_subscript ? name.substring_of_length(*bracket + 1,
                                                 name.count() - *bracket - 2)
                      : StringView{};
    if (has_subscript) name = name.substring_of_length(0, *bracket);

    if (!name_is_valid_identifier(name)) {
      report_invalid_identifier(ec, cxt, ec.arg_location_at(i), operand);
      status = declaration_assignment_failure_status(cxt);
      continue;
    }

    if (!should_be_global && cxt.scope_store().local_scope_depth() > 0 &&
        cxt.is_bash_argument_array(name))
    {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                String{name} +
                                    ": variable may not be assigned value");
      status = 1;
      continue;
    }

    if ((should_mark_integer_attribute || should_unmark_integer_attribute ||
         should_mark_lowercase_attribute || should_unmark_lowercase_attribute ||
         should_mark_uppercase_attribute || should_unmark_uppercase_attribute ||
         should_mark_nameref || should_unmark_nameref) &&
        cxt.is_readonly(name))
    {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                StringView{"'"} + name + "' is read-only");
      status = 1;
      continue;
    }

    if (should_make_associative &&
        (cxt.variable_store().indexed_arrays().find(name).has_value() ||
         cxt.is_bash_directory_stack_special(name) ||
         cxt.is_bash_argument_array(name)))
    {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                StringView{"Unable to convert '"} + name +
                                    "' from an indexed array to an "
                                    "associative array");
      status = 1;
      continue;
    }
    if (should_make_indexed && cxt.is_associative_array(name)) {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                StringView{"Unable to convert '"} + name +
                                    "' from an associative array to an "
                                    "indexed array");
      status = 1;
      continue;
    }

    if (should_mark_nameref && !has_subscript &&
        update_mode == assignment_update_mode::Replace && value == name &&
        equals.has_value() && cxt.scope_store().local_scope_depth() > 0)
    {
      if (!bind_declared_self_nameref(ec, cxt, i, name, !should_be_global))
        status = 1;
      continue;
    }

    if (!should_be_global)
      cxt.declare_local(name, !cxt.runtime_state().is_bash_compatible() ||
                                  cxt.runtime_state().is_shopt_enabled(
                                      shopt_option_id::LocalvarInherit));

    let attribute_name = name;
    let resolved_attribute_name = Maybe<String>{};
    if (!should_mark_nameref && !should_unmark_nameref &&
        cxt.variable_store().attributes().is_nameref(name) &&
        !cxt.is_circular_nameref(name)) rarely
      {
        resolved_attribute_name = cxt.resolve_nameref_base_for_write(name);
        attribute_name = resolved_attribute_name->view();
        if ((should_mark_integer_attribute || should_unmark_integer_attribute ||
             should_mark_lowercase_attribute ||
             should_unmark_lowercase_attribute ||
             should_mark_uppercase_attribute ||
             should_unmark_uppercase_attribute) &&
            cxt.is_readonly(attribute_name))
        {
          report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                    StringView{"'"} + attribute_name +
                                        "' is read-only");
          status = 1;
          continue;
        }
      }
    if (should_mark_integer_attribute)
      cxt.variable_store().attributes().mark_integer(attribute_name);
    if (should_unmark_integer_attribute)
      cxt.variable_store().attributes().unmark_integer(attribute_name);
    if (should_unmark_lowercase_attribute)
      cxt.variable_store().attributes().unmark_lowercase(attribute_name);
    if (should_unmark_uppercase_attribute)
      cxt.variable_store().attributes().unmark_uppercase(attribute_name);
    if (should_mark_lowercase_attribute)
      cxt.variable_store().attributes().mark_lowercase(attribute_name);
    if (should_mark_uppercase_attribute)
      cxt.variable_store().attributes().mark_uppercase(attribute_name);
    if (should_unmark_nameref)
      cxt.variable_store().attributes().set(name, variable_attribute::Nameref,
                                            false);

    if (should_mark_nameref && !has_subscript) {
      let target = Maybe<String>{};
      if (equals.has_value())
        target = String{cxt.scratch_allocator(), value};
      else if (let const stored =
                   cxt.variable_store().shell_variables().find(name);
               stored.has_value())
        target = String{cxt.scratch_allocator(), stored->view()};
      else if (cxt.has_generated_value(name))
        target = cxt.get_variable_value(name);

      if (!declare_nameref(ec, cxt, i, name,
                           target.has_value()
                               ? Maybe<StringView>{target->view()}
                               : Maybe<StringView>{None},
                           should_mark_readonly))
      {
        status = 1;
      }
      continue;
    }

    if (!equals.has_value() && !has_subscript && !should_make_associative &&
        !should_make_indexed)
    {
      cxt.variable_store().attributes().mark_declared(name);
    }

    LOG(All, "declare applying attributes to '%.*s'",
        static_cast<int>(name.length), name.data);

    if (equals.has_value() && !has_subscript &&
        cxt.variable_store().attributes().is_nameref(name) &&
        cxt.is_circular_nameref(name))
    {
      cxt.warn_circular_nameref(name);
      continue;
    }

    if (has_subscript && equals.has_value()) {
      cxt.assign_array_element(name, subscript, value, update_mode);
    } else if (should_make_associative) {
      LOG(All, "declare making '%.*s' an associative array",
          static_cast<int>(name.length), name.data);
      let const was_associative = cxt.is_associative_array(name);
      if (equals.has_value()) cxt.set_shell_variable(name, value);
      cxt.declare_associative_array(name);
      if (!equals.has_value() && !has_subscript && !was_associative) {
        cxt.variable_store().attributes().mark_declared(name);
      }
    } else if (should_make_indexed) {
      if (!cxt.variable_store().indexed_arrays().find(name).has_value() &&
          !cxt.is_bash_directory_stack_special(name) &&
          !cxt.is_bash_argument_array(name))
      {
        let values = ArrayList<String>{heap_allocator()};
        if (equals.has_value())
          values.push(String{heap_allocator(), value});
        else if (let const scalar =
                     cxt.variable_store().shell_variables().find(name);
                 scalar.has_value())
          values.push(String{heap_allocator(), scalar->view()});
        let const is_valueless = values.is_empty() && !has_subscript;
        cxt.set_indexed_array(name, steal(values));
        if (is_valueless) cxt.variable_store().attributes().mark_declared(name);
      }
    } else if (equals.has_value()) {
      if (update_mode == assignment_update_mode::Append) {
        let appended = String{cxt.scratch_allocator()};
        if (let const existing = cxt.get_variable_value(name))
          appended.append(existing->view());
        let integer_name = name;
        let resolved_name = Maybe<String>{};
        if (cxt.variable_store().attributes().is_nameref(name)) rarely
          {
            resolved_name = cxt.resolve_nameref_base_for_write(name);
            integer_name = resolved_name->view();
          }
        if (cxt.is_integer_variable(integer_name))
          cxt.append_integer_expression(appended, value);
        else
          appended.append(value);
        cxt.set_shell_variable(name, appended.view());
      } else {
        cxt.set_shell_variable(name, value);
      }
    }

    if (should_export && !has_subscript &&
        (cxt.variable_store().indexed_arrays().find(name).has_value() ||
         cxt.is_associative_array(name)))
    {
      cxt.mark_exported(name);
    } else if (should_export && !has_subscript && !should_make_associative &&
               !should_make_indexed)
    {
      LOG(All, "declare exporting '%.*s' to the environment",
          static_cast<int>(name.length), name.data);
      let exported_name = name;
      let resolved_name = Maybe<String>{};
      if (cxt.variable_store().attributes().is_nameref(name)) rarely
        {
          resolved_name = cxt.resolve_nameref_for_write(name);
          exported_name = resolved_name->view();
        }
      cxt.record_environment_change(exported_name);
      cxt.mark_exported(exported_name);
      if (let const stored = cxt.get_variable_value(exported_name))
        os::set_environment_variable(exported_name, stored->view());
    }

    if (should_unexport && !has_subscript) cxt.unexport_shell_variable(name);

    if (should_mark_readonly) {
      if (cxt.variable_store().attributes().is_nameref(name)) rarely
        {
          cxt.variable_store().attributes().mark_readonly(
              cxt.resolve_nameref_base_for_write(name));
        }
      else
        cxt.variable_store().attributes().mark_readonly(name);
    }
  }

  return status;
}

} /* namespace koshka */
