/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines common scalar types, numeric bases, attributes, and
 * low-level helpers shared across the shell without pulling in heavier
 * subsystems.
 */

#pragma once

#if defined __SANITIZE_ADDRESS__
#define KOSH_HAS_ADDRESS_SANITIZER 1
#elif defined __has_feature
#if __has_feature(address_sanitizer)
#define KOSH_HAS_ADDRESS_SANITIZER 1
#endif
#endif

#if !defined KOSH_ENVCXXFLAGS
#warning KOSH_ENVCXXFLAGS should be defined. Please use provided makefile \
         for compilation!
#define KOSH_ENVCXXFLAGS "<unknown>"
#endif

#if !defined KOSH_COMPILER_COMMAND
#warning KOSH_COMPILER_COMMAND should be defined. Please use provided makefile \
         for compilation!
#define KOSH_COMPILER_COMMAND "<unknown>"
#endif

#if !defined KOSH_COMMIT_HASH
#warning KOSH_COMMIT_HASH should be defined. Please use provided makefile for \
         compilation!
#define KOSH_COMMIT_HASH "<unknown>"
#endif

#if !defined KOSH_BUILD_MODE
#warning KOSH_BUILD_MODE should be defined. Please use provided makefile for \
         compilation!
#define KOSH_BUILD_MODE "<unset>"
#endif

#if !defined KOSH_OS_INFO
#warning KOSH_OS_INFO should be defined. Please use provided makefile for \
         compilation!
#define KOSH_OS_INFO "<unset>"
#endif

#if !defined KOSH_LIBC
#warning KOSH_LIBC should be defined. Please use provided makefile for \
         compilation!
#define KOSH_LIBC "<unknown libc>"
#endif

#define KOSH_BUILD_DATE (__DATE__ " at " __TIME__)

#define KOSH_COMPILER KOSH_COMPILER_COMMAND " (" __VERSION__ ", " KOSH_LIBC ")"

#define KOSH_VER_MAJOR  0
#define KOSH_VER_MINOR  4
#define KOSH_VER_PATCH  0
#define KOSH_VER_SUFFIX ""

#define KOSH_STRINGIFY_INNER(x) #x
#define KOSH_STRINGIFY(x)       KOSH_STRINGIFY_INNER(x)
#define KOSH_VERSION_STRING                                                    \
  KOSH_STRINGIFY(KOSH_VER_MAJOR)                                               \
  "." KOSH_STRINGIFY(KOSH_VER_MINOR) "." KOSH_STRINGIFY(KOSH_VER_PATCH)        \
      KOSH_VER_SUFFIX

#define KOSH_SHORT_LICENSE                                                     \
  "Licensed under the 3-Clause BSD License.\n"                                 \
  "There is NO WARRANTY, to the extent permitted by law."

/* clang-format off */
#include <cctype>
#include <climits>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <clocale>
#include <cmath>
#include <cstring>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <initializer_list>
#include <new>
#include <exception>

/* clang-format on */

using u8   = uint8_t;
using u16  = uint16_t;
using u32  = uint32_t;
using u64  = uint64_t;
using u128 = __uint128_t;

using i8   = int8_t;
using i16  = int16_t;
using i32  = int32_t;
using i64  = int64_t;
using i128 = __int128_t;

using f64 = double;

using uchar = u8;

using usize   = size_t;
using isize   = ptrdiff_t;
using uintptr = uintptr_t;

using opaque = void;

#if defined __GNUC__ || defined __clang__ || defined __COSMOCC__
#define T__HAS_GCC_EXTENSIONS 1
#define t__used               __attribute__((used))
#define t__pure               __attribute__((pure))
#if defined KOSH_TINYREL
#define t__forceinline inline
#else
#define t__forceinline inline __attribute__((always_inline))
#endif
#define t__unreachable() __builtin_unreachable()
#define t__debugtrap()   __builtin_trap()
#else
#error Oh no! Segmentation fault. Please download a better compiler that \
       supports GNU extensions!
#define T__HAS_GCC_EXTENSIONS 0
#define t__used
#define t__pure
#define t__forceinline
#define t__unreachable() abort()
#define t__debugtrap()   abort()
#endif
#define t__concat_literal(x, y) x##y
#define concat_literal(x, y)    t__concat_literal(x, y)

template <typename T>
class t__exit_scope
{
public:
  t__exit_scope(T lambda) : m_lambda(lambda) {}
  ~t__exit_scope() { m_lambda(); }
  t__exit_scope(const t__exit_scope &);

private:
  T m_lambda;

  t__exit_scope &
  operator=(const t__exit_scope &);
};

class t__exit_scope_help
{
public:
  template <typename T>
  t__exit_scope<T>
  operator+(T t)
  {
    return t;
  }
};

#define defer                                                                  \
  const auto &concat_literal(defer__, __LINE__) =                              \
      t__exit_scope_help() + [&]() -> void

#define ENUM(e) static_cast<int>(e)

#define sub_sat(a, b) ((a) > (b) ? (a) - (b) : 0)

