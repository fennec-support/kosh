/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of the pure text operations of Path on a POSIX
 * target. It checks filename, extension, and parent splitting, component
 * iteration, joining with and without separators, extension replacement,
 * normalization of dot, dot-dot, repeated, and trailing separators for absolute
 * and relative paths, absolute detection, and the existence queries of the
 * root directory and a missing name. A target whose separator is not a slash
 * runs no case, because every expectation spells POSIX separators.
 */

#include "Platform.hpp"
#include "Unit.hpp"
#include "base/Path.hpp"

using namespace koshka;

static fn normalized_text_of(StringView text) throws -> String
{
  return Path{text}.normalized().text().clone();
}

static fn test_filename_and_extension() throws -> void
{
  CHECK(Path{"/usr/lib/libc.so"}.filename() == StringView{"libc.so"});
  CHECK(Path{"plain"}.filename() == StringView{"plain"});
  CHECK(Path{"/"}.filename().is_empty());
  CHECK(Path{"dir/"}.filename().is_empty());
  CHECK(Path{""}.filename().is_empty());
  CHECK(Path::filename("a/b/c") == StringView{"c"});

  CHECK(Path{"a/file.txt"}.extension() == StringView{".txt"});
  CHECK(Path{"a/archive.tar.gz"}.extension() == StringView{".gz"});
  CHECK(Path{"a/noextension"}.extension().is_empty());
  CHECK(Path{"a/.hidden"}.extension().is_empty());
  CHECK(Path{"a/.hidden.conf"}.extension() == StringView{".conf"});
  CHECK(Path{"a/."}.extension().is_empty());
  CHECK(Path{"a/.."}.extension().is_empty());
  CHECK(Path{"dir.d/file"}.extension().is_empty());
  CHECK(Path{"a/trailing."}.extension() == StringView{"."});
}

static fn test_invocation_filename() throws -> void
{
  CHECK(Path::invocation_filename("-kosh", true) == StringView{"kosh"});
  CHECK(Path::invocation_filename("-kosh", false) == StringView{"-kosh"});
  CHECK(Path::invocation_filename("/bin/-sh", true) == StringView{"sh"});
  CHECK(Path::invocation_filename("/bin/sh", true) == StringView{"sh"});
  CHECK(Path::invocation_filename("", true).is_empty());
}

static fn test_parent() throws -> void
{
  CHECK(Path{"/a/b/c"}.parent().text() == StringView{"/a/b"});
  CHECK(Path{"/a"}.parent().text() == StringView{"/"});
  CHECK(Path{"/"}.parent().text() == StringView{"/"});
  CHECK(Path{"a/b"}.parent().text() == StringView{"a"});
  CHECK(Path{"a"}.parent().text().is_empty());
  CHECK(Path{""}.parent().text().is_empty());
  CHECK(Path{"a/b/"}.parent().text() == StringView{"a/b"});

  CHECK(Path{"a"}.parent_or_current().text() == StringView{"."});
  CHECK(Path{"a/b"}.parent_or_current().text() == StringView{"a"});
  CHECK(Path{"/a"}.parent_or_current().text() == StringView{"/"});
}

static fn test_absolute_detection() throws -> void
{
  CHECK(Path{"/"}.is_absolute());
  CHECK(Path{"/a/b"}.is_absolute());
  CHECK(Path{"a/b"}.is_relative());
  CHECK(Path{"./a"}.is_relative());
  CHECK(Path{""}.is_relative());
  CHECK(Path{"a/b/"}.has_trailing_separator());
  CHECK(!Path{"a/b"}.has_trailing_separator());
  CHECK(!Path{""}.has_trailing_separator());
}

static fn test_component_iteration() throws -> void
{
  let const path = Path{"//usr//local/bin/"};
  usize position = 0;

  let first = path.next_component(position);
  CHECK(first.text == StringView{"usr"});
  CHECK_EQUAL(first.start, 2);
  CHECK_EQUAL(first.end, 5);

  CHECK(path.next_component(position).text == StringView{"local"});
  CHECK(path.next_component(position).text == StringView{"bin"});
  CHECK(path.next_component(position).text.is_empty());
  CHECK_EQUAL(position, path.count());

  usize empty_position = 0;
  CHECK(Path{""}.next_component(empty_position).text.is_empty());
  usize root_position = 0;
  CHECK(Path{"///"}.next_component(root_position).text.is_empty());
}

static fn test_join() throws -> void
{
  let path = Path{"a"};

  path.append("b");
  CHECK(path.text() == StringView{"a/b"});

  path.append("/c");
  CHECK(path.text() == StringView{"a/b/c"});

  path.append("");
  CHECK(path.text() == StringView{"a/b/c"});

  path.push_component("d/");
  CHECK(path.text() == StringView{"a/b/c/d/"});

  path.push_component("e");
  CHECK(path.text() == StringView{"a/b/c/d/e"});

  let from_empty = Path{""};
  from_empty.append("first");
  CHECK(from_empty.text() == StringView{"first"});

  let root = Path{"/"};
  root.append("etc");
  CHECK(root.text() == StringView{"/etc"});

  let raw = Path{"a"};
  raw.append_raw("b");
  CHECK(raw.text() == StringView{"ab"});
}

