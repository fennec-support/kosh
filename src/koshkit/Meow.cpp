/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the meow utility, a terminal screensaver. A walking cat
 * bounces across a twinkling starfield, blinks, and meows at the walls until a
 * key is pressed; redirected output receives one still cat instead.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../CliLive.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

#include <cmath>

KOSHKIT_UTIL_DECL("[-f fps]",
                  "The meow utility shows a walking cat until a key is "
                  "pressed.");

FLAG(MEOW_FPS,
     String,
     'f',
     "fps",
     "Draw this many frames per second, from 1 to 240. The default is 60.");

REGISTER_KOSHKIT_UTIL_FLAGS(Meow);

namespace koshka::koshkit {

namespace {

constexpr u32   DEFAULT_FRAMES_PER_SECOND = 60;
constexpr u32   MAXIMUM_FRAMES_PER_SECOND = 240;
constexpr usize CAT_ROW_COUNT             = 4;
constexpr usize CAT_COLUMN_COUNT          = 18;
constexpr usize CAT_HEAD_COLUMN_COUNT     = 7;
constexpr f64   COLUMNS_PER_SECOND        = 14.0;
constexpr f64   ROWS_PER_SECOND           = 4.0;
constexpr f64   STRIDE_SECONDS            = 0.2;
constexpr f64   BLINK_PERIOD_SECONDS      = 3.1;
constexpr f64   BLINK_SECONDS             = 0.15;
constexpr f64   MEOW_SECONDS              = 1.2;
constexpr f64   TWINKLE_PERIOD_SECONDS    = 0.6;
constexpr f64   LONGEST_STEP_SECONDS      = 0.1;

constexpr StringView CAT_LEFT_ROWS[CAT_ROW_COUNT] = {
    " /\\_/\\            ",
    "( o.o )_________  ",
    " > ^ <          )~",
    "  ||  ||   ||  || ",
};

constexpr StringView CAT_RIGHT_ROWS[CAT_ROW_COUNT] = {
    "            /\\_/\\ ",
    "  _________( o.o )",
    "~(          > ^ < ",
    " ||  ||   ||  ||  ",
};

constexpr StringView CAT_LEFT_STRIDE_ROW  = "  //  \\\\   //  \\\\ ";
constexpr StringView CAT_RIGHT_STRIDE_ROW = " //  \\\\   //  \\\\  ";
constexpr StringView CAT_BLINK_FACE       = "( -.- )";
constexpr StringView MEOW_TEXT            = "meow!";

enum class cell_style : u8
{
  Plain,
  Star,
  Cat,
  Bubble,
};

struct cat_motion
{
  f64  column{0.0};
  f64  row{1.0};
  bool is_moving_right{true};
  bool is_moving_down{true};
  f64  meow_seconds_left{0.0};
};

pure fn
mix_bits(u64 value) wontthrow -> u64
{
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33;

  return value;
}

pure fn
style_escape(cell_style style) wontthrow -> StringView
{
  switch (style) {
  case cell_style::Plain:  return colors::ansi::RESET;
  case cell_style::Star:   return colors::ansi::DIM;
  case cell_style::Cat:    return colors::ansi::BOLD_YELLOW;
  case cell_style::Bubble: return colors::ansi::BRIGHT_MAGENTA;
  }

  return colors::ansi::RESET;
}

fn
advance_cat(cat_motion &motion,
            f64         step_seconds,
            usize       column_count,
            usize       row_count) wontthrow -> void
{
  let const last_column =
      column_count > CAT_COLUMN_COUNT
          ? static_cast<f64>(column_count - CAT_COLUMN_COUNT)
          : 0.0;
  let const last_row     = row_count > CAT_ROW_COUNT + 1
                               ? static_cast<f64>(row_count - CAT_ROW_COUNT)
                               : 1.0;
  bool      did_hit_wall = false;

  motion.column +=
      (motion.is_moving_right ? 1.0 : -1.0) * COLUMNS_PER_SECOND * step_seconds;
  motion.row +=
      (motion.is_moving_down ? 1.0 : -1.0) * ROWS_PER_SECOND * step_seconds;

  if (motion.column >= last_column) {
    motion.column          = last_column;
    did_hit_wall           = motion.is_moving_right;
    motion.is_moving_right = false;
  } else if (motion.column <= 0.0) {
    motion.column          = 0.0;
    did_hit_wall           = !motion.is_moving_right;
    motion.is_moving_right = true;
  }

  if (motion.row >= last_row) {
    motion.row            = last_row;
    did_hit_wall          = did_hit_wall || motion.is_moving_down;
    motion.is_moving_down = false;
  } else if (motion.row <= 1.0) {
    motion.row            = 1.0;
    did_hit_wall          = did_hit_wall || !motion.is_moving_down;
    motion.is_moving_down = true;
  }

  if (did_hit_wall) {
    motion.meow_seconds_left = MEOW_SECONDS;
  } else if (motion.meow_seconds_left > 0.0) {
    motion.meow_seconds_left -= step_seconds;
  }
}

fn
render_scene(String           &frame,
             const cat_motion &motion,
             f64               elapsed_seconds,
             usize             column_count,
             usize             row_count,
             bool              should_color,
             Allocator         allocator) throws -> void
{
  let cells  = ArrayList<char>{allocator};
  let styles = ArrayList<cell_style>{allocator};

  cells.reserve(column_count * row_count);
  styles.reserve(column_count * row_count);

  let const twinkle_epoch =
      static_cast<u64>(elapsed_seconds / TWINKLE_PERIOD_SECONDS);

  for (usize row = 0; row < row_count; row++) {
    for (usize column = 0; column < column_count; column++) {
      let const cell_seed = mix_bits((static_cast<u64>(row) << 32) | column);

      if (cell_seed % 23 != 0) {
        cells.push(' ');
        styles.push(cell_style::Plain);
        continue;
      }

      let const phase = mix_bits(cell_seed ^ twinkle_epoch) % 4;

      cells.push(phase == 0 ? '*' : phase == 1 ? '+' : '.');
      styles.push(cell_style::Star);
    }
  }

  let const do_place = [&](usize row, usize column, StringView text,
                           cell_style style) wontthrow {
    if (row >= row_count) return;

    usize first_index = 0;
    usize end_index   = text.length;

    while (first_index < end_index && text[first_index] == ' ') {
      first_index++;
    }

    while (end_index > first_index && text[end_index - 1] == ' ') {
      end_index--;
    }

    for (usize index = first_index; index < end_index; index++) {
      if (column + index >= column_count) break;

      let const cell_index = (row * column_count) + column + index;

      cells[cell_index]  = text[index];
      styles[cell_index] = style;
    }
  };

  let const  cat_row    = static_cast<usize>(motion.row);
  let const  cat_column = static_cast<usize>(motion.column);
  let const *cat_rows = motion.is_moving_right ? CAT_RIGHT_ROWS : CAT_LEFT_ROWS;
  let const  is_striding =
      static_cast<u64>(elapsed_seconds / STRIDE_SECONDS) % 2 == 1;
  let const is_blinking =
      elapsed_seconds > BLINK_PERIOD_SECONDS &&
      std::fmod(elapsed_seconds, BLINK_PERIOD_SECONDS) < BLINK_SECONDS;
  let const head_column =
      motion.is_moving_right ? CAT_COLUMN_COUNT - CAT_HEAD_COLUMN_COUNT : 0;

  for (usize index = 0; index < CAT_ROW_COUNT; index++) {
    let row_text = cat_rows[index];

    if (index + 1 == CAT_ROW_COUNT && is_striding) {
      row_text =
          motion.is_moving_right ? CAT_RIGHT_STRIDE_ROW : CAT_LEFT_STRIDE_ROW;
    }

    do_place(cat_row + index, cat_column, row_text, cell_style::Cat);
  }

  if (is_blinking) {
    do_place(cat_row + 1, cat_column + head_column, CAT_BLINK_FACE,
             cell_style::Cat);
  }

  if (motion.meow_seconds_left > 0.0 && cat_row > 0) {
    let const head_start = cat_column + head_column;
    let const bubble_start =
        motion.is_moving_right
            ? head_start + CAT_HEAD_COLUMN_COUNT
            : (head_start > MEOW_TEXT.length ? head_start - MEOW_TEXT.length
                                             : 0);

    do_place(cat_row - 1, bubble_start, MEOW_TEXT, cell_style::Bubble);
  }

  for (usize row = 0; row < row_count; row++) {
    let active_style = cell_style::Plain;

    for (usize column = 0; column < column_count; column++) {
      let const cell_index = (row * column_count) + column;

      if (should_color && styles[cell_index] != active_style) {
        active_style = styles[cell_index];
        frame += style_escape(active_style);
      }

      frame.push(cells[cell_index]);
    }

    if (should_color && active_style != cell_style::Plain)
      frame += colors::ansi::RESET;

    if (row + 1 < row_count) frame += "\n";
  }
}

fn
print_still_cat(const ExecContext &ec, Allocator allocator) throws
    -> void
{
  let text = String{allocator};

  text += MEOW_TEXT;
  text += "\n";

  for (let const row : CAT_LEFT_ROWS) {
    usize kept_length = row.length;

    while (kept_length > 0 && row[kept_length - 1] == ' ') {
      kept_length--;
    }

    text += row.substring_of_length(0, kept_length);
    text += "\n";
  }

  ec.print_to_stdout(text.view());
}

} /* namespace */

fn
Meow::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "extra operand '" + operands[0] + "'");

    return 1;
  }

  let const allocator         = cxt.scratch_allocator();
  u32       frames_per_second = DEFAULT_FRAMES_PER_SECOND;

  if (FLAG_MEOW_FPS.is_set()) {
    let const parsed = FLAG_MEOW_FPS.value().to<u32>();

    if (parsed.is_error() || parsed.value() == 0 ||
        parsed.value() > MAXIMUM_FRAMES_PER_SECOND)
    {
      KOSHKIT_REPORT_ERROR_AT(FLAG_MEOW_FPS.value_location(),
                              "invalid frame rate '" +
                                  String{allocator, FLAG_MEOW_FPS.value()} +
                                  "', expected 1 to 240");

      return 1;
    }

    frames_per_second = parsed.value();
  }

  if (!os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT))) {
    print_still_cat(ec, allocator);

    return 0;
  }

  live_view_options options{};

  options.title                    = "meow";
  options.refresh_interval_seconds = 1.0 / static_cast<f64>(frames_per_second);
  options.should_render_first_frame_immediately = true;
  options.should_show_header                    = false;

  let const should_color           = koshkit_should_color();
  let const started_at_nanoseconds = os::monotonic_nanos();
  let const seed                   = mix_bits(started_at_nanoseconds);
  let       motion                 = cat_motion{};
  u64       last_frame_nanoseconds = started_at_nanoseconds;

  motion.column          = static_cast<f64>(seed % 13);
  motion.row             = 1.0 + static_cast<f64>((seed >> 8) % 5);
  motion.is_moving_right = (seed >> 16) % 2 == 0;

  let const do_sample = [](u64, Allocator) wontthrow -> Maybe<i32> {
    return None;
  };

  let const do_render =
      [&](String &frame, const live_view_dimensions &dimensions,
          Allocator frame_allocator) throws -> Maybe<i32> {
    let const column_count = static_cast<usize>(dimensions.columns);
    let const row_count =
        dimensions.rows > 1 ? static_cast<usize>(dimensions.rows) - 1 : 1;
    let const now_nanoseconds = os::monotonic_nanos();
    let       step_seconds =
        static_cast<f64>(now_nanoseconds - last_frame_nanoseconds) / 1e9;

    if (step_seconds > LONGEST_STEP_SECONDS)
      step_seconds = LONGEST_STEP_SECONDS;

    last_frame_nanoseconds = now_nanoseconds;
    advance_cat(motion, step_seconds, column_count, row_count);

    let const elapsed_seconds =
        static_cast<f64>(now_nanoseconds - started_at_nanoseconds) / 1e9;

    render_scene(frame, motion, elapsed_seconds, column_count, row_count,
                 should_color, frame_allocator);

    return None;
  };

  let const do_key = [](live_view_key) wontthrow -> live_view_key_action {
    return live_view_key_action::Quit;
  };

  return run_live_view(ec, options, do_sample, do_render, do_key);
}

} /* namespace koshka::koshkit */
