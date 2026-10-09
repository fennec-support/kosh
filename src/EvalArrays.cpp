/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements indexed, sparse, and associative shell arrays. It owns
 * assignment, lookup, removal, subscripting, key collection, local bindings,
 * and scalar or array expansion through the common variable interface. It
 * projects call-stack variables, flattened Bash call arguments, and DIRSTACK
 * through the same array paths. EvalContext retains the frame and directory
 * storage. This split exists because aggregate subscripting and list expansion
 * are shared even when an array has no ordinary indexed-array allocation.
 */

#include "Builtin.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/StaticStringMap.hpp"
#include "base/Trace.hpp"

namespace koshka {

static constexpr static_string_entry<EvalContext::DynamicArray>
    DYNAMIC_ARRAY_ENTRIES[] = {
        {SSK("BASH_ARGC"),   EvalContext::DynamicArray::ArgumentCount},
        {SSK("BASH_ARGV"),   EvalContext::DynamicArray::ArgumentValue},
        {SSK("BASH_LINENO"), EvalContext::DynamicArray::LineNumber   },
        {SSK("BASH_SOURCE"), EvalContext::DynamicArray::SourcePath   },
        {SSK("FUNCNAME"),    EvalContext::DynamicArray::FunctionName },
};
static constexpr StaticStringMap DYNAMIC_ARRAYS{DYNAMIC_ARRAY_ENTRIES};

static fn
evaluate_array_index(EvalContext &context, StringView subscript,
                     const SourceLocation *source_location = nullptr) throws
    -> i64
{
  return context.evaluate_arithmetic(subscript, source_location, true);
}

static fn sparse_array_key(StringView name, usize index,
                           Allocator allocator) throws -> String
{
  let key = String{allocator, name};
  key.push('\x01');
  char index_text[24];
  key.append(utils::int_to_text_into(static_cast<i64>(index), index_text,
                                     sizeof(index_text)));
  return key;
}

template <class Visit>
static fn for_each_sparse_index(const StringMap<String> &sparse,
                                StringView name, Allocator allocator,
                                Visit do_visit) throws -> void
{
  let const prefix = sparse_array_key(name, 0, allocator);
  let const name_prefix = prefix.view().substring_of_length(0, name.length + 1);
  sparse.for_each([&](StringView key, const String &value) throws {
    if (key.length <= name_prefix.length ||
        key.substring_of_length(0, name_prefix.length) != name_prefix)
    {
      return;
    }
    if (let const parsed = key.substring(name_prefix.length).to<i64>();
        !parsed.is_error() && parsed.value() >= 0)
    {
      do_visit(static_cast<usize>(parsed.value()), value);
    }
  });
}

static fn sparse_array_has_entries(const StringMap<String> &sparse,
                                   StringView name, Allocator allocator) throws
    -> bool
{
  let has_entries = false;
  for_each_sparse_index(sparse, name, allocator,
                        [&](usize index, const String &value) {
                          unused(index);
                          unused(value);
                          has_entries = true;
                        });
  return has_entries;
}

struct sparse_array_entry
{
  usize index;
  String value;
};

struct sparse_array_entry_comparator
{
  pure fn operator()(const sparse_array_entry &left,
                     const sparse_array_entry &right) const wontthrow->bool
  {
    return left.index < right.index;
  }
};

static fn collect_sparse_array_entries(const StringMap<String> &sparse,
                                       StringView name,
                                       Allocator allocator) throws
    -> SortedArrayList<sparse_array_entry, sparse_array_entry_comparator>
{
  let out = ArrayList<sparse_array_entry>{allocator};
  for_each_sparse_index(sparse, name, allocator,
                        [&](usize index, const String &value) throws {
                          out.push(sparse_array_entry{
                              index, String{allocator, value.view()}
                          });
                        });
  return steal(out).make_sorted(sparse_array_entry_comparator{});
}

fn EvalContext::clear_sparse_array(StringView name) throws -> void
{
  if (!variable_store().sparse_arrays().has(name)) return;

  let indices = ArrayList<usize>{scratch_allocator()};
  for_each_sparse_index(variable_store().sparse_arrays().values(), name,
                        scratch_allocator(),
                        [&](usize index, const String &value) throws {
                          unused(value);
                          indices.push(index);
                        });

  for (let const index : indices)
    variable_store().sparse_arrays().values().erase(
        sparse_array_key(name, index, scratch_allocator()).view());
  variable_store().sparse_arrays().forget(name);
}

static fn parse_explicit_array_index(StringView element,
                                     StringView &subscript_out,
                                     StringView &value_out) wontthrow -> bool
{
  if (element.length < 3 || element[0] != '[') {
    return false;
  }
  for (usize i = 1; i + 1 < element.length; i++) {
    if (element[i] == ']' && element[i + 1] == '=') {
      subscript_out = element.substring_of_length(1, i - 1);
      value_out = element.substring(i + 2);
      return true;
    }
  }
  return false;
}

fn EvalContext::assign_associative_elements(
    StringView name, const ArrayList<String> &elements) throws -> void
{
  if (elements.is_empty()) return;

  let const is_integer = is_integer_variable(name);
  let integer_text = String{scratch_allocator()};
  let const do_set = [&](StringView key, StringView element_value) throws {
    if (is_integer) {
      integer_text = evaluate_arithmetic_text(element_value);
      element_value = integer_text.view();
    }

    set_associative_element(name, key, element_value);
  };

  StringView subscript;
  StringView value;
  if (parse_explicit_array_index(elements[0].view(), subscript, value)) {
    for (let const &element : elements) {
      if (!parse_explicit_array_index(element.view(), subscript, value)) {
        let error = ErrorWithDetails{
            "Unable to assign '" + element + "' to the associative array '" +
                name + "' because it has no [key]= subscript",
            "Write every element as [key]=value, or write none that way to "
            "alternate keys and values."};
        mark_expansion_error(error, expansion_error_reach::LineOrPosixScript);
        throw steal(error);
      }

      do_set(subscript, value);
    }

    return;
  }

  for (usize i = 0; i < elements.count(); i += 2) {
    let const key = elements[i].view();
    if (key.is_empty()) {
      show_runtime_error_at(source_store().current_location(),
                            "Unable to assign to the associative array '" +
                                name + "' because a key is empty");
      continue;
    }

    do_set(key, i + 1 < elements.count() ? elements[i + 1].view() : "");
  }
}

fn EvalContext::assign_indexed_array_elements(
    StringView name, const ArrayList<String> &elements,
    assignment_update_mode update_mode, const Bitset *subscript_flags) throws
    -> void
{
  let resolved_name = Maybe<String>{};
  if (variable_store().attributes().is_nameref(name)) rarely
    {
      if (unbind_circular_nameref(name)) {
        warn_circular_nameref(name);
      } else {
        resolved_name = resolve_nameref_whole_variable_for_write(name, true);
        name = resolved_name->view();
      }
    }
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};
  if (is_write_discarded_dynamic_variable(name)) return;

