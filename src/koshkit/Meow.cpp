/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the meow utility, a terminal screensaver. Round
 * pixel-art cats drawn with half blocks float and paddle through space, chase
 * a ball of yarn, curl up to nap, and explode against the walls of the sky.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../CLILive.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

#include <cmath>

KOSHKIT_UTIL_DECL("[-f fps] [-s speed] [-n cats]",
                  "The meow utility shows cats floating in a night sky until a "
                  "key other than a control key is pressed.");

FLAG(MEOW_FPS,
     String,
     'f',
     "fps",
     "Draw this many frames per second, from 1 to 240. The default is 60.");
FLAG(MEOW_SPEED,
     String,
     's',
     "speed",
     "Run the scene this many times faster, from 0.1 to 10. The default is 1.");
FLAG(MEOW_CATS,
     String,
     'n',
     "cats",
     "Show this many cats, from 1 to 8. The default is 1.");

REGISTER_KOSHKIT_UTIL_FLAGS(Meow);

namespace koshka::koshkit {

namespace {

constexpr u32   DEFAULT_FRAMES_PER_SECOND = 60;
constexpr u32   MAXIMUM_FRAMES_PER_SECOND = 240;
constexpr f64   MINIMUM_SPEED             = 0.1;
constexpr f64   MAXIMUM_SPEED             = 10.0;
constexpr f64   SPEED_STEP                = 1.25;
constexpr usize MAXIMUM_CAT_COUNT         = 8;
constexpr usize CAT_PIXEL_WIDTH           = 28;
constexpr usize CAT_PIXEL_HEIGHT          = 12;
constexpr usize CAT_ROW_COUNT             = CAT_PIXEL_HEIGHT / 2;
constexpr usize YARN_THREAD_POINT_COUNT   = 24;
constexpr usize PARTICLE_COUNT            = 320;
constexpr usize EXPLOSION_COUNT           = 6;
constexpr usize SHOOTING_STAR_COUNT       = 3;
constexpr usize SHOOTING_STAR_TAIL_COUNT  = 7;
constexpr usize STAR_LAYER_COUNT          = 3;
constexpr f64   PI                        = 3.14159265358979323846;

constexpr f64 DRIFT_COLUMNS_PER_SECOND   = 9.0;
constexpr f64 DRIFT_ROWS_PER_SECOND      = 4.0;
constexpr f64 CAT_HEAD_ROW               = 2.0;
constexpr f64 CHASE_COLUMNS_PER_SECOND   = 15.0;
constexpr f64 DASH_COLUMNS_PER_SECOND    = 36.0;
constexpr f64 STEERING_PER_SECOND        = 1.6;
constexpr f64 WANDER_RADIANS_PER_SECOND  = 0.9;
constexpr f64 PADDLE_SECONDS             = 0.32;
constexpr f64 BOB_RADIANS_PER_SECOND     = 2.3;
constexpr f64 TAIL_SWISH_SECONDS         = 0.45;
constexpr f64 BLINK_PERIOD_SECONDS       = 3.4;
constexpr f64 BLINK_SECONDS              = 0.14;
constexpr f64 BUBBLE_SECONDS             = 1.3;
constexpr f64 BRACE_SECONDS              = 0.5;
constexpr f64 DASH_SECONDS               = 0.5;
constexpr f64 HAPPY_SECONDS              = 1.1;
constexpr f64 DIZZY_SECONDS              = 1.2;
constexpr f64 TUMBLE_SECONDS             = 0.12;
constexpr f64 HISS_SECONDS               = 0.9;
constexpr f64 WALL_COOLDOWN_SECONDS      = 0.7;
constexpr f64 BOUNCE_COLUMNS_PER_SECOND  = 28.0;
constexpr f64 BOUNCE_ROWS_PER_SECOND     = 8.0;
constexpr f64 COLLISION_IMPULSE          = 22.0;
constexpr f64 MAXIMUM_COLUMNS_PER_SECOND = 38.0;
constexpr f64 MAXIMUM_ROWS_PER_SECOND    = 11.0;
constexpr f64 COAST_STEERING_PER_SECOND  = 0.9;
constexpr f64 YARN_DRIFT_PER_SECOND      = 5.0;
constexpr f64 YARN_KICK_MINIMUM          = 45.0;
constexpr f64 YARN_KICK_MAXIMUM          = 70.0;
constexpr f64 CURL_RATE_PER_SECOND       = 0.05;
constexpr f64 CHASE_RATE_PER_SECOND      = 0.2;
constexpr f64 GROOM_SECONDS              = 1.8;
constexpr f64 DOZE_AFTER_SECONDS         = 4.0;
constexpr f64 YARN_FRICTION_PER_SECOND   = 0.35;
constexpr f64 YARN_THREAD_SAMPLE_SECONDS = 0.04;
constexpr f64 EXPLOSION_SECONDS          = 0.85;
constexpr f64 FIREBALL_SECONDS           = 0.4;
constexpr f64 FIREBALL_GROWTH            = 26.0;
constexpr f64 FIREBALL_MAXIMUM_RADIUS    = 7.0;
constexpr f64 SHOCKWAVE_GROWTH           = 40.0;
constexpr f64 SHAKE_SECONDS              = 0.3;
constexpr f64 TWINKLE_PERIOD_SECONDS     = 0.6;
constexpr f64 SHOOTING_STAR_GAP_SECONDS  = 3.0;
constexpr f64 SHOOTING_STAR_COLUMN_SPEED = 46.0;
constexpr f64 SHOOTING_STAR_ROW_SPEED    = 14.0;
constexpr f64 SPEED_NOTICE_SECONDS       = 1.2;
constexpr f64 LONGEST_STEP_SECONDS       = 0.1;

constexpr usize CAT_HEAD_CENTER = 4;

struct cat_palette
{
  u8 body;
  u8 dark;
  u8 light;
  u8 eye;
};

struct cat_breed_art
{
  const StringView  *body;
  usize              body_row_count;
  const StringView  *tail;
  usize              tail_row_count;
  usize              tail_row;
  usize              tail_column;
  const StringView  *paddle;
  usize              paddle_frame_count;
  usize              leg_row_count;
  const StringView  *tucked;
  const StringView  *dash;
  const StringView  *curled;
  usize              curled_row_count;
  StringView         spike;
  usize              spike_row;
  const cat_palette *palettes;
  usize              palette_count;
};

constexpr StringView DOMESTIC_BODY[] = {
    "..B....B....................", ".BIB..BIB.....DDDDD.........",
    ".BBBBBBBB..BBBBDBBBBBB......", "BBEBBBBEBBBBBBBDBBBBBBB.....",
    "BBBBNNBBBBBBBBBBDBBBBBBB....", ".BLLLLLLBBBBBBBBDBBBBBBB....",
    "..CCGCCBBBBBLLLLLLBBBBB.....", "...BBBBBBBBLLLLLLLLBBBB.....",
    "....BBBBB..LLLLLLL.BBB......",
};

constexpr StringView DOMESTIC_TAIL[] = {
    "...DD.", "....BD", "....DB", "...BD.", "..DB..", "BB....",
    ".DD...", "BD....", "DB....", ".BD...", "..DB..", "BB....",
};

constexpr StringView DOMESTIC_PADDLE[] = {
    "..BB.................BB.....", ".DB...................BD....",
    "............................", "...BB...............BB......",
    "...DB...............BD......", "............................",
    "....BB.BB.........BB.BB.....", "....DD.DD.........DD.DD.....",
    "............................", "...BB...............BB......",
    "...DB...............BD......", "............................",
};

constexpr StringView DOMESTIC_TUCKED[] = {
    ".....BBBB.........BBBB......",
    ".....DDDD.........DDDD......",
    "............................",
};

constexpr StringView DOMESTIC_DASH[] = {
    "BB......................BB..",
    "D........................BD.",
    "............................",
};

constexpr StringView DOMESTIC_CURLED[] = {
    "....BB..BB......", "...BIBBBIB......", "..BBBBBBBBBB....",
    ".BBEBBBBEBBBBB..", ".BBBBNNBBBBBBBB.", ".BLLLLLLBBBDBBB.",
    "..BBBBBBBBBBBDD.", "....BBBBBBBDD...",
};

constexpr cat_palette DOMESTIC_PALETTES[] = {
    {208, 166, 223, 46 },
    {245, 240, 252, 39 },
    {236, 233, 240, 226},
    {255, 250, 231, 33 },
    {231, 208, 255, 46 },
    {230, 94,  255, 33 },
    {234, 232, 255, 118},
    {137, 94,  180, 214},
};

constexpr StringView BRITISH_BODY[] = {
    "..B.....B...................", ".BIB...BIB....BBBBBB........",
    ".BBBBBBBBB..BBBBBBBBBBB.....", "BBEEBBBEEBBBBBBBBBBBBBBB....",
    "BBBBBNNBBBBBBBBBBBBBBBBBB...", "BBLLLLLLLBBBBBBBBBBBBBBBBB..",
    ".BLLLLLLLBBBBBLLLLLLBBBBBB..", "..BBCCGCCBBBLLLLLLLLLBBBBB..",
    "...BBBBBBBBLLLLLLLLLLBBBB...", "....BBBBBBBBBBBBBBBBBBB.....",
};

constexpr StringView BRITISH_TAIL[] = {
    "..DD", "..BD", ".BB.", "BB..", "B...",
    ".DD.", ".DB.", "..BB", ".BB.", "B...",
};

constexpr StringView BRITISH_PADDLE[] = {
    "...BB..BB.......BB..BB......", "...DD..DD.......DD..DD......",
    "..BB....BB.....BB....BB.....", "..DD....DD.....DD....DD.....",
    "...BB..BB.......BB..BB......", "...DD..DD.......DD..DD......",
    "....BB.BB.......BB.BB.......", "....DD.DD.......DD.DD.......",
};

constexpr StringView BRITISH_TUCKED[] = {
    "....BBBB........BBBB........",
    "............................",
};

constexpr StringView BRITISH_DASH[] = {
    ".BB.....................BB..",
    "DD........................DD",
};

constexpr StringView BRITISH_CURLED[] = {
    "...B......B.....", "..BIB....BIB....", ".BBBBBBBBBBBBB..",
    "BBEEBBBEEBBBBBB.", "BBBBBNNBBBBBBBBB", "BLLLLLLLBBBBBBBB",
    ".BBBBBBBBBBBBBDD", "..BBBBBBBBBBBDD.",
};

constexpr cat_palette BRITISH_PALETTES[] = {
    {245, 240, 250, 214},
    {223, 180, 230, 214},
    {250, 246, 255, 208},
    {240, 237, 246, 172},
};

constexpr StringView ORIENTAL_BODY[] = {
    ".B.....B....................", ".BB...BB....................",
    ".BIB.BIB....................", "..BBBBB.....................",
    ".BEBBBEB.......BBBB.........", "..BBNBB..BBBBBBBBBBBBB......",
    "...LLL.BBBBBBBBBBBBBBBB.....", "...CGCBBBLLLLLLLLBBBBB......",
};

constexpr StringView ORIENTAL_TAIL[] = {
    "......D", ".....D.", "....B..", "...B...", "..B....", "BB.....",
    "...DD..", "..D....", "...B...", "....B..", "...B...", "BB.....",
};

constexpr StringView ORIENTAL_PADDLE[] = {
    "......BB............BB......", ".....BB..............BB.....",
    "....BB................BB....", "...DD..................DD...",
    "......BB............BB......", "......BB............BB......",
    "......BB............BB......", "......DD............DD......",
    ".......BB..........BB.......", "........BB........BB........",
    ".........BB......BB.........", ".........DD......DD.........",
    "......BB............BB......", "......BB............BB......",
    "......BB............BB......", "......DD............DD......",
};

constexpr StringView ORIENTAL_TUCKED[] = {
    "........BBBB.....BBBB.......",
    "........DDDD.....DDDD.......",
    "............................",
    "............................",
};

constexpr StringView ORIENTAL_DASH[] = {
    "....BB..............BB......",
    "..BB..................BB....",
    "DD......................DD..",
    "............................",
};

constexpr StringView ORIENTAL_CURLED[] = {
    ".B....B.........", ".BB..BB.........", ".BIBBIB.........",
    "..BBBBB.........", ".BEBBBEBBBBB....", "..BBNBBBBBBBBB..",
    "...LLLBBBBBBDDD.", ".....BBBBBDD....",
};

constexpr cat_palette ORIENTAL_PALETTES[] = {
    {235, 233, 239, 46 },
    {137, 94,  180, 118},
    {255, 250, 231, 33 },
    {223, 180, 230, 46 },
    {245, 240, 252, 40 },
};

constexpr cat_breed_art DOMESTIC_ART = {
    DOMESTIC_BODY,
    countof(DOMESTIC_BODY),
    DOMESTIC_TAIL,
    countof(DOMESTIC_TAIL) / 2,
    0,
    22,
    DOMESTIC_PADDLE,
    4,
    countof(DOMESTIC_TUCKED),
    DOMESTIC_TUCKED,
    DOMESTIC_DASH,
    DOMESTIC_CURLED,
    countof(DOMESTIC_CURLED),
    "...........D.D.D.D.D.D......",
    0,
    DOMESTIC_PALETTES,
    countof(DOMESTIC_PALETTES),
};

constexpr cat_breed_art BRITISH_ART = {
    BRITISH_BODY,
    countof(BRITISH_BODY),
    BRITISH_TAIL,
    countof(BRITISH_TAIL) / 2,
    1,
    24,
    BRITISH_PADDLE,
    4,
    countof(BRITISH_TUCKED),
    BRITISH_TUCKED,
    BRITISH_DASH,
    BRITISH_CURLED,
    countof(BRITISH_CURLED),
    "............D.D.D.D.D.D.....",
    1,
    BRITISH_PALETTES,
    countof(BRITISH_PALETTES),
};

constexpr cat_breed_art ORIENTAL_ART = {
    ORIENTAL_BODY,
    countof(ORIENTAL_BODY),
    ORIENTAL_TAIL,
    countof(ORIENTAL_TAIL) / 2,
    0,
    21,
    ORIENTAL_PADDLE,
    4,
    countof(ORIENTAL_TUCKED),
    ORIENTAL_TUCKED,
    ORIENTAL_DASH,
    ORIENTAL_CURLED,
    countof(ORIENTAL_CURLED),
    "...............D.D.D........",
    4,
    ORIENTAL_PALETTES,
    countof(ORIENTAL_PALETTES),
};

constexpr const cat_breed_art *CAT_BREEDS[] = {
    &DOMESTIC_ART,
    &DOMESTIC_ART,
    &BRITISH_ART,
    &ORIENTAL_ART,
};

constexpr u8 NOSE_COLOR      = 211;
constexpr u8 INNER_EAR_COLOR = 217;
constexpr u8 COLLAR_COLOR    = 196;
constexpr u8 BELL_COLOR      = 220;

constexpr StringView WALL_PHRASES[]   = {"MEOW!", "mrrROW!", "yowl!", "MRAH!"};
constexpr StringView HAPPY_PHRASES[]  = {"mrrp!", "nya~", "prrt!", "mew!"};
constexpr StringView FRIEND_PHRASES[] = {"nya~", "mew", "prrr"};

constexpr u8  STAR_LAYER_COLORS[STAR_LAYER_COUNT]             = {238, 244, 252};
constexpr f64 STAR_LAYER_COLUMNS_PER_SECOND[STAR_LAYER_COUNT] = {0.6, 1.5, 3.5};
constexpr u64 STAR_LAYER_DENSITY[STAR_LAYER_COUNT]            = {29, 47, 89};
constexpr u8  SHOCKWAVE_COLORS[] = {231, 229, 226, 220, 214,
                                    208, 202, 196, 160, 124};
constexpr u8  DEBRIS_COLORS[]    = {226, 214, 208, 202, 196};
constexpr u8  SMOKE_COLORS[]     = {252, 248, 244, 240, 237};
constexpr u8  MOON_COLOR         = 229;
constexpr u8  MOON_SHADE_COLOR   = 187;
constexpr u8  BUBBLE_COLOR       = 213;
constexpr u8  HEART_COLOR        = 205;
constexpr u8  SLEEP_COLOR        = 117;
constexpr u8  DIZZY_STAR_COLOR   = 226;
constexpr u8  YARN_COLOR         = 197;
constexpr u8  YARN_SHINE_COLOR   = 211;
constexpr u8  THREAD_COLOR       = 161;
constexpr u8  NOTICE_COLOR       = 159;
constexpr u16 BOLD_STYLE_BIT     = 0x100;
constexpr u16 OVERLAY_STYLE_BIT  = 0x200;

enum class cat_activity : u8
{
  Floating,
  Chasing,
  Bracing,
  Dashing,
  Curling,
  Dizzy,
  Hissing,
};

enum class particle_kind : u8
{
  Debris,
  Smoke,
  Heart,
  Sleep,
  Sparkle,
};

struct noise_source
{
  u64 state{0x9e3779b97f4a7c15ULL};

