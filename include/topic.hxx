///
/// @file topic.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief The topic engine - the umbrella header.
/// @version 0.1
/// @date 2026-10-14
/// @copyright Copyright (c) 2026
///
// SPDX-License-Identifier: MIT

#pragma once

#include "cache.hxx"
#include "error.hxx"
#include "filter.hxx"
#include "matcher.hxx"
#include "pattern.hxx"
#include "qos.hxx"
#include "result.hxx"
#include "trie.hxx"

#ifndef BA7LYA_TOPIC_NO_ROUTER
#include "router.hxx"
#endif