  if (runtime_state().is_posix_mode()) rarely
    {
      LOG(Debug,
          "posix mode stores the array literal for '%.*s' as an empty scalar",
          static_cast<int>(name.length), name.data);
      set_shell_variable(name, "");
      return;
    }

  let const is_integer = is_integer_variable(name);
  let integer_text = String{scratch_allocator()};
  let const do_element_value = [&](StringView value) throws -> StringView {
    if (!is_integer) return value;

    integer_text = evaluate_arithmetic_text(value);
    return integer_text.view();
  };

  if (is_associative_array(name)) {
    if (update_mode != assignment_update_mode::Append) {
      clear_associative_array(name);
      declare_associative_array(name);
    }

    assign_associative_elements(name, elements);
    return;
  }

  usize running_index = 0;
  if (is_bash_directory_stack_special(name)) {
    if (update_mode == assignment_update_mode::Append)
      running_index = variable_store().directory_stack().count() + 1;
  } else if (update_mode == assignment_update_mode::Append) {
    if (let const array = variable_store().indexed_arrays().find(name);
        array.has_value())
      running_index = array->count();
    else if (variable_store().shell_variables().find(name).has_value())
      running_index = 1;
    if (variable_store().sparse_arrays().has(name))
      for_each_sparse_index(variable_store().sparse_arrays().values(), name,
                            scratch_allocator(),
                            [&](usize index, const String &value) throws {
                              unused(value);
                              if (index == SIZE_MAX)
                                throw Error{"Unable to append to '" + name +
                                            "' because its index is too large"};
                              if (index >= running_index)
                                running_index = index + 1;
                            });
  } else {
    set_indexed_array(name, ArrayList<String>{heap_allocator()});
  }

  for (usize element_position = 0; element_position < elements.count();
       element_position++)
  {
    let const &element = elements[element_position];
    StringView subscript;
    StringView value;
    let index = running_index;
    let const is_subscripted =
        subscript_flags == nullptr || (*subscript_flags)[element_position];
    if (is_subscripted &&
        parse_explicit_array_index(element.view(), subscript, value))
    {
      i64 raw_index = evaluate_array_index(*this, subscript);
      if (raw_index < 0) raw_index += array_negative_index_base(name);
      if (raw_index < 0)
        throw Error{"Unable to index '" + name +
                    "' because the array subscript is invalid"};
      index = static_cast<usize>(raw_index);
      set_array_element(name, index, do_element_value(value));
    } else {
      set_array_element(name, index, do_element_value(element.view()));
    }
    running_index = index + 1;
  }
}

