#pragma once
// ============================================================
//  birun · theme.hpp
//  Professional terminal theme (Tokyo Night inspired).
//
//  Auto-detects capability:
//    TrueColor  → 24-bit ANSI   (COLORTERM=truecolor)
//    Ansi256    → 256-color     (TERM=*-256color)
//    Basic      → 8-color ANSI  (tty fallback)
//    None       → no escapes    (pipe / file)
//
//  Unicode symbols auto-disable when:
//    TERM=dumb  OR  BIRUN_ASCII=1
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace birun::theme {

// ------------------------------------------------------------
//  Capability detection
// ------------------------------------------------------------
enum class Level { None, Basic, Ansi256, TrueColor };

inline Level g_level   = Level::None;
inline bool  g_unicode = true;

inline void detect() {
    const char* ct   = std::getenv("COLORTERM");
    const char* term = std::getenv("TERM");

    if (ct && (std::strcmp(ct, "truecolor") == 0 ||
               std::strcmp(ct, "24bit")    == 0))
        g_level = Level::TrueColor;
    else if (term && std::strstr(term, "256color"))
        g_level = Level::Ansi256;
    else if (isatty(fileno(stdout)))
        g_level = Level::Basic;
    else
        g_level = Level::None;

    if (term && std::strcmp(term, "dumb") == 0) g_unicode = false;
    if (std::getenv("BIRUN_ASCII"))             g_unicode = false;
}

// ------------------------------------------------------------
//  Palette — Tokyo Night
// ------------------------------------------------------------
struct RGB { int r, g, b; };

namespace tokyo {
    constexpr RGB fg      {0xc0, 0xca, 0xf5};   // primary text
    constexpr RGB fgDim   {0x9a, 0xa5, 0xce};   // secondary
    constexpr RGB muted   {0x56, 0x5f, 0x89};   // comments / rules
    constexpr RGB blue    {0x7a, 0xa2, 0xf7};   // accent (task names)
    constexpr RGB cyan    {0x7d, 0xcf, 0xff};   // info
    constexpr RGB green   {0x9e, 0xce, 0x6a};   // success
    constexpr RGB red     {0xf7, 0x76, 0x8e};   // failure
    constexpr RGB yellow  {0xe0, 0xaf, 0x68};   // warning
    constexpr RGB orange  {0xff, 0x9e, 0x64};   // running
    constexpr RGB purple  {0xbb, 0x9a, 0xf7};   // special
}

// ------------------------------------------------------------
//  Colorize
// ------------------------------------------------------------
inline std::string fg(RGB c, const std::string& s) {
    char buf[48];
    switch (g_level) {
        case Level::TrueColor:
            std::snprintf(buf, sizeof buf,
                          "\033[38;2;%d;%d;%dm", c.r, c.g, c.b);
            return std::string(buf) + s + "\033[0m";

        case Level::Ansi256: {
            // nearest 6x6x6 cube index
            int r = c.r / 51, g = c.g / 51, b = c.b / 51;
            int idx = 16 + 36 * r + 6 * g + b;
            std::snprintf(buf, sizeof buf, "\033[38;5;%dm", idx);
            return std::string(buf) + s + "\033[0m";
        }
        case Level::Basic: {
            if (c.r > 200 && c.g < 150 && c.b < 150) return "\033[31m" + s + "\033[0m";
            if (c.g > 180 && c.r < 180)              return "\033[32m" + s + "\033[0m";
            if (c.r > 200 && c.g > 170 && c.b < 150) return "\033[33m" + s + "\033[0m";
            if (c.b > 200 && c.r < 180)              return "\033[34m" + s + "\033[0m";
            if (c.r > 170 && c.b > 200)              return "\033[35m" + s + "\033[0m";
            if (c.g > 180 && c.b > 200)              return "\033[36m" + s + "\033[0m";
            return s;
        }
        default: return s;
    }
}

inline std::string bold  (const std::string& s) {
    return g_level == Level::None ? s : "\033[1m" + s + "\033[0m";
}
inline std::string dim   (const std::string& s) {
    return g_level == Level::None ? s : "\033[2m" + s + "\033[0m";
}
inline std::string italic(const std::string& s) {
    return g_level == Level::None ? s : "\033[3m" + s + "\033[0m";
}

// ------------------------------------------------------------
//  Semantic helpers
// ------------------------------------------------------------
inline std::string text   (const std::string& s) { return fg(tokyo::fg,     s); }
inline std::string hint   (const std::string& s) { return fg(tokyo::fgDim,  s); }
inline std::string rule   (const std::string& s) { return fg(tokyo::muted,  s); }
inline std::string accent (const std::string& s) { return fg(tokyo::blue,   s); }
inline std::string info   (const std::string& s) { return fg(tokyo::cyan,   s); }
inline std::string success(const std::string& s) { return fg(tokyo::green,  s); }
inline std::string danger (const std::string& s) { return fg(tokyo::red,    s); }
inline std::string warn   (const std::string& s) { return fg(tokyo::yellow, s); }
inline std::string running(const std::string& s) { return fg(tokyo::orange, s); }
inline std::string special(const std::string& s) { return fg(tokyo::purple, s); }

inline std::string name   (const std::string& s) { return bold(accent(s)); }

// ------------------------------------------------------------
//  Symbols
// ------------------------------------------------------------
namespace sym {
    inline const char* ok()      { return g_unicode ? "✓" : "OK";  }
    inline const char* fail()    { return g_unicode ? "✗" : "XX";  }
    inline const char* running() { return g_unicode ? "●" : "*";   }
    inline const char* idle()    { return g_unicode ? "○" : "o";   }
    inline const char* arrow()   { return g_unicode ? "→" : "->";  }
    inline const char* bullet()  { return g_unicode ? "•" : "-";   }
    inline const char* branch()  { return g_unicode ? "├─" : "|-"; }
    inline const char* last()    { return g_unicode ? "└─" : "\\-";}
    inline const char* vbar()    { return g_unicode ? "│" : "|";   }
    inline const char* hbar()    { return g_unicode ? "─" : "-";   }
    inline const char* spark()   { return g_unicode ? "⚡" : "!";   }
    inline const char* clock()   { return g_unicode ? "⏱" : "@";   }
    inline const char* dot()     { return g_unicode ? "·" : ".";   }
}

// ------------------------------------------------------------
//  Format helpers
// ------------------------------------------------------------
inline std::string duration(double seconds) {
    char buf[32];
    if (seconds < 1.0)     std::snprintf(buf, sizeof buf, "%dms", (int)(seconds * 1000));
    else if (seconds < 60) std::snprintf(buf, sizeof buf, "%.2fs", seconds);
    else                   std::snprintf(buf, sizeof buf, "%dm%ds",
                                          (int)(seconds / 60),
                                          (int)seconds % 60);
    return hint(std::string(buf));
}

// Left-pad task name to fixed column, so output aligns
inline std::string padName(const std::string& n, size_t col = 24) {
    if (n.size() >= col) return n;
    return n + std::string(col - n.size(), ' ');
}

} // namespace birun::theme