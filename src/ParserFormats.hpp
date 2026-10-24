/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares host-format detection, embedded fragment storage, source
 * mapping, codecs, and parser interfaces. The separate interface produces
 * mapped shell fragments instead of shell syntax-tree nodes.
 */

#pragma once

#include "MimicMood.hpp"
#include "base/ArrayList.hpp"
#include "base/Maybe.hpp"
#include "base/StaticStringMap.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka {

#define PARSER_FORMAT_KIND_ENTRIES(X)                                          \
  X(Shell, "shell scripts")                                                    \
  X(UnknownHost, "")                                                           \
  X(GithubActions, "GitHub Actions")                                           \
  X(GiteaActions, "Gitea Actions")                                             \
  X(ForgejoActions, "Forgejo Actions")                                         \
  X(GitlabCi, "GitLab CI")                                                     \
  X(Ansible, "Ansible")                                                        \
  X(Dockerfile, "Dockerfiles")                                                 \
  X(Compose, "Compose files")                                                  \
  X(Markdown, "Markdown")                                                      \
  X(Makefile, "Makefiles")                                                     \
  X(CloudBuild, "Google Cloud Build")                                          \
  X(CircleCi, "CircleCI")                                                      \
  X(AzurePipelines, "Azure Pipelines")                                         \
  X(BitbucketPipelines, "Bitbucket Pipelines")                                 \
  X(Buildkite, "Buildkite")                                                    \
  X(TravisCi, "Travis CI")                                                     \
  X(Kubernetes, "Kubernetes")                                                  \
  X(PackageJson, "package.json")                                               \
  X(Drone, "Drone")                                                            \
  X(Woodpecker, "Woodpecker")                                                  \
  X(Taskfile, "Taskfiles")                                                     \
  X(Justfile, "Justfiles")                                                     \
  X(Jinja, "Jinja templates")                                                  \
  X(RpmSpec, "RPM specs")                                                      \
  X(VscodeTasks, "VS Code tasks")                                              \
  X(DevContainer, "Dev Containers")

enum class parser_format_kind : u8
{
#define PARSER_FORMAT_KIND_ENUM(kind, label) kind,
  PARSER_FORMAT_KIND_ENTRIES(PARSER_FORMAT_KIND_ENUM)
#undef PARSER_FORMAT_KIND_ENUM
};

constexpr usize PARSER_FORMAT_KIND_COUNT =
    static_cast<usize>(parser_format_kind::DevContainer) + 1;

inline pure fn
parser_format_kind_name(parser_format_kind kind) wontthrow -> StringView
{
  static constexpr StringView NAMES[PARSER_FORMAT_KIND_COUNT] = {
#define PARSER_FORMAT_KIND_NAME(kind, label) label,
      PARSER_FORMAT_KIND_ENTRIES(PARSER_FORMAT_KIND_NAME)
#undef PARSER_FORMAT_KIND_NAME
  };
  let const index = static_cast<usize>(kind);

  return index < PARSER_FORMAT_KIND_COUNT ? NAMES[index] : StringView{};
}

#undef PARSER_FORMAT_KIND_ENTRIES

enum class parser_format_codec : u8
{
  Direct,
  Indented,
  JsonString,
  Continued,
};

enum class yaml_shell_selection : u8
{
  Nearby,
  Workflow,
};

struct parser_format_json_key
{
  StringView name;
  bool       should_silence_unresolved_commands;
  usize      depth{0};
  StringView container_key{};
  StringView excluded_container_key{};
};

struct parser_format_template_span
{
  String host_text{heap_allocator()};
  String placeholder{heap_allocator()};
};

struct parser_format_template_masks
{
  ArrayList<parser_format_template_span> spans{heap_allocator()};
  bool                                   is_restorable{true};
};

struct parser_format_fragment
{
  String              analysis_source{heap_allocator()};
  String              shell_source{heap_allocator()};
  mimic_mood          mood{mimic_mood::Posix};
  parser_format_codec codec{parser_format_codec::Direct};
  bool                should_silence_unresolved_commands{false};
  bool                should_select_end{false};
  bool                should_skip_format{false};
  ArrayList<parser_format_template_span> template_spans{heap_allocator()};
  ArrayList<String>                      assumed_variables{heap_allocator()};
  char                                   continuation_byte{' '};
  usize                                  host_start{0};
  usize                                  host_end{0};
  usize                                  indent_length{0};