fn EvalContext::set_array_element(StringView name, usize index,
                                  StringView value) throws -> void
{
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  if (is_write_discarded_dynamic_variable(name)) return;

  if (is_bash_directory_stack_special(name)) {
    set_bash_directory_stack_element(index, value);
    return;
  }

  let adjusted = String{scratch_allocator()};
  if (variable_store().attributes().has_case(name)) rarely
    {
      adjusted.append(value);
      variable_store().attributes().apply_case(name, adjusted);
      value = adjusted.view();
    }

  let dense = variable_store().indexed_arrays().find(name);
  if (!dense.has_value()) {
    let elements = ArrayList<String>{heap_allocator()};
    if (let const scalar = variable_store().shell_variables().find(name);
        scalar.has_value())
      elements.push(String{heap_allocator(), scalar->view()});
    set_indexed_array(name, steal(elements));
    dense = variable_store().indexed_arrays().find(name);
  }
  variable_store().shell_variables().erase(name);
  ASSERT(dense.has_value());

  let const dense_count = dense->count();
  if (index < dense_count) {
    (*dense.value())[index] = String{heap_allocator(), value};
    return;
  }
  if (index == dense_count) {
    dense->push(String{heap_allocator(), value});
    loop
    {
      let const key =
          sparse_array_key(name, dense->count(), scratch_allocator());
      let const migrated =
          variable_store().sparse_arrays().values().find(key.view());
      if (!migrated.has_value()) break;
      dense->push(String{heap_allocator(), migrated->view()});
      variable_store().sparse_arrays().values().erase(key.view());
    }
    if (variable_store().sparse_arrays().has(name) &&
        !sparse_array_has_entries(variable_store().sparse_arrays().values(),
                                  name, scratch_allocator()))
      variable_store().sparse_arrays().forget(name);
    return;
  }
  LOG(All, "holding element %zu of '%.*s' sparsely past the dense run of %zu",
      index, static_cast<int>(name.length), name.data, dense_count);
  variable_store().sparse_arrays().declare(name);
  variable_store().sparse_arrays().values().set(
      sparse_array_key(name, index, scratch_allocator()).view(), value);
}

fn EvalContext::get_bash_directory_stack_element(
    usize index, Allocator allocator) const throws -> Maybe<String>
{
  if (index >= variable_store().directory_stack().count() + 1) return None;
  if (index == 0) {
    let const current = logical_working_directory(*this);
    return String{allocator, current.text()};
  }

  let const &stack = variable_store().directory_stack();
  return String{allocator, stack[stack.count() - index].view()};
}

fn EvalContext::set_bash_directory_stack_element(usize index,
                                                 StringView value) throws
    -> void
{
  if (index == 0 || index >= variable_store().directory_stack().count() + 1)
    return;

  let &stack = variable_store().directory_stack();
  stack[stack.count() - index] = String{heap_allocator(), value};
}

static fn associative_composite_key(StringView name, StringView key,
                                    Allocator allocator) throws -> String
{
  let composite = String{allocator, name};
  composite.push('\x01');
  composite.append(key);
  return composite;
}

fn EvalContext::assign_array_element(StringView name, StringView subscript,
                                     StringView value,
                                     assignment_update_mode update_mode) throws
    -> void
{
  LOG(All, "assigning the array element '%.*s[%.*s]'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(subscript.length), subscript.data);
  let resolved_name = Maybe<String>{};
  if (variable_store().attributes().is_nameref(name)) rarely
    {
      if (unbind_circular_nameref(name)) {
        warn_circular_nameref(name);
      } else {
        resolved_name = resolve_nameref_whole_variable_for_write(name, true);
        name = resolved_name->view();
      }
    }
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  char integer_result[24];
  let const do_integer_element_value = [&](Maybe<String> existing)
                                           throws -> StringView {
    let joined = String{scratch_allocator()};
    if (update_mode == assignment_update_mode::Append) {
      if (existing.has_value()) joined.append(existing->view());
      append_integer_expression(joined, value);
    } else {
      joined.append(value);
    }
    return utils::int_to_text_into(
        joined.is_empty() ? 0 : evaluate_arithmetic(joined.view()),
        integer_result, sizeof(integer_result));
  };

  if (is_associative_array(name)) {
    let const key = expand_modifier_word(subscript);
    if (is_integer_variable(name)) rarely
      {
        set_associative_element(
            name, key.view(),
            do_integer_element_value(
                lookup_associative_element(name, key.view())));
        return;
      }
    if (update_mode == assignment_update_mode::Append) {
      let const existing = lookup_associative_element(name, key.view());
      let combined = existing.has_value()
                         ? String{scratch_allocator(), existing->view()}
                         : String{scratch_allocator()};
      combined += value;
      set_associative_element(name, key.view(), combined.view());
    } else {
      set_associative_element(name, key.view(), value);
    }
    return;
  }

  i64 index = evaluate_array_index(*this, subscript);
  if (index < 0) index += array_negative_index_base(name);
  if (index < 0)
    throw Error{"Unable to index '" + name +
                "' because the array subscript is invalid"};

  let const resolved_index = static_cast<usize>(index);
  let const do_lookup_existing_element = [&]() throws -> Maybe<String> {
    if (is_bash_directory_stack_special(name))
      return get_bash_directory_stack_element(resolved_index,
                                              scratch_allocator());

    if (let const array = variable_store().indexed_arrays().find(name);
        array.has_value() && resolved_index < array->count())
      return String{array->operator[](resolved_index).view()};

    if (variable_store().sparse_arrays().has(name)) {
      let const key =
          sparse_array_key(name, resolved_index, scratch_allocator());
      if (let const sparse =
              variable_store().sparse_arrays().values().find(key.view());
          sparse.has_value())
        return String{sparse->view()};
    }

    if (resolved_index == 0)
      if (let const scalar = variable_store().shell_variables().find(name);
          scalar.has_value())
        return String{scalar->view()};

    return None;
  };

  if (is_integer_variable(name)) rarely
    {
      let existing = Maybe<String>{};
      if (update_mode == assignment_update_mode::Append)
        existing = do_lookup_existing_element();
      set_array_element(name, resolved_index,
                        do_integer_element_value(steal(existing)));
      return;
    }

  let element = String{scratch_allocator(), value};
  if (update_mode == assignment_update_mode::Append) {
    let combined = String{scratch_allocator()};
    if (let const existing = do_lookup_existing_element(); existing.has_value())
      combined = String{existing->view()};
    combined += value;
    element = steal(combined);
  }
  set_array_element(name, resolved_index, element.view());
}

fn EvalContext::declare_associative_array(StringView name) throws -> void
{
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};
  if (is_bash_aliases_special(name)) return;

  LOG(Debug, "declaring '%.*s' as an associative array",
      static_cast<int>(name.length), name.data);
  let scalar = Maybe<String>{};
  if (let const stored = variable_store().shell_variables().find(name);
      stored.has_value())
    scalar = *stored.value();
  variable_store().associative_arrays().declare(name);
  variable_store().shell_variables().erase(name);
  if (scalar.has_value()) set_associative_element(name, "0", scalar->view());
}

