/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of ReportTable. It checks exact rendered bytes:
 * that no row ends in a space, that the last left-aligned column is not padded
 * while a right-aligned column keeps its left padding, that styled cells never
 * wrap padding in a reset, that colorless and colored renderings agree apart
 * from escapes, the indentation of titleless and titled tables, empty cells,
 * multibyte display widths, the empty-table policy, and that repeated renders
 * return identical bytes.
 */

#include "Unit.hpp"

#include "CLI.hpp"
#include "CLIColors.hpp"

using namespace koshka;

static fn make_two_column_table() throws -> ReportTable
{
  let table = ReportTable{heap_allocator()};
  table.add_column("NAME");
  table.add_column("SIZE", report_table_alignment::Right);

  let cells = ArrayList<report_table_cell_view>{heap_allocator()};
  cells.push({"alpha", {}});
  cells.push({"7", {}});
  table.add_row(cells);
  cells.clear();
  cells.push({"b", {}});
  cells.push({"1234", {}});
  table.add_row(cells);

  return table;
}

static fn test_right_aligned_last_column_keeps_left_padding() throws -> void
{
  let const table = make_two_column_table();

  CHECK_EQUAL(table.to_string(false, "").view(),
              "NAME   SIZE\n"
              "alpha     7\n"
              "b      1234\n");
}

static fn test_last_left_column_is_not_padded() throws -> void
{
  let table = ReportTable{heap_allocator()};
  table.add_column("ID", report_table_alignment::Right);
  table.add_column("NAME");

  let cells = ArrayList<report_table_cell_view>{heap_allocator()};
  cells.push({"1", {}});
  cells.push({"a", {}});
  table.add_row(cells);
  cells.clear();
  cells.push({"22", {}});
  cells.push({"longer", {}});
  table.add_row(cells);

  CHECK_EQUAL(table.to_string(false, "").view(),
              "ID  NAME\n"
              " 1  a\n"
              "22  longer\n");
}

static fn test_titleless_default_indentation() throws -> void
{
  let const table = make_two_column_table();

  CHECK_EQUAL(table.to_string().view(), table.to_string(false).view());
  CHECK_EQUAL(table.to_string(false).view(),
              "  NAME   SIZE\n"
              "  alpha     7\n"
              "  b      1234\n");
}

static fn test_empty_cells_leave_no_trailing_space() throws -> void
{
  let table = ReportTable{heap_allocator()};
  table.add_column("A");
  table.add_column("B");
  table.add_column("C");

  let cells = ArrayList<report_table_cell_view>{heap_allocator()};
  cells.push({"x", {}});
  cells.push({"", {}});
  cells.push({"", {}});
  table.add_row(cells);
  cells.clear();
  cells.push({"", {}});
  cells.push({"", {}});
  cells.push({"z", {}});
  table.add_row(cells);
  cells.clear();
  cells.push({"", {}});
  cells.push({"", {}});
  cells.push({"", {}});
  table.add_row(cells);

  CHECK_EQUAL(table.to_string(false, "  ").view(),
              "  A  B  C\n"
              "  x\n"
              "        z\n"
              "\n");
}

static fn test_multibyte_display_width() throws -> void
{
  let table = ReportTable{heap_allocator()};
  table.add_column("NAME");
  table.add_column("N", report_table_alignment::Right);

  let cells = ArrayList<report_table_cell_view>{heap_allocator()};
  cells.push({"\xc3\xa9\xc3\xa9", {}});
  cells.push({"1", {}});
  table.add_row(cells);
  cells.clear();
  cells.push({"abcd", {}});
  cells.push({"22", {}});
  table.add_row(cells);

  CHECK_EQUAL(table.to_string(false, "").view(),
              "NAME   N\n"
              "\xc3\xa9\xc3\xa9     1\n"
              "abcd  22\n");
}

