/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the date utility. It queries the platform clock and
 * formats local or Coordinated Universal Time with strftime directives.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL("[-u] [-d date] [+format]",
                  "The date utility can write the date and time.");

FLAG(DATE_UTC, Bool, 'u', "utc", "Use Coordinated Universal Time.");
FLAG(DATE_DATE,
     String,
     'd',
     "date",
     "Show YYYY-MM-DD[ hh:mm[:ss]], with T also allowed before the time, or "
     "@SECONDS with a sign and a fraction.");

REGISTER_KOSHKIT_UTIL_FLAGS(Date);

namespace koshka::koshkit {

/* strftime leaves %Y unpadded, and glibc renders a year above 9999 with +. */
static fn
append_date_year(String &output, StringView year) throws -> void
{
  let const is_negative = !year.is_empty() && year[0] == '-';

  if (!year.is_empty() && year[0] == '+') year = year.substring(1);

  if (is_negative) {
    output += '-';
    year = year.substring(1);
  }

  for (usize width = year.length + (is_negative ? 1 : 0); width < 4; width++) {
    output += '0';
  }

  output += year;
}

fn
Date::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() > 1) return report_usage_error(ec, cxt, args[0].view());
  if (!operands.is_empty() && (operands[0].is_empty() || operands[0][0] != '+'))
  {
    report_soft_koshkit_error(ec, cxt, operand_locations[0],
                              "Setting the system clock is unsupported");

    return 1;
  }

  /* at this point: operands holds no operand or one operand that starts with
     '+'. */
  let const format = operands.is_empty() ? StringView{"%a %b %e %H:%M:%S %Z %Y"}
                                         : operands[0].view().substring(1);
  let const now_microseconds  = os::realtime_microseconds();
  i64       displayed_seconds = static_cast<i64>(now_microseconds / 1000000);
  u32       displayed_nanoseconds =
      static_cast<u32>(now_microseconds % 1000000) * 1000;

  if (FLAG_DATE_DATE.is_set()) {
    displayed_nanoseconds = 0;

    let const parsed = utils::parse_calendar_date(FLAG_DATE_DATE.value(),
                                                  FLAG_DATE_UTC.is_enabled(),
                                                  &displayed_nanoseconds);

    if (!parsed.has_value()) {
      report_soft_koshkit_error(ec, cxt, FLAG_DATE_DATE.value_location(),
                                "Invalid date '" + FLAG_DATE_DATE.value() +
                                    "'");

      return 1;
    }

    displayed_seconds = *parsed;
  }

  let const time_zone =
      FLAG_DATE_UTC.is_enabled() ? os::time_zone::Utc : os::time_zone::Local;
  let const zone_text  = os::format_time("%z", displayed_seconds, time_zone);
  let       local_year = os::format_time("%Y", displayed_seconds, time_zone);

  /* glibc computes %Y in an int, so a later year wraps below zero. */
  if (local_year.has_value() && displayed_seconds > 0 &&
      local_year->view().starts_with("-"))
  {
    let const wrapped_year = local_year->view().to<i64>();

    if (!wrapped_year.is_error()) {
      local_year = String::from(wrapped_year.value() + (i64{1} << 32),
                                cxt.scratch_allocator());
    }
  }

  let const local_year_text =
      local_year.has_value() ? local_year->view() : StringView{"0"};

  /* strftime renders %s through the local zone, so kosh writes the epoch. the
     other conversions here are missing from strftime or wrong in it. */
  let expanded_format = String{cxt.scratch_allocator()};

  for (usize position = 0; position < format.length; position++) {
    if (format[position] != '%' || position + 1 == format.length) {
      expanded_format += format[position];
      continue;
    }

    usize colon_count = 0;

    while (position + 1 + colon_count < format.length &&
           format[position + 1 + colon_count] == ':')
    {
      colon_count++;
    }

    if (colon_count >= 1 && colon_count <= 3 &&
        position + 1 + colon_count < format.length &&
        format[position + 1 + colon_count] == 'z' && zone_text.has_value() &&
        zone_text->count() >= 5)
    {
      let const zone = zone_text->view();

      expanded_format += zone.substring_of_length(0, 3);
      if (colon_count < 3 || zone.substring(3) != "00") {
        expanded_format += ':';
        expanded_format += zone.substring(3);
      }

      if (colon_count == 2) expanded_format += ":00";
      position += colon_count + 1;
      continue;
    }

    switch (format[position + 1]) {
    case 's':
      expanded_format +=
          String::from(displayed_seconds, cxt.scratch_allocator()).view();
      break;
    case 'N': {
      let const digits =
          String::from(displayed_nanoseconds, cxt.scratch_allocator());

      for (usize width = digits.count(); width < 9; width++) {
        expanded_format += '0';
      }

      expanded_format += digits.view();
      break;
    }
    case 'q':
      if (let const month = os::format_time("%m", displayed_seconds, time_zone);
          month.has_value() && month->count() == 2)
      {
        let const month_number = ((*month)[0] - '0') * 10 + ((*month)[1] - '0');

        expanded_format += static_cast<char>('1' + (month_number - 1) / 3);
      }

      break;
    case 'Y': append_date_year(expanded_format, local_year_text); break;
    case 'F':
      append_date_year(expanded_format, local_year_text);
      expanded_format += "-%m-%d";
      break;
    case 'Z':
      expanded_format += FLAG_DATE_UTC.is_enabled() ? "UTC" : "%Z";
      break;
    default:
      expanded_format += format[position];
      expanded_format += format[position + 1];
    }

    position++;
  }

  let const formatted =
      os::format_time(expanded_format.view(), displayed_seconds, time_zone);

  if (!formatted.has_value()) {
    let const location =
        operands.is_empty() ? ec.source_location() : operand_locations[0];

    report_soft_koshkit_error(ec, cxt, location,
                              "Cannot format the current time, or the "
                              "result exceeds 8192 bytes");

    return 1;
  }

  ec.print_to_stdout(*formatted);
  ec.print_to_stdout("\n");

  return 0;
}

} /* namespace koshka::koshkit */