fn EvalContext::set_associative_element(StringView name, StringView key,
                                        StringView value) throws -> void
{
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};
  if (is_bash_aliases_special(name)) {
    scope_store().set_alias(key, value);
    return;
  }

  let adjusted = String{scratch_allocator()};
  if (variable_store().attributes().has_case(name)) rarely
    {
      adjusted.append(value);
      variable_store().attributes().apply_case(name, adjusted);
      value = adjusted.view();
    }

  if (!is_associative_array(name)) {
    variable_store().associative_arrays().declare(name);
    variable_store().shell_variables().erase(name);
  }
  variable_store().associative_arrays().values().set(
      associative_composite_key(name, key, scratch_allocator()).view(), value);
}

fn EvalContext::lookup_associative_element(StringView name,
                                           StringView key) const throws
    -> Maybe<String>
{
  if (is_bash_aliases_special(name)) return scope_store().get_alias(key);

  if (let const value = variable_store().associative_arrays().values().find(
          associative_composite_key(name, key, scratch_allocator()).view());
      value.has_value())
    return *value.value();
  return None;
}

fn EvalContext::associative_keys(StringView name) const throws
    -> ArrayList<String>
{
  let keys = ArrayList<String>{heap_allocator()};
  if (is_bash_aliases_special(name)) {
    keys.reserve(scope_store().aliases().count());
    scope_store().aliases().for_each([&](StringView key, const String &value) {
      unused(value);
      keys.push_managed(key);
    });
    return keys;
  }

  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  variable_store().associative_arrays().values().for_each(
      [&](StringView composite, const String &value) {
        unused(value);
        if (composite.starts_with(prefix.view()))
          keys.push_managed(composite.substring(prefix.count()));
      });
  return keys;
}

fn EvalContext::associative_values(StringView name) const throws
    -> ArrayList<String>
{
  let values = ArrayList<String>{heap_allocator()};
  if (is_bash_aliases_special(name)) {
    values.reserve(scope_store().aliases().count());
    scope_store().aliases().for_each([&](StringView key, const String &value) {
      unused(key);
      values.push_managed(value.view());
    });
    return values;
  }

  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  variable_store().associative_arrays().values().for_each(
      [&](StringView composite, const String &value) {
        if (composite.starts_with(prefix.view()))
          values.push_managed(value.view());
      });
  return values;
}

fn EvalContext::clear_associative_array(StringView name) throws -> void
{
  if (is_bash_aliases_special(name)) return;
  if (!is_associative_array(name)) return;
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  let to_erase = ArrayList<String>{heap_allocator()};
  variable_store().associative_arrays().values().for_each(
      [&](StringView composite, const String &) {
        if (composite.starts_with(prefix.view()))
          to_erase.push_managed(composite);
      });
  for (let const &composite : to_erase)
    variable_store().associative_arrays().values().erase(composite.view());
  variable_store().associative_arrays().forget(name);
}