static fn test_with_extension() throws -> void
{
  CHECK(Path{"a/file.txt"}.with_extension("md").text() ==
        StringView{"a/file.md"});
  CHECK(Path{"a/file.txt"}.with_extension(".md").text() ==
        StringView{"a/file.md"});
  CHECK(Path{"a/file"}.with_extension("md").text() == StringView{"a/file.md"});
  CHECK(Path{"a/file.txt"}.with_extension("").text() == StringView{"a/file"});
  CHECK(Path{"a/archive.tar.gz"}.with_extension("zst").text() ==
        StringView{"a/archive.tar.zst"});
  CHECK(Path{"a/.hidden"}.with_extension("bak").text() ==
        StringView{"a/.hidden.bak"});
}

static fn test_normalized_absolute() throws -> void
{
  CHECK(normalized_text_of("/") == StringView{"/"});
  CHECK(normalized_text_of("/a/b") == StringView{"/a/b"});
  CHECK(normalized_text_of("/a/./b//c/../d") == StringView{"/a/b/d"});
  CHECK(normalized_text_of("/a/b/..") == StringView{"/a"});
  CHECK(normalized_text_of("/a/..") == StringView{"/"});
  CHECK(normalized_text_of("/..") == StringView{"/"});
  CHECK(normalized_text_of("/../../a") == StringView{"/a"});
  CHECK(normalized_text_of("/a/b/") == StringView{"/a/b"});
  CHECK(normalized_text_of("//a") == StringView{"/a"});
  CHECK(normalized_text_of("/./.") == StringView{"/"});
}

static fn test_normalized_relative() throws -> void
{
  CHECK(normalized_text_of("") == StringView{"."});
  CHECK(normalized_text_of(".") == StringView{"."});
  CHECK(normalized_text_of("./") == StringView{"."});
  CHECK(normalized_text_of("a/..") == StringView{"."});
  CHECK(normalized_text_of("a/b/../..") == StringView{"."});
  CHECK(normalized_text_of("..") == StringView{".."});
  CHECK(normalized_text_of("../a") == StringView{"../a"});
  CHECK(normalized_text_of("../..") == StringView{"../.."});
  CHECK(normalized_text_of("a/../..") == StringView{".."});
  CHECK(normalized_text_of("a/../../b") == StringView{"../b"});
  CHECK(normalized_text_of("./a/./b/") == StringView{"a/b"});
  CHECK(normalized_text_of("a//b") == StringView{"a/b"});
  CHECK(normalized_text_of("a...b/.../c") == StringView{"a...b/.../c"});
}

static fn test_normalized_does_not_change_source() throws -> void
{
  let const path = Path{"/a/./b/../c"};

  let const normal = path.normalized();
  CHECK(path.text() == StringView{"/a/./b/../c"});
  CHECK(normal.text() == StringView{"/a/c"});
  CHECK(normal.normalized() == normal);
}

static fn test_equality() throws -> void
{
  CHECK(Path{"a/b"} == Path{"a/b"});
  CHECK(!(Path{"a/b"} == Path{"a/b/"}));
  CHECK(!(Path{"a/b"} == Path{"a/c"}));
}

static fn test_filesystem_queries() throws -> void
{
  CHECK(Path{"/"}.exists());
  CHECK(Path{"/"}.is_directory());
  CHECK(!Path{"/"}.is_regular_file());
  CHECK(!Path{"/koshka-unit-test-missing-name"}.exists());
  CHECK(!Path{"/koshka-unit-test-missing-name"}.is_directory());
  CHECK(!Path{"/koshka-unit-test-missing-name"}.file_size().has_value());
  CHECK(!Path{""}.exists());

  let const current = Path::current_directory();
  CHECK(current.is_absolute());
  CHECK(current.is_directory());
  CHECK(current.normalized() == current);
}

static fn test_absolute_resolution() throws -> void
{
  let const current = Path::current_directory();

  CHECK(Path{"/x/../y"}.to_absolute().text() == StringView{"/y"});

  let relative = Path{"sub/../file"}.to_absolute();
  let expected = current.clone();
  expected.append("file");
  CHECK(relative == expected.normalized());

  let kept = Path{"sub/../file"}.to_absolute_without_normalizing();
  CHECK(kept.is_absolute());
  CHECK(kept.view().substring(kept.count() - 11) == StringView{"sub/../file"});
}

fn kosh_main(int, char **) -> int
{
  if (os::DIRECTORY_SEPARATOR != '/') {
    std::printf("path_test: skipped, the directory separator is not a slash\n");
    return 0;
  }

  RUN_TEST(test_filename_and_extension);
  RUN_TEST(test_invocation_filename);
  RUN_TEST(test_parent);
  RUN_TEST(test_absolute_detection);
  RUN_TEST(test_component_iteration);
  RUN_TEST(test_join);
  RUN_TEST(test_with_extension);
  RUN_TEST(test_normalized_absolute);
  RUN_TEST(test_normalized_relative);
  RUN_TEST(test_normalized_does_not_change_source);
  RUN_TEST(test_equality);
  RUN_TEST(test_filesystem_queries);
  RUN_TEST(test_absolute_resolution);

  return koshka::unit::finish("path_test");
}