  fn
  next_bits() wontthrow -> u64
  {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;

    return state;
  }

  fn
  next_unit() wontthrow -> f64
  {
    return static_cast<f64>(next_bits() >> 11) * 0x1.0p-53;
  }

  fn
  next_between(f64 low, f64 high) wontthrow -> f64
  {
    return low + ((high - low) * next_unit());
  }

  fn
  next_index(usize count) wontthrow -> usize
  {
    return static_cast<usize>(next_bits() % count);
  }
};

struct thread_point
{
  f64  column{0.0};
  f64  row{0.0};
  bool is_used{false};
};

struct cat_state
{
  f64                  column{0.0};
  f64                  row{1.0};
  f64                  columns_per_second{0.0};
  f64                  rows_per_second{0.0};
  f64                  wander_radians{0.0};
  f64                  cruise_factor{1.0};
  bool                 is_facing_right{true};
  cat_activity         activity{cat_activity::Floating};
  f64                  activity_seconds{0.0};
  f64                  activity_limit_seconds{0.0};
  f64                  phase_seconds{0.0};
  f64                  happy_seconds{0.0};
  f64                  wall_cooldown_seconds{0.0};
  f64                  bubble_seconds{0.0};
  StringView           bubble_text{};
  cat_palette          palette{DOMESTIC_PALETTES[0]};
  const cat_breed_art *art{&DOMESTIC_ART};
};

struct particle
{
  particle_kind kind{particle_kind::Sparkle};
  f64           column{0.0};
  f64           row{0.0};
  f64           columns_per_second{0.0};
  f64           rows_per_second{0.0};
  f64           seconds_left{0.0};
  f64           lifetime_seconds{1.0};
  u8            color{231};
  char          glyph{'*'};
};

struct explosion
{
  f64  column{0.0};
  f64  row{0.0};
  f64  age_seconds{0.0};
  bool is_active{false};
};

struct shooting_star
{
  f64  column{0.0};
  f64  row{0.0};
  bool is_active{false};
};

struct yarn_ball
{
  f64          column{10.0};
  f64          row{5.0};
  f64          columns_per_second{0.0};
  f64          rows_per_second{0.0};
  f64          spin_seconds{0.0};
  f64          drift_radians{0.0};
  thread_point thread[YARN_THREAD_POINT_COUNT]{};
  usize        thread_head{0};
  f64          thread_sample_seconds{0.0};
};

struct scene_state
{
  noise_source  noise{};
  cat_state     cats[MAXIMUM_CAT_COUNT]{};
  usize         cat_count{1};
  particle      particles[PARTICLE_COUNT]{};
  usize         next_particle{0};
  explosion     explosions[EXPLOSION_COUNT]{};
  usize         next_explosion{0};
  shooting_star shooting_stars[SHOOTING_STAR_COUNT]{};
  f64           next_shooting_star_seconds{SHOOTING_STAR_GAP_SECONDS};
  yarn_ball     yarn{};
  f64           elapsed_seconds{0.0};
  f64           shake_seconds{0.0};
  f64           speed{1.0};
  f64           speed_notice_seconds{0.0};
  usize         column_count{80};
  usize         row_count{24};
  bool          is_paused{false};
};

struct scene_canvas
{
  ArrayList<char> cells;
  ArrayList<u16>  styles;
  ArrayList<u8>   pixels;
  usize           column_count{0};
  usize           row_count{0};