fn EvalContext::unset_array_element(StringView name,
                                    StringView subscript) throws -> void
{
  LOG(All, "unsetting the array element '%.*s[%.*s]'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(subscript.length), subscript.data);
  let resolved_name = Maybe<String>{};
  if (variable_store().attributes().is_nameref(name)) rarely
    {
      resolved_name = resolve_nameref_for_write(name);
      name = resolved_name->view();
    }
  if (is_readonly(name))
    throw Error{"Unable to unset '" + name + "' because it is read only"};

  if (is_bash_argument_array(name))
    throw Error{String{name} + ": cannot unset"};

  if (is_bash_directory_stack_special(name)) {
    unused(evaluate_array_index(*this, subscript));
    return;
  }

  if (is_associative_array(name)) {
    let const key = expand_modifier_word(subscript);
    if (is_bash_aliases_special(name)) return;
    variable_store().associative_arrays().values().erase(
        associative_composite_key(name, key.view(), scratch_allocator())
            .view());
    return;
  }

  if (variable_store().indexed_arrays().find(name).has_value()) {
    let const index = evaluate_array_index(*this, subscript);
    let array = variable_store().indexed_arrays().find(name);
    if (!array.has_value()) return;

    let const array_count = static_cast<i64>(array->count());
    const i64 resolved =
        index < 0 ? index + array_negative_index_base(name) : index;
    if (resolved < 0) return;
    if (resolved < array_count) {
      if (static_cast<usize>(resolved) + 1 < array->count())
        variable_store().sparse_arrays().declare(name);
      for (usize i = static_cast<usize>(resolved) + 1;
           i < static_cast<usize>(array_count); i++)
        variable_store().sparse_arrays().values().set(
            sparse_array_key(name, i, scratch_allocator()).view(),
            (*array.value())[i].view());
      while (array->count() > static_cast<usize>(resolved))
        array->remove(array->count() - 1);
    } else {
      variable_store().sparse_arrays().values().erase(
          sparse_array_key(name, static_cast<usize>(resolved),
                           scratch_allocator())
              .view());
      if (variable_store().sparse_arrays().has(name) &&
          !sparse_array_has_entries(variable_store().sparse_arrays().values(),
                                    name, scratch_allocator()))
        variable_store().sparse_arrays().forget(name);
    }
    return;
  }

  if (name.find_character('[').has_value()) return;

  if (variable_store().shell_variables().find(name).has_value() &&
      !is_dynamic_write_owner(name))
  {
    if (subscript == "@" || subscript == "*" ||
        evaluate_array_index(*this, subscript) != 0)
      throw Error{String{name} + ": not an array variable"};

    unset_shell_variable(name);
  }
}

fn EvalContext::declare_local(StringView name, bool should_inherit_value) throws
    -> void
{
  if (scope_store().local_scope_depth() == 0) return;
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};
  ASSERT(scope_store().local_scope_depth() <=
         scope_store().local_scopes().count());
  scope_store().forget_current_self_reference(name);
  if (scope_store().has_current_local(name)) return;
  LOG(All, "declaring '%.*s' local in scope depth %zu",
      static_cast<int>(name.length), name.data,
      scope_store().local_scope_depth());

  let const was_bash_directory_stack_special =
      is_bash_directory_stack_special(name);
  let inherited_directory_stack = ArrayList<String>{heap_allocator()};
  if (should_inherit_value && was_bash_directory_stack_special)
    inherited_directory_stack = collect_array_elements(name);

  let previous_array = Maybe<ArrayList<String>>{};
  if (variable_store().indexed_arrays().count() != 0)
    if (let const array = variable_store().indexed_arrays().find(name);
        array.has_value())
    {
      let copy = ArrayList<String>{heap_allocator()};
      copy.reserve(array->count());
      for (let const &element : *array.value())
        copy.push_managed(element.view());
      previous_array = steal(copy);
    }

  let const previous_was_associative = is_associative_array(name);
  let previous_keys = ArrayList<String>{heap_allocator()};
  let previous_values = ArrayList<String>{heap_allocator()};
  if (previous_was_associative) {
    previous_keys = associative_keys(name);
    previous_values = associative_values(name);
  }

  let previous_sparse_indices = ArrayList<usize>{heap_allocator()};
  let previous_sparse_values = ArrayList<String>{heap_allocator()};
  if (variable_store().sparse_arrays().has(name)) {
    let previous_sparse_entries = collect_sparse_array_entries(
        variable_store().sparse_arrays().values(), name, heap_allocator());
    for (sparse_array_entry &entry : previous_sparse_entries) {
      previous_sparse_indices.push(entry.index);
      previous_sparse_values.push(steal(entry.value));
    }
  }

  let const previous_attributes = variable_store().attributes().get_bits(name);
  let const previous_special_definition_location =
      special_variable_definition_location(name);
  if (!should_inherit_value) variable_store().attributes().erase(name);
  variable_store().attributes().unmark_readonly(name);

  let const previous_was_exported = is_exported(name);

  let previous_value = Maybe<String>{};
  if (let const scalar = variable_store().shell_variables().find(name);
      scalar.has_value())
  {
    previous_value = *scalar.value();
  } else if (previous_array.has_value() && !previous_array->is_empty()) {
    previous_value = previous_array->front();
  } else if (!is_dynamic_write_owner(name) &&
             (previous_was_exported ||
              (variable_requires_dynamic_lookup(name) &&
               !was_bash_directory_stack_special)))
  {
    previous_value = get_variable_value(name);
  }

  scope_store().current_local_scope().push(local_binding{
      String{name}, steal(previous_value), previous_special_definition_location,
      steal(previous_array), steal(previous_keys), steal(previous_values),
      steal(previous_sparse_indices), steal(previous_sparse_values),
      previous_attributes, previous_was_associative, previous_was_exported,
      false});

  if (should_inherit_value && was_bash_directory_stack_special)
    set_indexed_array(name, steal(inherited_directory_stack));

  if (!should_inherit_value) {
    force_unset_shell_variable(name);
    if (previous_was_exported) mark_exported(name);
  }

  if (!should_inherit_value) {
    variable_store().indexed_arrays().erase(name);
    clear_sparse_array(name);
    clear_associative_array(name);
  }
}

