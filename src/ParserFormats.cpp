/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file detects host document formats and extracts embedded shell fragments
 * for analysis and formatting. It maps positions between host and shell source,
 * handles indentation and JSON string codecs, and applies formatted
 * replacements to the host document. Each source under formats implements one
 * host format, and shared format code stays here.
 */

#include "ParserFormats.hpp"

#include "Lexer.hpp"
#include "MimicMood.hpp"
#include "Utils.hpp"
#include "base/Path.hpp"
#include "base/StaticStringMap.hpp"

namespace koshka {

pure fn
parser_format_has_substring(StringView source, StringView wanted) wontthrow
    -> bool
{
  return source.find_substring(wanted).has_value();
}

pure fn
parser_format_filename(StringView path) wontthrow -> StringView
{
  usize start = 0;

  for (usize position = 0; position < path.length; position++)
    if (path[position] == '/' || path[position] == '\\') {
      start = position + 1;
    }

  return path.substring(start);
}

pure fn
parser_format_ascii_equal(StringView left, StringView right) wontthrow -> bool
{
  if (left.length != right.length) return false;

  for (usize position = 0; position < left.length; position++) {
    let left_byte  = left[position];
    let right_byte = right[position];

    if (left_byte == right_byte) continue;
    if (left_byte >= 'A' && left_byte <= 'Z') {
      left_byte += 'a' - 'A';
    }

    if (right_byte >= 'A' && right_byte <= 'Z') {
      right_byte += 'a' - 'A';
    }

    if (left_byte != right_byte) return false;
  }

  return true;
}

static pure fn
path_ends_with(StringView path, StringView suffix) wontthrow -> bool
{
  return suffix.length <= path.length &&
         parser_format_ascii_equal(path.substring(path.length - suffix.length),
                                   suffix);
}

static pure fn
path_starts_with(StringView path, StringView prefix) wontthrow -> bool
{
  return prefix.length <= path.length &&
         parser_format_ascii_equal(path.substring_of_length(0, prefix.length),
                                   prefix);
}

static pure fn
known_host_extension(StringView path) wontthrow -> bool
{
  static const StringView EXTENSIONS[] = {
      ".json", ".jsonc", ".markdown", ".md",   ".mk",
      ".nix",  ".spec",  ".tf",       ".yaml", ".yml",
  };

  for (let const extension : EXTENSIONS)
    if (path_ends_with(path, extension)) return true;

  return false;
}

constexpr static_string_entry<parser_format_kind> FORMAT_FILENAME_ENTRIES[] = {
    {SSK("action.yml"),               parser_format_kind::GithubActions     },
    {SSK("action.yaml"),              parser_format_kind::GithubActions     },
    {SSK(".gitlab-ci.yml"),           parser_format_kind::GitlabCi          },
    {SSK(".drone.yml"),               parser_format_kind::Drone             },
    {SSK(".woodpecker.yml"),          parser_format_kind::Woodpecker        },
    {SSK(".woodpecker.yaml"),         parser_format_kind::Woodpecker        },
    {SSK("azure-pipelines.yml"),      parser_format_kind::AzurePipelines    },
    {SSK("azure-pipelines.yaml"),     parser_format_kind::AzurePipelines    },
    {SSK("bitbucket-pipelines.yml"),  parser_format_kind::BitbucketPipelines},
    {SSK("bitbucket-pipelines.yaml"), parser_format_kind::BitbucketPipelines},
    {SSK(".travis.yml"),              parser_format_kind::TravisCi          },
    {SSK("cloudbuild.yml"),           parser_format_kind::CloudBuild        },
    {SSK("cloudbuild.yaml"),          parser_format_kind::CloudBuild        },
    {SSK("compose.yml"),              parser_format_kind::Compose           },
    {SSK("compose.yaml"),             parser_format_kind::Compose           },
    {SSK("docker-compose.yml"),       parser_format_kind::Compose           },
    {SSK("docker-compose.yaml"),      parser_format_kind::Compose           },
    {SSK("taskfile.yml"),             parser_format_kind::Taskfile          },
    {SSK("taskfile.yaml"),            parser_format_kind::Taskfile          },
    {SSK("taskfile.dist.yml"),        parser_format_kind::Taskfile          },
    {SSK("taskfile.dist.yaml"),       parser_format_kind::Taskfile          },
    {SSK("dockerfile"),               parser_format_kind::Dockerfile        },
    {SSK("containerfile"),            parser_format_kind::Dockerfile        },
    {SSK("makefile"),                 parser_format_kind::Makefile          },
    {SSK("gnumakefile"),              parser_format_kind::Makefile          },
    {SSK("justfile"),                 parser_format_kind::Justfile          },
    {SSK(".justfile"),                parser_format_kind::Justfile          },
    {SSK("package.json"),             parser_format_kind::PackageJson       },
    {SSK("devcontainer.json"),        parser_format_kind::DevContainer      },
    {SSK("readme"),                   parser_format_kind::Markdown          },
};
constexpr StaticStringMap FORMAT_FILENAMES{FORMAT_FILENAME_ENTRIES};

static pure fn
path_has_directory(StringView path, StringView directory) wontthrow -> bool
{
  if (path.length >= directory.length &&
      parser_format_ascii_equal(path.substring_of_length(0, directory.length),
                                directory))
  {
    return true;
  }

  for (usize position = 0; position + directory.length < path.length;
       position++)
  {
    if ((path[position] == '/' || path[position] == '\\') &&
        parser_format_ascii_equal(
            path.substring_of_length(position + 1, directory.length),
            directory))
    {
      return true;
    }
  }

  return false;
}

static pure fn
path_has_tail(StringView path, StringView tail) wontthrow -> bool
{
  if (!path_ends_with(path, tail)) return false;
  if (path.length == tail.length) return true;

  let const separator = path[path.length - tail.length - 1];

  return separator == '/' || separator == '\\';
}

static fn
detect_format_kind(const parser_format_input &input) throws
    -> parser_format_kind
{
  let       path     = input.path.has_value() ? *input.path : StringView{};
  let const filename = parser_format_filename(path);
  let const named_kind =
      parser_format_find_ascii_folded(FORMAT_FILENAMES, filename);
  let const source = input.source;
  let const language_id =
      input.language_id.has_value() ? *input.language_id : StringView{};

  if (path_has_directory(path, ".github/workflows/") ||
      named_kind == parser_format_kind::GithubActions)
  {
    return parser_format_kind::GithubActions;
  }

  if (path_has_directory(path, ".gitea/workflows/"))
    return parser_format_kind::GiteaActions;
  if (path_has_directory(path, ".forgejo/workflows/"))
    return parser_format_kind::ForgejoActions;
  if (named_kind == parser_format_kind::GitlabCi)
    return parser_format_kind::GitlabCi;
  if (named_kind == parser_format_kind::Drone) return parser_format_kind::Drone;
  if (path_has_directory(path, ".woodpecker/") ||
      named_kind == parser_format_kind::Woodpecker)
  {
    return parser_format_kind::Woodpecker;
  }

  if (path_has_directory(path, ".circleci/") &&
      (path_ends_with(path, ".yml") || path_ends_with(path, ".yaml")))
  {
    return parser_format_kind::CircleCi;
  }

  if (named_kind == parser_format_kind::AzurePipelines)
    return parser_format_kind::AzurePipelines;
  if (named_kind == parser_format_kind::BitbucketPipelines)
    return parser_format_kind::BitbucketPipelines;
  if (path_has_directory(path, ".buildkite/") &&
      (path_ends_with(path, ".yml") || path_ends_with(path, ".yaml")))
  {
    return parser_format_kind::Buildkite;
  }

  if (named_kind == parser_format_kind::TravisCi)
    return parser_format_kind::TravisCi;
  if (named_kind == parser_format_kind::CloudBuild)
    return parser_format_kind::CloudBuild;
  if (named_kind == parser_format_kind::Compose)
    return parser_format_kind::Compose;
  if (named_kind == parser_format_kind::Taskfile)
    return parser_format_kind::Taskfile;
  if (named_kind == parser_format_kind::Dockerfile ||
      path_ends_with(filename, ".dockerfile") ||
      path_ends_with(filename, ".containerfile") ||
      path_starts_with(filename, "dockerfile.") ||
      path_starts_with(filename, "containerfile."))
  {
    return parser_format_kind::Dockerfile;
  }

  if (named_kind == parser_format_kind::Makefile ||
      path_ends_with(filename, ".mk"))
  {
    return parser_format_kind::Makefile;
  }

  if (named_kind == parser_format_kind::Justfile ||
      path_ends_with(filename, ".just"))
  {
    return parser_format_kind::Justfile;
  }

  if (let const inner_name = jinja_template_inner_name(filename)) {
    return jinja_template_shell_mood(*inner_name, source).has_value()
               ? parser_format_kind::Jinja
               : parser_format_kind::UnknownHost;
  }

  if (named_kind == parser_format_kind::PackageJson)
    return parser_format_kind::PackageJson;
  if (path_has_tail(path, ".vscode/tasks.json"))
    return parser_format_kind::VscodeTasks;
  if (named_kind == parser_format_kind::DevContainer &&
      (path_has_directory(path, ".devcontainer/") ||
       path_has_tail(path, ".devcontainer.json")))
  {
    return parser_format_kind::DevContainer;
  }

  if (path_ends_with(path, ".spec")) return parser_format_kind::RpmSpec;
  if (path_ends_with(path, ".md") || path_ends_with(path, ".markdown") ||
      named_kind == parser_format_kind::Markdown ||
      parser_format_ascii_equal(language_id, "markdown"))
  {
    return parser_format_kind::Markdown;
  }

  if (parser_format_ascii_equal(language_id, "dockercompose"))
    return parser_format_kind::Compose;

  if (path_ends_with(path, ".yml") || path_ends_with(path, ".yaml") ||
      parser_format_ascii_equal(language_id, "yaml"))
  {
    if (parser_format_has_substring(source, "apiVersion:") &&
        parser_format_has_substring(source, "kind:"))
    {
      return parser_format_kind::Kubernetes;
    }

    if (parser_format_has_substring(source, "runs-on:") &&
        parser_format_has_substring(source, "steps:"))
    {
      return parser_format_kind::GithubActions;
    }

    if (parser_format_has_substring(source, "tasks:") &&
        parser_format_has_substring(source, "version:"))
    {
      return parser_format_kind::Taskfile;
    }

    if (parser_format_has_substring(source, "hosts:") &&
        parser_format_has_substring(source, "tasks:"))
    {
      return parser_format_kind::Ansible;
    }

    if (parser_format_has_substring(source, "services:") &&
        (parser_format_has_substring(source, "image:") ||
         parser_format_has_substring(source, "build:")))
    {
      return parser_format_kind::Compose;
    }

    if (parser_format_has_substring(source, "pipelines:") &&
        parser_format_has_substring(source, "script:"))
    {
      return parser_format_kind::BitbucketPipelines;
    }

    if (parser_format_has_substring(source, "jobs:") &&
        parser_format_has_substring(source, "script:"))
    {
      return parser_format_kind::GitlabCi;
    }

    return parser_format_kind::UnknownHost;
  }

  if (parser_format_ascii_equal(language_id, "dockerfile"))
    return parser_format_kind::Dockerfile;
  if (parser_format_ascii_equal(language_id, "makefile"))
    return parser_format_kind::Makefile;
  if (parser_format_ascii_equal(language_id, "json") ||
      parser_format_ascii_equal(language_id, "jsonc"))
  {
    return parser_format_kind::UnknownHost;
  }

  if (known_host_extension(path) ||
      parser_format_ascii_equal(filename, "jenkinsfile"))
  {
    return parser_format_kind::UnknownHost;
  }

  if (path.is_empty() || !language_id.is_empty() ||
      Path{path}.is_shell_source(source))
  {
    return parser_format_kind::Shell;
  }

  return parser_format_kind::UnknownHost;
}

static fn
make_analysis_source(StringView host_source,
                     usize      host_start,
                     usize      host_end) throws -> String
{
  let analysis_source = String{heap_allocator()};

  analysis_source.reserve(host_end);

  let const literal_start = host_start < host_end ? host_start : host_end;

  for (usize position = 0; position < literal_start; position++) {
    let const byte = host_source[position];
    analysis_source.push(byte == '\n' ? '\n' : ' ');
  }

  analysis_source.append(
      host_source.substring_of_length(literal_start, host_end - literal_start));

  return analysis_source;
}

fn
parser_format_add_fragment(parsed_format_document &document,
                           StringView              host_source,
                           usize                   host_start,
                           usize                   host_end,
                           usize                   indent_length,
                           Maybe<String>           prepared_analysis_source,
                           parser_format_codec     codec,
                           mimic_mood              mood) throws -> void
{
  if (host_start >= host_end || host_end > host_source.length) {
    return;
  }

  let fragment = parser_format_fragment{};

  fragment.analysis_source =
      prepared_analysis_source.has_value()
          ? prepared_analysis_source.take()
          : make_analysis_source(host_source, host_start, host_end);
  fragment.shell_source = String{
      heap_allocator(),
      host_source.substring_of_length(host_start, host_end - host_start)};
  fragment.mood  = mood;
  fragment.codec = codec;
  fragment.should_silence_unresolved_commands =
      parser_format_should_silence_unresolved_commands(document.kind);
  fragment.host_start    = host_start;
  fragment.host_end      = host_end;
  fragment.indent_length = indent_length;
  document.fragments.push(steal(fragment));
}

static pure fn
template_construct_end(StringView source,
                       usize      position,
                       usize      end,
                       u8         template_syntax,
                       bool      &is_value) wontthrow -> Maybe<usize>
{
  let const remaining = end - position;
  let const do_find = [&](usize from, char first, char second) -> Maybe<usize> {
    for (usize at = from; at + 1 < end; at++) {
      if (source[at] == first && source[at + 1] == second) return at + 2;
    }

    return None;
  };

  is_value = true;

  if ((template_syntax & TEMPLATE_DOLLAR_CURLY) != 0 && remaining >= 3 &&
      source[position] == '$' && source[position + 1] == '{' &&
      source[position + 2] == '{')
  {
    return do_find(position + 3, '}', '}');
  }

  if (source[position] == '{' && remaining >= 2) {
    let const opener = source[position + 1];

    if (opener == '{' &&
        (template_syntax & (TEMPLATE_CURLY | TEMPLATE_DOLLAR_CURLY)) != 0)
    {
      return do_find(position + 2, '}', '}');
    }

    if ((template_syntax & TEMPLATE_STATEMENTS) != 0 &&
        (opener == '%' || opener == '#'))
    {
      is_value = false;

      return do_find(position + 2, opener, '}');
    }
  }

  if ((template_syntax & TEMPLATE_AZURE_MACROS) != 0 && remaining >= 3 &&
      source[position] == '$')
  {
    if (source[position + 1] == '[') {
      for (usize at = position + 2; at < end && source[at] != '\n'; at++) {
        if (source[at] == ']') return at + 1;
      }

      return None;
    }

    if (source[position + 1] == '(') {
      bool has_macro_marker = false;

      for (usize at = position + 2; at < end; at++) {
        let const byte = source[at];

        if (byte == ')') {
          if (!has_macro_marker || at == position + 2) return None;
          return at + 1;
        }

        let const is_word =
            (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '_' || byte == '.';

        if (!is_word) return None;
        if (byte == '.' || (byte >= 'A' && byte <= 'Z'))
          has_macro_marker = true;
      }

      return None;
    }
  }

  if ((template_syntax & TEMPLATE_CIRCLE_PARAMETERS) != 0 && remaining >= 4 &&
      source[position] == '<' && source[position + 1] == '<')
  {
    usize at = position + 2;

    while (at < end && source[at] == ' ')
      at++;

    let const word_start = source.substring_of_length(at, end - at);

    if (!word_start.starts_with("parameters.") &&
        !word_start.starts_with("pipeline.") &&
        !word_start.starts_with("matrix."))
    {
      return None;
    }

    for (; at + 1 < end && source[at] != '\n'; at++) {
      if (source[at] == '>' && source[at + 1] == '>') return at + 2;
    }
  }

  return None;
}

fn
parser_format_masked_template_source(StringView host_source,
                                     usize      host_start,
                                     usize      host_end,
                                     u8         template_syntax,
                                     bool      &did_mask) throws -> String
{
  let analysis_source = String{heap_allocator()};

  analysis_source.reserve(host_end);
  did_mask = false;

  for (usize position = 0; position < host_start; position++)
    analysis_source.push(host_source[position] == '\n' ? '\n' : ' ');

  let const has_curly_escape   = template_syntax == TEMPLATE_CURLY;
  bool      is_in_single_quote = false;
  bool      is_in_double_quote = false;
  bool      is_escaped         = false;
  usize     position           = host_start;

  while (position < host_end) {
    let const byte = host_source[position];

    if (has_curly_escape && position + 3 < host_end &&
        host_source.substring_of_length(position, 4) == "{{{{")
    {
      analysis_source.append("xxxx");
      position += 4;
      continue;
    }

    bool      is_value      = true;
    let const construct_end = template_construct_end(
        host_source, position, host_end, template_syntax, is_value);

    if (!construct_end.has_value()) {
      if (is_escaped) {
        is_escaped = false;
      } else if (byte == '\\' && !is_in_single_quote) {
        is_escaped = true;
      } else if (byte == '\'' && !is_in_double_quote) {
        is_in_single_quote = !is_in_single_quote;
      } else if (byte == '"' && !is_in_single_quote) {
        is_in_double_quote = !is_in_double_quote;
      } else if (byte == '\n') {
        is_in_single_quote = false;
        is_in_double_quote = false;
      }

      analysis_source.push(byte);
      position++;
      continue;
    }

    let const length      = *construct_end - position;
    let const construct   = host_source.substring_of_length(position, length);
    let const has_newline = construct.find_character('\n').has_value();

    if (is_value && !has_newline && !is_in_single_quote && length >= 4) {
      if (is_in_double_quote && length >= 5) {
        analysis_source.append("${1#");
        analysis_source.append_repeated('x', length - 5);
        analysis_source.push('}');
      } else {
        analysis_source.append(is_in_double_quote ? "$1" : "$$");
        analysis_source.append_repeated('x', length - 2);
      }
    } else {
      for (usize at = 0; at < construct.length; at++) {
        analysis_source.push(construct[at] == '\n' ? '\n'
                             : is_value            ? 'x'
                                                   : ' ');
      }
    }

    did_mask = true;
    position = *construct_end;
  }

  return analysis_source;
}

fn
parser_format_add_indented_fragment(
    parsed_format_document &document,
    StringView              host_source,
    usize                   host_start,
    usize                   host_end,
    usize                   indent_length,
    mimic_mood              mood,
    Maybe<String>           prepared_analysis_source) throws -> void
{
  parser_format_add_fragment(document, host_source, host_start, host_end,
                             indent_length, steal(prepared_analysis_source),
                             parser_format_codec::Indented, mood);
  if (document.fragments.is_empty()) return;

  let  &fragment   = document.fragments.back();
  let   deindented = String{heap_allocator()};
  usize position   = host_start;

  while (position < host_end) {
    let const line_start = position;
    let       line       = host_source.next_line(position);

    if (line_start + line.length > host_end)
      line = host_source.substring_of_length(line_start, host_end - line_start);

    let removed = usize{0};

    while (removed < line.length && removed < indent_length &&
           line[removed] == ' ')
    {
      removed++;
    }

    deindented.append(line.substring(removed));
    if (position <= host_end && position > line_start + line.length) {
      deindented.push('\n');
    }
  }

  fragment.shell_source = steal(deindented);
}

static fn
yaml_key_match(StringView        line,
               const StringView *keys,
               usize             key_count,
               usize            &content_position) wontthrow -> bool
{
  usize position = 0;

  while (position < line.length &&
         (line[position] == ' ' || line[position] == '\t'))
  {
    position++;
  }

  if (position < line.length && line[position] == '-') {
    position++;

    while (position < line.length && line[position] == ' ') {
      position++;
    }
  }

  if (position >= line.length) return false;

  for (usize key_index = 0; key_index < key_count; key_index++) {
    let const key = keys[key_index];

    if (key.is_empty()) continue;
    if (line[position] != key[0]) continue;
    if (position + key.length >= line.length) continue;
    if (line.substring_of_length(position, key.length) != key) continue;
    if (line[position + key.length] != ':') continue;
    content_position = position + key.length + 1;

    while (content_position < line.length &&
           (line[content_position] == ' ' || line[content_position] == '\t'))
    {
      content_position++;
    }

    return true;
  }

  return false;
}

static pure fn
leading_spaces(StringView line) wontthrow -> usize
{
  usize count = 0;

  while (count < line.length && line[count] == ' ') {
    count++;
  }

  return count;
}

static pure fn
mood_near(StringView source, usize position, mimic_mood fallback) wontthrow
    -> mimic_mood
{
  let const start  = position > 512 ? position - 512 : 0;
  let const nearby = source.substring_of_length(start, position - start);

  if (parser_format_has_substring(nearby, "shell: bash") ||
      parser_format_has_substring(nearby, "executable: /bin/bash") ||
      parser_format_has_substring(nearby, "interpreter: bash"))
  {
    return mimic_mood::Bash;
  }

  if (parser_format_has_substring(nearby, "shell: kosh") ||
      parser_format_has_substring(nearby, "executable: kosh"))
  {
    return mimic_mood::Default;
  }

  if (parser_format_has_substring(nearby, "shell: sh") ||
      parser_format_has_substring(nearby, "executable: /bin/sh"))
  {
    return mimic_mood::Posix;
  }

  return fallback;
}

static pure fn
yaml_key_position(StringView line) wontthrow -> usize
{
  usize position = leading_spaces(line);

  if (position < line.length && line[position] == '-') {
    position++;

    while (position < line.length && line[position] == ' ') {
      position++;
    }
  }

  return position;
}

static pure fn
yaml_line_value(StringView line, StringView key) wontthrow -> Maybe<StringView>
{
  let const position = yaml_key_position(line);

  if (position + key.length >= line.length ||
      line.substring_of_length(position, key.length) != key ||
      line[position + key.length] != ':')
  {
    return None;
  }

  return line.substring(position + key.length + 1).trim_blanks();
}

static fn
previous_line(StringView source, usize &position) wontthrow -> StringView
{
  if (position == 0) return StringView{};

  usize end = position;

  if (end > 0 && source[end - 1] == '\n') {
    end--;
  }

  let start = end;

  while (start > 0 && source[start - 1] != '\n') {
    start--;
  }

  position = start;

  return source.substring_of_length(start, end - start);
}

static pure fn
workflow_shell_mood(StringView value) wontthrow -> Maybe<mimic_mood>
{
  if (!value.is_empty() && (value[0] == '\'' || value[0] == '"')) {
    value = value.substring(1);
  }

  if (value.is_empty()) return None;

  switch (value[0]) {
  case 'b':
    if (value.starts_with("bash")) return mimic_mood::Bash;
    break;

  case 'd':
    if (value.starts_with("dash")) return mimic_mood::Posix;
    break;

  case 's':
    if (value.starts_with("sh")) return mimic_mood::Posix;
    break;

  case 'k':
    if (value.starts_with("kosh")) return mimic_mood::Default;
    break;

  default: break;
  }

  return None;
}

static pure fn
selected_workflow_mood(StringView source,
                       usize      line_start,
                       StringView line,
                       mimic_mood fallback) wontthrow -> Maybe<mimic_mood>
{
  let const current_indent = leading_spaces(line);
  let       scan_position  = line_start;

  while (scan_position > 0) {
    let const previous = previous_line(source, scan_position);

    if (previous.trim_blanks().is_empty()) continue;

    let const previous_indent = leading_spaces(previous);

    if (previous_indent == current_indent &&
        previous_indent < previous.length && previous[previous_indent] == '-')
    {
      break;
    }

    if (let const shell = yaml_line_value(previous, "shell"); shell.has_value())
      return workflow_shell_mood(*shell);
    if (previous_indent < current_indent) break;
  }

  scan_position = line_start;

  while (scan_position > 0) {
    let const previous = previous_line(source, scan_position);
    let const runner   = yaml_line_value(previous, "runs-on");

    if (!runner.has_value()) continue;
    if (parser_format_has_substring(*runner, "windows")) return None;
    break;
  }

  return fallback;
}

static fn
replace_analysis_newlines(StringView source,
                          usize      start,
                          usize      end,
                          usize      content_indent,
                          bool       is_folded_block) throws -> String
{
  let result = String{heap_allocator()};

  result.reserve(source.length);

  for (usize position = 0; position < source.length; position++) {
    let const byte = source[position];

    if (byte != '\n' || position < start || position + 1 >= end) {
      result.push(byte);
      continue;
    }

    if (!is_folded_block) {
      result.push(' ');
      continue;
    }

    usize line_begin = position;

    while (line_begin > 0 && source[line_begin - 1] != '\n')
      line_begin--;

    let const current =
        source.substring_of_length(line_begin, position - line_begin);
    usize next_end = position + 1;

    while (next_end < source.length && source[next_end] != '\n')
      next_end++;

    let const next =
        source.substring_of_length(position + 1, next_end - position - 1);
    let const is_joined = !current.trim_blanks().is_empty() &&
                          !next.trim_blanks().is_empty() &&
                          leading_spaces(current) == content_indent &&
                          leading_spaces(next) == content_indent;

    result.push(is_joined ? ' ' : '\n');
  }

  return result;
}

static fn
decode_yaml_quoted_analysis(StringView source,
                            usize      start,
                            usize      end,
                            char       quote) throws -> String
{
  let result = String{heap_allocator()};

  result.reserve(source.length);

  for (usize position = 0; position < source.length; position++) {
    let const byte      = source[position];
    let const is_inside = position >= start && position + 1 < end;

    if (is_inside && quote == '"' && byte == '\\') {
      result.push(' ');
      result.push(source[position + 1] == '\n' ? '\n' : source[position + 1]);
      position++;
      continue;
    }

    if (is_inside && quote == '\'' && byte == '\'' &&
        source[position + 1] == '\'')
    {
      result.push(' ');
      result.push('\'');
      position++;
      continue;
    }

    result.push(byte);
  }

  return result;
}

static pure fn
heredoc_delimiter_at(StringView line, usize &position) wontthrow
    -> Maybe<StringView>
{
  while (position + 1 < line.length) {
    if (line[position] != '<' || line[position + 1] != '<') {
      position++;
      continue;
    }

    position += 2;
    if (position < line.length && line[position] == '<') {
      position++;
      continue;
    }

    if (position < line.length && line[position] == '-') position++;

    while (position < line.length && line[position] == ' ')
      position++;

    if (position < line.length &&
        (line[position] == '\'' || line[position] == '"'))
    {
      position++;
    }

    let const word_start = position;

    if (position >= line.length ||
        !(lexer::is_variable_name_start(line[position])))
    {
      continue;
    }

    while (position < line.length && lexer::is_variable_name(line[position]))
      position++;

    return line.substring_of_length(word_start, position - word_start);
  }

  return None;
}

static fn
align_indented_heredoc_terminators(StringView source,
                                   usize      start,
                                   usize      end) throws -> String
{
  let        result    = String{heap_allocator(), source};
  let        rewritten = String{heap_allocator()};
  StringView pending[16]{};
  usize      pending_count = 0;
  usize      pending_index = 0;
  usize      position      = start;
  bool       has_rewrite   = false;

  rewritten.reserve(source.length);

  while (position < end) {
    let const line_start = position;
    let const line       = source.next_line(position);

    if (pending_index < pending_count) {
      let const content = line.trim_blanks();
      let const indent  = leading_spaces(line);

      if (content == pending[pending_index] && indent > 0 && line_start > 0 &&
          leading_spaces(line) + content.length <= line.length)
      {
        rewritten.append(source.substring_of_length(
            rewritten.length(), line_start - 1 - rewritten.length()));
        rewritten.append_repeated(' ', indent);
        rewritten.push('\n');
        rewritten.append(line.substring(indent));
        has_rewrite = true;
        pending_index++;
      } else if (content == pending[pending_index]) {
        pending_index++;
      }

      if (pending_index == pending_count) {
        pending_count = 0;
        pending_index = 0;
      }

      continue;
    }

    usize scan = 0;

    while (pending_count < countof(pending)) {
      let const delimiter = heredoc_delimiter_at(line, scan);

      if (!delimiter.has_value()) break;
      pending[pending_count++] = *delimiter;
    }
  }

  if (!has_rewrite) return result;

  rewritten.append(source.substring(rewritten.length()));

  return rewritten;
}

static pure fn
yaml_block_indicator(StringView value) wontthrow -> Maybe<bool>
{
  if (value.is_empty() || (value[0] != '|' && value[0] != '>')) return None;

  usize position = 1;

  while (position < value.length &&
         (value[position] == '-' || value[position] == '+' ||
          (value[position] >= '0' && value[position] <= '9')))
    position++;

  while (position < value.length &&
         (value[position] == ' ' || value[position] == '\t'))
    position++;

  if (position < value.length && value[position] != '#') return None;

  return value[0] == '>';
}

static pure fn
skip_yaml_value_decorations(StringView line,
                            usize      position,
                            bool      &is_skipped) wontthrow -> usize
{
  is_skipped = false;

  loop {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t'))
      position++;

    if (position >= line.length) return position;
    if (line[position] != '&' && line[position] != '!') return position;

    let const decoration_start = position;

    while (position < line.length && line[position] != ' ')
      position++;

    let const decoration =
        line.substring_of_length(decoration_start, position - decoration_start);

    if (decoration == "!reference") {
      is_skipped = true;

      return position;
    }
  }
}

static pure fn
is_yaml_mapping_entry(StringView value) wontthrow -> bool
{
  usize position = 0;

  while (position < value.length &&
         ((value[position] >= 'a' && value[position] <= 'z') ||
          (value[position] >= 'A' && value[position] <= 'Z') ||
          (value[position] >= '0' && value[position] <= '9') ||
          value[position] == '_' || value[position] == '-' ||
          value[position] == '.'))
    position++;

  return position > 0 && position < value.length && value[position] == ':' &&
         (position + 1 == value.length || value[position + 1] == ' ');
}

static pure fn
plain_yaml_value_end(StringView line, usize start) wontthrow -> usize
{
  usize end = line.length;

  for (usize position = start + 1; position < line.length; position++) {
    if (line[position] == '#' &&
        (line[position - 1] == ' ' || line[position - 1] == '\t'))
    {
      end = position;
      break;
    }
  }

  while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t' ||
                         line[end - 1] == '\r'))
    end--;

