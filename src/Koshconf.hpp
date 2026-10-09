/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the koshconf file reader and writer, the preset file
 * text, and the binary KOSHCONF form that carries the settings to another
 * shell.
 */

#pragma once

#include "MimicMood.hpp"
#include "Options.hpp"
#include "base/ArrayList.hpp"
#include "base/Path.hpp"

namespace koshka {

inline constexpr StringView KOSHCONF_VARIABLE_NAME{"KOSHCONF"};

struct koshconf_setting
{
  const option_descriptor *option;
  String value;
  String kosh_mood_warning{heap_allocator()};
};

struct koshconf_reading
{
  ArrayList<koshconf_setting> settings{heap_allocator()};
  ArrayList<String> warnings{heap_allocator()};
};

fn get_user_koshconf_path() throws -> Maybe<Path>;
fn read_koshconf_text(StringView text, StringView origin_name,
                      koshconf_reading &reading) throws -> void;
fn read_koshconf_file(const Path &path, koshconf_reading &reading) throws
    -> bool;
fn read_system_koshconf_file(const Path &path, koshconf_reading &reading) throws
    -> bool;
fn read_koshconf_blob(StringView encoded, koshconf_reading &reading) throws
    -> Maybe<StringView>;
fn encode_koshconf_blob(const EvalContext &cxt) throws -> String;
fn apply_koshconf_settings(EvalContext &cxt,
                           const ArrayList<koshconf_setting> &settings,
                           option_origin origin,
                           ArrayList<String> &warnings) throws -> void;
fn parse_mood_list(StringView list, ArrayList<mimic_mood> &moods) throws
    -> Maybe<StringView>;
fn find_koshconf_value_problem(const option_descriptor &option,
                               StringView value) throws -> Maybe<String>;
pure fn koshconf_option_takes_count(const option_descriptor &option) wontthrow
    -> bool;
fn describe_kosh_mood_hold(const option_descriptor &option) throws -> String;
fn suggest_koshconf_option_name(StringView name) throws -> Maybe<String>;
fn format_koshconf_line(const option_descriptor &option,
                        StringView value) throws -> String;
fn format_koshconf_display_line(const option_descriptor &option,
                                StringView value) throws -> String;
fn make_koshconf_preset(mimic_mood preset) throws -> String;
fn make_koshconf_snapshot(const EvalContext &cxt) throws -> String;
fn write_koshconf_file(const Path &path, StringView contents) throws -> void;
fn persist_koshconf_setting(const Path &path, const option_descriptor &option,
                            StringView value) throws -> void;

} /* namespace koshka */