fn EvalContext::declare_self_reference(StringView name) throws -> void
{
  ASSERT(scope_store().local_scope_depth() != 0);
  let self_reference = local_binding{
      .name = String{name},
      .previous_value = None,
      .previous_special_definition_location = None,
      .previous_indexed_array = None,
      .previous_associative_keys = ArrayList<String>{heap_allocator()},
      .previous_associative_values = ArrayList<String>{heap_allocator()},
      .previous_sparse_indices = ArrayList<usize>{heap_allocator()},
      .previous_sparse_values = ArrayList<String>{heap_allocator()},
      .previous_attributes = 0,
      .previous_was_associative = false,
      .previous_was_exported = false,
      .is_self_reference = true,
  };

  for (let &binding : scope_store().current_local_scope()) {
    if (binding.name.view() != name) continue;

    if (!binding.is_self_reference) {
      restore_local_binding(binding);
      binding = steal(self_reference);
    }

    return;
  }

  scope_store().current_local_scope().push(steal(self_reference));
}

hot fn EvalContext::expand_variable(StringView name) const throws -> String
{
  let value = get_variable_value(name);
  if (value.has_value()) return value.take();

  return String{heap_allocator()};
}

fn EvalContext::array_negative_index_base(StringView name) const throws -> i64
{
  if (runtime_state().bash_dynamic_variables_enabled()) rarely
    {
      if (let const which = DYNAMIC_ARRAYS.find(name); which.has_value())
        return static_cast<i64>(dynamic_array_element_count(*which));
    }

  if (is_bash_directory_stack_special(name))
    return static_cast<i64>(variable_store().directory_stack().count() + 1);

  i64 base = 0;
  if (let const array = variable_store().indexed_arrays().find(name);
      array.has_value())
    base = static_cast<i64>(array->count());

  if (variable_store().sparse_arrays().has(name)) {
    for_each_sparse_index(
        variable_store().sparse_arrays().values(), name, scratch_allocator(),
        [&](usize index, const String &value) throws {
          unused(value);
          let const past_index = index >= static_cast<usize>(INT64_MAX)
                                     ? INT64_MAX
                                     : static_cast<i64>(index) + 1;
          if (past_index > base) base = past_index;
        });
  }

  return base;
}

fn EvalContext::array_element_count(StringView name) const throws -> usize
{
  if (runtime_state().bash_dynamic_variables_enabled()) rarely
    {
      if (let const which = DYNAMIC_ARRAYS.find(name); which.has_value())
        return dynamic_array_element_count(*which);
    }

  if (is_bash_aliases_special(name)) return scope_store().aliases().count();
  if (is_bash_directory_stack_special(name))
    return variable_store().directory_stack().count() + 1;

  if (is_associative_array(name)) {
    usize element_count = 0;
    let const prefix = associative_composite_key(name, "", scratch_allocator());
    variable_store().associative_arrays().values().for_each(
        [&](StringView composite, const String &value) {
          unused(value);
          if (composite.starts_with(prefix.view())) element_count++;
        });

    return element_count;
  }

  usize element_count = 0;
  if (let const array = variable_store().indexed_arrays().find(name);
      array.has_value())
    element_count = array->count();

  if (variable_store().sparse_arrays().has(name)) {
    for_each_sparse_index(variable_store().sparse_arrays().values(), name,
                          scratch_allocator(),
                          [&](usize index, const String &value) throws {
                            unused(index);
                            unused(value);
                            element_count++;
                          });
  }

  if (element_count == 0 && get_variable_value(name).has_value())
    element_count = 1;

  return element_count;
}