  return end;
}

struct yaml_scalar_item
{
  usize start{0};
  usize end{0};
  char  quote{'\0'};
  bool  is_block{false};
  bool  is_folded{false};
  usize block_indent{0};
};

static fn
read_yaml_scalar(StringView        source,
                 usize             line_start,
                 StringView        line,
                 usize             value_position,
                 usize             parent_indent,
                 usize            &next_position,
                 yaml_scalar_item &item) wontthrow -> bool
{
  let const value = line.substring(value_position);

  if (value.is_empty() || value[0] == '#' || value[0] == '*' || value[0] == '{')
  {
    return false;
  }

  if (let const is_folded = yaml_block_indicator(value)) {
    usize block_position = next_position;
    usize block_start    = next_position;
    usize block_end      = next_position;
    usize content_indent = 0;

    while (block_position < source.length) {
      let const candidate_start = block_position;
      let const candidate       = source.next_line(block_position);

      if (!candidate.trim_blanks().is_empty()) {
        let const indent = leading_spaces(candidate);

        if (indent <= parent_indent) {
          block_position = candidate_start;
          break;
        }

        if (content_indent == 0) content_indent = indent;
        block_end = candidate_start + candidate.length;
      }
    }

    next_position = block_end < block_position ? block_position : block_end;
    if (content_indent == 0 || block_end <= block_start) return false;

    item = yaml_scalar_item{block_start, block_end,  '\0',
                            true,        *is_folded, content_indent};

    return true;
  }

  let const quote = value[0];

  if (quote == '"' || quote == '\'') {
    let const open = line_start + value_position;

    for (usize position = open + 1; position < source.length; position++) {
      let const byte = source[position];

      if (quote == '"' && byte == '\\') {
        position++;
        continue;
      }

      if (byte != quote) continue;
      if (quote == '\'' && position + 1 < source.length &&
          source[position + 1] == '\'')
      {
        position++;
        continue;
      }

      if (position > line_start + line.length) {
        usize line_end = position;

        while (line_end < source.length && source[line_end] != '\n')
          line_end++;

        next_position = line_end < source.length ? line_end + 1 : line_end;
      }

      item = yaml_scalar_item{open + 1, position, quote};

      return position > open + 1;
    }

    return false;
  }

  let const end = plain_yaml_value_end(line, value_position);

  if (end <= value_position) return false;

  item = yaml_scalar_item{line_start + value_position, line_start + end};

  return true;
}