  pure fn
  contains(usize host_position) const wontthrow -> bool
  {
    return host_position >= host_start && host_position < host_end;
  }
};

struct parsed_format_document
{
  parser_format_kind                kind{parser_format_kind::Shell};
  bool                              is_host_format{false};
  ArrayList<parser_format_fragment> fragments{heap_allocator()};
};

struct parser_format_input
{
  StringView        source;
  Maybe<StringView> path;
  Maybe<StringView> language_id;
};

struct parser_format_replacement
{
  usize  start_position{0};
  usize  end_position{0};
  String replacement{heap_allocator()};
};

fn
parse_format_document(const parser_format_input &input) throws
    -> parsed_format_document;
pure fn
parser_format_fragment_at(const parsed_format_document &document,
                          usize host_position) wontthrow -> Maybe<usize>;
fn
parser_format_encode(const parser_format_fragment &fragment,
                     StringView replacement) throws -> Maybe<String>;
/* replacements come in ascending start order, as the fragments of a parsed
   document do. returns None when two overlap or one is out of order. */
fn
parser_format_apply_replacements(
    StringView                                  source,
    const ArrayList<parser_format_replacement> &replacements) throws
    -> Maybe<String>;
fn
parser_format_analysis_source(const parsed_format_document &document,
                              StringView host_source) throws -> String;
pure fn
parser_format_should_silence_unresolved_commands(
    parser_format_kind kind) wontthrow -> bool;

pure fn
parser_format_has_substring(StringView source, StringView wanted) wontthrow
    -> bool;
pure fn
parser_format_filename(StringView path) wontthrow -> StringView;
pure fn
parser_format_leading_spaces(StringView line) wontthrow -> usize;
pure fn
parser_format_json_string_end(StringView source, usize position) wontthrow
    -> usize;

template <class Value, usize Count, class Key>
fn
parser_format_find_ascii_folded(const StaticStringMap<Value, Count, Key> &map,
                                StringView text) throws -> Maybe<Value>
{
  char folded[32];

  if (text.length > sizeof(folded)) return None;

  /* at this point: text fits in folded, so the loop below stays in bounds. */
  for (usize position = 0; position < text.length; position++) {
    let const byte = text[position];

    folded[position] = byte >= 'A' && byte <= 'Z'
                           ? static_cast<char>(byte + ('a' - 'A'))
                           : byte;
  }

  return map.find(StringView{folded, text.length});
}

fn
parser_format_add_fragment(
    parsed_format_document &document,
    StringView              host_source,
    usize                   host_start,
    usize                   host_end,
    usize                   indent_length            = 0,
    Maybe<String>           prepared_analysis_source = None,
    parser_format_codec     codec = parser_format_codec::Direct,
    mimic_mood              mood  = mimic_mood::Posix) throws -> void;
constexpr u8 TEMPLATE_CURLY             = 1u << 0;
constexpr u8 TEMPLATE_STATEMENTS        = 1u << 1;
constexpr u8 TEMPLATE_DOLLAR_CURLY      = 1u << 2;
constexpr u8 TEMPLATE_AZURE_MACROS      = 1u << 3;
constexpr u8 TEMPLATE_CIRCLE_PARAMETERS = 1u << 4;
constexpr u8 TEMPLATE_IDE_VARIABLES     = 1u << 5;
constexpr u8 TEMPLATE_DOUBLE_DOLLAR     = 1u << 6;

fn
parser_format_masked_template_source(
    StringView                    host_source,
    usize                         host_start,
    usize                         host_end,
    u8                            template_syntax,
    bool                         &did_mask,
    parser_format_template_masks *masks = nullptr) throws -> String;
fn
parser_format_formattable_source(
    const parser_format_fragment &fragment) throws -> Maybe<String>;
fn
parser_format_host_replacement(const parser_format_fragment &fragment,
                               StringView                    host_source,
                               StringView                    formatted) throws
    -> Maybe<String>;