fn EvalContext::apply_array_subscript(
    StringView name, StringView subscript,
    const SourceLocation *source_location) throws -> String
{
  if (runtime_state().bash_dynamic_variables_enabled()) rarely
    {
      if (let const which = DYNAMIC_ARRAYS.find(name); which.has_value()) {
        let const element_count = dynamic_array_element_count(*which);

        if (subscript == "@" || subscript == "*") {
          let separator = ' ';
          let has_separator = true;
          if (subscript == "*") {
            has_separator = !variable_store().field_separators().is_empty();
            if (has_separator)
              separator = variable_store().field_separators()[0];
          }

          let out = String{scratch_allocator()};
          for (usize i = 0; i < element_count; i++) {
            if (i > 0 && has_separator) {
              out.push(separator);
            }
            if (*which == DynamicArray::ArgumentValue) {
              ASSERT(variable_store().bash_arguments().is_active());
              let const &values = variable_store().bash_arguments().values();
              out.append(values[values.count() - 1 - i].view());
            } else {
              out.append(
                  dynamic_array_element_text(*which, i, scratch_allocator())
                      .view());
            }
          }

          return out;
        }

        let index = evaluate_array_index(*this, subscript, source_location);
        if (index < 0) index += static_cast<i64>(element_count);

        if (index >= 0 && static_cast<usize>(index) < element_count) {
          return dynamic_array_element_text(*which, static_cast<usize>(index),
                                            scratch_allocator());
        }

        return String{scratch_allocator()};
      }
    }

  if (is_bash_directory_stack_special(name)) {
    let const count = variable_store().directory_stack().count() + 1;
    if (subscript == "@" || subscript == "*") {
      let separator = ' ';
      let has_separator = true;
      if (subscript == "*") {
        has_separator = !variable_store().field_separators().is_empty();
        if (has_separator) separator = variable_store().field_separators()[0];
      }

      let out = String{scratch_allocator()};
      let const current_directory = logical_working_directory(*this);
      for (usize index = 0; index < count; index++) {
        if (index > 0 && has_separator) out.push(separator);
        if (index == 0)
          out.append(current_directory.text());
        else {
          let const &stack = variable_store().directory_stack();
          out.append(stack[stack.count() - index].view());
        }
      }
      return out;
    }

    let index = evaluate_array_index(*this, subscript, source_location);
    if (index < 0) index += static_cast<i64>(count);
    if (index < 0 || static_cast<usize>(index) >= count)
      return String{scratch_allocator()};

    return get_bash_directory_stack_element(static_cast<usize>(index),
                                            scratch_allocator())
        .take();
  }

  if (is_associative_array(name)) {
    if (subscript == "@" || subscript == "*") {
      let separator = ' ';
      let has_separator = true;
      if (subscript == "*") {
        has_separator = !variable_store().field_separators().is_empty();
        if (has_separator) separator = variable_store().field_separators()[0];
      }
      let out = String{scratch_allocator()};
      let const values = associative_values(name);
      for (usize i = 0; i < values.count(); i++) {
        if (i > 0 && has_separator) {
          out.push(separator);
        }
        out.append(values[i].view());
      }
      return out;
    }
    let const key =
        expand_modifier_word(subscript, true, true, source_location);
    let const value = lookup_associative_element(name, key.view());
    if (value.has_value()) return String{heap_allocator(), value->view()};

    return String{heap_allocator()};
  }

  if (subscript == "@" || subscript == "*") {
    let const array = variable_store().indexed_arrays().find(name);
    if (!array.has_value()) return expand_variable(name);
    let separator = ' ';
    let has_separator = true;
    if (subscript == "*") {
      has_separator = !variable_store().field_separators().is_empty();
      if (has_separator) separator = variable_store().field_separators()[0];
    }
    let out = String{scratch_allocator()};
    if (variable_store().sparse_arrays().has(name)) rarely
      {
        let const elements = collect_array_elements(name);
        for (usize i = 0; i < elements.count(); i++) {
          if (i > 0 && has_separator) {
            out.push(separator);
          }
          out.append(elements[i].view());
        }
        return out;
      }

    for (usize i = 0; i < array->count(); i++) {
      if (i > 0 && has_separator) {
        out.push(separator);
      }
      out.append(array->operator[](i).view());
    }
    return out;
  }

  i64 index = evaluate_array_index(*this, subscript, source_location);
  let const array = variable_store().indexed_arrays().find(name);
  if (!array.has_value()) {
    if (index == 0) return expand_variable(name);
    return String{scratch_allocator()};
  }
  let const array_count = static_cast<i64>(array->count());
  if (index < 0) index += array_negative_index_base(name);
  if (index < 0 || index >= array_count) {
    if (index >= 0) {
      let const probe = sparse_array_key(name, static_cast<usize>(index),
                                         scratch_allocator());
      if (let const sparse =
              variable_store().sparse_arrays().values().find(probe.view());
          sparse.has_value())
      {
        return String{scratch_allocator(), sparse->view()};
      }
    }
    return String{scratch_allocator()};
  }
  return String{scratch_allocator(),
                array->operator[](static_cast<usize>(index)).view()};
}

fn EvalContext::read_literal_array_element(StringView name,
                                           StringView subscript) const throws
    -> Maybe<String>
{
  if (is_associative_array(name))
    return lookup_associative_element(name, subscript);

  let const index = subscript.to<i64>();
  if (index.is_error() || index.value() < 0) return None;

  let const element_index = static_cast<usize>(index.value());
  let const array = variable_store().indexed_arrays().find(name);
  if (!array.has_value())
    return element_index == 0 ? get_variable_value(name) : None;
  if (element_index < array->count()) return (*array.value())[element_index];

  let const key = sparse_array_key(name, element_index, scratch_allocator());
  if (let const sparse =
          variable_store().sparse_arrays().values().find(key.view());
      sparse.has_value())
    return String{heap_allocator(), sparse->view()};

  return None;
}

fn EvalContext::collect_array_elements(StringView name) const throws
    -> ArrayList<String>
{
  if (runtime_state().bash_dynamic_variables_enabled()) rarely
    {
      if (let const which = DYNAMIC_ARRAYS.find(name); which.has_value()) {
        let const element_count = dynamic_array_element_count(*which);

        if (element_count > 0) {
          let frames = ArrayList<String>{heap_allocator()};
          frames.reserve(element_count);
          for (usize i = 0; i < element_count; i++)
            frames.push(
                dynamic_array_element_text(*which, i, heap_allocator()));

          return frames;
        }
      }
    }

  if (is_bash_directory_stack_special(name)) {
    let elements = ArrayList<String>{heap_allocator()};
    let const count = variable_store().directory_stack().count() + 1;
    elements.reserve(count);
    for (usize index = 0; index < count; index++)
      elements.push(
          get_bash_directory_stack_element(index, heap_allocator()).take());
    return elements;
  }

  if (is_associative_array(name)) return associative_values(name);

  let out = ArrayList<String>{heap_allocator()};
  if (let const array = variable_store().indexed_arrays().find(name);
      array.has_value())
  {
    out.reserve(array->count());
    for (let const &element : *array.value())
      out.push_managed(element.view());
    if (variable_store().sparse_arrays().has(name)) {
      let sparse = collect_sparse_array_entries(
          variable_store().sparse_arrays().values(), name, scratch_allocator());
      for (sparse_array_entry &entry : sparse)
        out.push(steal(entry.value));
    }
    return out;
  }
  if (Maybe<String> scalar = get_variable_value(name); scalar.has_value())
    out.push(steal(*scalar));
  return out;
}