static fn
add_yaml_scalar_fragment(parsed_format_document &document,
                         StringView              source,
                         const yaml_scalar_item &item,
                         mimic_mood              mood,
                         u8                      template_syntax) throws -> void
{
  bool did_mask = false;
  let  analysis = parser_format_masked_template_source(
      source, item.start, item.end, template_syntax, did_mask);
  let const fragment_count = document.fragments.count();

  if (item.is_block) {
    if (item.is_folded) {
      analysis = replace_analysis_newlines(analysis.view(), item.start,
                                           item.end, item.block_indent, true);
    }

    analysis = align_indented_heredoc_terminators(analysis.view(), item.start,
                                                  item.end);
    parser_format_add_indented_fragment(document, source, item.start, item.end,
                                        item.block_indent, mood,
                                        steal(analysis));
  } else {
    if (item.quote != '\0') {
      analysis = decode_yaml_quoted_analysis(analysis.view(), item.start,
                                             item.end + 1, item.quote);
      analysis = replace_analysis_newlines(analysis.view(), item.start,
                                           item.end + 1, 0, false);
    }

    parser_format_add_fragment(document, source, item.start, item.end, 0,
                               steal(analysis), parser_format_codec::Direct,
                               mood);
  }

  if (did_mask && document.fragments.count() != fragment_count)
    document.fragments.back().should_silence_unresolved_commands = true;
}