static fn test_styled_cells_never_wrap_padding() throws -> void
{
  let table = ReportTable{heap_allocator()};
  table.add_column("KEY", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("VALUE", report_table_alignment::Left, colors::ansi::GREEN);

  let cells = ArrayList<report_table_cell_view>{heap_allocator()};
  cells.push({"k", {}});
  cells.push({"v", {}});
  table.add_row(cells);

  cells.clear();
  cells.push({"longer", colors::ansi::RED});
  cells.push({"w", {}});
  table.add_row(cells);

  let expected = String{heap_allocator()};
  expected += colors::ansi::BOLD_CYAN;
  expected += "KEY";
  expected += colors::ansi::RESET;
  expected += "     ";
  expected += colors::ansi::GREEN;
  expected += "VALUE";
  expected += colors::ansi::RESET;
  expected += '\n';
  expected += colors::ansi::BOLD_CYAN;
  expected += "k";
  expected += colors::ansi::RESET;
  expected += "       ";
  expected += colors::ansi::GREEN;
  expected += "v";
  expected += colors::ansi::RESET;
  expected += '\n';
  expected += colors::ansi::RED;
  expected += "longer";
  expected += colors::ansi::RESET;
  expected += "  ";
  expected += colors::ansi::GREEN;
  expected += "w";
  expected += colors::ansi::RESET;
  expected += '\n';

  CHECK_EQUAL(table.to_string(false, "").view(),
              "KEY     VALUE\n"
              "k       v\n"
              "longer  w\n");
  CHECK_EQUAL(table.to_string(true, "").view(), expected.view());
}

static fn test_titled_table_indents_body_beneath_title() throws -> void
{
  let const table = make_two_column_table();
  let output = String{heap_allocator()};
  append_titled_report_table(output, "Files", table, false);

  CHECK_EQUAL(output.view(),
              "Files\n"
              "  NAME   SIZE\n"
              "  alpha     7\n"
              "  b      1234\n");

  append_titled_report_table(output, "More", table, false);
  CHECK_EQUAL(output.view(),
              "Files\n"
              "  NAME   SIZE\n"
              "  alpha     7\n"
              "  b      1234\n"
              "\n"
              "More\n"
              "  NAME   SIZE\n"
              "  alpha     7\n"
              "  b      1234\n");
}

static fn test_empty_table_policy() throws -> void
{
  let table = ReportTable{heap_allocator()};
  table.add_column("NAME");
  table.add_column("SIZE");

  CHECK_EQUAL(table.get_row_count(), 0);
  CHECK(table.to_string().is_empty());

  let output = String{heap_allocator()};
  append_titled_report_table(output, "Nothing", table, false);
  CHECK(output.is_empty());

  table.set_empty_visible(true);
  CHECK_EQUAL(table.to_string(false, "").view(), "NAME  SIZE\n");
  append_titled_report_table(output, "Nothing", table, false);
  CHECK_EQUAL(output.view(), "Nothing\n  NAME  SIZE\n");
}

static fn test_repeated_render_is_identical() throws -> void
{
  let const table = make_two_column_table();
  let const first = table.to_string(false, "  ");
  let const second = table.to_string(false, "  ");
  let const colored_first = table.to_string(true, "  ");
  let const colored_second = table.to_string(true, "  ");

  CHECK_EQUAL(first.view(), second.view());
  CHECK_EQUAL(colored_first.view(), colored_second.view());
}

static fn test_no_row_ends_in_a_space() throws -> void
{
  let const table = make_two_column_table();
  let const rendered = table.to_string(false, "  ");
  let const view = rendered.view();

  for (usize index = 1; index < view.length; index++)
    if (view[index] == '\n') CHECK(view[index - 1] != ' ');
}

static fn test_minimum_width_survives_clearing_rows() throws -> void
{
  let table = ReportTable{heap_allocator()};
  table.set_header_visible(false);
  table.add_column("", report_table_alignment::Right);
  table.add_column("");
  table.set_column_min_width(0, 4);

  let cells = ArrayList<report_table_cell_view>{heap_allocator()};
  cells.push({"7", {}});
  cells.push({"a", {}});
  table.add_row(cells);
  CHECK_EQUAL(table.to_string(false, "").view(), "   7  a\n");

  table.clear_rows();
  CHECK_EQUAL(table.get_row_count(), 0);

  cells.clear();
  cells.push({"123456", {}});
  cells.push({"b", {}});
  table.add_row(cells);
  CHECK_EQUAL(table.to_string(false, "").view(), "123456  b\n");
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_minimum_width_survives_clearing_rows);
  RUN_TEST(test_right_aligned_last_column_keeps_left_padding);
  RUN_TEST(test_last_left_column_is_not_padded);
  RUN_TEST(test_titleless_default_indentation);
  RUN_TEST(test_empty_cells_leave_no_trailing_space);
  RUN_TEST(test_multibyte_display_width);
  RUN_TEST(test_styled_cells_never_wrap_padding);
  RUN_TEST(test_titled_table_indents_body_beneath_title);
  RUN_TEST(test_empty_table_policy);
  RUN_TEST(test_repeated_render_is_identical);
  RUN_TEST(test_no_row_ends_in_a_space);

  return koshka::unit::finish("report_table_test");
}
