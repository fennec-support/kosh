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
      ".json", ".jsonc", ".markdown", ".md", ".mdc",  ".mdx", ".mk",
      ".make", ".nix",   ".spec",     ".tf", ".yaml", ".yml",
  };

  for (let const extension : EXTENSIONS)
    if (path_ends_with(path, extension)) return true;

  return false;
}

constexpr static_string_entry<parser_format_kind> FORMAT_FILENAME_ENTRIES[] = {
    {SSK("action.yml"),               parser_format_kind::GithubActions     },
    {SSK("action.yaml"),              parser_format_kind::GithubActions     },
    {SSK(".gitlab-ci.yml"),           parser_format_kind::GitlabCi          },
    {SSK(".gitlab-ci.yaml"),          parser_format_kind::GitlabCi          },
    {SSK(".drone.yml"),               parser_format_kind::Drone             },
    {SSK(".drone.yaml"),              parser_format_kind::Drone             },
    {SSK("buildkite.yml"),            parser_format_kind::Buildkite         },
    {SSK("buildkite.yaml"),           parser_format_kind::Buildkite         },
    {SSK(".buildkite.yml"),           parser_format_kind::Buildkite         },
    {SSK(".buildkite.yaml"),          parser_format_kind::Buildkite         },
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
    {SSK("bsdmakefile"),              parser_format_kind::Makefile          },
    {SSK("makefile.inc"),             parser_format_kind::Makefile          },
    {SSK("makefile.am"),              parser_format_kind::Makefile          },
    {SSK("justfile"),                 parser_format_kind::Justfile          },
    {SSK(".justfile"),                parser_format_kind::Justfile          },
    {SSK("package.json"),             parser_format_kind::PackageJson       },
    {SSK("devcontainer.json"),        parser_format_kind::DevContainer      },
    {SSK("devcontainer.jsonc"),       parser_format_kind::DevContainer      },
    {SSK(".devcontainer.json"),       parser_format_kind::DevContainer      },
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
has_yaml_extension(StringView path) wontthrow -> bool
{
  return path_ends_with(path, ".yml") || path_ends_with(path, ".yaml");
}

static pure fn
filename_pattern_kind(StringView path, StringView filename) wontthrow
    -> Maybe<parser_format_kind>
{
  if (has_yaml_extension(filename)) {
    if (path_has_directory(path, ".gitlab/ci/") ||
        path_has_directory(path, ".gitlab-ci/"))
    {
      return parser_format_kind::GitlabCi;
    }

    if (path_starts_with(filename, "compose.") ||
        path_starts_with(filename, "docker-compose.") ||
        path_ends_with(filename, ".compose.yml") ||
        path_ends_with(filename, ".compose.yaml"))
    {
      return parser_format_kind::Compose;
    }

    if (path_starts_with(filename, "cloudbuild-") ||
        path_starts_with(filename, "cloudbuild.") ||
        path_ends_with(filename, ".cloudbuild.yml") ||
        path_ends_with(filename, ".cloudbuild.yaml"))
    {
      return parser_format_kind::CloudBuild;
    }

    if (path_starts_with(filename, "azure-pipelines-") ||
        path_starts_with(filename, "azure-pipelines.") ||
        path_has_directory(path, ".azure-pipelines/") ||
        path_has_directory(path, ".azuredevops/"))
    {
      return parser_format_kind::AzurePipelines;
    }
  }

  if (path_ends_with(filename, ".devcontainer.json"))
    return parser_format_kind::DevContainer;
  if (path_ends_with(filename, ".make")) return parser_format_kind::Makefile;
  if (path_starts_with(filename, "dockerfile-") ||
      path_starts_with(filename, "dockerfile_") ||
      path_starts_with(filename, "containerfile-") ||
      path_starts_with(filename, "containerfile_"))
  {
    return parser_format_kind::Dockerfile;
  }

  return None;
}

static pure fn
has_top_level_key(StringView source, StringView key) wontthrow -> bool
{
  usize position = 0;

  while (position < source.length) {
    let const line = source.next_line(position);

    if (line.starts_with(key)) return true;
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
  let const path     = input.path.has_value() ? *input.path : StringView{};
  let const filename = parser_format_filename(path);
  let named_kind = parser_format_find_ascii_folded(FORMAT_FILENAMES, filename);

  if (!named_kind.has_value())
    named_kind = filename_pattern_kind(path, filename);

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

  if (path_has_directory(path, ".circleci/") && has_yaml_extension(path)) {
    return parser_format_kind::CircleCi;
  }

  if (named_kind == parser_format_kind::AzurePipelines)
    return parser_format_kind::AzurePipelines;
  if (named_kind == parser_format_kind::BitbucketPipelines)
    return parser_format_kind::BitbucketPipelines;
  if ((path_has_directory(path, ".buildkite/") && has_yaml_extension(path)) ||
      named_kind == parser_format_kind::Buildkite)
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
       path_ends_with(filename, ".devcontainer.json")))
  {
    return parser_format_kind::DevContainer;
  }

  if (path_ends_with(path, ".spec") || path_ends_with(path, ".spec.in")) {
    return parser_format_kind::RpmSpec;
  }
  if (path_ends_with(path, ".md") || path_ends_with(path, ".markdown") ||
      path_ends_with(path, ".mdx") || path_ends_with(path, ".mdc") ||
      named_kind == parser_format_kind::Markdown ||
      parser_format_ascii_equal(language_id, "markdown"))
  {
    return parser_format_kind::Markdown;
  }

  if (parser_format_ascii_equal(language_id, "dockercompose"))
    return parser_format_kind::Compose;

  if (has_yaml_extension(path) ||
      parser_format_ascii_equal(language_id, "yaml"))
  {
    let const is_ansible_task_file =
        (path_has_directory(path, "tasks/") ||
         path_has_directory(path, "handlers/")) &&
        (parser_format_has_substring(source, "shell:") ||
         parser_format_has_substring(source, "command:") ||
         parser_format_has_substring(source, "- name:"));

    if ((parser_format_has_substring(source, "hosts:") &&
         parser_format_has_substring(source, "tasks:")) ||
        is_ansible_task_file)
    {
      return parser_format_kind::Ansible;
    }

    if (has_top_level_key(source, "apiVersion:") &&
        has_top_level_key(source, "kind:"))
    {
      return parser_format_kind::Kubernetes;
    }

    if (parser_format_has_substring(source, "runs-on:") &&
        parser_format_has_substring(source, "steps:"))
    {
      return parser_format_kind::GithubActions;
    }

    if (has_top_level_key(source, "tasks:") &&
        has_top_level_key(source, "version:"))
    {
      return parser_format_kind::Taskfile;
    }

    if (has_top_level_key(source, "services:") ||
        (parser_format_has_substring(source, "services:") &&
         (parser_format_has_substring(source, "image:") ||
          parser_format_has_substring(source, "build:"))))
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

  /* at this point: path has no known host extension, filename is not
     jenkinsfile, and language_id is none of dockerfile, makefile, json,
     jsonc, markdown, dockercompose, or yaml. */
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

/* memory: shell_source copies host_end - host_start bytes. analysis_source is
           prepared_analysis_source when it has a value, and otherwise a
           host_end byte copy from make_analysis_source(). both are freed with
           document.fragments.
   time:   O(host_end) to build analysis_source, O(host_end - host_start) for
           shell_source. no loops over the document. */
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

/* memory: none.
   time:   O(end - position) per call, since do_find() scans to end. the
           caller parser_format_masked_template_source() makes one call for
           each position it visits. */
static pure fn
template_construct_end(StringView source,
                       usize      position,
                       usize      end,
                       u8         template_syntax,
                       bool      &is_value) wontthrow -> Maybe<usize>
{
  let const remaining = end - position;
  let const do_find = [&](usize from, char first, char second) -> Maybe<usize> {
    char quote = '\0';

    for (usize at = from; at + 1 < end; at++) {
      let const byte = source[at];

      if (quote != '\0') {
        if (byte == '\\' && quote == '"') {
          at++;
        } else if (byte == quote)
          quote = '\0';
        continue;
      }

      if (byte == '"' || byte == '\'') {
        quote = byte;
        continue;
      }

      if (byte == first && source[at + 1] == second) {
        return at + 2;
      }
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

  if ((template_syntax & TEMPLATE_IDE_VARIABLES) != 0 && remaining >= 4 &&
      source[position] == '$' && source[position + 1] == '{')
  {
    static constexpr StringView NAMES[] = {
        "workspaceFolder",
        "workspaceFolderBasename",
        "workspaceRoot",
        "userHome",
        "file",
        "fileWorkspaceFolder",
        "relativeFile",
        "relativeFileDirname",
        "fileBasename",
        "fileBasenameNoExtension",
        "fileExtname",
        "fileDirname",
        "fileDirnameBasename",
        "cwd",
        "lineNumber",
        "selectedText",
        "execPath",
        "pathSeparator",
        "defaultBuildTask",
        "localWorkspaceFolder",
        "localWorkspaceFolderBasename",
        "containerWorkspaceFolder",
        "containerWorkspaceFolderBasename",
        "devcontainerId",
    };
    static constexpr StringView PREFIXES[] = {
        "env",
        "config",
        "input",
        "command",
        "localEnv",
        "containerEnv",
        "workspaceFolder",
        "workspaceFolderBasename",
    };

    usize name_end = position + 2;

    while (name_end < end &&
           ((source[name_end] >= 'a' && source[name_end] <= 'z') ||
            (source[name_end] >= 'A' && source[name_end] <= 'Z')))
      name_end++;

    let const name =
        source.substring_of_length(position + 2, name_end - position - 2);

    if (name_end < end && source[name_end] == '}') {
      for (let const known : NAMES) {
        if (known == name) return name_end + 1;
      }
    }

    if (name_end < end && source[name_end] == ':') {
      for (let const prefix : PREFIXES) {
        if (prefix != name) continue;

        for (usize close = name_end; close < end; close++) {
          if (source[close] == '}') return close + 1;
          if (source[close] == '\n') break;
        }
      }
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
          if (!has_macro_marker || at == position + 2) {
            return None;
          }
          return at + 1;
        }

        let const is_word =
            (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '_' || byte == '.';

        if (!is_word) return None;
        if (byte == '.' || (byte >= 'A' && byte <= 'Z')) {
          has_macro_marker = true;
        }
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
      if (source[at] == '>' && source[at + 1] == '>') {
        return at + 2;
      }
    }
  }

  return None;
}

fn
parser_format_masked_template_source(
    StringView                    host_source,
    usize                         host_start,
    usize                         host_end,
    u8                            template_syntax,
    bool                         &did_mask,
    parser_format_template_masks *masks) throws -> String
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

    if ((template_syntax & TEMPLATE_DOUBLE_DOLLAR) != 0 && byte == '$' &&
        position + 1 < host_end && host_source[position + 1] == '$')
    {
      analysis_source.append(" $");
      position += 2;
      if (masks != nullptr) masks->is_restorable = false;
      continue;
    }

    if (has_curly_escape && position + 3 < host_end &&
        host_source.substring_of_length(position, 4) == "{{{{")
    {
      analysis_source.append("xxxx");
      position += 4;
      if (masks != nullptr) masks->is_restorable = false;
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
    let const placeholder_start = analysis_source.length();

    if (is_value && !has_newline && length >= 4) {
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

    if (masks != nullptr) {
      let const placeholder =
          analysis_source.view().substring(placeholder_start);

      if (placeholder.is_empty() || placeholder[0] != '$') {
        masks->is_restorable = false;
      } else {
        masks->spans.push(parser_format_template_span{
            String{heap_allocator(), construct  },
            String{heap_allocator(), placeholder}
        });
      }
    }

    did_mask = true;
    position = *construct_end;
  }

  return analysis_source;
}

static fn
replace_in_order(StringView                                    text,
                 const ArrayList<parser_format_template_span> &spans,
                 bool is_restoring) throws -> Maybe<String>
{
  let   result   = String{heap_allocator()};
  usize position = 0;

  for (let const &span : spans) {
    let const wanted =
        is_restoring ? span.placeholder.view() : span.host_text.view();
    let const replacement =
        is_restoring ? span.host_text.view() : span.placeholder.view();
    let const found = text.substring(position).find_substring(wanted);

    if (!found.has_value()) return None;
    result.append(text.substring_of_length(position, *found));
    result.append(replacement);
    position += *found + wanted.length;
  }

  result.append(text.substring(position));

  return result;
}

fn
parser_format_formattable_source(
    const parser_format_fragment &fragment) throws -> Maybe<String>
{
  return replace_in_order(fragment.shell_source.view(), fragment.template_spans,
                          false);
}

fn
parser_format_host_replacement(const parser_format_fragment &fragment,
                               StringView                    host_source,
                               StringView                    formatted) throws
    -> Maybe<String>
{
  let restored = replace_in_order(formatted, fragment.template_spans, true);

  if (!restored.has_value()) return None;

  let const host = host_source.substring_of_length(
      fragment.host_start, fragment.host_end - fragment.host_start);
  let text = restored->view();

  if (host.is_empty() || host[host.length - 1] != '\n') {
    while (!text.is_empty() && text[text.length - 1] == '\n')
      text = text.substring_of_length(0, text.length - 1);
  }

  if (fragment.codec != parser_format_codec::Indented &&
      !host.find_character('\n').has_value() &&
      text.find_character('\n').has_value())
  {
    return None;
  }

  /* at this point: text views restored, and text has a newline only when
     the host has one or the codec is indented. */
  return String{heap_allocator(), text};
}

/* memory: deindented holds each line without up to indent_length leading
           spaces. it is moved into fragment.shell_source, which replaces the
           copy made by parser_format_add_fragment().
   time:   O(host_end - host_start). each line is appended once. */
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
  let const fragment_count = document.fragments.count();

  parser_format_add_fragment(document, host_source, host_start, host_end,
                             indent_length, steal(prepared_analysis_source),
                             parser_format_codec::Indented, mood);
  if (document.fragments.count() == fragment_count) return;

  /* at this point: fragment is the last entry in document.fragments, and
     parser_format_add_fragment() pushed it for host_start to host_end. */
  let &fragment = document.fragments.back();

  fragment.should_skip_format =
      fragment.analysis_source.view().substring_of_length(
          host_start, host_end - host_start) !=
      host_source.substring_of_length(host_start, host_end - host_start);

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
    /* at this point: key matches line at position, and the byte after it
       is ':'. */
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

pure fn
parser_format_leading_spaces(StringView line) wontthrow -> usize
{
  usize count = 0;

  while (count < line.length && line[count] == ' ') {
    count++;
  }

  return count;
}

static pure fn
yaml_key_position(StringView line) wontthrow -> usize
{
  usize position = parser_format_leading_spaces(line);

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

  if (source[end - 1] == '\n') {
    end--;
  }

  let start = end;

  while (start > 0 && source[start - 1] != '\n') {
    start--;
  }

  position = start;

  return source.substring_of_length(start, end - start);
}

pure fn
parser_format_shell_mood(StringView program) wontthrow -> Maybe<mimic_mood>
{
  if (let const slash = program.find_last_character('/'))
    program = program.substring(*slash + 1);
  if (path_ends_with(program, ".exe"))
    program = program.substring_of_length(0, program.length - 4);

  if (program == "bash") return mimic_mood::Bash;
  if (program == "kosh") return mimic_mood::Default;
  if (program == "sh" || program == "dash" || program == "ash") {
    return mimic_mood::Posix;
  }

  return None;
}

pure fn
parser_format_shebang_program(StringView line) wontthrow -> StringView
{
  if (!line.starts_with("#!")) return {};

  usize     position     = 2;
  let const do_next_word = [&]() {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t'))
      position++;

    let const start = position;

    while (position < line.length && line[position] != ' ' &&
           line[position] != '\t' && line[position] != '\r')
      position++;

    return line.substring_of_length(start, position - start);
  };

  /* e.g. "#!/usr/bin/env -S bash -e": the first word is "/usr/bin/env", the
     word "-S" is skipped, and "bash" is returned. */
  let       program = do_next_word();
  let const slash   = program.find_last_character('/');
  let const basename =
      slash.has_value() ? program.substring(*slash + 1) : program;

  if (basename == "env") {
    program = do_next_word();
    if (program == "-S") program = do_next_word();
  }

  return program;
}

struct interpreter_choice
{
  bool              is_found{false};
  Maybe<mimic_mood> mood{};
};

static pure fn
interpreter_of_value(StringView value) wontthrow -> interpreter_choice
{
  while (!value.is_empty() &&
         (value[0] == '\'' || value[0] == '"' || value[0] == ' '))
    value = value.substring(1);

  usize word_end = 0;

  while (word_end < value.length && value[word_end] != ' ' &&
         value[word_end] != '\'' && value[word_end] != '"')
    word_end++;

  let word = value.substring_of_length(0, word_end);

  if (let const slash = word.find_last_character('/'))
    word = word.substring(*slash + 1);
  if (let const at = word.find_character('@'))
    word = word.substring_of_length(0, *at);
  if (path_ends_with(word, ".exe"))
    word = word.substring_of_length(0, word.length - 4);

  /* at this point: word holds no '/' or '@' character. */
  if (let const mood = parser_format_shell_mood(word)) return {true, *mood};

  static constexpr StringView OTHERS[] = {
      "pwsh",   "powershell", "azurepowershell",
      "python", "python3",    "cmd",
      "node",   "ruby",       "perl",
      "zsh",    "fish",       "ps",
      "pscore", "batch",
  };

  for (let const other : OTHERS) {
    if (parser_format_ascii_equal(word, other)) return {true, None};
  }

  return {};
}

static pure fn
mood_near(StringView source,
          usize      line_start,
          StringView line,
          mimic_mood fallback) wontthrow -> Maybe<mimic_mood>
{
  usize key_column = 0;

  while (key_column < line.length &&
         (line[key_column] == ' ' || line[key_column] == '-'))
    key_column++;

  usize scope_start  = line_start;
  usize scope_indent = key_column;
  usize scan         = line_start;
  bool  is_item =
      line.substring_of_length(0, key_column).find_character('-').has_value();

  if (is_item) scope_indent = parser_format_leading_spaces(line);

  while (!is_item && scan > 0) {
    let const previous = previous_line(source, scan);

    if (previous.trim_blanks().is_empty()) continue;

    let const indent = parser_format_leading_spaces(previous);

    if (indent >= scope_indent) continue;
    scope_indent = indent;
    if (indent < previous.length && previous[indent] == '-') {
      scope_start = scan;
      is_item     = true;
      break;
    }

    if (indent == 0) break;
  }

  if (!is_item) {
    scope_start  = line_start;
    scope_indent = key_column;
    scan         = line_start;

    while (scan > 0) {
      let const previous = previous_line(source, scan);

      if (previous.trim_blanks().is_empty()) continue;
      if (parser_format_leading_spaces(previous) != key_column) break;
      scope_start = scan;
    }
  }

  usize position = scope_start;
  bool  is_first = true;

  while (position < source.length) {
    let const candidate_start = position;
    let const candidate       = source.next_line(position);

    if (candidate.trim_blanks().is_empty()) continue;
    if (!is_first &&
        (is_item ? parser_format_leading_spaces(candidate) <= scope_indent
                 : parser_format_leading_spaces(candidate) < key_column))
    {
      break;
    }

    is_first = false;
    if (candidate_start == line_start) continue;

    static constexpr StringView KEYS[] = {"executable", "shell", "interpreter",
                                          "scriptType", "task"};

    for (let const key : KEYS) {
      let const value = yaml_line_value(candidate, key);

      if (!value.has_value()) continue;

      let const choice = interpreter_of_value(*value);

      if (choice.is_found) return choice.mood;
    }
  }

  return fallback;
}

static pure fn
workflow_shell_mood(StringView value) wontthrow -> interpreter_choice
{
  if (!value.is_empty() && (value[0] == '\'' || value[0] == '"')) {
    value = value.substring(1);
  }

  usize word_end = 0;

  while (word_end < value.length && value[word_end] != ' ' &&
         value[word_end] != '\'' && value[word_end] != '"')
    word_end++;

  let word = value.substring_of_length(0, word_end);

  if (word.is_empty()) return {};

  /* at this point: word is non-empty and holds no quote characters. */
  return {true, parser_format_shell_mood(word)};
}

static fn
find_child_shell(StringView source,
                 usize      parent_end,
                 usize      parent_indent,
                 StringView child_key) wontthrow -> interpreter_choice
{
  usize position     = parent_end;
  usize child_indent = static_cast<usize>(-1);

  while (position < source.length) {
    let const line = source.next_line(position);

    if (line.trim_blanks().is_empty()) continue;

    let const indent = parser_format_leading_spaces(line);

    if (parent_indent != static_cast<usize>(-1) && indent <= parent_indent) {
      break;
    }
    if (child_indent == static_cast<usize>(-1)) child_indent = indent;
    if (indent != child_indent) continue;
    if (!yaml_line_value(line, child_key).has_value()) continue;

    /* at this point: line is not blank, its indent is child_indent, and line
       is a mapping entry for child_key. */
    usize run_position = position;

    while (run_position < source.length) {
      let const run_line = source.next_line(run_position);

      if (run_line.trim_blanks().is_empty()) continue;
      if (parser_format_leading_spaces(run_line) <= child_indent) break;
      if (!yaml_line_value(run_line, "run").has_value()) continue;

      while (run_position < source.length) {
        let const shell_line = source.next_line(run_position);

        if (shell_line.trim_blanks().is_empty()) continue;
        if (parser_format_leading_spaces(shell_line) <=
            parser_format_leading_spaces(run_line))
          break;
        if (let const shell = yaml_line_value(shell_line, "shell"))
          return workflow_shell_mood(*shell);
      }
    }
  }

  return {};
}

static pure fn
selected_workflow_mood(StringView source,
                       usize      line_start,
                       StringView line,
                       mimic_mood fallback) wontthrow -> Maybe<mimic_mood>
{
  let const key_column = yaml_key_position(line);
  let       item_start = line_start;
  let       item_line  = line;
  bool      has_item   = parser_format_leading_spaces(line) < key_column;

  if (!has_item) {
    usize scan = line_start;

    while (scan > 0) {
      let const previous = previous_line(source, scan);

      if (previous.trim_blanks().is_empty()) continue;

      let const indent = parser_format_leading_spaces(previous);

      if (indent < key_column && indent < previous.length &&
          previous[indent] == '-' && yaml_key_position(previous) == key_column)
      {
        item_start = scan;
        item_line  = previous;
        has_item   = true;
        break;
      }

      if (indent < key_column) break;
    }
  }

  if (has_item) {
    usize position = item_start;
    bool  is_first = true;

    while (position < source.length) {
      let const candidate = source.next_line(position);

      if (candidate.trim_blanks().is_empty()) continue;
      if (!is_first && parser_format_leading_spaces(candidate) < key_column) {
        break;
      }
      is_first = false;
      if (yaml_key_position(candidate) != key_column) continue;
      if (let const shell = yaml_line_value(candidate, "shell")) {
        let const choice = workflow_shell_mood(*shell);

        if (choice.is_found) return choice.mood;
      }
    }
  }

  usize ancestor_scan  = item_start;
  usize minimum_indent = parser_format_leading_spaces(item_line);

  while (ancestor_scan > 0) {
    let const ancestor = previous_line(source, ancestor_scan);

    if (ancestor.trim_blanks().is_empty()) continue;

    let const indent = parser_format_leading_spaces(ancestor);

    if (indent >= minimum_indent) continue;
    minimum_indent = indent;

    usize ancestor_end = ancestor_scan;

    unused(source.next_line(ancestor_end));

    let const choice =
        find_child_shell(source, ancestor_end, indent, "defaults");

    if (choice.is_found) return choice.mood;
    if (let const runner = yaml_line_value(ancestor, "runs-on")) {
      if (parser_format_has_substring(*runner, "windows")) return None;
    }

    usize sibling = ancestor_end;

    while (sibling < source.length) {
      let const sibling_line = source.next_line(sibling);

      if (sibling_line.trim_blanks().is_empty()) continue;
      if (parser_format_leading_spaces(sibling_line) <= indent) break;
      if (parser_format_leading_spaces(sibling_line) != minimum_indent + 2 &&
          yaml_key_position(sibling_line) != minimum_indent + 2)
      {
        continue;
      }

      if (let const runner = yaml_line_value(sibling_line, "runs-on")) {
        if (parser_format_has_substring(*runner, "windows")) return None;
      }
    }
  }

  let const root_choice =
      find_child_shell(source, 0, static_cast<usize>(-1), "defaults");

  if (root_choice.is_found) return root_choice.mood;

  return fallback;
}

/* memory: result reserves source.length bytes and is returned to the caller.
   time:   O(source.length). in folded mode, each newline in [start, end)
           scans the line before it and the line after it. A line borders at
           most two newlines, so each line is scanned at most twice. */
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

    /* at this point: source[position] is a newline, start <= position,
       position + 1 < end, and is_folded_block is true. */
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
    let const is_joined =
        !current.trim_blanks().is_empty() && !next.trim_blanks().is_empty() &&
        parser_format_leading_spaces(current) == content_indent &&
        parser_format_leading_spaces(next) == content_indent;

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
      result.push(source[position + 1]);
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

    if (position < line.length && line[position] == '-') {
      position++;
    }

    while (position < line.length && line[position] == ' ')
      position++;

    if (position < line.length &&
        (line[position] == '\'' || line[position] == '"'))
    {
      position++;
    }

    if (position < line.length && line[position] == '\\') {
      position++;
    }

    let const word_start = position;

    if (position >= line.length ||
        !(lexer::is_variable_name_start(line[position])))
    {
      continue;
    }

    /* at this point: line[position] starts a variable name at word_start. */

    while (position < line.length && !lexer::is_whitespace(line[position]) &&
           !lexer::is_shell_sentinel(line[position]) &&
           line[position] != '\'' && line[position] != '"')
      position++;

    return line.substring_of_length(word_start, position - word_start);
  }

  return None;
}

/* memory: a copy of source is returned when no line is rewritten. otherwise
           rewritten, which reserves source.length bytes, is returned.
   time:   O(source.length). each line in [start, end) is scanned once, either
           for heredoc operators or for a pending terminator. */
static fn
align_indented_heredoc_terminators(StringView source,
                                   usize      start,
                                   usize      end) throws -> String
{
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
      let const indent  = parser_format_leading_spaces(line);

      if (content == pending[pending_index] && indent > 0) {
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

  if (!has_rewrite) return String{heap_allocator(), source};

  rewritten.append(source.substring(rewritten.length()));

  return rewritten;
}

static pure fn
yaml_block_indicator(StringView value) wontthrow -> Maybe<bool>
{
  if (value.is_empty() || (value[0] != '|' && value[0] != '>')) {
    return None;
  }

  usize position = 1;

  while (position < value.length &&
         (value[position] == '-' || value[position] == '+' ||
          (value[position] >= '0' && value[position] <= '9')))
    position++;

  while (position < value.length &&
         (value[position] == ' ' || value[position] == '\t'))
    position++;

  if (position < value.length && value[position] != '#') {
    return None;
  }

  /* at this point: value[0] is '|' or '>', and the rest of value holds only
     indicator characters, blanks, and an optional comment. */
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
    if (line[position] != '&' && line[position] != '!') {
      return position;
    }

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
        let const indent = parser_format_leading_spaces(candidate);

        if (indent <= parent_indent) {
          block_position = candidate_start;
          break;
        }

        if (content_indent == 0) content_indent = indent;
        block_end = candidate_start + candidate.length;
      }
    }

    next_position = block_position;
    if (content_indent == 0 || block_end <= block_start) {
      return false;
    }

    item = yaml_scalar_item{block_start, block_end, '\0', *is_folded,
                            content_indent};

    return true;
  }

  /* at this point: value is non-empty, value[0] is not '#', '*', or '{', and
     value is not a block scalar header. */
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

  /* at this point: value is a plain scalar, and item.start < item.end. */
  item = yaml_scalar_item{line_start + value_position, line_start + end};

  return true;
}