static fn
collect_flow_sequence_items(StringView                   source,
                            usize                        open,
                            usize                        line_end,
                            ArrayList<yaml_scalar_item> &items) throws
    -> void
{
  usize position = open + 1;

  loop {
    while (position < line_end &&
           (source[position] == ' ' || source[position] == ','))
      position++;

    if (position >= line_end || source[position] == ']') return;

    let const byte = source[position];

    if (byte == '"' || byte == '\'') {
      usize close = position + 1;

      while (close < line_end && source[close] != byte) {
        if (byte == '"' && source[close] == '\\') close++;
        close++;
      }

      if (close >= line_end) return;
      items.push(yaml_scalar_item{position + 1, close, byte});
      position = close + 1;
      continue;
    }

    usize end = position;

    while (end < line_end && source[end] != ',' && source[end] != ']')
      end++;

    usize trimmed = end;

    while (trimmed > position && source[trimmed - 1] == ' ')
      trimmed--;

    items.push(yaml_scalar_item{position, trimmed});
    position = end;
  }
}

static fn
collect_yaml_value_items(StringView                   source,
                         usize                        line_start,
                         StringView                   line,
                         usize                        content_position,
                         usize                        key_column,
                         usize                       &next_position,
                         ArrayList<yaml_scalar_item> &items) throws
    -> void
{
  bool      is_skipped = false;
  let const value_position =
      skip_yaml_value_decorations(line, content_position, is_skipped);

  if (is_skipped) return;

  if (value_position < line.length && line[value_position] == '[') {
    collect_flow_sequence_items(source, line_start + value_position,
                                line_start + line.length, items);
    return;
  }

  yaml_scalar_item item{};

  if (read_yaml_scalar(source, line_start, line, value_position, key_column,
                       next_position, item))
  {
    items.push(item);
    return;
  }

  if (value_position < line.length && line[value_position] != '#') return;

  usize item_indent = static_cast<usize>(-1);

  while (next_position < source.length) {
    let const item_start = next_position;
    let const item_line  = source.next_line(next_position);

    if (item_line.trim_blanks().is_empty()) continue;

    let const indent = leading_spaces(item_line);
    let const is_dash =
        indent < item_line.length && item_line[indent] == '-' &&
        (indent + 1 == item_line.length || item_line[indent + 1] == ' ');

    if (indent < key_column || (indent == key_column && !is_dash)) {
      next_position = item_start;
      break;
    }

    if (item_line[indent] == '#') continue;
    if (!is_dash) continue;
    if (item_indent == static_cast<usize>(-1)) item_indent = indent;
    if (indent != item_indent) continue;

    bool      is_item_skipped = false;
    let const item_value =
        skip_yaml_value_decorations(item_line, indent + 1, is_item_skipped);

    if (is_item_skipped ||
        is_yaml_mapping_entry(item_line.substring(item_value)))
    {
      continue;
    }

    if (item_value < item_line.length && item_line[item_value] == '[') {
      collect_flow_sequence_items(source, item_start + item_value,
                                  item_start + item_line.length, items);
      continue;
    }

    yaml_scalar_item element{};

    if (read_yaml_scalar(source, item_start, item_line, item_value, indent,
                         next_position, element))
    {
      items.push(element);
    }
  }
}

