/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the builtin command, including sorted listing and direct
 * dispatch that bypasses functions and PATH lookup.
 */

#include "../Builtin.hpp"

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[--list] [name [argument ...]]");

HELP_DESCRIPTION_DECL(
    "A builtin can be run even when a function has the same name.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(BUILTIN_LIST, Bool, '\0', "list", "List every builtin one per line.");

REGISTER_BUILTIN_FLAGS(BuiltinBuiltin);

namespace koshka {

using sorted_builtin_name_list =
    SortedArrayList<String, order_comparator<String>>;

/* memory: the sorted list is allocated in heap_allocator() and lives until
           exit. the static initializer runs once.
   time:   O(B log B) for B builtin words on the first call, then O(1). */
static fn
sorted_builtin_names() throws -> const sorted_builtin_name_list &
{
  static sorted_builtin_name_list names = [] throws {
    let collected = ArrayList<String>{heap_allocator()};

    for (let const &builtin_name : builtin_names())
      collected.push_managed(builtin_name);

    return steal(collected).make_sorted(sort_order::ascending);
  }();

  return names;
}

static pure fn
get_builtin_section(Builtin::Kind kind, StringView name) wontthrow
    -> builtin_section
{
  if (name == "source") return builtin_section::Bash;

  return BUILTIN_SECTIONS[static_cast<usize>(kind)];
}

struct builtin_name_sections
{
  explicit builtin_name_sections(Allocator allocator)
      : posix_names(allocator), bash_names(allocator), koshka_names(allocator)
  {}

  ArrayList<StringView> posix_names;
  ArrayList<StringView> bash_names;
  ArrayList<StringView> koshka_names;
};

static fn
collect_builtin_name_sections(Allocator allocator) throws
    -> builtin_name_sections
{
  let sections = builtin_name_sections{allocator};

  for (let const &name : sorted_builtin_names()) {
    let const kind = search_builtin(name.view());

    ASSERT(kind.has_value());
    switch (get_builtin_section(*kind, name.view())) {
    case builtin_section::Posix: sections.posix_names.push(name.view()); break;

    case builtin_section::Bash: sections.bash_names.push(name.view()); break;

    case builtin_section::Koshka:
      sections.koshka_names.push(name.view());
      break;
    }
  }

  return sections;
}

static fn
print_builtin_columns(ExecContext &ec, Allocator allocator) throws
    -> void
{
  let const sections = collect_builtin_name_sections(allocator);
  let       out      = String{allocator};

  out += "Koshka has ";
  out +=
      String::from(static_cast<i64>(sorted_builtin_names().count()), allocator);
  out += " builtins:\n\n";

  let const should_color = colors::stdout_wants_color();

  append_report_name_section(out, "POSIX", sections.posix_names, should_color);
  append_report_name_section(out, "BASH", sections.bash_names, should_color);
  append_report_name_section(out, "Koshka", sections.koshka_names,
                             should_color);
  ec.print_to_stdout(out.view());
}

static fn
append_builtin_help_section(String                      &out,
                            StringView                   title,
                            const ArrayList<StringView> &names) throws
    -> void
{
  if (!out.is_empty()) out += '\n';
  out += title;
  out += '\n';
  utils::append_name_columns(out, names.count(),
                             [&](usize index) { return names[index]; });
}

static fn
format_builtin_help_sections(Allocator allocator) throws -> String
{
  let const sections = collect_builtin_name_sections(allocator);
  let       out      = String{allocator};

  append_builtin_help_section(out, "POSIX BUILTINS", sections.posix_names);
  append_builtin_help_section(out, "BASH BUILTINS", sections.bash_names);
  append_builtin_help_section(out, "KOSHKA BUILTINS", sections.koshka_names);

  return out;
}

fn
BuiltinBuiltin::execute(ExecContext &ec, EvalContext &cxt) const throws
    -> i32
{
  if (ec.args().count() < 2) {
    if (cxt.runtime_state().get_mood() == mimic_mood::Default)
      print_builtin_columns(ec, cxt.scratch_allocator());

    return 0;
  }

  let const &name = ec.args()[1];

  if (name == "--help") {
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(
        ec, format_builtin_help_sections(cxt.scratch_allocator()).view());
  }

  if (name == "--list") {
    let const &sorted = sorted_builtin_names();
    let        out    = String{cxt.scratch_allocator()};

    for (let const &builtin_name : sorted) {
      out += builtin_name.view();
      out += "\n";
    }

    ec.print_to_stdout(out.view());

    return 0;
  }

  LOG(Debug, "builtin forwarding to '%s' past functions and PATH",
      name.c_str());

  let const target = search_builtin(name.view());

  if (!target.has_value() || cxt.is_builtin_name_disabled(name.view())) {
    report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                              StringView{"'"} + name +
                                  "' is not a shell builtin");

    return 1;
  }

  /* at this point: target holds the builtin kind of name, and name is not
     disabled. */
  let forwarded           = ArrayList<String>{heap_allocator()};
  let forwarded_locations = ArrayList<SourceLocation>{heap_allocator()};

  for (usize i = 1; i < ec.args().count(); i++) {
    forwarded.push_managed(ec.args()[i]);
    forwarded_locations.push(ec.arg_location_at(i));
  }

  let sub = ExecContext::make_from_resolved(
      ec.source_location(), ResolvedCommand::from_builtin(*target),
      steal(forwarded), steal(forwarded_locations));

  return execute_builtin(steal(sub), cxt);
}

} /* namespace koshka */