  fn
  put(isize row, isize column, char glyph, u16 style) wontthrow -> void
  {
    if (row < 0 || column < 0) return;
    if (static_cast<usize>(row) >= row_count) return;
    if (static_cast<usize>(column) >= column_count) return;

    let const cell_index =
        (static_cast<usize>(row) * column_count) + static_cast<usize>(column);

    cells[cell_index]  = glyph;
    styles[cell_index] = style;
  }

  fn
  put_text(isize row, isize column, StringView text, u16 style)
      wontthrow -> void
  {
    for (usize index = 0; index < text.length; index++)
      put(row, column + static_cast<isize>(index), text[index], style);
  }

  fn
  put_pixel(isize pixel_row, isize column, u8 color) wontthrow -> void
  {
    if (pixel_row < 0 || column < 0 || color == 0) return;
    if (static_cast<usize>(pixel_row) >= row_count * 2) return;
    if (static_cast<usize>(column) >= column_count) return;

    pixels[(static_cast<usize>(pixel_row) * column_count) +
           static_cast<usize>(column)] = color;
  }

  fn
  pixel_at(usize pixel_row, usize column) const wontthrow -> u8
  {
    return pixels[(pixel_row * column_count) + column];
  }
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
bold(u8 color) wontthrow -> u16
{
  return static_cast<u16>(color | BOLD_STYLE_BIT);
}

pure fn
overlay(u16 style) wontthrow -> u16
{
  return static_cast<u16>(style | OVERLAY_STYLE_BIT);
}

pure fn
approach(f64 value, f64 target, f64 rate, f64 step_seconds) wontthrow -> f64
{
  return target + ((value - target) * std::exp(-rate * step_seconds));
}

pure fn
cat_last_column(usize column_count) wontthrow -> f64
{
  return static_cast<f64>(sub_sat(column_count, CAT_PIXEL_WIDTH));
}

pure fn
cat_last_row(usize row_count) wontthrow -> f64
{
  return row_count > CAT_ROW_COUNT + 2
             ? static_cast<f64>(row_count - CAT_ROW_COUNT)
             : 2.0;
}

pure fn
cat_head_column(const cat_state &cat) wontthrow -> f64
{
  return cat.is_facing_right
             ? cat.column +
                   static_cast<f64>(CAT_PIXEL_WIDTH - 1 - CAT_HEAD_CENTER)
             : cat.column + static_cast<f64>(CAT_HEAD_CENTER);
}

fn
spawn_particle(scene_state  &scene,
               particle_kind kind,
               f64           column,
               f64           row,
               f64           columns_per_second,
               f64           rows_per_second,
               f64           lifetime_seconds,
               u8            color,
               char          glyph) wontthrow -> void
{
  let &spawned = scene.particles[scene.next_particle];

  scene.next_particle = (scene.next_particle + 1) % PARTICLE_COUNT;

  spawned.kind               = kind;
  spawned.column             = column;
  spawned.row                = row;
  spawned.columns_per_second = columns_per_second;
  spawned.rows_per_second    = rows_per_second;
  spawned.seconds_left       = lifetime_seconds;
  spawned.lifetime_seconds   = lifetime_seconds;
  spawned.color              = color;
  spawned.glyph              = glyph;
}

fn
explode(scene_state &scene, f64 column, f64 row) wontthrow -> void
{
  let &blast = scene.explosions[scene.next_explosion];

  scene.next_explosion = (scene.next_explosion + 1) % EXPLOSION_COUNT;
  blast.column         = column;
  blast.row            = row;
  blast.age_seconds    = 0.0;
  blast.is_active      = true;
  scene.shake_seconds  = SHAKE_SECONDS;

  for (usize index = 0; index < 26; index++) {
    let const angle = scene.noise.next_between(0.0, 2.0 * PI);
    let const power = scene.noise.next_between(10.0, 32.0);

    spawn_particle(
        scene, particle_kind::Debris, column, row, std::cos(angle) * power,
        std::sin(angle) * power * 0.5, scene.noise.next_between(0.8, 1.5),
        DEBRIS_COLORS[scene.noise.next_index(countof(DEBRIS_COLORS))],
        "#%*+"[scene.noise.next_index(4)]);
  }

  for (usize index = 0; index < 9; index++) {
    spawn_particle(scene, particle_kind::Smoke,
                   column + scene.noise.next_between(-3.0, 3.0),
                   row + scene.noise.next_between(-1.0, 1.0),
                   scene.noise.next_between(-3.0, 3.0),
                   scene.noise.next_between(-1.5, 1.5),
                   scene.noise.next_between(1.4, 2.4), SMOKE_COLORS[0], 'O');
  }
}

fn
say(cat_state &cat, StringView text) wontthrow -> void
{
  cat.bubble_text    = text;
  cat.bubble_seconds = BUBBLE_SECONDS;
}

fn
set_activity(cat_state &cat, cat_activity activity, f64 limit_seconds) wontthrow
    -> void
{
  cat.activity               = activity;
  cat.activity_seconds       = 0.0;
  cat.activity_limit_seconds = limit_seconds;
}

fn
place_cats(scene_state &scene) wontthrow -> void
{
  for (usize index = 0; index < scene.cat_count; index++) {
    let &cat = scene.cats[index];

    for (usize attempt = 0; attempt < 8; attempt++) {
      cat.art = CAT_BREEDS[scene.noise.next_index(countof(CAT_BREEDS))];
      cat.palette =
          cat.art->palettes[scene.noise.next_index(cat.art->palette_count)];

      bool is_duplicate = false;

      for (usize other = 0; other < index; other++) {
        if (scene.cats[other].art == cat.art &&
            scene.cats[other].palette.body == cat.palette.body)
        {
          is_duplicate = true;
        }
      }

      if (!is_duplicate) break;
    }

    cat.column =
        scene.noise.next_between(0.0, cat_last_column(scene.column_count));
    cat.row = scene.noise.next_between(1.0, cat_last_row(scene.row_count));
    cat.wander_radians = scene.noise.next_between(0.0, 2.0 * PI);
    cat.cruise_factor  = scene.noise.next_between(0.75, 1.3);
    cat.columns_per_second =
        std::cos(cat.wander_radians) * DRIFT_COLUMNS_PER_SECOND;
    cat.rows_per_second = std::sin(cat.wander_radians) * DRIFT_ROWS_PER_SECOND;
    cat.is_facing_right = cat.columns_per_second > 0.0;
    cat.phase_seconds   = scene.noise.next_between(0.0, BLINK_PERIOD_SECONDS);
  }

  scene.yarn.column =
      scene.noise.next_between(4.0, static_cast<f64>(scene.column_count) - 4.0);
  scene.yarn.row =
      scene.noise.next_between(3.0, static_cast<f64>(scene.row_count) - 2.0);
  scene.yarn.columns_per_second = scene.noise.next_between(-4.0, 4.0);
  scene.yarn.rows_per_second    = scene.noise.next_between(-1.5, 1.5);
}

fn
hit_wall(scene_state &scene, cat_state &cat, f64 column, f64 row) wontthrow
    -> void
{
  if (cat.wall_cooldown_seconds > 0.0) return;

  cat.wall_cooldown_seconds = WALL_COOLDOWN_SECONDS;
  explode(scene, column, row);
  say(cat, WALL_PHRASES[scene.noise.next_index(countof(WALL_PHRASES))]);
  set_activity(cat, cat_activity::Dizzy, DIZZY_SECONDS);
}

fn
keep_cat_inside(scene_state &scene, cat_state &cat) wontthrow -> void
{
  let const last_column   = cat_last_column(scene.column_count);
  let const last_row      = cat_last_row(scene.row_count);
  let const middle_row    = cat.row + (CAT_ROW_COUNT / 2.0);
  let const middle_column = cat.column + (CAT_PIXEL_WIDTH / 2.0);

  let const do_bounce = [](f64 speed, f64 floor) wontthrow -> f64 {
    return speed > 0.0 ? -floor : floor;
  };

  if (cat.column >= last_column && cat.columns_per_second > 0.0) {
    cat.column = last_column;
    cat.columns_per_second =
        do_bounce(cat.columns_per_second, BOUNCE_COLUMNS_PER_SECOND);
    cat.wander_radians = PI - cat.wander_radians;
    hit_wall(scene, cat, static_cast<f64>(scene.column_count) - 1.0,
             middle_row);
  } else if (cat.column <= 0.0 && cat.columns_per_second < 0.0) {
    cat.column = 0.0;
    cat.columns_per_second =
        do_bounce(cat.columns_per_second, BOUNCE_COLUMNS_PER_SECOND);
    cat.wander_radians = PI - cat.wander_radians;
    hit_wall(scene, cat, 0.0, middle_row);
  }

  if (cat.row >= last_row && cat.rows_per_second > 0.0) {
    cat.row = last_row;
    cat.rows_per_second =
        do_bounce(cat.rows_per_second, BOUNCE_ROWS_PER_SECOND);
    cat.wander_radians = -cat.wander_radians;
    hit_wall(scene, cat, middle_column,
             static_cast<f64>(scene.row_count) - 1.0);
  } else if (cat.row <= 1.0 && cat.rows_per_second < 0.0) {
    cat.row = 1.0;
    cat.rows_per_second =
        do_bounce(cat.rows_per_second, BOUNCE_ROWS_PER_SECOND);
    cat.wander_radians = -cat.wander_radians;
    hit_wall(scene, cat, middle_column, 0.0);
  }

  cat.column = std::fmax(0.0, std::fmin(last_column, cat.column));
  cat.row    = std::fmax(1.0, std::fmin(last_row, cat.row));
}

fn
kick_yarn(scene_state &scene, cat_state &cat) wontthrow -> void
{
  let const direction = cat.is_facing_right ? 1.0 : -1.0;

  scene.yarn.columns_per_second =
      direction *
      scene.noise.next_between(YARN_KICK_MINIMUM, YARN_KICK_MAXIMUM);
  scene.yarn.rows_per_second = scene.noise.next_between(-14.0, 14.0);
  cat.happy_seconds          = HAPPY_SECONDS;
  cat.columns_per_second *= -0.4;
  say(cat, HAPPY_PHRASES[scene.noise.next_index(countof(HAPPY_PHRASES))]);

  for (usize index = 0; index < 10; index++) {
    spawn_particle(scene, particle_kind::Sparkle, scene.yarn.column,
                   scene.yarn.row, scene.noise.next_between(-14.0, 14.0),
                   scene.noise.next_between(-5.0, 5.0), 0.7, 226, '*');
  }
}

fn
advance_cat(scene_state &scene, cat_state &cat, f64 step_seconds) wontthrow
    -> void
{
  cat.phase_seconds += step_seconds;
  cat.activity_seconds += step_seconds;

  if (cat.happy_seconds > 0.0) cat.happy_seconds -= step_seconds;
  if (cat.bubble_seconds > 0.0) cat.bubble_seconds -= step_seconds;
  if (cat.wall_cooldown_seconds > 0.0) {
    cat.wall_cooldown_seconds -= step_seconds;
  }

  let const head_column        = cat_head_column(cat);
  let const yarn_columns       = scene.yarn.column - head_column;
  let const reachable_yarn_row = std::fmax(
      1.0 + CAT_HEAD_ROW,
      std::fmin(cat_last_row(scene.row_count) + CAT_HEAD_ROW, scene.yarn.row));
  let const yarn_rows = reachable_yarn_row - (cat.row + CAT_HEAD_ROW);
  let       target_columns_per_second = 0.0;
  let       target_rows_per_second    = 0.0;
  let       steering                  = STEERING_PER_SECOND;

  switch (cat.activity) {
  case cat_activity::Floating:
    cat.wander_radians += scene.noise.next_between(-1.0, 1.0) *
                          WANDER_RADIANS_PER_SECOND * step_seconds * 3.0;
    target_columns_per_second = std::cos(cat.wander_radians) *
                                DRIFT_COLUMNS_PER_SECOND * cat.cruise_factor;
    target_rows_per_second = std::sin(cat.wander_radians) *
                             DRIFT_ROWS_PER_SECOND * cat.cruise_factor;

    if (scene.noise.next_unit() < CURL_RATE_PER_SECOND * step_seconds) {
      set_activity(cat, cat_activity::Curling,
                   scene.noise.next_between(3.0, 8.0));
    } else if (scene.noise.next_unit() < CHASE_RATE_PER_SECOND * step_seconds) {
      set_activity(cat, cat_activity::Chasing, 6.0);
    }
    break;

  case cat_activity::Chasing: {
    let const distance = std::sqrt((yarn_columns * yarn_columns) +
                                   (yarn_rows * yarn_rows * 4.0)) +
                         0.001;

    target_columns_per_second =
        CHASE_COLUMNS_PER_SECOND * yarn_columns / distance;
    target_rows_per_second =
        CHASE_COLUMNS_PER_SECOND * yarn_rows * 2.0 / distance * 0.5;

    if (std::fabs(yarn_columns) < 16.0 && std::fabs(yarn_rows) < 2.5) {
      set_activity(cat, cat_activity::Bracing, BRACE_SECONDS);
    } else if (cat.activity_seconds > cat.activity_limit_seconds) {
      set_activity(cat, cat_activity::Floating, 0.0);
    }
    break;
  }

  case cat_activity::Bracing:
    steering = 4.0;

    if (cat.activity_seconds > cat.activity_limit_seconds) {
      cat.is_facing_right = yarn_columns > 0.0;
      cat.columns_per_second =
          (cat.is_facing_right ? 1.0 : -1.0) * DASH_COLUMNS_PER_SECOND;
      cat.rows_per_second = yarn_rows * 3.0;
      set_activity(cat, cat_activity::Dashing, DASH_SECONDS);
    }
    break;

  case cat_activity::Dashing:
    target_columns_per_second = cat.columns_per_second;
    target_rows_per_second    = cat.rows_per_second;

    if (std::fabs(yarn_columns) < 6.0 && std::fabs(yarn_rows) < 3.0) {
      kick_yarn(scene, cat);
      set_activity(cat, cat_activity::Floating, 0.0);
    } else if (cat.activity_seconds > cat.activity_limit_seconds) {
      set_activity(cat, cat_activity::Floating, 0.0);
    }
    break;

  case cat_activity::Curling:
    steering = 1.0;

    if (cat.activity_seconds < GROOM_SECONDS && cat.bubble_seconds <= 0.0 &&
        scene.noise.next_unit() < 0.8 * step_seconds)
    {
      say(cat, "*lick*");
    }

    if (cat.activity_seconds > DOZE_AFTER_SECONDS &&
        scene.noise.next_unit() < 1.6 * step_seconds)
    {
      spawn_particle(scene, particle_kind::Sleep, head_column, cat.row,
                     cat.is_facing_right ? 2.0 : -2.0, -1.6, 2.2, SLEEP_COLOR,
                     scene.noise.next_index(2) == 0 ? 'z' : 'Z');
    } else if (cat.activity_seconds > GROOM_SECONDS &&
               cat.activity_seconds < DOZE_AFTER_SECONDS &&
               cat.bubble_seconds <= 0.0)
    {
      say(cat, "prrr");
    }

    if (cat.activity_seconds > cat.activity_limit_seconds) {
      cat.wander_radians = scene.noise.next_between(0.0, 2.0 * PI);
      set_activity(cat, cat_activity::Floating, 0.0);
    }
    break;

  case cat_activity::Dizzy:
  case cat_activity::Hissing:
    steering = COAST_STEERING_PER_SECOND;
    target_columns_per_second =
        std::cos(cat.wander_radians) * DRIFT_COLUMNS_PER_SECOND;
    target_rows_per_second =
        std::sin(cat.wander_radians) * DRIFT_ROWS_PER_SECOND;

    if (cat.activity_seconds > cat.activity_limit_seconds) {
      set_activity(cat, cat_activity::Floating, 0.0);
    }
    break;
  }

  cat.columns_per_second =
      approach(cat.columns_per_second, target_columns_per_second, steering,
               step_seconds);
  cat.rows_per_second = approach(cat.rows_per_second, target_rows_per_second,
                                 steering, step_seconds);
  cat.columns_per_second =
      std::fmax(-MAXIMUM_COLUMNS_PER_SECOND,
                std::fmin(MAXIMUM_COLUMNS_PER_SECOND, cat.columns_per_second));
  cat.rows_per_second =
      std::fmax(-MAXIMUM_ROWS_PER_SECOND,
                std::fmin(MAXIMUM_ROWS_PER_SECOND, cat.rows_per_second));
  cat.column += cat.columns_per_second * step_seconds;
  cat.row += cat.rows_per_second * step_seconds;

  if (cat.activity != cat_activity::Dizzy &&
      std::fabs(cat.columns_per_second) > 0.8)
  {
    cat.is_facing_right = cat.columns_per_second > 0.0;
  }

  keep_cat_inside(scene, cat);
}

fn
meet_cats(scene_state &scene) wontthrow -> void
{
  for (usize first = 0; first < scene.cat_count; first++) {
    for (usize second = first + 1; second < scene.cat_count; second++) {
      let &left_cat  = scene.cats[first];
      let &right_cat = scene.cats[second];

      let const are_close = std::fabs(left_cat.column - right_cat.column) <
                                static_cast<f64>(CAT_PIXEL_WIDTH) - 8.0 &&
                            std::fabs(left_cat.row - right_cat.row) <
                                static_cast<f64>(CAT_ROW_COUNT) - 1.0;

      if (!are_close) continue;

      let const are_approaching =
          (right_cat.column - left_cat.column) *
              (left_cat.columns_per_second - right_cat.columns_per_second) >
          0.0;

      if (!are_approaching || left_cat.activity == cat_activity::Hissing) {
        continue;
      }

      let const is_friendly   = scene.noise.next_index(2) == 0;
      let const middle_column = ((left_cat.column + right_cat.column) / 2.0) +
                                (CAT_PIXEL_WIDTH / 2.0);
      let const middle_row    = (left_cat.row + right_cat.row) / 2.0;
      let const apart_columns = right_cat.column - left_cat.column;
      let const apart_rows    = (right_cat.row - left_cat.row) * 2.0;
      let const apart_length  = std::sqrt((apart_columns * apart_columns) +
                                          (apart_rows * apart_rows)) +
                               0.001;
      let const push_columns = COLLISION_IMPULSE * apart_columns / apart_length;
      let const push_rows =
          COLLISION_IMPULSE * apart_rows / apart_length * 0.5 +
          scene.noise.next_between(-3.0, 3.0);

      left_cat.columns_per_second -= push_columns;
      left_cat.rows_per_second -= push_rows;
      right_cat.columns_per_second += push_columns;
      right_cat.rows_per_second += push_rows;

      for (let *cat : {&left_cat, &right_cat}) {
        cat->wander_radians =
            std::atan2(cat->rows_per_second, cat->columns_per_second);

        if (is_friendly) {
          cat->happy_seconds = HAPPY_SECONDS;
          say(*cat,
              FRIEND_PHRASES[scene.noise.next_index(countof(FRIEND_PHRASES))]);
          set_activity(*cat, cat_activity::Dizzy, DIZZY_SECONDS * 0.6);
        } else {
          say(*cat, "hsss!");
          set_activity(*cat, cat_activity::Hissing, HISS_SECONDS);
        }
      }

      for (usize index = 0; index < 12; index++) {
        let const angle = scene.noise.next_between(0.0, 2.0 * PI);
        let const power = scene.noise.next_between(8.0, 22.0);

        spawn_particle(
            scene, particle_kind::Sparkle, middle_column, middle_row + 2.0,
            std::cos(angle) * power, std::sin(angle) * power * 0.5, 0.7,
            is_friendly ? HEART_COLOR : 226, "*+x"[scene.noise.next_index(3)]);
      }

      if (!is_friendly) continue;

      for (usize index = 0; index < 5; index++) {
        spawn_particle(scene, particle_kind::Heart,
                       middle_column + scene.noise.next_between(-3.0, 3.0),
                       middle_row, scene.noise.next_between(-2.0, 2.0),
                       scene.noise.next_between(-3.0, -1.0), 1.8, HEART_COLOR,
                       '<');
      }
    }
  }
}

fn
advance_yarn(scene_state &scene, f64 step_seconds) wontthrow -> void
{
  let &yarn = scene.yarn;

  yarn.column += yarn.columns_per_second * step_seconds;
  yarn.row += yarn.rows_per_second * step_seconds;
  yarn.spin_seconds +=
      step_seconds * (1.0 + (std::fabs(yarn.columns_per_second) / 8.0));

  let const damping = std::exp(-YARN_FRICTION_PER_SECOND * step_seconds);

  yarn.columns_per_second *= damping;
  yarn.rows_per_second *= damping;
  yarn.drift_radians +=
      scene.noise.next_between(-1.0, 1.0) * 2.0 * step_seconds;

  let const drift_speed =
      std::sqrt((yarn.columns_per_second * yarn.columns_per_second) +
                (4.0 * yarn.rows_per_second * yarn.rows_per_second));

  if (drift_speed < YARN_DRIFT_PER_SECOND) {
    yarn.columns_per_second =
        approach(yarn.columns_per_second,
                 std::cos(yarn.drift_radians) * YARN_DRIFT_PER_SECOND, 1.5,
                 step_seconds);
    yarn.rows_per_second =
        approach(yarn.rows_per_second,
                 std::sin(yarn.drift_radians) * YARN_DRIFT_PER_SECOND * 0.4,
                 1.5, step_seconds);
  }

  let const last_column = static_cast<f64>(scene.column_count) - 2.0;
  let const first_row   = 1.0 + CAT_HEAD_ROW;
  let const last_row    = cat_last_row(scene.row_count) + CAT_HEAD_ROW;

  if (yarn.column < 1.0 || yarn.column > last_column) {
    yarn.column = std::fmax(1.0, std::fmin(last_column, yarn.column));
    yarn.columns_per_second = -yarn.columns_per_second * 0.9;
  }

  if (yarn.row < first_row || yarn.row > last_row) {
    yarn.row             = std::fmax(first_row, std::fmin(last_row, yarn.row));
    yarn.rows_per_second = -yarn.rows_per_second * 0.9;
  }

  yarn.thread_sample_seconds += step_seconds;

  if (yarn.thread_sample_seconds >= YARN_THREAD_SAMPLE_SECONDS) {
    yarn.thread_sample_seconds = 0.0;
    yarn.thread_head = (yarn.thread_head + 1) % YARN_THREAD_POINT_COUNT;

    let &point = yarn.thread[yarn.thread_head];

    point.column  = yarn.column;
    point.row     = yarn.row;
    point.is_used = true;
  }
}

fn
advance_scene(scene_state &scene, f64 step_seconds) wontthrow -> void
{
  scene.elapsed_seconds += step_seconds;

  if (scene.shake_seconds > 0.0) scene.shake_seconds -= step_seconds;

  for (usize index = 0; index < scene.cat_count; index++)
    advance_cat(scene, scene.cats[index], step_seconds);

  meet_cats(scene);
  advance_yarn(scene, step_seconds);

  let const drag = std::exp(-1.4 * step_seconds);

  for (let &each_particle : scene.particles) {
    if (each_particle.seconds_left <= 0.0) continue;

    each_particle.seconds_left -= step_seconds;

    switch (each_particle.kind) {
    case particle_kind::Debris:
    case particle_kind::Sparkle:
    case particle_kind::Smoke:
      each_particle.columns_per_second *= drag;
      each_particle.rows_per_second *= drag;
      break;

    case particle_kind::Heart:
    case particle_kind::Sleep: break;
    }

    each_particle.column += each_particle.columns_per_second * step_seconds;
    each_particle.row += each_particle.rows_per_second * step_seconds;
  }

  for (let &blast : scene.explosions) {
    if (!blast.is_active) continue;

    blast.age_seconds += step_seconds;

    if (blast.age_seconds > EXPLOSION_SECONDS) blast.is_active = false;
  }

  scene.next_shooting_star_seconds -= step_seconds;

  for (let &star : scene.shooting_stars) {
    if (!star.is_active) {
      if (scene.next_shooting_star_seconds > 0.0 || scene.column_count < 20) {
        continue;
      }

      star.is_active = true;
      star.column =
          scene.noise.next_between(static_cast<f64>(scene.column_count) * 0.3,
                                   static_cast<f64>(scene.column_count) + 10.0);
      star.row                         = 0.0;
      scene.next_shooting_star_seconds = scene.noise.next_between(
          SHOOTING_STAR_GAP_SECONDS, SHOOTING_STAR_GAP_SECONDS * 3.0);
      continue;
    }

    star.column -= SHOOTING_STAR_COLUMN_SPEED * step_seconds;
    star.row += SHOOTING_STAR_ROW_SPEED * step_seconds;

    if (star.column < -static_cast<f64>(SHOOTING_STAR_TAIL_COUNT) * 3.0 ||
        star.row > static_cast<f64>(scene.row_count) + SHOOTING_STAR_TAIL_COUNT)
    {
      star.is_active = false;
    }
  }
}

fn
draw_starfield(scene_canvas &canvas, const scene_state &scene) wontthrow -> void
{
  let const twinkle_epoch =
      static_cast<u64>(scene.elapsed_seconds / TWINKLE_PERIOD_SECONDS);

  for (usize layer = 0; layer < STAR_LAYER_COUNT; layer++) {
    let const drift_columns = static_cast<u64>(
        scene.elapsed_seconds * STAR_LAYER_COLUMNS_PER_SECOND[layer]);

    for (usize row = 0; row < canvas.row_count; row++) {
      for (usize column = 0; column < canvas.column_count; column++) {
        let const cell_seed =
            mix_bits((static_cast<u64>(layer) << 56) ^
                     (static_cast<u64>(row) << 32) ^ (column + drift_columns));

        if (cell_seed % STAR_LAYER_DENSITY[layer] != 0) continue;

        let const phase = mix_bits(cell_seed ^ twinkle_epoch) % 5;
        let const glyph = layer == STAR_LAYER_COUNT - 1
                              ? (phase == 0   ? '*'
                                 : phase == 1 ? '+'
                                              : '.')
                              : (phase == 0 ? '+' : '.');

        canvas.put(static_cast<isize>(row), static_cast<isize>(column), glyph,
                   STAR_LAYER_COLORS[layer]);
      }
    }
  }
}

fn
draw_moon(scene_canvas &canvas) wontthrow -> void
{
  if (canvas.column_count < 50 || canvas.row_count < 14) return;

  let const center_column = static_cast<f64>(canvas.column_count) - 9.0;
  let const center_row    = 6.0;
  let const radius        = 5.5;

  for (isize pixel_row = 0; pixel_row < 14; pixel_row++) {
    for (isize column = -7; column <= 7; column++) {
      let const row_offset    = static_cast<f64>(pixel_row) - center_row;
      let const column_offset = static_cast<f64>(column);
      let const distance      = std::sqrt((row_offset * row_offset) +
                                          (column_offset * column_offset));
      let const shadow_distance =
          std::sqrt(((row_offset + 1.5) * (row_offset + 1.5)) +
                    ((column_offset + 3.5) * (column_offset + 3.5)));

      if (distance > radius || shadow_distance < radius - 0.5) continue;

      let const is_crater =
          (column == 3 && pixel_row == 4) || (column == 4 && pixel_row == 8);

      canvas.put_pixel(pixel_row,
                       static_cast<isize>(center_column + column_offset),
                       is_crater ? MOON_SHADE_COLOR : MOON_COLOR);
    }
  }
}

fn
draw_shooting_stars(scene_canvas &canvas, const scene_state &scene) wontthrow
    -> void
{
  for (let const &star : scene.shooting_stars) {
    if (!star.is_active) continue;

    for (usize index = 0; index < SHOOTING_STAR_TAIL_COUNT; index++) {
      let const offset = static_cast<f64>(index);
      let const glyph  = index == 0 ? '*' : index < 3 ? '=' : '-';
      let const color  = static_cast<u8>(index == 0 ? 231 : 254 - (index * 2));

      canvas.put(static_cast<isize>(star.row - (offset * 0.3)),
                 static_cast<isize>(star.column + (offset * 1.6)), glyph,
                 color);
    }
  }
}

fn
draw_yarn(scene_canvas &canvas, const scene_state &scene) wontthrow -> void
{
  let const &yarn = scene.yarn;

  for (usize age = 1; age < YARN_THREAD_POINT_COUNT; age++) {
    let const &point =
        yarn.thread[(yarn.thread_head + YARN_THREAD_POINT_COUNT - age) %
                    YARN_THREAD_POINT_COUNT];

    if (!point.is_used) continue;

    let const wave =
        std::sin((static_cast<f64>(age) * 0.7) + (scene.elapsed_seconds * 3.0));

    canvas.put_pixel(static_cast<isize>((point.row * 2.0) + 1.0 + wave),
                     static_cast<isize>(point.column), THREAD_COLOR);
  }

  let const pixel_row = static_cast<isize>(yarn.row * 2.0);
  let const column    = static_cast<isize>(yarn.column);
  let const shine     = (static_cast<isize>(yarn.spin_seconds * 2.0) % 3) - 1;

  for (isize row_offset = 0; row_offset < 3; row_offset++) {
    for (isize column_offset = -1; column_offset <= 1; column_offset++) {
      if (row_offset != 1 && column_offset != 0) continue;

      canvas.put_pixel(pixel_row + row_offset, column + column_offset,
                       column_offset == shine && row_offset == 1
                           ? YARN_SHINE_COLOR
                           : YARN_COLOR);
    }
  }
}

fn
cat_pixel_color(char letter, const cat_palette &palette, u8 eye_color) wontthrow
    -> u8
{
  switch (letter) {
  case 'B': return palette.body;
  case 'D': return palette.dark;
  case 'L': return palette.light;
  case 'E': return eye_color;
  case 'N': return NOSE_COLOR;
  case 'I': return INNER_EAR_COLOR;
  case 'C': return COLLAR_COLOR;
  case 'G': return BELL_COLOR;
  default:  return 0;
  }
}

fn
cat_eye_color(const cat_state &cat) wontthrow -> u8
{
  let const &palette = cat.palette;

  switch (cat.activity) {
  case cat_activity::Dizzy:
    return static_cast<u64>(cat.activity_seconds / 0.12) % 2 == 0 ? 196 : 226;

  case cat_activity::Hissing: return 196;
  case cat_activity::Bracing: return 46;
  case cat_activity::Curling:
    if (cat.activity_seconds > DOZE_AFTER_SECONDS ||
        cat.activity_seconds < GROOM_SECONDS)
    {
      return palette.dark;
    }
    break;

  case cat_activity::Floating:
  case cat_activity::Chasing:
  case cat_activity::Dashing:  break;
  }

  if (cat.happy_seconds > 0.0) return palette.dark;
  if (std::fmod(cat.phase_seconds, BLINK_PERIOD_SECONDS) < BLINK_SECONDS) {
    return palette.body;
  }

  return palette.eye;
}

fn
draw_pixels(scene_canvas      &canvas,
            isize              pixel_row,
            isize              origin_column,
            usize              sprite_width,
            usize              offset,
            StringView         text,
            bool               is_mirrored,
            const cat_palette &palette,
            u8                 eye_color) wontthrow -> void
{
  for (usize index = 0; index < text.length; index++) {
    let const color = cat_pixel_color(text[index], palette, eye_color);

    if (color == 0) continue;

    let const sprite_column = offset + index;
    let const column =
        is_mirrored ? origin_column +
                          static_cast<isize>(sprite_width - 1 - sprite_column)
                    : origin_column + static_cast<isize>(sprite_column);

    canvas.put_pixel(pixel_row, column, color);
  }
}

fn
draw_cat(scene_canvas &canvas, const cat_state &cat) wontthrow -> void
{
  let const &palette   = cat.palette;
  let const &art       = *cat.art;
  let const  eye_color = cat_eye_color(cat);
  let const  is_tumbling =
      cat.activity == cat_activity::Dizzy &&
      static_cast<u64>(cat.activity_seconds / TUMBLE_SECONDS) % 2 == 1;
  let const is_mirrored = cat.is_facing_right != is_tumbling;
  let const column      = static_cast<isize>(cat.column);
  let const bob_pixels  = static_cast<isize>(
      std::lround(std::sin(cat.phase_seconds * BOB_RADIANS_PER_SECOND) * 1.2));
  let const pixel_row = static_cast<isize>(cat.row * 2.0) + bob_pixels;

  if (cat.activity == cat_activity::Curling) {
    let const curled_width = art.curled[0].length;
    let const curled_column =
        column + static_cast<isize>((CAT_PIXEL_WIDTH - curled_width) / 2);
    let const curled_row =
        pixel_row + static_cast<isize>(CAT_PIXEL_HEIGHT - art.curled_row_count);

    for (usize index = 0; index < art.curled_row_count; index++)
      draw_pixels(canvas, curled_row + static_cast<isize>(index), curled_column,
                  curled_width, 0, art.curled[index], is_mirrored, palette,
                  eye_color);
  } else {
    for (usize index = 0; index < art.body_row_count; index++)
      draw_pixels(canvas, pixel_row + static_cast<isize>(index), column,
                  CAT_PIXEL_WIDTH, 0, art.body[index], is_mirrored, palette,
                  eye_color);

    let const tail_speed = cat.activity == cat_activity::Bracing
                               ? TAIL_SWISH_SECONDS / 4.0
                               : TAIL_SWISH_SECONDS;
    let const tail_frame =
        static_cast<usize>(cat.phase_seconds / tail_speed) % 2;

    for (usize index = 0; index < art.tail_row_count; index++)
      draw_pixels(canvas, pixel_row + static_cast<isize>(art.tail_row + index),
                  column, CAT_PIXEL_WIDTH, art.tail_column,
                  art.tail[(tail_frame * art.tail_row_count) + index],
                  is_mirrored, palette, eye_color);

    if (cat.activity == cat_activity::Hissing) {
      draw_pixels(canvas, pixel_row + static_cast<isize>(art.spike_row), column,
                  CAT_PIXEL_WIDTH, 0, art.spike, is_mirrored, palette,
                  eye_color);
    }

    let const paddle_frame =
        static_cast<usize>(cat.phase_seconds / PADDLE_SECONDS) %
        art.paddle_frame_count;
    let const *leg_pixels =
        cat.activity == cat_activity::Dashing ? art.dash
        : cat.activity == cat_activity::Bracing
            ? art.tucked
            : art.paddle + (paddle_frame * art.leg_row_count);

    for (usize index = 0; index < art.leg_row_count; index++)
      draw_pixels(canvas,
                  pixel_row + static_cast<isize>(art.body_row_count + index),
                  column, CAT_PIXEL_WIDTH, 0, leg_pixels[index], is_mirrored,
                  palette, eye_color);
  }

  let const row = pixel_row / 2;

  if (cat.activity == cat_activity::Dizzy) {
    let const angle = cat.activity_seconds * 9.0;

    for (usize index = 0; index < 3; index++) {
      let const orbit = angle + (2.0 * PI * static_cast<f64>(index) / 3.0);

      canvas.put(
          row - 1 + static_cast<isize>(std::sin(orbit) * 1.2),
          static_cast<isize>(cat_head_column(cat) + std::cos(orbit) * 5.0), '*',
          overlay(bold(DIZZY_STAR_COLOR)));
    }
  }

  if (cat.bubble_seconds > 0.0 && !cat.bubble_text.is_empty()) {
    let const head = cat_head_column(cat);
    let const start =
        cat.is_facing_right
            ? head + 4.0
            : head - 4.0 - static_cast<f64>(cat.bubble_text.length);

    canvas.put_text(row - 1, static_cast<isize>(start), cat.bubble_text,
                    overlay(bold(BUBBLE_COLOR)));
  }
}

fn
draw_explosions(scene_canvas &canvas, const scene_state &scene)
    wontthrow -> void
{
  for (let const &blast : scene.explosions) {
    if (!blast.is_active) continue;

    let const center_row    = blast.row * 2.0;
    let const center_column = blast.column;
    let const shock_radius  = blast.age_seconds * SHOCKWAVE_GROWTH;
    let const fraction      = blast.age_seconds / EXPLOSION_SECONDS;
    let const shock_color   = SHOCKWAVE_COLORS[static_cast<usize>(
        fraction * static_cast<f64>(countof(SHOCKWAVE_COLORS) - 1))];
    let const fire_radius =
        blast.age_seconds < FIREBALL_SECONDS
            ? std::fmin(blast.age_seconds * FIREBALL_GROWTH,
                        FIREBALL_MAXIMUM_RADIUS) *
                  (1.0 - ((blast.age_seconds / FIREBALL_SECONDS) * 0.5))
            : 0.0;
    let const reach = static_cast<isize>(shock_radius) + 2;

    for (isize row_offset = -reach; row_offset <= reach; row_offset++) {
      for (isize column_offset = -reach; column_offset <= reach;
           column_offset++)
      {
        let const distance  = std::sqrt(static_cast<f64>(
            (row_offset * row_offset) + (column_offset * column_offset)));
        let const pixel_row = static_cast<isize>(center_row) + row_offset;
        let const pixel_column =
            static_cast<isize>(center_column) + column_offset;

        if (fire_radius > 0.0 && distance <= fire_radius) {
          let const heat  = distance / fire_radius;
          let const color = heat < 0.35   ? 231
                            : heat < 0.6  ? 226
                            : heat < 0.85 ? 208
                                          : 196;

          canvas.put_pixel(pixel_row, pixel_column, static_cast<u8>(color));
          continue;
        }

        if (std::fabs(distance - shock_radius) < 0.6) {
          canvas.put_pixel(pixel_row, pixel_column, shock_color);
        }
      }
    }
  }
}

fn
draw_particles(scene_canvas &canvas, const scene_state &scene) wontthrow -> void
{
  for (let const &each_particle : scene.particles) {
    if (each_particle.seconds_left <= 0.0) continue;

    let const life =
        each_particle.seconds_left / each_particle.lifetime_seconds;
    let const row    = static_cast<isize>(each_particle.row);
    let const column = static_cast<isize>(each_particle.column);

    switch (each_particle.kind) {
    case particle_kind::Debris:
      canvas.put(row, column, life < 0.3 ? '.' : each_particle.glyph,
                 overlay(each_particle.color));
      break;

    case particle_kind::Smoke: {
      let const shade_index = static_cast<usize>(
          (1.0 - life) * static_cast<f64>(countof(SMOKE_COLORS) - 1));
      let const glyph = life > 0.66 ? 'O' : life > 0.33 ? 'o' : '.';

      canvas.put(row, column, glyph, overlay(SMOKE_COLORS[shade_index]));
      break;
    }

    case particle_kind::Heart:
      canvas.put_text(row, column, "<3", overlay(bold(each_particle.color)));
      break;

    case particle_kind::Sleep:
    case particle_kind::Sparkle:
      canvas.put(row, column, each_particle.glyph,
                 overlay(life > 0.5 ? bold(each_particle.color)
                                    : static_cast<u16>(each_particle.color)));
      break;
    }
  }
}

fn
draw_notice(scene_canvas      &canvas,
            const scene_state &scene,
            Allocator          allocator) throws -> void
{
  let text = String{allocator};

  if (scene.is_paused) {
    text += "paused";
  } else if (scene.speed_notice_seconds > 0.0) {
    let const tenths = static_cast<u64>((scene.speed * 10.0) + 0.5);

    text += "speed ";
    text += String::from(tenths / 10, allocator).view();
    text += ".";
    text += String::from(tenths % 10, allocator).view();
    text += "x";
  }

  if (text.is_empty()) return;

  let const column = static_cast<isize>(canvas.column_count) -
                     static_cast<isize>(text.length()) - 1;

  canvas.put_text(static_cast<isize>(canvas.row_count) - 1, column, text.view(),
                  overlay(bold(NOTICE_COLOR)));
}

fn
append_colors(String &frame,
              u8      foreground,
              u8      background,
              bool    is_bold) throws -> void
{
  frame += colors::ansi::RESET;

  if (is_bold) frame += colors::ansi::BOLD;

  if (foreground != 0) {
    frame += "\x1b[38;5;";
    frame +=
        String::from(static_cast<u32>(foreground), frame.allocator()).view();
    frame += "m";
  }

  if (background != 0) {
    frame += "\x1b[48;5;";
    frame +=
        String::from(static_cast<u32>(background), frame.allocator()).view();
    frame += "m";
  }
}

fn
render_scene(String      &frame,
             scene_state &scene,
             bool         should_color,
             Allocator    allocator) throws -> void
{
  let const column_count = scene.column_count;
  let const row_count    = scene.row_count;
  let const cell_count   = column_count * row_count;
  let       canvas =
      scene_canvas{ArrayList<char>{allocator}, ArrayList<u16>{allocator},
                   ArrayList<u8>{allocator}, column_count, row_count};

  canvas.cells.reserve(cell_count);
  canvas.styles.reserve(cell_count);
  canvas.pixels.reserve(cell_count * 2);

  for (usize index = 0; index < cell_count; index++) {
    canvas.cells.push(' ');
    canvas.styles.push(0);
    canvas.pixels.push(0);
    canvas.pixels.push(0);
  }

  draw_starfield(canvas, scene);
  draw_moon(canvas);
  draw_shooting_stars(canvas, scene);
  draw_yarn(canvas, scene);

  for (usize index = 0; index < scene.cat_count; index++)
    draw_cat(canvas, scene.cats[index]);

  draw_explosions(canvas, scene);
  draw_particles(canvas, scene);
  draw_notice(canvas, scene, allocator);

  let const shake_columns =
      scene.shake_seconds > 0.0
          ? static_cast<isize>(scene.noise.next_index(3)) - 1
          : 0;
  u32 active_key = 0;

  for (usize row = 0; row < row_count; row++) {
    for (usize screen_column = 0; screen_column < column_count; screen_column++)
    {
      let const  source     = static_cast<isize>(screen_column) - shake_columns;
      u8         foreground = 0;
      u8         background = 0;
      bool       is_bold    = false;
      StringView glyph      = " ";
      char       ascii_glyph = ' ';

      if (source >= 0 && source < static_cast<isize>(column_count)) {
        let const column     = static_cast<usize>(source);
        let const cell_index = (row * column_count) + column;
        let const style      = canvas.styles[cell_index];
        let const top        = canvas.pixel_at(row * 2, column);
        let const bottom     = canvas.pixel_at((row * 2) + 1, column);
        let const has_pixels = top != 0 || bottom != 0;

        if (has_pixels && (style & OVERLAY_STYLE_BIT) == 0) {
          if (!should_color) {
            glyph = top != 0 && bottom != 0 ? "\xe2\x96\x88"
                    : top != 0              ? "\xe2\x96\x80"
                                            : "\xe2\x96\x84";
          } else if (top != 0) {
            glyph      = "\xe2\x96\x80";
            foreground = top;
            background = bottom;
          } else {
            glyph      = "\xe2\x96\x84";
            foreground = bottom;
          }
        } else {
          ascii_glyph = canvas.cells[cell_index];
          foreground  = static_cast<u8>(style & 0xff);
          is_bold     = (style & BOLD_STYLE_BIT) != 0;
          background  = has_pixels ? (bottom != 0 ? bottom : top) : 0;
          glyph       = StringView{};
        }
      }

      if (should_color) {
        let const key = static_cast<u32>(foreground) |
                        (static_cast<u32>(background) << 8) |
                        (is_bold ? 0x10000u : 0u) | 0x20000u;

        if (key != active_key) {
          active_key = key;
          append_colors(frame, foreground, background, is_bold);
        }
      }

      if (glyph.is_empty()) {
        frame.push(ascii_glyph);
      } else {
        frame += glyph;
      }
    }

    if (should_color) {
      frame += colors::ansi::RESET;
      active_key = 0;
    }

    if (row + 1 < row_count) frame += "\n";
  }
}

fn
print_still_cat(const ExecContext   &ec,
                const cat_breed_art &art,
                Allocator            allocator) throws -> void
{
  bool is_filled[CAT_PIXEL_HEIGHT][CAT_PIXEL_WIDTH]{};

  let const do_stamp = [&](usize row, usize offset, StringView pixels) wontthrow {
    for (usize index = 0; index < pixels.length; index++) {
      if (pixels[index] == '.' || pixels[index] == 'E') continue;
      if (offset + index >= CAT_PIXEL_WIDTH) break;

      is_filled[row][offset + index] = true;
    }
  };

  for (usize index = 0; index < art.body_row_count; index++)
    do_stamp(index, 0, art.body[index]);

  for (usize index = 0; index < art.tail_row_count; index++)
    do_stamp(art.tail_row + index, art.tail_column, art.tail[index]);

  for (usize index = 0; index < art.leg_row_count; index++)
    do_stamp(art.body_row_count + index, 0, art.paddle[index]);

  let text = String{allocator};

  text += "meow!\n";

  for (usize row = 0; row < CAT_PIXEL_HEIGHT; row += 2) {
    usize kept_length = 0;

    for (usize column = 0; column < CAT_PIXEL_WIDTH; column++) {
      if (is_filled[row][column] || is_filled[row + 1][column]) {
        kept_length = column + 1;
      }
    }

    for (usize column = 0; column < kept_length; column++) {
      let const has_top    = is_filled[row][column];
      let const has_bottom = is_filled[row + 1][column];

      text += has_top && has_bottom ? StringView{"\xe2\x96\x88"}
              : has_top             ? StringView{"\xe2\x96\x80"}
              : has_bottom          ? StringView{"\xe2\x96\x84"}
                                    : StringView{" "};
    }

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
  f64       speed             = 1.0;
  usize     cat_count         = 1;

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

  if (FLAG_MEOW_SPEED.is_set()) {
    let const parsed = String{allocator, FLAG_MEOW_SPEED.value()}.to<f64>();

    if (parsed.is_error() || !(parsed.value() >= MINIMUM_SPEED) ||
        parsed.value() > MAXIMUM_SPEED)
    {
      KOSHKIT_REPORT_ERROR_AT(FLAG_MEOW_SPEED.value_location(),
                              "invalid speed '" +
                                  String{allocator, FLAG_MEOW_SPEED.value()} +
                                  "', expected 0.1 to 10");

      return 1;
    }

    speed = parsed.value();
  }

  if (FLAG_MEOW_CATS.is_set()) {
    let const parsed = FLAG_MEOW_CATS.value().to<u32>();

    if (parsed.is_error() || parsed.value() == 0 ||
        parsed.value() > MAXIMUM_CAT_COUNT)
    {
      KOSHKIT_REPORT_ERROR_AT(FLAG_MEOW_CATS.value_location(),
                              "invalid cat count '" +
                                  String{allocator, FLAG_MEOW_CATS.value()} +
                                  "', expected 1 to 8");

      return 1;
    }

    cat_count = parsed.value();
  }

  let const started_at_nanoseconds = os::monotonic_nanos();
  let       seed_noise             = noise_source{};

  seed_noise.state =
      mix_bits(started_at_nanoseconds ^
               (static_cast<u64>(os::current_unix_seconds()) << 32) ^
               static_cast<u64>(reinterpret_cast<uintptr_t>(&seed_noise))) |
      1;

  if (!os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT))) {
    print_still_cat(ec, *CAT_BREEDS[seed_noise.next_index(countof(CAT_BREEDS))],
                    allocator);

    return 0;
  }