static fn
has_yaml_ancestor(StringView source,
                  usize      line_start,
                  StringView line,
                  StringView ancestor) wontthrow -> bool
{
  usize indent = parser_format_leading_spaces(line);
  usize scan   = line_start;

  while (scan > 0 && indent > 0) {
    let const previous = previous_line(source, scan);

    if (previous.trim_blanks().is_empty()) continue;

    let const previous_indent = parser_format_leading_spaces(previous);

    if (previous_indent >= indent) continue;
    indent = previous_indent;
    if (yaml_line_value(previous, ancestor).has_value()) return true;
  }

  return false;
}

static fn
add_yaml_scalar_fragment(parsed_format_document &document,
                         StringView              source,
                         const yaml_scalar_item &item,
                         mimic_mood              mood,
                         u8                      template_syntax) throws -> void
{
  if (item.start >= item.end) return;

  bool did_mask = false;
  let  masks    = parser_format_template_masks{};
  let  analysis = parser_format_masked_template_source(
      source, item.start, item.end, template_syntax, did_mask, &masks);
  let const fragment_count      = document.fragments.count();
  usize     first_line_position = item.start;
  let const first_line          = source.next_line(first_line_position);
  let const first_content =
      first_line.substring(parser_format_leading_spaces(first_line));

  if (first_content.starts_with("#!")) {
    let const choice =
        interpreter_of_value(parser_format_shebang_program(first_content));

    if (!choice.mood.has_value()) return;
    mood = *choice.mood;

    let blanked = String{heap_allocator()};

    blanked.reserve(analysis.length());
    blanked.append(analysis.view().substring_of_length(0, item.start));
    blanked.append_repeated(' ', first_line.length);
    blanked.append(analysis.view().substring(item.start + first_line.length));
    analysis = steal(blanked);
  }

  let const masked = analysis.clone();

  if (item.block_indent != 0) {
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

  if (document.fragments.count() == fragment_count) return;

  /* at this point: fragment is the last element of document.fragments, added
     for the host range [item.start, item.end). */
  let &fragment = document.fragments.back();

  let const compared_start = first_content.starts_with("#!")
                                 ? item.start + first_line.length
                                 : item.start;

  let const do_differs_from = [&](StringView other) {
    return compared_start < item.end &&
           fragment.analysis_source.view().substring_of_length(
               compared_start, item.end - compared_start) !=
               other.substring_of_length(compared_start,
                                         item.end - compared_start);
  };
  let const has_other_change = do_differs_from(masked.view());

  if (did_mask) fragment.should_silence_unresolved_commands = true;
  fragment.should_skip_format = has_other_change ||
                                (did_mask && !masks.is_restorable) ||
                                (!did_mask && do_differs_from(source));
  if (did_mask && !fragment.should_skip_format) {
    fragment.template_spans = steal(masks.spans);
  }
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

    if (position >= line_end || source[position] == ']') {
      return;
    }

    let const byte = source[position];

    if (byte == '"' || byte == '\'') {
      usize close = position + 1;

      while (close < line_end && source[close] != byte) {
        if (byte == '"' && source[close] == '\\') {
          close++;
        }
        close++;
      }

      while (byte == '\'' && close + 1 < line_end && source[close + 1] == '\'')
      {
        close += 2;

        while (close < line_end && source[close] != byte) {
          close++;
        }
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

  if (value_position < line.length && line[value_position] != '#') {
    return;
  }

  /* at this point: value_position is at the end of line or at a '#'
     comment, so any value items start on a following line. */
  usize item_indent = static_cast<usize>(-1);

  while (next_position < source.length) {
    let const item_start = next_position;
    let const item_line  = source.next_line(next_position);

    if (item_line.trim_blanks().is_empty()) continue;

    let const indent = parser_format_leading_spaces(item_line);
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
                                u8                      template_syntax,
                                StringView              excluded_ancestor) throws
    -> void
{
  usize position = 0;

  while (position < source.length) {
    let const line_start       = position;
    let const line             = source.next_line(position);
    usize     content_position = 0;

    if (!yaml_key_match(line, keys, key_count, content_position)) continue;
    if (!excluded_ancestor.is_empty() &&
        has_yaml_ancestor(source, line_start, line, excluded_ancestor))
    {
      continue;
    }

    let const mood =
        selection == yaml_shell_selection::Workflow
            ? selected_workflow_mood(source, line_start, line, default_mood)
            : mood_near(source, line_start, line, default_mood);

    if (!mood.has_value()) continue;

    /* at this point: line matches one of keys, and mood holds a value. */
    let   items         = ArrayList<yaml_scalar_item>{heap_allocator()};
    usize next_position = position;

    collect_yaml_value_items(source, line_start, line, content_position,
                             yaml_key_position(line), next_position, items);

    for (let const &item : items)
      add_yaml_scalar_fragment(document, source, item, *mood, template_syntax);

    if (next_position > position) position = next_position;
  }
}

fn
parser_format_extract_yaml_value_at(parsed_format_document &document,
                                    StringView              source,
                                    usize                   value_line_start,
                                    usize                   mood_line_start,
                                    mimic_mood              default_mood,
                                    u8 template_syntax) throws -> void
{
  usize     mood_position = mood_line_start;
  let const mood_line     = source.next_line(mood_position);
  let const mood = mood_near(source, mood_line_start, mood_line, default_mood);

  if (!mood.has_value()) return;

  usize     position   = value_line_start;
  let const line       = source.next_line(position);
  let const key_column = yaml_key_position(line);
  let const colon      = line.substring(key_column).find_character(':');

  if (!colon.has_value()) return;

  /* at this point: mood holds a value, and line has a ':' at or after
     key_column. */
  usize content_position = key_column + *colon + 1;

  while (content_position < line.length && line[content_position] == ' ')
    content_position++;

  let items = ArrayList<yaml_scalar_item>{heap_allocator()};

  collect_yaml_value_items(source, value_line_start, line, content_position,
                           key_column, position, items);

  for (let const &item : items)
    add_yaml_scalar_fragment(document, source, item, *mood, template_syntax);
}

static pure fn
is_shell_command_flag(StringView word) wontthrow -> bool
{
  if (word.length < 2 || word[0] != '-' || word[1] == '-') {
    return false;
  }

  for (usize position = 1; position < word.length; position++) {
    if (word[position] < 'a' || word[position] > 'z') {
      return false;
    }
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
    return parser_format_leading_spaces(candidate) <= key_column &&
           yaml_key_position(candidate) == key_column &&
           yaml_line_value(candidate, sibling).has_value();
  };
  let const do_is_item_start = [&](StringView candidate) {
    let const indent = parser_format_leading_spaces(candidate);

    return indent < key_column && indent < candidate.length &&
           candidate[indent] == '-';
  };

  usize scan = line_start;

  if (!do_is_item_start(line)) {
    while (scan > 0) {
      let const candidate       = previous_line(source, scan);
      let const candidate_start = scan;

      if (candidate.trim_blanks().is_empty()) continue;
      if (parser_format_leading_spaces(candidate) < key_column &&
          !do_is_item_start(candidate))
      {
        break;
      }
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
    if (parser_format_leading_spaces(candidate) < key_column) break;
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

    /* at this point: line matches one of keys, and items is not empty. */
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

    if (!argv.is_empty() && yaml_item_text(source, argv[0]) == "CMD") {
      first = 1;
    }
    if (argv.count() < first + 3) continue;

    let const mood =
        parser_format_shell_mood(yaml_item_text(source, argv[first]));

    if (!mood.has_value()) continue;

    usize script_index = 0;

    for (usize index = first + 1; index + 1 < argv.count(); index++) {
      let const word = yaml_item_text(source, argv[index]);

      if (word.is_empty() || word[0] != '-') {
        break;
      }
      if (is_shell_command_flag(word)) {
        script_index = index + 1;
        break;
      }
    }

    if (script_index == 0 || script_index < prefix.count()) {
      continue;
    }
    add_yaml_scalar_fragment(document, source, argv[script_index], *mood,
                             template_syntax);
  }
}

static pure fn
json_ascii_escape(StringView source, usize backslash, usize end) wontthrow
    -> Maybe<char>
{
  if (backslash + 6 > end || source[backslash + 1] != 'u') {
    return None;
  }

  /* at this point: source[backslash + 1] is 'u', and the four bytes after it
     lie before end. */
  u32 code = 0;

  for (usize at = backslash + 2; at < backslash + 6; at++) {
    let const byte = source[at];

    code <<= 4;
    if (byte >= '0' && byte <= '9')
      code |= static_cast<u32>(byte - '0');
    else if (byte >= 'a' && byte <= 'f')
      code |= static_cast<u32>(byte - 'a' + 10);
    else if (byte >= 'A' && byte <= 'F')
      code |= static_cast<u32>(byte - 'A' + 10);
    else
      return None;
  }

  if (code < 0x20 || code >= 0x7f) {
    return None;
  }

  return static_cast<char>(code);
}

fn
parser_format_add_json_fragment(parsed_format_document &document,
                                StringView              source,
                                usize                   start,
                                usize                   end,
                                mimic_mood              mood) throws -> void
{
  if (start >= end) return;

  let masked = String{heap_allocator()};

  masked.reserve(end);

  for (usize host_position = 0; host_position < start; host_position++) {
    let const byte = source[host_position];

    masked.push(byte == '\n' ? '\n' : ' ');
  }

  /* e.g. the string body is a\nb with start = 0 and end = 4, bytes 'a', '\\',
          'n', 'b'.
       the escape at position 1 gives run character '\n' and run_end = 3, so
       append_repeated() adds 3 - 1 - 1 = 1 space before it.
       -> masked = "a \nb" (4 bytes); shell_source = "a\nb" (3 bytes) */
  for (usize host_position = start; host_position < end; host_position++) {
    let const byte = source[host_position];

    if (byte != '\\' || host_position + 1 >= end) {
      masked.push(byte);
      continue;
    }

    char  run[64];
    usize run_count = 0;
    usize run_end   = host_position;

    while (run_end + 1 < end && source[run_end] == '\\' &&
           run_count < countof(run))
    {
      let const escaped = source[run_end + 1];

      if (let const code = json_ascii_escape(source, run_end, end)) {
        run[run_count++] = *code;
        run_end += 6;
        continue;
      }

      switch (escaped) {
      case 'n': run[run_count++] = '\n'; break;
      case 't': run[run_count++] = '\t'; break;
      case 'r': run[run_count++] = ' '; break;
      default:  run[run_count++] = escaped; break;
      }

      run_end += 2;
    }

    masked.append_repeated(' ', run_end - host_position - run_count);
    for (usize index = 0; index < run_count; index++)
      masked.push(run[index]);
    host_position = run_end - 1;
  }

  parser_format_add_fragment(document, source, start, end, 0, steal(masked),
                             parser_format_codec::JsonString, mood);
  if (document.fragments.is_empty()) return;

  /* at this point: start is less than end, and fragment is the last element
     of document.fragments. */
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
    case '/':  decoded.push('/'); break;
    case 'u':
      if (let const code = json_ascii_escape(source, position - 2, end)) {
        decoded.push(*code);
        position += 4;
        break;
      }
      [[fallthrough]];
    default:
      /* encoding would double the kept backslash, so skip formatting. */
      fragment.should_skip_format = true;
      decoded.push('\\');
      decoded.push(escaped);
      break;
    }
  }

  fragment.shell_source = steal(decoded);
}

static pure fn
skip_json_trivia(StringView source, usize position) wontthrow -> usize
{
  while (position < source.length) {
    let const byte = source[position];

    if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' ||
        byte == ',')
    {
      position++;
      continue;
    }

    if (byte == '/' && position + 1 < source.length &&
        source[position + 1] == '/')
    {
      while (position < source.length && source[position] != '\n')
        position++;
      continue;
    }

    if (byte == '/' && position + 1 < source.length &&
        source[position + 1] == '*')
    {
      position += 2;
      while (position + 1 < source.length &&
             !(source[position] == '*' && source[position + 1] == '/'))
        position++;
      position += 2;
      continue;
    }

    break;
  }

  /* an unterminated block comment advances position past source.length. */
  return position < source.length ? position : source.length;
}

pure fn
parser_format_json_string_end(StringView source, usize position) wontthrow
    -> usize
{
  while (position < source.length && source[position] != '"') {
    if (source[position] == '\\' && position + 1 < source.length) {
      position++;
    }
    position++;
  }

  return position;
}

struct json_container
{
  bool                          is_array{false};
  StringView                    key{};
  const parser_format_json_key *command_key{nullptr};
};

static fn
add_json_command_fragment(parsed_format_document       &document,
                          StringView                    source,
                          usize                         start,
                          usize                         end,
                          mimic_mood                    mood,
                          const parser_format_json_key &key,
                          u8                            template_syntax) throws -> void
{
  let const fragment_count = document.fragments.count();

  parser_format_add_json_fragment(document, source, start, end, mood);
  if (document.fragments.count() == fragment_count) return;

  /* at this point: fragment is the last element of document.fragments, added
     by parser_format_add_json_fragment() for the host range [start, end). */
  let &fragment = document.fragments.back();

  fragment.should_silence_unresolved_commands =
      key.should_silence_unresolved_commands;
  if (template_syntax == 0) return;

  bool did_mask = false;
  let  masked   = parser_format_masked_template_source(
      fragment.analysis_source.view(), start, end, template_syntax, did_mask);

  if (!did_mask) return;
  fragment.analysis_source                    = steal(masked);
  fragment.should_silence_unresolved_commands = true;
}

static fn
add_json_exec_array(parsed_format_document       &document,
                    StringView                    source,
                    usize                         open,
                    const parser_format_json_key &key,
                    u8                            template_syntax) throws -> void
{
  usize starts[4]{};
  usize ends[4]{};
  usize item_count = 0;
  usize position   = skip_json_trivia(source, open + 1);

  while (position < source.length && source[position] == '"' &&
         item_count < countof(starts))
  {
    starts[item_count] = position + 1;
    position           = parser_format_json_string_end(source, position + 1);
    ends[item_count++] = position;
    position           = skip_json_trivia(source, position + 1);
  }

  if (item_count < 3) return;

  /* at this point: item_count is at least 3, so starts[0] to starts[2] and
     ends[0] to ends[2] bound the mood, flag, and script strings. */
  let const mood = parser_format_shell_mood(
      source.substring_of_length(starts[0], ends[0] - starts[0]));
  let const flag = source.substring_of_length(starts[1], ends[1] - starts[1]);

  if (!mood.has_value() || !is_shell_command_flag(flag)) {
    return;
  }

  add_json_command_fragment(document, source, starts[2], ends[2], *mood, key,
                            template_syntax);
}

fn
parser_format_extract_json_keys(parsed_format_document       &document,
                                StringView                    source,
                                const parser_format_json_key *keys,
                                usize                         key_count,
                                mimic_mood                    default_mood,
                                u8 template_syntax) throws -> void
{
  let        stack    = ArrayList<json_container>{heap_allocator()};
  usize      position = 0;
  StringView pending_key{};

  let const do_find_key =
      [&](StringView key) -> const parser_format_json_key * {
    for (usize key_index = 0; key_index < key_count; key_index++) {
      let const &candidate = keys[key_index];

      if (candidate.name != key) continue;
      if (candidate.depth != 0 && candidate.depth != stack.count()) {
        continue;
      }
      bool has_container = candidate.container_key.is_empty();
      bool is_excluded   = false;

      for (let const &container : stack) {
        if (container.key == candidate.container_key) has_container = true;
        if (!candidate.excluded_container_key.is_empty() &&
            container.key == candidate.excluded_container_key)
        {
          is_excluded = true;
        }
      }

      if (!has_container || is_excluded) {
        continue;
      }

      return &candidate;
    }

    return nullptr;
  };

  while (position < source.length) {
    position = skip_json_trivia(source, position);
    if (position >= source.length) break;

    let const byte = source[position];

    if (byte == '{' || byte == '[') {
      let const *matched =
          pending_key.is_empty() ? nullptr : do_find_key(pending_key);

      if (byte == '[' && matched != nullptr) {
        add_json_exec_array(document, source, position, *matched,
                            template_syntax);
      }

      stack.push(json_container{byte == '[', pending_key,
                                byte == '{' ? matched : nullptr});
      pending_key = {};
      position++;
      continue;
    }

    if (byte == '}' || byte == ']') {
      if (!stack.is_empty()) stack.pop_back();
      pending_key = {};
      position++;
      continue;
    }

    if (byte != '"') {
      while (position < source.length && source[position] != ',' &&
             source[position] != '}' && source[position] != ']' &&
             source[position] != '\n')
        position++;
      pending_key = {};
      continue;
    }

    let const string_start = position + 1;
    let const string_end = parser_format_json_string_end(source, string_start);

    position = skip_json_trivia(source, string_end + 1);

    let const is_object_member = !stack.is_empty() && !stack.back().is_array;

    if (pending_key.is_empty() && is_object_member &&
        position < source.length && source[position] == ':')
    {
      pending_key =
          source.substring_of_length(string_start, string_end - string_start);
      position++;
      continue;
    }

    if (!pending_key.is_empty()) {
      if (let const *matched = do_find_key(pending_key)) {
        add_json_command_fragment(document, source, string_start, string_end,
                                  default_mood, *matched, template_syntax);
      } else if (!stack.is_empty() && stack.back().command_key != nullptr) {
        add_json_command_fragment(document, source, string_start, string_end,
                                  default_mood, *stack.back().command_key,
                                  template_syntax);
      }
    }

    pending_key = {};
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

static pure fn
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

  /* e.g. replacement = "case $x in\na)\nrun\n;;\nesac", indent_length = 4,
     continuation_byte = ' '
       "case $x in": case header, so no ';' follows it
       "a)": case pattern, so no ';' follows it
       "run": next line is ";;", so no ';' follows it
       ";;": in LIST_OPENERS, so no ';' before "esac"
       -> "case $x in \\\n    a) \\\n    run \\\n    ;; \\\n    esac" */
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

    if (is_case_header || !is_wrapped) {
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
    let const line_start = position;
    let const line       = replacement.next_line(position);

    if (!line.is_empty()) encoded.append_repeated(' ', fragment.indent_length);

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

  /* e.g. source = "abcdefgh", replacements = [(1, 3, "YY"), (5, 7, "X")]
       append "a" and "YY", position = 3
       append "de" and "X", position = 7
       append "h"; result = "aYYdeXh" */
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

/* memory: returns a String of host_source.length bytes, which the caller owns.
   time:   O(host_source.length + document.fragments.count()). fragment_index
           only increases, so the inner while loop advances it at most once per
           fragment in total. */
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

static fn
host_variable_patterns(parser_format_kind kind, usize &count) wontthrow
    -> const StringView *
{
  static constexpr StringView GITHUB[]  = {"GITHUB_*", "RUNNER_*", "CI"};
  static constexpr StringView GITEA[]   = {"GITHUB_*", "GITEA_*", "RUNNER_*",
                                           "CI"};
  static constexpr StringView FORGEJO[] = {"GITHUB_*", "FORGEJO_*", "RUNNER_*",
                                           "CI"};
  static constexpr StringView GITLAB[]  = {"CI_*", "GITLAB_*", "CI"};
  static constexpr StringView CIRCLE[]  = {"CIRCLE_*", "CIRCLECI", "CI"};
  static constexpr StringView AZURE[]   = {"BUILD_*", "SYSTEM_*", "AGENT_*",
                                           "PIPELINE_*", "TF_BUILD"};
  static constexpr StringView BITBUCKET[]  = {"BITBUCKET_*", "CI"};
  static constexpr StringView BUILDKITE[]  = {"BUILDKITE_*", "BUILDKITE", "CI"};
  static constexpr StringView TRAVIS[]     = {"TRAVIS_*", "TRAVIS", "CI"};
  static constexpr StringView DRONE[]      = {"DRONE_*", "DRONE", "CI_*", "CI"};
  static constexpr StringView WOODPECKER[] = {"CI_*", "CI"};
  static constexpr StringView CLOUD_BUILD[] = {
      "PROJECT_ID",  "PROJECT_NUMBER", "BUILD_ID",    "LOCATION",
      "REPO_NAME",   "REPO_FULL_NAME", "BRANCH_NAME", "TAG_NAME",
      "REVISION_ID", "COMMIT_SHA",     "SHORT_SHA",   "_*"};
  static constexpr StringView NPM[] = {"npm_*"};

  struct host_variable_entry
  {
    parser_format_kind kind;
    const StringView  *patterns;
    usize              pattern_count;
  };

  static constexpr host_variable_entry ENTRIES[] = {
      {parser_format_kind::GithubActions,      GITHUB,      countof(GITHUB)     },
      {parser_format_kind::GiteaActions,       GITEA,       countof(GITEA)      },
      {parser_format_kind::ForgejoActions,     FORGEJO,     countof(FORGEJO)    },
      {parser_format_kind::GitlabCi,           GITLAB,      countof(GITLAB)     },
      {parser_format_kind::CircleCi,           CIRCLE,      countof(CIRCLE)     },
      {parser_format_kind::AzurePipelines,     AZURE,       countof(AZURE)      },
      {parser_format_kind::BitbucketPipelines, BITBUCKET,   countof(BITBUCKET)  },
      {parser_format_kind::Buildkite,          BUILDKITE,   countof(BUILDKITE)  },
      {parser_format_kind::TravisCi,           TRAVIS,      countof(TRAVIS)     },
      {parser_format_kind::Drone,              DRONE,       countof(DRONE)      },
      {parser_format_kind::Woodpecker,         WOODPECKER,  countof(WOODPECKER) },
      {parser_format_kind::CloudBuild,         CLOUD_BUILD, countof(CLOUD_BUILD)},
      {parser_format_kind::PackageJson,        NPM,         countof(NPM)        },
  };

  for (let const &entry : ENTRIES) {
    if (entry.kind != kind) continue;
    count = entry.pattern_count;

    return entry.patterns;
  }

  count = 0;

  return nullptr;
}

/* memory: each fragment holds one String per host variable pattern, at most
           12 for CloudBuild. the returned document owns them.
   time:   O(F log F) for sort() over F fragments, then O(F * V) for V host
           variable patterns, with V at most 12. the detected kind's parser
           adds its own cost. */
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

  document.fragments.sort([](const parser_format_fragment &left,
                             const parser_format_fragment &right) {
    return left.host_start < right.host_start;
  });

  usize     host_variable_count = 0;
  let const host_variables =
      host_variable_patterns(document.kind, host_variable_count);

  for (let &fragment : document.fragments) {
    for (usize index = 0; index < host_variable_count; index++) {
      fragment.assumed_variables.push(
          String{heap_allocator(), host_variables[index]});
    }
  }

  return document;
}

} /* namespace koshka */