fn
parser_format_add_indented_fragment(
    parsed_format_document &document,
    StringView              host_source,
    usize                   host_start,
    usize                   host_end,
    usize                   indent_length,
    mimic_mood              mood,
    Maybe<String>           prepared_analysis_source = None) throws -> void;
fn
parser_format_extract_yaml_keys(
    parsed_format_document &document,
    StringView              source,
    const StringView       *keys,
    usize                   key_count,
    mimic_mood              default_mood      = mimic_mood::Posix,
    yaml_shell_selection    selection         = yaml_shell_selection::Nearby,
    u8                      template_syntax   = TEMPLATE_CURLY,
    StringView              excluded_ancestor = {}) throws -> void;
fn
parser_format_extract_yaml_value_at(parsed_format_document &document,
                                    StringView              source,
                                    usize                   value_line_start,
                                    usize                   mood_line_start,
                                    mimic_mood              default_mood,
                                    u8 template_syntax) throws -> void;
fn
parser_format_extract_yaml_exec_keys(
    parsed_format_document &document,
    StringView              source,
    const StringView       *keys,
    usize                   key_count,
    u8                      template_syntax = TEMPLATE_CURLY) throws -> void;
fn
parser_format_extract_json_keys(parsed_format_document       &document,
                                StringView                    source,
                                const parser_format_json_key *keys,
                                usize                         key_count,
                                mimic_mood                    default_mood,
                                u8 template_syntax = 0) throws -> void;
fn
parser_format_add_json_fragment(parsed_format_document &document,
                                StringView              source,
                                usize                   host_start,
                                usize                   host_end,
                                mimic_mood              mood) throws -> void;

fn
parse_workflow_actions_format(const parser_format_input &input,
                              parsed_format_document    &document) throws
    -> void;
fn
parse_gitlab_ci_format(const parser_format_input &input,
                       parsed_format_document    &document) throws -> void;
fn
parse_ansible_format(const parser_format_input &input,
                     parsed_format_document    &document) throws -> void;
fn
parse_dockerfile_format(const parser_format_input &input,
                        parsed_format_document    &document) throws
    -> void;
fn
parse_compose_format(const parser_format_input &input,
                     parsed_format_document    &document) throws -> void;
fn
parse_markdown_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void;
fn
parse_makefile_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void;
fn
parse_cloud_build_format(const parser_format_input &input,
                         parsed_format_document    &document) throws
    -> void;
fn
parse_circle_ci_format(const parser_format_input &input,
                       parsed_format_document    &document) throws -> void;
fn
parse_azure_pipelines_format(const parser_format_input &input,
                             parsed_format_document    &document) throws
    -> void;
fn
parse_bitbucket_pipelines_format(
    const parser_format_input &input,
    parsed_format_document    &document) throws -> void;
fn
parse_buildkite_format(const parser_format_input &input,
                       parsed_format_document    &document) throws -> void;
fn
parse_travis_ci_format(const parser_format_input &input,
                       parsed_format_document    &document) throws -> void;
fn
parse_kubernetes_format(const parser_format_input &input,
                        parsed_format_document    &document) throws
    -> void;
fn
parse_package_json_format(const parser_format_input &input,
                          parsed_format_document    &document) throws
    -> void;
fn
parse_drone_format(const parser_format_input &input,
                   parsed_format_document    &document) throws -> void;
fn
parse_woodpecker_format(const parser_format_input &input,
                        parsed_format_document    &document) throws
    -> void;
fn
parse_taskfile_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void;
fn
parse_justfile_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void;
fn
parse_jinja_format(const parser_format_input &input,
                   parsed_format_document    &document) throws -> void;
pure fn
jinja_template_inner_name(StringView filename) wontthrow -> Maybe<StringView>;
pure fn
jinja_template_shell_mood(StringView inner_name, StringView source) wontthrow
    -> Maybe<mimic_mood>;
fn
parse_rpm_spec_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void;
fn
parse_vscode_tasks_format(const parser_format_input &input,
                          parsed_format_document    &document) throws
    -> void;
fn
parse_dev_container_format(const parser_format_input &input,
                           parsed_format_document    &document) throws
    -> void;

} /* namespace koshka */