#define unused(x) ((void) (x))

#define countof(arr) (sizeof(arr) / sizeof(*(arr)))
#define steal        ::koshka::move_value
#define mustuse      [[nodiscard]]

#if defined __has_cpp_attribute && __has_cpp_attribute(no_unique_address)
#define notunique [[no_unique_address]]
#else
#define notunique
#endif

#define fallthru    [[fallthrough]]
#define usually     [[likely]]
#define rarely      [[unlikely]]
#define maybeunused [[maybe_unused]]
#define wontreturn  [[noreturn]]

#define fn   auto
#define let  auto
#define loop for (;;)

#define wontthrow noexcept
#define throws    noexcept(false)
#define abstract  = 0

#define donteliminate t__used
#define alwaysinline  t__forceinline

#if T__HAS_GCC_EXTENSIONS
#define pure t__pure
#define cold [[gnu::cold]]
#if defined KOSH_PROFILE_USE
#define hot
#else
#define hot [[gnu::hot]]
#endif
#if defined KOSH_TINYREL
#define flatten
#elif defined __clang__
#define flatten [[gnu::flatten]]
#else
#define flatten
#endif
#define noinline       [[gnu::noinline]]
#define targetisa(isa) [[gnu::target(isa)]]
#else
#define pure
#define cold
#define hot
#define flatten
#define noinline
#define targetisa(isa)
#endif

namespace koshka {

template <class T, T Value>
struct integral_constant
{
  static constexpr T value = Value;
};

using true_type  = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template <class T>
struct remove_reference
{
  using type = T;
};
template <class T>
struct remove_reference<T &>
{
  using type = T;
};
template <class T>
struct remove_reference<T &&>
{
  using type = T;
};
template <class T>
using remove_reference_t = typename remove_reference<T>::type;

template <class T>
struct remove_cv
{
  using type = T;
};
template <class T>
struct remove_cv<const T>
{
  using type = T;
};
template <class T>
struct remove_cv<volatile T>
{
  using type = T;
};
template <class T>
struct remove_cv<const volatile T>
{
  using type = T;
};
template <class T>
using remove_cv_t = typename remove_cv<T>::type;
template <class T>
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

template <bool Condition, class Then, class Else>
struct conditional
{
  using type = Then;
};
template <class Then, class Else>
struct conditional<false, Then, Else>
{
  using type = Else;
};
template <bool Condition, class Then, class Else>
using conditional_t = typename conditional<Condition, Then, Else>::type;

template <class T>
fn
declval() wontthrow -> T &&;

template <class T>
constexpr alwaysinline fn
move_value(T &&value) wontthrow -> remove_reference_t<T> &&
{
  return static_cast<remove_reference_t<T> &&>(value);
}

template <class T>
constexpr alwaysinline fn
forward(remove_reference_t<T> &value) wontthrow -> T &&
{
  return static_cast<T &&>(value);
}

template <class T>
constexpr alwaysinline fn
forward(remove_reference_t<T> &&value) wontthrow -> T &&
{
  return static_cast<T &&>(value);
}

template <class T, class U>
inline constexpr bool is_same_v = __is_same(T, U);
template <class T>
inline constexpr bool is_void_v = is_same_v<remove_cv_t<T>, void>;
template <class T>
inline constexpr bool is_enum_v = __is_enum(T);
template <class T>
using underlying_type_t = __underlying_type(T);
template <class T>
inline constexpr bool is_abstract_v = __is_abstract(T);
template <class Base, class Derived>
inline constexpr bool is_base_of_v = __is_base_of(Base, Derived);
template <class From, class To>
inline constexpr bool is_convertible_v = __is_convertible(From, To);
template <class T, class... Args>
inline constexpr bool is_constructible_v = __is_constructible(T, Args...);
template <class T>
inline constexpr bool is_copy_constructible_v =
    __is_constructible(T, const T &);
template <class T>
inline constexpr bool is_nothrow_move_constructible_v =
    __is_nothrow_constructible(T, T &&);
template <class T>
inline constexpr bool is_nothrow_move_assignable_v =
    __is_nothrow_assignable(T &, T &&);
template <class T>
inline constexpr bool is_nothrow_destructible_v = noexcept(declval<T &>().~T());
template <class T>
inline constexpr bool is_trivially_copyable_v = __is_trivially_copyable(T);
#if __has_builtin(__is_trivially_destructible)
template <class T>
inline constexpr bool is_trivially_destructible_v =
    __is_trivially_destructible(T);
#else
template <class T>
inline constexpr bool is_trivially_destructible_v = __has_trivial_destructor(T);
#endif
template <class T>
inline constexpr bool is_member_pointer_v = __is_member_pointer(T);
template <class T>
inline constexpr bool has_unique_object_representations_v =
    __has_unique_object_representations(T);

template <class T>
struct is_pointer_type : false_type
{};
template <class T>
struct is_pointer_type<T *> : true_type
{};
template <class T>
inline constexpr bool is_pointer_v = is_pointer_type<remove_cv_t<T>>::value;

template <class T>
struct is_integral_type : false_type
{};
template <>
struct is_integral_type<bool> : true_type
{};
template <>
struct is_integral_type<char> : true_type
{};
template <>
struct is_integral_type<signed char> : true_type
{};
template <>
struct is_integral_type<unsigned char> : true_type
{};
template <>
struct is_integral_type<wchar_t> : true_type
{};
template <>
struct is_integral_type<char8_t> : true_type
{};
template <>
struct is_integral_type<char16_t> : true_type
{};
template <>
struct is_integral_type<char32_t> : true_type
{};
template <>
struct is_integral_type<short> : true_type
{};
template <>
struct is_integral_type<unsigned short> : true_type
{};
template <>
struct is_integral_type<int> : true_type
{};
template <>
struct is_integral_type<unsigned int> : true_type
{};
template <>
struct is_integral_type<long> : true_type
{};
template <>
struct is_integral_type<unsigned long> : true_type
{};
template <>
struct is_integral_type<long long> : true_type
{};
template <>
struct is_integral_type<unsigned long long> : true_type
{};
template <>
struct is_integral_type<__int128> : true_type
{};
template <>
struct is_integral_type<unsigned __int128> : true_type
{};
template <class T>
inline constexpr bool is_integral_v = is_integral_type<remove_cv_t<T>>::value;

template <class T>
inline constexpr bool is_floating_point_v =
    is_same_v<remove_cv_t<T>, float> || is_same_v<remove_cv_t<T>, double> ||
    is_same_v<remove_cv_t<T>, long double>;
template <class T>
inline constexpr bool is_arithmetic_v =
    is_integral_v<T> || is_floating_point_v<T>;

template <class T, bool IsArithmetic = is_arithmetic_v<T>>
struct is_signed_type : false_type
{};
template <class T>
struct is_signed_type<T, true> : integral_constant<bool, T(-1) < T(0)>
{};
template <class T>
inline constexpr bool is_signed_v = is_signed_type<T>::value;

template <class T>
struct integer_limits
{
  static_assert(is_integral_v<T>);

