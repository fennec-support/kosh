/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements debug assertions, tracing, traps, and
 * unreachable checks, with release definitions that remove debug-only work.
 */

#pragma once

#include "Common.hpp"

namespace koshka {
class String;
}

#if !defined NDEBUG
namespace koshka {

/* writes text as an S-expression string with the escapes of the -A
   printer. */
inline fn
debug_write_quoted(FILE *stream, const char *text) wontthrow -> void
{
  unused(fputc('"', stream));
  for (const char *cursor = text; *cursor != '\0'; cursor++) {
    let const byte = static_cast<unsigned char>(*cursor);

    if (byte == '"' || byte == '\\') {
      unused(fputc('\\', stream));
      unused(fputc(byte, stream));
    } else if (byte == '\n') {
      unused(fputs("\\n", stream));
    } else if (byte == '\t') {
      unused(fputs("\\t", stream));
    } else if (byte < 0x20 || byte == 0x7f) {
      unused(fprintf(stream, "\\x%02x", byte));
    } else {
      unused(fputc(byte, stream));
    }
  }
  unused(fputc('"', stream));
}

__attribute__((format(printf, 4, 5))) inline fn
debug_print_trace(const char *file,
                  int         line,
                  bool        should_end_line,
                  const char *format,
                  ...) wontthrow -> void
{
  char    message[1024];
  va_list arguments;

  va_start(arguments, format);
  unused(vsnprintf(message, sizeof(message), format, arguments));
  va_end(arguments);

  unused(fprintf(stderr, "(trace :at \"%s:%d\" ", file, line));
  debug_write_quoted(stderr, message);
  unused(fputs(should_end_line ? ")\n" : ")", stderr));
  unused(fflush(stderr));
}

__attribute__((format(printf, 5, 6))) inline fn
debug_print_log(FILE       *stream,
                const char *level,
                const char *location,
                const char *function,
                const char *format,
                ...) wontthrow -> void
{
  char    message[1024];
  va_list arguments;

  va_start(arguments, format);
  unused(vsnprintf(message, sizeof(message), format, arguments));
  va_end(arguments);

  unused(fprintf(stream, "(log :level %s :at \"%s\" :function \"%s\" ", level,
                 location, function));
  debug_write_quoted(stream, message);
  unused(fputs(")\n", stream));
  unused(fflush(stream));
}

} /* namespace koshka */

#define TRACE(...)                                                             \
  ::koshka::debug_print_trace(__FILE__, __LINE__, false, __VA_ARGS__)
#define TRACELN(...)                                                           \
  ::koshka::debug_print_trace(__FILE__, __LINE__, true, __VA_ARGS__)
#else
#define TRACE(...)
#define TRACELN(...)
#endif

#define t__va_are_empty(...) (sizeof((char[]) {#__VA_ARGS__}) == 1)

#define VA_ARE_EMPTY(...) t__va_are_empty(__VA_ARGS__)

#if !defined NDEBUG
#define TRAP(...)                                                              \
  do {                                                                         \
    TRACELN("Encountered a debug trap");                                       \
    if (!VA_ARE_EMPTY(__VA_ARGS__)) {                                          \
      TRACELN("Details: " __VA_ARGS__);                                        \
    }                                                                          \
    t__debugtrap();                                                            \
  } while (0)
#else
#define TRAP(...) abort()
#endif

#if !defined NDEBUG
#define unreachable(...)                                                       \
  do {                                                                         \
    TRACELN("Reached an unreachable statement");                               \
    if (!VA_ARE_EMPTY(__VA_ARGS__)) {                                          \
      TRACELN("Details: " __VA_ARGS__);                                        \
    }                                                                          \
    t__unreachable();                                                          \
  } while (0)
#else
#define unreachable(...) t__debugtrap()
#endif

#if !defined NDEBUG
#define ASSERT(x, ...)                                                         \
  do {                                                                         \
    if (!(x)) rarely {                                                         \
      TRACELN("'ASSERT(" #x ")' fail in %s().", __func__);                     \
      if (!VA_ARE_EMPTY(__VA_ARGS__)) {                                        \
        TRACELN("Details: " __VA_ARGS__);                                      \
      }                                                                        \
      TRAP();                                                                  \
    }                                                                          \
  } while (0)
#else
#define ASSERT(...)
#endif

/* an index check of a container accessor. a debug build asserts it, and a
   release build with KOSH_HARDENED traps on a failed check. */
#if !defined NDEBUG
#define ASSERT_INDEX(x, ...) ASSERT(x, __VA_ARGS__)
#elif defined KOSH_HARDENED
#define ASSERT_INDEX(x, ...)                                                   \
  do {                                                                         \
    if (!(x)) rarely                                                           \
      __builtin_trap();                                                        \
  } while (0)
#else
#define ASSERT_INDEX(...)
#endif