  live_view_options options{};

  options.title                    = "meow";
  options.refresh_interval_seconds = 1.0 / static_cast<f64>(frames_per_second);
  options.should_render_first_frame_immediately = true;
  options.should_show_header                    = false;

  let const should_color           = koshkit_should_color();
  u64       last_frame_nanoseconds = started_at_nanoseconds;
  let       scene                  = scene_state{};
  bool      are_cats_placed        = false;

  scene.noise     = seed_noise;
  scene.cat_count = cat_count;
  scene.speed     = speed;

  let const do_sample = [](u64, Allocator) wontthrow -> Maybe<i32> {
    return None;
  };

  let const do_render =
      [&](String &frame, const live_view_dimensions &dimensions,
          Allocator frame_allocator) throws -> Maybe<i32> {
    scene.column_count = static_cast<usize>(dimensions.columns);
    scene.row_count =
        dimensions.rows > 1 ? static_cast<usize>(dimensions.rows) - 1 : 1;

    let const now_nanoseconds = os::monotonic_nanos();
    let       step_seconds =
        static_cast<f64>(now_nanoseconds - last_frame_nanoseconds) / 1e9;

    last_frame_nanoseconds = now_nanoseconds;

    if (step_seconds > LONGEST_STEP_SECONDS) {
      step_seconds = LONGEST_STEP_SECONDS;
    }

    if (!are_cats_placed) {
      place_cats(scene);
      are_cats_placed = true;
    }

    advance_scene(scene, scene.is_paused ? 0.0 : step_seconds * scene.speed);

    if (scene.speed_notice_seconds > 0.0) {
      scene.speed_notice_seconds -= step_seconds;
    }

    render_scene(frame, scene, should_color, frame_allocator);

    return None;
  };

  let const do_key = [&](live_view_key key) wontthrow -> live_view_key_action {
    if (key.special != live_view_special_key::None) {
      return live_view_key_action::Quit;
    }

    switch (key.character) {
    case ' ':
      scene.is_paused = !scene.is_paused;

      return live_view_key_action::Redraw;

    case '+':
    case '=':
      scene.speed = std::fmin(scene.speed * SPEED_STEP, MAXIMUM_SPEED);
      scene.speed_notice_seconds = SPEED_NOTICE_SECONDS;

      return live_view_key_action::Redraw;

    case '-':
    case '_':
      scene.speed = std::fmax(scene.speed / SPEED_STEP, MINIMUM_SPEED);
      scene.speed_notice_seconds = SPEED_NOTICE_SECONDS;

      return live_view_key_action::Redraw;

    default: return live_view_key_action::Quit;
    }
  };

  return run_live_view(ec, options, do_sample, do_render, do_key);
}

} /* namespace koshka::koshkit */