fn
parser_format_extract_yaml_keys(parsed_format_document &document,
                                StringView              source,
                                const StringView       *keys,
                                usize                   key_count,
                                mimic_mood              default_mood,
                                yaml_shell_selection    selection,
                                u8                      template_syntax) throws -> void
{
  usize position = 0;

  while (position < source.length) {
    let const line_start       = position;
    let const line             = source.next_line(position);
    usize     content_position = 0;

    if (!yaml_key_match(line, keys, key_count, content_position)) continue;

    let const mood =
        selection == yaml_shell_selection::Workflow
            ? selected_workflow_mood(source, line_start, line, default_mood)
            : Maybe<mimic_mood>{mood_near(source, line_start, default_mood)};

    if (!mood.has_value()) continue;

    let   items         = ArrayList<yaml_scalar_item>{heap_allocator()};
    usize next_position = position;

    collect_yaml_value_items(source, line_start, line, content_position,
                             yaml_key_position(line), next_position, items);

    for (let const &item : items)
      add_yaml_scalar_fragment(document, source, item, *mood, template_syntax);

    if (next_position > position) position = next_position;
  }
}

static pure fn
shell_mood_for_program(StringView program) wontthrow -> Maybe<mimic_mood>
{
  if (let const slash = program.find_last_character('/'))
    program = program.substring(*slash + 1);

  if (program == "bash") return mimic_mood::Bash;
  if (program == "kosh") return mimic_mood::Default;
  if (program == "sh" || program == "dash" || program == "ash")
    return mimic_mood::Posix;

  return None;
}