fn EvalContext::array_element_is_set(StringView name,
                                     StringView subscript) throws -> bool
{
  if (subscript == "@" || subscript == "*") {
    return array_element_count(name) != 0;
  }
  if (runtime_state().bash_dynamic_variables_enabled()) rarely
    {
      if (let const which = DYNAMIC_ARRAYS.find(name); which.has_value()) {
        let index = evaluate_array_index(*this, subscript);
        let const element_count =
            static_cast<i64>(dynamic_array_element_count(*which));
        if (index < 0) index += element_count;
        return index >= 0 && index < element_count;
      }
    }
  if (is_bash_directory_stack_special(name)) {
    let index = evaluate_array_index(*this, subscript);
    let const count =
        static_cast<i64>(variable_store().directory_stack().count() + 1);
    if (index < 0) index += count;
    return index >= 0 && index < count;
  }
  if (is_associative_array(name)) {
    let const key = expand_modifier_word(subscript);
    return lookup_associative_element(name, key.view()).has_value();
  }
  let const index = evaluate_array_index(*this, subscript);
  if (let const array = variable_store().indexed_arrays().find(name);
      array.has_value())
  {
    let const array_count = static_cast<i64>(array->count());
    const i64 resolved =
        index < 0 ? index + array_negative_index_base(name) : index;
    if (resolved >= 0 && resolved < array_count) {
      return true;
    }
    return resolved >= 0 &&
           variable_store()
               .sparse_arrays()
               .values()
               .find(sparse_array_key(name, static_cast<usize>(resolved),
                                      scratch_allocator())
                         .view())
               .has_value();
  }
  return index == 0 && get_variable_value(name).has_value();
}

fn EvalContext::matching_prefix_names(StringView prefix) const throws
    -> SortedArrayList<String, order_comparator<String>>
{
  LOG(All, "listing variable names with the prefix '%.*s'",
      static_cast<int>(prefix.length), prefix.data);
  let names = ArrayList<String>{heap_allocator()};
  let seen = HashSet{heap_allocator()};
  let const do_consider = [&](StringView candidate) throws {
    if (candidate.starts_with(prefix) && seen.add(candidate)) {
      names.push_managed(candidate);
    }
  };
  let const stored_names = variable_names();
  stored_names.for_each(do_consider);
  for (let const &environment_name : os::environment_names())
    do_consider(environment_name.view());
  let dynamic_names = ArrayList<StringView>{heap_allocator()};
  append_dynamic_variable_names(dynamic_names);
  for (let const dynamic_name : dynamic_names)
    do_consider(dynamic_name);
  return steal(names).make_sorted(sort_order::ascending);
}

fn EvalContext::collect_array_subscripts(StringView name) const throws
    -> ArrayList<String>
{
  if (is_associative_array(name)) return associative_keys(name);

  let out = ArrayList<String>{heap_allocator()};
  if (runtime_state().bash_dynamic_variables_enabled()) rarely
    {
      if (let const which = DYNAMIC_ARRAYS.find(name); which.has_value()) {
        let const element_count = dynamic_array_element_count(*which);
        out.reserve(element_count);
        for (usize index = 0; index < element_count; index++)
          out.push(String::from(index, heap_allocator()));
        return out;
      }
    }
  if (is_bash_directory_stack_special(name)) {
    let const count = variable_store().directory_stack().count() + 1;
    out.reserve(count);
    for (usize index = 0; index < count; index++)
      out.push(String::from(index, heap_allocator()));
    return out;
  }
  if (let const array = variable_store().indexed_arrays().find(name);
      array.has_value())
  {
    out.reserve(array->count());
    for (usize i = 0; i < array->count(); i++)
      out.push(String::from(i, heap_allocator()));
    if (variable_store().sparse_arrays().has(name)) {
      let collected_sparse_indices = ArrayList<usize>{scratch_allocator()};
      for_each_sparse_index(variable_store().sparse_arrays().values(), name,
                            scratch_allocator(),
                            [&](usize index, const String &value) throws {
                              unused(value);
                              collected_sparse_indices.push(index);
                            });
      let const sparse_indices =
          steal(collected_sparse_indices).make_sorted(sort_order::ascending);
      for (let const index : sparse_indices)
        out.push(String::from(index, heap_allocator()));
    }
    return out;
  }
  if (get_variable_value(name).has_value())
    out.push(String{heap_allocator(), "0"});
  return out;
}

} /* namespace koshka */
