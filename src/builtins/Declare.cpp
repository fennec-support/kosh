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
#include "../base/Trace.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAfFgilnprtux] [+i] [name[=value] ...]");

HELP_DESCRIPTION_DECL("Variables and their attributes can be declared.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(DECLARE_INDEXED, Bool, 'a', "", "Declare an indexed array.");
FLAG(DECLARE_ASSOCIATIVE, Bool, 'A', "", "Declare an associative array.");
FLAG(DECLARE_FUNCTIONS,
     Bool,
     'f',
     "",
     "Restrict to functions and print their recorded definitions.");
FLAG(DECLARE_FUNCTION_NAMES,
     Bool,
     'F',
     "",
     "Print only the identifiers of defined functions and report their "
     "existence through the exit status.");
FLAG(DECLARE_GLOBAL,
     Bool,
     'g',
     "",
     "Declare or assign the variable at global scope inside a function.");
FLAG(DECLARE_INTEGER,
     Bool,
     'i',
     "",
     "Mark the variable as integer. Evaluate each assignment as arithmetic. "
     "The +i form removes the mark.");
FLAG(DECLARE_LOWERCASE,
     Bool,
     'l',
     "",
     "Convert every assigned value to lowercase. The +l form removes the "
     "attribute.");
FLAG(DECLARE_NAMEREF,
     Bool,
     'n',
     "",
     "Make the variable a reference to the variable its value selects. The +n "
     "form removes the reference and keeps the value.");
FLAG(DECLARE_PRINT, Bool, 'p', "", "Print the matching declarations.");
FLAG(DECLARE_READONLY, Bool, 'r', "", "Make the variable read-only.");
FLAG(DECLARE_TRACE, Bool, 't', "", "Accepted without effect.");
FLAG(DECLARE_UPPERCASE,
     Bool,
     'u',
     "",
     "Convert every assigned value to uppercase. The +u form removes the "
     "attribute.");
FLAG(DECLARE_EXPORT, Bool, 'x', "", "Mark the variable for the environment.");

REGISTER_BUILTIN_FLAGS(Declare);

namespace koshka {

fn
Declare::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();

  ASSERT(!args.is_empty());

  let should_make_associative           = false;
  let should_make_indexed               = false;
  let should_export                     = false;
  let should_unexport                   = false;
  let should_print                      = false;
  let should_mark_integer_attribute     = false;
  let should_unmark_integer_attribute   = false;
  let should_mark_lowercase_attribute   = false;
  let should_unmark_lowercase_attribute = false;
  let should_mark_uppercase_attribute   = false;
  let should_unmark_uppercase_attribute = false;
  let should_mark_readonly              = false;
  let should_mark_nameref               = false;
  let should_unmark_nameref             = false;
  let should_restrict_to_functions      = false;
  let should_print_function_names_only  = false;
  let should_be_global                  = false;
  let has_trace_flag                    = false;

  i32 status          = 0;
  let operand_indices = ArrayList<usize>{cxt.scratch_allocator()};

  unused(parse_flags_vec(
      FLAG_LIST, args, ec.source_location().position, nullptr,
      &ec.arg_locations(), nullptr, builtin_error_context(ec.program()),
      flag_parse_options{.should_stop_at_first_operand = true,
                         .plus_letters                 = "aAfFgilnprtux"},
      cxt.scratch_allocator(), &operand_indices));
  defer { reset_flags(FLAG_LIST); };

  /* a + form removes i, l, u, n, and x, is ignored for r, and acts as the -
     form for the other letters. */
  let const do_is_given = [](const FlagBool &flag) wontthrow -> bool {
    return flag.is_enabled() || flag.was_given_after_plus();
  };

  should_make_associative         = do_is_given(FLAG_DECLARE_ASSOCIATIVE);
  should_make_indexed             = do_is_given(FLAG_DECLARE_INDEXED);
  should_export                   = FLAG_DECLARE_EXPORT.is_enabled();
  should_unexport                 = FLAG_DECLARE_EXPORT.was_given_after_plus();
  should_print                    = do_is_given(FLAG_DECLARE_PRINT);
  should_mark_integer_attribute   = FLAG_DECLARE_INTEGER.is_enabled();
  should_unmark_integer_attribute = FLAG_DECLARE_INTEGER.was_given_after_plus();
  should_mark_lowercase_attribute = FLAG_DECLARE_LOWERCASE.is_enabled();
  should_unmark_lowercase_attribute =
      FLAG_DECLARE_LOWERCASE.was_given_after_plus();
  should_mark_uppercase_attribute = FLAG_DECLARE_UPPERCASE.is_enabled();
  should_unmark_uppercase_attribute =
      FLAG_DECLARE_UPPERCASE.was_given_after_plus();
  should_print_function_names_only = do_is_given(FLAG_DECLARE_FUNCTION_NAMES);
  should_restrict_to_functions =
      do_is_given(FLAG_DECLARE_FUNCTIONS) || should_print_function_names_only;
  should_mark_readonly  = FLAG_DECLARE_READONLY.is_enabled();
  should_be_global      = do_is_given(FLAG_DECLARE_GLOBAL);
  should_mark_nameref   = FLAG_DECLARE_NAMEREF.is_enabled();
  should_unmark_nameref = FLAG_DECLARE_NAMEREF.was_given_after_plus();
  has_trace_flag        = do_is_given(FLAG_DECLARE_TRACE);

  /* the first operand index belongs to the command word. */
  usize i = operand_indices.count() < 2 ? args.count() : operand_indices[1];

  if (should_make_associative && should_make_indexed) {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              "'-a' and '-A' cannot be used together");

    return 2;
  }

  if (should_mark_lowercase_attribute && should_mark_uppercase_attribute) {
    should_mark_lowercase_attribute   = false;
    should_mark_uppercase_attribute   = false;
    should_unmark_lowercase_attribute = true;
    should_unmark_uppercase_attribute = true;
  }

  if (should_restrict_to_functions) {
    if (i >= args.count()) {
      let listing = String{cxt.scratch_allocator()};

      for (let const &name : cxt.function_store().sorted_names()) {
        if (should_print_function_names_only) {
          listing += "declare -f ";
          listing.append(name.view());
        } else if (const String *source =
                       cxt.function_store().find_source(name.view());
                   source != nullptr)
        {
          listing.append(format_bash_function_source(source->view()).view());
        }

        listing += '\n';
      }

      ec.print_to_stdout(listing.view());

      return 0;
    }

    let const should_set_function_attributes =
        !should_print_function_names_only &&
        (has_trace_flag || should_mark_readonly);

    for (; i < args.count(); i++) {
      let const name = args[i].view();

      if (!cxt.function_store().find_function(name).has_value()) {
        if (!should_print_function_names_only &&
            !should_set_function_attributes)
        {
          report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                    StringView{"'"} + name +
                                        "' is not a function");
        }
        status = 1;
        continue;
      }

      if (should_set_function_attributes) {
        if (should_mark_readonly) cxt.mark_function_readonly(name);
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

    let listing = String{cxt.scratch_allocator()};

    for (let const &name : sorted_names) {
      if (do_matches_attribute_filter(name.view()))
        unused(append_variable_declaration(cxt, name.view(), listing));
    }

    ec.print_to_stdout(listing.view());

    return 0;
  }

  if (should_print) {
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

  /* at this point: the -f, -F, and -p paths have returned, so each operand
     from index i is an assignment or a name to declare. */
  for (; i < args.count(); i++) {
    /* e.g. operand = "a[1]+=x"
            equals = 5, name = "a[1]+", value = "x"
            update_mode = Append, name = "a[1]"
            bracket = 1, has_subscript, subscript = "1", name = "a" */
    let const operand = args[i].view();
    let const equals  = operand.find_character('=');
    let       name =
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
                                String{heap_allocator(), name} +
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

    let const is_plain_global_assignment =
        should_be_global && equals.has_value() && !has_subscript &&
        !has_attribute_filter && !should_unexport &&
        !should_unmark_integer_attribute &&
        !should_unmark_lowercase_attribute &&
        !should_unmark_uppercase_attribute && !should_unmark_nameref &&
        !ec.has_stripped_array_operands &&
        cxt.scope_store().local_scope_depth() > 0;

    if (is_plain_global_assignment &&
        cxt.assign_global_beneath_locals(
            name, value, update_mode == assignment_update_mode::Append))
    {
      continue;
    }

    let attribute_name          = name;
    let resolved_attribute_name = Maybe<String>{};

    if (!should_mark_nameref && !should_unmark_nameref &&
        cxt.variable_store().attributes().is_nameref(name) &&
        !cxt.is_circular_nameref(name))
    {
      rarely {
        resolved_attribute_name = cxt.resolve_nameref_base_for_write(name);
        attribute_name          = resolved_attribute_name->view();
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
    }

    if (should_mark_integer_attribute) {
      cxt.variable_store().attributes().mark_integer(attribute_name);
    }

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

    /* at this point: name is a valid identifier without its subscript, the
       attribute flags are applied, and name is not a circular nameref when
       the operand assigns a value without a subscript. */
    if (has_subscript && equals.has_value()) {
      let const data_scope = cxt.guard_expanded_subscript(subscript);
      cxt.assign_array_element(name, subscript, value, update_mode);
    } else if (should_make_associative) {
      LOG(All, "declare making '%.*s' an associative array",
          static_cast<int>(name.length), name.data);

      let const was_associative = cxt.is_associative_array(name);

      if (equals.has_value()) {
        cxt.set_shell_variable(name, value);
      }

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

        if (equals.has_value()) {
          values.push_managed(value);
        } else if (let const scalar =
                       cxt.variable_store().shell_variables().find(name);
                   scalar.has_value())
        {
          values.push_managed(scalar->view());
        }

        let const is_valueless = values.is_empty() && !has_subscript;

        cxt.set_indexed_array(name, steal(values));
        if (is_valueless) {
          cxt.variable_store().attributes().mark_declared(name);
        }
      }
    } else if (equals.has_value()) {
      if (update_mode == assignment_update_mode::Append) {
        let appended = String{cxt.scratch_allocator()};

        if (let const existing = cxt.get_variable_value(name)) {
          appended.append(existing->view());
        }

        let integer_name  = name;
        let resolved_name = Maybe<String>{};

        if (cxt.variable_store().attributes().is_nameref(name)) {
          rarely {
            resolved_name = cxt.resolve_nameref_base_for_write(name);
            integer_name  = resolved_name->view();
          }
        }

        if (cxt.is_integer_variable(integer_name)) {
          cxt.append_integer_expression(appended, value);
        } else {
          appended.append(value);
        }

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

      if (cxt.variable_store().attributes().is_nameref(name)) {
        rarely {
          resolved_name = cxt.resolve_nameref_for_write(name);
          exported_name = resolved_name->view();
        }
      }

      cxt.record_environment_change(exported_name);
      cxt.mark_exported(exported_name);
      if (let const stored = cxt.get_variable_value(exported_name)) {
        os::set_environment_variable(exported_name, stored->view());
      }
    }

    if (should_unexport && !has_subscript) {
      cxt.unexport_shell_variable(name);
    }

    if (should_mark_readonly) {
      if (cxt.variable_store().attributes().is_nameref(name)) rarely {
        cxt.variable_store().attributes().mark_readonly(
            cxt.resolve_nameref_base_for_write(name));
      } else
        cxt.variable_store().attributes().mark_readonly(name);
    }
  }

  return status;
}

} /* namespace koshka */