static pure fn
is_shell_command_flag(StringView word) wontthrow -> bool
{
  if (word.length < 2 || word[0] != '-' || word[1] == '-') return false;

  for (usize position = 1; position < word.length; position++) {
    if (word[position] < 'a' || word[position] > 'z') return false;
  }

  return word.find_character('c').has_value();
}

static fn
collect_yaml_key_items(StringView                   source,
                       usize                        line_start,
                       StringView                   line,
                       ArrayList<yaml_scalar_item> &items) throws
    -> void
{
  let const key_column = yaml_key_position(line);
  let const colon      = line.substring(key_column).find_character(':');

  if (!colon.has_value()) return;

  usize content_position = key_column + *colon + 1;

  while (content_position < line.length && line[content_position] == ' ')
    content_position++;

  usize next_position = line_start + line.length + 1;

  collect_yaml_value_items(source, line_start, line, content_position,
                           key_column, next_position, items);
}

static fn
find_yaml_sibling_items(StringView                   source,
                        usize                        line_start,
                        StringView                   line,
                        StringView                   sibling,
                        ArrayList<yaml_scalar_item> &items) throws
    -> bool
{
  let const key_column = yaml_key_position(line);
  let const do_matches = [&](StringView candidate) {
    return leading_spaces(candidate) <= key_column &&
           yaml_key_position(candidate) == key_column &&
           yaml_line_value(candidate, sibling).has_value();
  };
  let const do_is_item_start = [&](StringView candidate) {
    let const indent = leading_spaces(candidate);

    return indent < key_column && indent < candidate.length &&
           candidate[indent] == '-';
  };

  usize scan = line_start;

  if (!do_is_item_start(line)) {
    while (scan > 0) {
      let const candidate       = previous_line(source, scan);
      let const candidate_start = scan;

      if (candidate.trim_blanks().is_empty()) continue;
      if (leading_spaces(candidate) < key_column &&
          !do_is_item_start(candidate))
        break;
      if (do_matches(candidate)) {
        collect_yaml_key_items(source, candidate_start, candidate, items);

        return true;
      }

      if (do_is_item_start(candidate)) break;
    }
  }

  usize forward = line_start;

  unused(source.next_line(forward));

  while (forward < source.length) {
    let const candidate_start = forward;
    let const candidate       = source.next_line(forward);

    if (candidate.trim_blanks().is_empty()) continue;
    if (leading_spaces(candidate) < key_column) break;
    if (do_matches(candidate)) {
      collect_yaml_key_items(source, candidate_start, candidate, items);

      return true;
    }
  }

  return false;
}

static fn
yaml_item_text(StringView source, const yaml_scalar_item &item) wontthrow
    -> StringView
{
  return source.substring_of_length(item.start, item.end - item.start);
}

fn
parser_format_extract_yaml_exec_keys(parsed_format_document &document,
                                     StringView              source,
                                     const StringView       *keys,
                                     usize                   key_count,
                                     u8 template_syntax) throws -> void
{
  usize position = 0;

  while (position < source.length) {
    let const line_start       = position;
    let const line             = source.next_line(position);
    usize     content_position = 0;

    if (!yaml_key_match(line, keys, key_count, content_position)) continue;

    let const key = line.substring_of_length(
        yaml_key_position(line), content_position - yaml_key_position(line));
    let   items         = ArrayList<yaml_scalar_item>{heap_allocator()};
    usize next_position = position;

    collect_yaml_value_items(source, line_start, line, content_position,
                             yaml_key_position(line), next_position, items);
    if (next_position > position) position = next_position;
    if (items.is_empty()) continue;

    let const value = line.substring(content_position).trim_blanks();
    let const is_sequence =
        value.is_empty() || value.starts_with("[") || value.starts_with("#");

    if (!is_sequence) {
      add_yaml_scalar_fragment(document, source, items[0], mimic_mood::Posix,
                               template_syntax);
      continue;
    }

    let prefix = ArrayList<yaml_scalar_item>{heap_allocator()};

    if (key.starts_with("args")) {
      if (!find_yaml_sibling_items(source, line_start, line, "command", prefix))
      {
        unused(find_yaml_sibling_items(source, line_start, line, "entrypoint",
                                       prefix));
      }
    } else if (key.starts_with("command")) {
      unused(find_yaml_sibling_items(source, line_start, line, "entrypoint",
                                     prefix));
    }

    let argv = ArrayList<yaml_scalar_item>{heap_allocator()};

    for (let const &item : prefix)
      argv.push(item);
    for (let const &item : items)
      argv.push(item);

    usize first = 0;

    if (!argv.is_empty() && yaml_item_text(source, argv[0]) == "CMD-SHELL") {
      if (argv.count() > 1)
        add_yaml_scalar_fragment(document, source, argv[1], mimic_mood::Posix,
                                 template_syntax);
      continue;
    }

    if (!argv.is_empty() && yaml_item_text(source, argv[0]) == "CMD") first = 1;
    if (argv.count() < first + 3) continue;

    let const mood =
        shell_mood_for_program(yaml_item_text(source, argv[first]));

    if (!mood.has_value()) continue;

    usize script_index = 0;

    for (usize index = first + 1; index + 1 < argv.count(); index++) {
      let const word = yaml_item_text(source, argv[index]);

      if (word.is_empty() || word[0] != '-') break;
      if (is_shell_command_flag(word)) {
        script_index = index + 1;
        break;
      }
    }

    if (script_index == 0 || script_index < prefix.count()) continue;
    add_yaml_scalar_fragment(document, source, argv[script_index], *mood,
                             template_syntax);
  }
}

