// SPDX-License-Identifier: MIT
//
// MQTT topic engine — the umbrella header.
//
// Brings the whole public API into one include. The individual headers stay usable on their own;
// this one exists so a consumer does not have to remember which type lives where.

#pragma once

#include "cache_strategy.hxx"
#include "error.hxx"
#include "qos.hxx"
#include "result.hxx"
#include "topic_match.hxx"
#include "topic_pattern_item.hxx"
#include "topic_pattern_path.hxx"
#include "topic_wellformedness.hxx"

#ifndef BA7LYA_TOPIC_ENGINE_NO_ROUTER
#include "topic_matcher.hxx"
#include "topic_router.hxx"
#endif
