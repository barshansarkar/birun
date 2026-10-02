#pragma once
// ============================================================
//  birun · version.hpp
//  Single source of truth for version + cache format.
//  API stability policy: see VERSIONING.md
// ============================================================

#define BIRUN_VERSION_MAJOR 1
#define BIRUN_VERSION_MINOR 0
#define BIRUN_VERSION_PATCH 0
#define BIRUN_VERSION_STRING "1.0.0"

// C++ header API version — bumped on ANY breaking source change.
#define BIRUN_API_VERSION 1

// Cache file format version.
#define BIRUN_CACHE_FORMAT 2
#define BIRUN_CACHE_MAGIC "birun-cache-v2"

// Stringify helpers
#define BIRUN_STR_(x) #x
#define BIRUN_STR(x)  BIRUN_STR_(x)

// #define BIRUN_VERSION_FULL "1.0.0"