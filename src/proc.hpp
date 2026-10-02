#pragma once
// ============================================================
//  birun · proc.hpp  (v1.0.0)
//  Thin dispatcher. Real implementations live in src/platform/.
//    - POSIX:   platform/proc_posix.hpp
//    - Windows: platform/proc_win32.hpp
//  Public API is identical on both platforms.
// ============================================================

#if defined(_WIN32)
#  include "platform/proc_win32.hpp"
#else
#  include "platform/proc_posix.hpp"
#endif