  static constexpr T max_value =
      is_signed_v<T>
          ? static_cast<T>(
                (static_cast<unsigned __int128>(1) << (sizeof(T) * 8 - 1)) - 1)
          : static_cast<T>(~static_cast<T>(0));
  static constexpr T min_value =
      is_signed_v<T> ? static_cast<T>(-max_value - 1) : static_cast<T>(0);
};

enum class root_evaluation_mode : u8
{
  Normal,
  PreparedPipelineStage,
  PreparedAsyncCommand,
  ContainedSubstitution,
};

enum class int_base : u8
{
  binary  = 2,
  octal   = 8,
  decimal = 10,
  hex     = 16
};

template <int_base TagBase, class T>
struct tagged_int
{
  using underlying               = T;
  static constexpr int_base base = TagBase;

  T value{0};

  constexpr tagged_int() wontthrow = default;
  constexpr tagged_int(T initial) wontthrow : value(initial) {}
  constexpr
  operator T() const wontthrow
  {
    return value;
  }
};

template <class T>
struct is_tagged_int : false_type
{};
template <int_base TagBase, class T>
struct is_tagged_int<tagged_int<TagBase, T>> : true_type
{};
template <class T>
inline constexpr bool is_tagged_int_v = is_tagged_int<T>::value;

template <class T>
concept is_cloneable = requires(const T &value) { value.clone(); } &&
                       is_same_v<decltype(declval<const T &>().clone()), T>;

template <class T>
concept is_plainly_copyable =
    is_trivially_copyable_v<T> && !is_pointer_v<T> && !is_member_pointer_v<T>;

template <class T>
mustuse fn
clone_value(const T &value) throws -> T
{
  static_assert(is_cloneable<T> || is_plainly_copyable<T>,
                "a type that owns resources or is a pointer needs a clone()");

  if constexpr (is_cloneable<T>)
    return value.clone();
  else
    return value;
}

using bi16 = tagged_int<int_base::binary, i16>;
using bi32 = tagged_int<int_base::binary, i32>;
using bi64 = tagged_int<int_base::binary, i64>;
using bu16 = tagged_int<int_base::binary, u16>;
using bu32 = tagged_int<int_base::binary, u32>;
using bu64 = tagged_int<int_base::binary, u64>;

using oi16 = tagged_int<int_base::octal, i16>;
using oi32 = tagged_int<int_base::octal, i32>;
using oi64 = tagged_int<int_base::octal, i64>;
using ou16 = tagged_int<int_base::octal, u16>;
using ou32 = tagged_int<int_base::octal, u32>;
using ou64 = tagged_int<int_base::octal, u64>;

using hi16 = tagged_int<int_base::hex, i16>;
using hi32 = tagged_int<int_base::hex, i32>;
using hi64 = tagged_int<int_base::hex, i64>;
using hu16 = tagged_int<int_base::hex, u16>;
using hu32 = tagged_int<int_base::hex, u32>;
using hu64 = tagged_int<int_base::hex, u64>;
} /* namespace koshka */
