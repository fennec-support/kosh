/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file holds the shell scripts built into Koshka. koshconf create writes
 * DEFAULT_RC_SCRIPT as the user rc script.
 */

#pragma once

#include "base/StringView.hpp"

namespace koshka {

/* the default user rc script. A login shell sources the Bash login files, and
 * an interactive shell sources the POSIX and Bash rc files. */
constexpr StringView DEFAULT_RC_SCRIPT{
    R"(#!/usr/bin/env kosh
if shopt -q login_shell; then
  [ -r /etc/profile ] && . /etc/profile
  if [ -r ~/.bash_profile ]; then
    . ~/.bash_profile
  elif [ -r ~/.bash_login ]; then
    . ~/.bash_login
  elif [ -r ~/.profile ]; then
    . ~/.profile
  fi
else
  [ -r ~/.profile ] && . ~/.profile
  [ -r ~/.bashrc ] && . ~/.bashrc
fi
)"};

} /* namespace koshka */