fn
parser_format_add_json_fragment(parsed_format_document &document,
                                StringView              source,
                                usize                   start,
                                usize                   end,
                                mimic_mood              mood) throws -> void
{
  if (start >= end) return;
  parser_format_add_fragment(document, source, start, end, 0, None,
                             parser_format_codec::JsonString, mood);
  if (document.fragments.is_empty()) return;

  let  &fragment = document.fragments.back();
  let   decoded  = String{heap_allocator()};
  usize position = start;

  while (position < end) {
    let byte = source[position++];

    if (byte != '\\' || position >= end) {
      decoded.push(byte);
      continue;
    }

    let const escaped = source[position++];

    switch (escaped) {
    case 'n':  decoded.push('\n'); break;
    case 'r':  decoded.push('\r'); break;
    case 't':  decoded.push('\t'); break;
    case '"':  decoded.push('"'); break;
    case '\\': decoded.push('\\'); break;
    default:
      decoded.push('\\');
      decoded.push(escaped);
      break;
    }
  }

  fragment.shell_source = steal(decoded);

  let masked = String{heap_allocator()};

  masked.reserve(end);

  let const literal_start = start < end ? start : end;

  for (usize host_position = 0; host_position < literal_start; host_position++)
  {
    let const byte = source[host_position];

    masked.push(byte == '\n' ? '\n' : ' ');
  }

  for (usize host_position = literal_start; host_position < end;
       host_position++)
  {
    let byte = source[host_position];

    if (byte == '\\' && host_position + 1 < end) {
      byte = ' ';
    }

    masked.push(byte);
  }

  fragment.analysis_source = steal(masked);
}

fn
parser_format_extract_json_keys(parsed_format_document       &document,
                                StringView                    source,
                                const parser_format_json_key *keys,
                                usize                         key_count,
                                mimic_mood default_mood) throws -> void
{
  usize position = 0;

  while (position < source.length) {
    if (source[position] != '"') {
      position++;
      continue;
    }

    let const key_start = ++position;

    while (position < source.length && source[position] != '"') {
      if (source[position] == '\\' && position + 1 < source.length) {
        position++;
      }
      position++;
    }

    if (position >= source.length) break;

    let const key = source.substring_of_length(key_start, position - key_start);

    position++;
    if (key.is_empty()) continue;

    while (position < source.length &&
           (source[position] == ' ' || source[position] == '\t' ||
            source[position] == '\n' || source[position] == '\r'))
    {
      position++;
    }

    if (position >= source.length || source[position] != ':') {
      continue;
    }

    position++;

    const parser_format_json_key *matched_key = nullptr;

    for (usize key_index = 0; key_index < key_count; key_index++) {
      if (keys[key_index].name.is_empty()) continue;
      if (key[0] != keys[key_index].name[0]) continue;
      if (key != keys[key_index].name) continue;
      matched_key = &keys[key_index];
      break;
    }

    if (matched_key == nullptr) continue;

    while (position < source.length &&
           (source[position] == ' ' || source[position] == '\t' ||
            source[position] == '\n' || source[position] == '\r'))
    {
      position++;
    }

    if (position >= source.length || source[position] != '"') {
      continue;
    }

    let const value_start = ++position;

    while (position < source.length && source[position] != '"') {
      if (source[position] == '\\' && position + 1 < source.length) {
        position++;
      }
      position++;
    }

    let const fragment_count = document.fragments.count();

    parser_format_add_json_fragment(document, source, value_start, position,
                                    default_mood);
    if (document.fragments.count() != fragment_count)
      document.fragments.back().should_silence_unresolved_commands =
          matched_key->should_silence_unresolved_commands;
    if (position < source.length) position++;
  }
}

pure fn
parser_format_fragment_at(const parsed_format_document &document,
                          usize host_position) wontthrow -> Maybe<usize>
{
  usize lower = 0;
  usize upper = document.fragments.count();

  while (lower < upper) {
    let const  middle   = lower + (upper - lower) / 2;
    let const &fragment = document.fragments[middle];

    if (host_position < fragment.host_start) {
      upper = middle;
    } else if (host_position > fragment.host_end ||
               (host_position == fragment.host_end &&
                !fragment.should_select_end))
    {
      lower = middle + 1;
    } else {
      return middle;
    }
  }

  return None;
}

constexpr PackedStringKey LIST_OPENER_KEYS[] = {
    SSK(";"),  SSK("&"),  SSK("|"),    SSK("&&"), SSK("||"),
    SSK("("),  SSK("{"),  SSK("then"), SSK("do"), SSK("else"),
    SSK(";;"), SSK(";&"), SSK(";;&"),
};
constexpr StaticStringSet LIST_OPENERS{LIST_OPENER_KEYS};

constexpr PackedStringKey ARM_TERMINATOR_KEYS[] = {
    SSK(";;"),
    SSK(";&"),
    SSK(";;&"),
};
constexpr StaticStringSet ARM_TERMINATORS{ARM_TERMINATOR_KEYS};

pure static fn
line_first_token(StringView line) wontthrow -> StringView
{
  usize start = 0;

  while (start < line.length && (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }

  usize end = start;

  while (end < line.length && line[end] != ' ' && line[end] != '\t') {
    end++;
  }

  return line.substring_of_length(start, end - start);
}

pure static fn
line_last_token(StringView line) wontthrow -> StringView
{
  usize end = line.length;

  while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
    end--;
  }

  usize start = end;

  while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t') {
    start--;
  }

  return line.substring_of_length(start, end - start);
}

static fn
trimmed_of_trailing_newlines(StringView text) wontthrow -> StringView
{
  usize length = text.length;

  while (length > 0 && text[length - 1] == '\n') {
    length--;
  }

  return text.substring_of_length(0, length);
}

static fn
encode_continued(const parser_format_fragment &fragment,
                 StringView                    replacement,
                 String                       &encoded) throws -> void
{
  let const body     = trimmed_of_trailing_newlines(replacement);
  let       lines    = ArrayList<StringView>{heap_allocator()};
  usize     position = 0;

  while (position < body.length) {
    let const line = body.next_line(position);

    if (!line_first_token(line).is_empty()) lines.push(line);
  }

  bool is_expecting_case_pattern = false;

  for (usize index = 0; index < lines.count(); index++) {
    let       line       = lines[index];
    let const first      = line_first_token(line);
    let       last       = line_last_token(line);
    let const is_wrapped = last == "\\";

    if (is_wrapped) {
      usize length = line.length;

      while (length > 0 && line[length - 1] == '\\') {
        length--;
      }

      while (length > 0 &&
             (line[length - 1] == ' ' || line[length - 1] == '\t'))
      {
        length--;
      }

      line = line.substring_of_length(0, length);
      last = line_last_token(line);
    }

    let const is_case_pattern = is_expecting_case_pattern && first != "esac";
    let const is_case_header  = first == "case" && last == "in";

    if (!is_wrapped) {
      is_expecting_case_pattern =
          is_case_header || ARM_TERMINATORS.contains(last);
    }

    if (index > 0) {
      encoded.append(" \\\n");
      encoded.append_repeated(fragment.continuation_byte,
                              fragment.indent_length);
    }

    encoded.append(line);
    if (index + 1 >= lines.count()) break;

    let const next_first = line_first_token(lines[index + 1]);

    if (is_wrapped || is_case_pattern || is_case_header ||
        LIST_OPENERS.contains(last) || ARM_TERMINATORS.contains(next_first) ||
        next_first == ")")
    {
      continue;
    }

    encoded.push(';');
  }
}

