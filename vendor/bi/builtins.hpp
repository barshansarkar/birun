#pragma once
// birun: minimal builtins stub — replaces the full bi builtins.hpp
// We only keep what birun needs for config logic.

#include "value.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace bi {

// Thread-local callback hook (used by array methods like map/filter)
inline thread_local std::function<Value(const Value&, ValueList&)> g_callFn;

// Helper to register a global native
inline void def(std::shared_ptr<Env> g, const std::string& name, NativeFn fn) {
    g->define(name, makeNative(std::move(fn)));
}

// Small string helpers
inline std::string upper(const std::string& s) {
    std::string o = s;
    for (auto& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}
inline std::string lower(const std::string& s) {
    std::string o = s;
    for (auto& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}
inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Minimal member access: map fields + .length
inline Value getMember(const Value& o, const std::string& name) {
    if (o.type == Value::MAP) {
        auto& m = *o.mapPtr();
        auto it = m.find(name);
        if (it != m.end()) return it->second;
        if (name == "length") return vint((long long)m.size());
        return vnil();
    }
    if (name == "length") {
        if (o.type == Value::ARR) return vint((long long)o.arrPtr()->size());
        if (o.type == Value::STR) return vint((long long)utf8::length(o.strView()));
    }
    return vnil();
}

// Method lookup — return nullptr (no methods needed for birun config)
inline const NativeFn* lookupMethod(const Value&, const std::string&) {
    return nullptr;
}

// ============================================================
//  Minimal builtins for birun config logic
// ============================================================
inline void registerBuiltins(std::shared_ptr<Env> g) {

    def(g, "print", [](ValueList& a) {
        for (size_t i = 0; i < a.size(); i++) {
            if (i) std::cout << ' ';
            std::cout << toStr(a[i]);
        }
        std::cout << '\n';
        return vnil();
    });

    def(g, "len", [](ValueList& a) {
        if (a.empty()) return vint(0);
        if (a[0].type == Value::STR) return vint((long long)utf8::length(a[0].strView()));
        if (a[0].type == Value::ARR) return vint((long long)a[0].arrPtr()->size());
        if (a[0].type == Value::MAP) return vint((long long)a[0].mapPtr()->size());
        return vint(0);
    });

    def(g, "str", [](ValueList& a) {
        return vstr(a.empty() ? std::string_view{} : std::string_view(toStr(a[0])));
    });
    def(g, "num", [](ValueList& a) {
        return vnum(a.empty() ? 0.0 : toNum(a[0]));
    });
    def(g, "int", [](ValueList& a) {
        return vint(a.empty() ? 0LL : toInt(a[0]));
    });
    def(g, "bool", [](ValueList& a) {
        return vbool(!a.empty() && truthy(a[0]));
    });
    def(g, "type", [](ValueList& a) {
        return vstr(a.empty() ? "null" : typeName(a[0]));
    });
    def(g, "upper", [](ValueList& a) {
        return vstr(upper(std::string(a[0].strView())));
    });
    def(g, "lower", [](ValueList& a) {
        return vstr(lower(std::string(a[0].strView())));
    });
    def(g, "trim", [](ValueList& a) {
        return vstr(trim(std::string(a[0].strView())));
    });

    // env("NAME") / env("NAME", default) — read from process environment
    def(g, "env", [](ValueList& a) {
        if (a.empty()) return vstr("");
        std::string name(a[0].strView());
        const char* v = std::getenv(name.c_str());
        if (v) return vstr(v);
        return (a.size() > 1) ? a[1] : vnil();
    });

    def(g, "time", [](ValueList&) {
        using namespace std::chrono;
        return vnum((double)duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count() / 1000.0);
    });

    // range(n) / range(a, b) / range(a, b, step)
    def(g, "range", [](ValueList& a) {
        long long s = 0, e = 0, st = 1;
        if (a.size() == 1) {
            e = toInt(a[0]);
        } else if (a.size() >= 2) {
            s = toInt(a[0]); e = toInt(a[1]);
            if (a.size() > 2) st = toInt(a[2]);
        }
        if (st == 0) st = 1;
        auto out = std::make_shared<ValueList>();
        for (long long i = s; st > 0 ? i < e : i > e; i += st)
            out->push_back(vint(i));
        return varr(std::move(out));
    });
}

} // namespace bi