fn
parser_format_encode(const parser_format_fragment &fragment,
                     StringView                    replacement) throws -> Maybe<String>
{
  let encoded = String{heap_allocator()};

  switch (fragment.codec) {
  case parser_format_codec::Direct:
    encoded.append(trimmed_of_trailing_newlines(replacement));

    return encoded;
  case parser_format_codec::Continued:
    encode_continued(fragment, replacement, encoded);

    return encoded;
  case parser_format_codec::JsonString:
    for (usize position = 0; position < replacement.length; position++) {
      switch (replacement[position]) {
      case '\\': encoded.append("\\\\"); break;
      case '"':  encoded.append("\\\""); break;
      case '\n': encoded.append("\\n"); break;
      case '\r': encoded.append("\\r"); break;
      case '\t': encoded.append("\\t"); break;
      default:   encoded.push(replacement[position]); break;
      }
    }

    return encoded;
  case parser_format_codec::Indented: break;
  }

  usize position = 0;

  while (position < replacement.length) {
    encoded.append_repeated(' ', fragment.indent_length);

    let const line_start = position;
    let const line       = replacement.next_line(position);

    encoded.append(line);
    if (position > line_start + line.length) encoded.push('\n');
  }

  return encoded;
}

fn
parser_format_apply_replacements(
    StringView                           source,
    ArrayList<parser_format_replacement> replacements) throws
    -> Maybe<String>
{
  let const sorted_replacements =
      steal(replacements)
          .make_sorted([](const parser_format_replacement &left,
                          const parser_format_replacement &right) {
            return left.start_position < right.start_position;
          });

  let   result   = String{heap_allocator()};
  usize position = 0;

  for (let const &replacement : sorted_replacements) {
    if (replacement.start_position < position ||
        replacement.end_position < replacement.start_position ||
        replacement.end_position > source.length)
    {
      return None;
    }

    result.append(source.substring_of_length(
        position, replacement.start_position - position));
    result.append(replacement.replacement.view());
    position = replacement.end_position;
  }

  result.append(source.substring(position));

  return result;
}

fn
parser_format_analysis_source(const parsed_format_document &document,
                              StringView host_source) throws -> String
{
  if (!document.is_host_format) return String{heap_allocator(), host_source};

  let result = String{heap_allocator()};

  result.reserve(host_source.length);

  usize fragment_index = 0;

  for (usize position = 0; position < host_source.length; position++) {
    while (fragment_index < document.fragments.count() &&
           position >= document.fragments[fragment_index].host_end)
    {
      fragment_index++;
    }

    if (fragment_index < document.fragments.count() &&
        document.fragments[fragment_index].contains(position))
    {
      result.push(document.fragments[fragment_index].analysis_source[position]);
    } else {
      result.push(host_source[position] == '\n' ? '\n' : ' ');
    }
  }

  return result;
}

pure fn
parser_format_should_silence_unresolved_commands(
    parser_format_kind kind) wontthrow -> bool
{
  switch (kind) {
  case parser_format_kind::Shell:
  case parser_format_kind::UnknownHost:
  case parser_format_kind::Markdown:
  case parser_format_kind::Makefile:
  case parser_format_kind::Taskfile:
  case parser_format_kind::Justfile:
  case parser_format_kind::VscodeTasks:        return false;
  case parser_format_kind::Jinja:              return true;
  case parser_format_kind::GithubActions:
  case parser_format_kind::GiteaActions:
  case parser_format_kind::ForgejoActions:
  case parser_format_kind::GitlabCi:
  case parser_format_kind::Ansible:
  case parser_format_kind::Dockerfile:
  case parser_format_kind::Compose:
  case parser_format_kind::CloudBuild:
  case parser_format_kind::CircleCi:
  case parser_format_kind::AzurePipelines:
  case parser_format_kind::BitbucketPipelines:
  case parser_format_kind::Buildkite:
  case parser_format_kind::TravisCi:
  case parser_format_kind::Kubernetes:
  case parser_format_kind::PackageJson:
  case parser_format_kind::Drone:
  case parser_format_kind::Woodpecker:
  case parser_format_kind::RpmSpec:
  case parser_format_kind::DevContainer:       return true;
  }

  return false;
}

fn
parse_format_document(const parser_format_input &input) throws
    -> parsed_format_document
{
  let document = parsed_format_document{};

  document.kind           = detect_format_kind(input);
  document.is_host_format = document.kind != parser_format_kind::Shell;

  switch (document.kind) {
  case parser_format_kind::Shell:
  case parser_format_kind::UnknownHost: break;
  case parser_format_kind::GithubActions:
    parse_github_actions_format(input, document);
    break;
  case parser_format_kind::GiteaActions:
    parse_gitea_actions_format(input, document);
    break;
  case parser_format_kind::ForgejoActions:
    parse_forgejo_actions_format(input, document);
    break;
  case parser_format_kind::GitlabCi:
    parse_gitlab_ci_format(input, document);
    break;
  case parser_format_kind::Ansible:
    parse_ansible_format(input, document);
    break;
  case parser_format_kind::Dockerfile:
    parse_dockerfile_format(input, document);
    break;
  case parser_format_kind::Compose:
    parse_compose_format(input, document);
    break;
  case parser_format_kind::Markdown:
    parse_markdown_format(input, document);
    break;
  case parser_format_kind::Makefile:
    parse_makefile_format(input, document);
    break;
  case parser_format_kind::CloudBuild:
    parse_cloud_build_format(input, document);
    break;
  case parser_format_kind::CircleCi:
    parse_circle_ci_format(input, document);
    break;
  case parser_format_kind::AzurePipelines:
    parse_azure_pipelines_format(input, document);
    break;
  case parser_format_kind::BitbucketPipelines:
    parse_bitbucket_pipelines_format(input, document);
    break;
  case parser_format_kind::Buildkite:
    parse_buildkite_format(input, document);
    break;
  case parser_format_kind::TravisCi:
    parse_travis_ci_format(input, document);
    break;
  case parser_format_kind::Kubernetes:
    parse_kubernetes_format(input, document);
    break;
  case parser_format_kind::PackageJson:
    parse_package_json_format(input, document);
    break;
  case parser_format_kind::Drone: parse_drone_format(input, document); break;
  case parser_format_kind::Woodpecker:
    parse_woodpecker_format(input, document);
    break;
  case parser_format_kind::Taskfile:
    parse_taskfile_format(input, document);
    break;
  case parser_format_kind::Justfile:
    parse_justfile_format(input, document);
    break;
  case parser_format_kind::Jinja: parse_jinja_format(input, document); break;
  case parser_format_kind::RpmSpec:
    parse_rpm_spec_format(input, document);
    break;
  case parser_format_kind::VscodeTasks:
    parse_vscode_tasks_format(input, document);
    break;
  case parser_format_kind::DevContainer:
    parse_dev_container_format(input, document);
    break;
  }

  return document;
}

} /* namespace koshka */
