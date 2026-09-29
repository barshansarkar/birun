#pragma once
#include "ast.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bi {

struct Value;
struct Function;
struct Env;

// ============================================================
//  Insertion-order map with O(1) hash index
// ============================================================
template <typename V>
class OrderedMapT {
public:
    using Pair      = std::pair<std::string, V>;
    using Container = std::vector<Pair>;
    using iterator       = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;

    iterator       begin()       { return items_.begin(); }
    iterator       end()         { return items_.end(); }
    const_iterator begin() const { return items_.begin(); }
    const_iterator end()   const { return items_.end(); }

    iterator find(const std::string& k) {
        auto it = idx_.find(k);
        if (it == idx_.end()) return items_.end();
        return items_.begin() + (ptrdiff_t)it->second;
    }
    const_iterator find(const std::string& k) const {
        auto it = idx_.find(k);
        if (it == idx_.end()) return items_.end();
        return items_.begin() + (ptrdiff_t)it->second;
    }
    size_t count(const std::string& k) const { return idx_.count(k); }
    size_t size()  const { return items_.size(); }
    bool   empty() const { return items_.empty(); }

    V& operator[](const std::string& k) {
        auto it = idx_.find(k);
        if (it != idx_.end()) return items_[it->second].second;
        items_.emplace_back(k, V{});
        idx_.emplace(k, items_.size() - 1);
        return items_.back().second;
    }

    size_t erase(const std::string& k) {
        auto it = idx_.find(k);
        if (it == idx_.end()) return 0;
        size_t pos = it->second;
        items_.erase(items_.begin() + (ptrdiff_t)pos);
        idx_.erase(it);
        // Reindex the tail (rare op, O(n) — fine)
        for (size_t i = pos; i < items_.size(); ++i)
            idx_[items_[i].first] = i;
        return 1;
    }

private:
    Container                               items_;
    std::unordered_map<std::string, size_t> idx_;
};

using ValueList = std::vector<Value>;
using ValueMap  = OrderedMapT<Value>;
using NativeFn  = std::function<Value(ValueList&)>;

// ============================================================
//  Signals
// ============================================================
struct ControlSignal {};

struct ServeSignal : ControlSignal {
    int port;
    explicit ServeSignal(int p) : port(p) {}
};

struct Frame {
    std::string function;
    std::string file;
    int         line = 0;
    int         col  = 0;
};

struct BiError : std::runtime_error {
    int line = 0;
    int col  = 0;
    std::string file;
    std::vector<Frame> trace;

    BiError(const std::string& msg, int ln = 0, int cl = 0,
            std::string f = {}, std::vector<Frame> tr = {})
        : std::runtime_error(msg),
          line(ln), col(cl), file(std::move(f)), trace(std::move(tr)) {}
};

struct Response {
    int status = 200;
    std::string contentType = "text/html; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;
};

inline thread_local Response* currentResponse = nullptr;

// ============================================================
//  Value — 32-byte tagged union, 22-char SSO
// ============================================================
struct Value {
    enum Type : uint8_t {
        NIL, INT, NUM, BOOL, STR, ARR, MAP, FUNC, NATIVE
    };

    static constexpr size_t kSSOLen = 22;

    Type type      = NIL;
    bool isHeapStr = false;
    uint8_t _pad[6] = {0,0,0,0,0,0};

    union {
        long long i;
        double    num;
        bool      boolean;
    };

    struct SsoStr {
        char    buf[kSSOLen];
        uint8_t len;
    };
    union Payload {
        SsoStr                sso;
        std::shared_ptr<void> ref;
        Payload()  { sso.len = 0; }
        ~Payload() {}
    } u;

    // ---- ctor/dtor ----
    Value() : type(NIL), isHeapStr(false), i(0) {}
    Value(const Value& o) { copyInit(o); }
    Value(Value&& o) noexcept { moveInit(std::move(o)); }
    Value& operator=(const Value& o) {
        if (this != &o) { this->~Value(); new (this) Value(o); }
        return *this;
    }
    Value& operator=(Value&& o) noexcept {
        if (this != &o) { this->~Value(); new (this) Value(std::move(o)); }
        return *this;
    }
    ~Value() { destroy(); }

    // ---- accessors ----
    std::string_view strView() const {
        if (type != STR) return {};
        if (isHeapStr) return *static_cast<const std::string*>(u.ref.get());
        return std::string_view(u.sso.buf, u.sso.len);
    }
    std::string strVal() const {
        if (type != STR) return {};
        if (isHeapStr) return *static_cast<const std::string*>(u.ref.get());
        return std::string(u.sso.buf, u.sso.len);
    }
    size_t strLen() const { return strView().size(); }

    void setStr(std::string_view s) {
        destroy();
        type = STR;
        if (s.size() <= kSSOLen) {
            std::memcpy(u.sso.buf, s.data(), s.size());
            u.sso.len = (uint8_t)s.size();
            isHeapStr = false;
        } else {
            new (&u.ref) std::shared_ptr<void>(std::make_shared<std::string>(s));
            isHeapStr = true;
        }
    }

    const NativeFn* nativePtr() const {
        return static_cast<const NativeFn*>(u.ref.get());
    }
    std::shared_ptr<ValueList> arrPtr() const {
        return std::static_pointer_cast<ValueList>(u.ref);
    }
    std::shared_ptr<ValueMap> mapPtr() const {
        return std::static_pointer_cast<ValueMap>(u.ref);
    }
    std::shared_ptr<Function> funcPtr() const {
        return std::static_pointer_cast<Function>(u.ref);
    }

private:
    void destroy() {
        switch (type) {
            case STR:  if (isHeapStr) u.ref.~shared_ptr(); break;
            case ARR: case MAP: case FUNC: case NATIVE:
                u.ref.~shared_ptr(); break;
            default: break;
        }
        type = NIL;
        isHeapStr = false;
        i = 0;
    }
    void copyInit(const Value& o) {
        type = o.type;
        isHeapStr = o.isHeapStr;
        i = o.i;
        switch (o.type) {
            case STR:
                if (o.isHeapStr) new (&u.ref) std::shared_ptr<void>(o.u.ref);
                else             u.sso = o.u.sso;
                break;
            case ARR: case MAP: case FUNC: case NATIVE:
                new (&u.ref) std::shared_ptr<void>(o.u.ref);
                break;
            default: u.sso.len = 0; break;
        }
    }
    void moveInit(Value&& o) noexcept {
        type = o.type;
        isHeapStr = o.isHeapStr;
        i = o.i;
        switch (o.type) {
            case STR:
                if (o.isHeapStr) new (&u.ref) std::shared_ptr<void>(std::move(o.u.ref));
                else             u.sso = o.u.sso;
                break;
            case ARR: case MAP: case FUNC: case NATIVE:
                new (&u.ref) std::shared_ptr<void>(std::move(o.u.ref));
                break;
            default: u.sso.len = 0; break;
        }
        o.type = NIL;
        o.isHeapStr = false;
    }
};

// ============================================================
//  Factories
// ============================================================
inline Value vnil()  { return Value{}; }
inline Value vint(long long i) {
    Value v; v.type = Value::INT; v.i = i; return v;
}
inline Value vnum(double d) {
    Value v; v.type = Value::NUM; v.num = d; return v;
}
inline Value vbool(bool b) {
    Value v; v.type = Value::BOOL; v.boolean = b; return v;
}
inline Value vstr(std::string_view s) {
    Value v; v.setStr(s); return v;
}
inline Value varr(std::shared_ptr<ValueList> a) {
    Value v; v.type = Value::ARR;
    new (&v.u.ref) std::shared_ptr<void>(std::move(a));
    return v;
}
inline Value vmap(std::shared_ptr<ValueMap> m) {
    Value v; v.type = Value::MAP;
    new (&v.u.ref) std::shared_ptr<void>(std::move(m));
    return v;
}
inline Value makeNative(NativeFn fn) {
    Value v; v.type = Value::NATIVE;
    new (&v.u.ref) std::shared_ptr<void>(std::make_shared<NativeFn>(std::move(fn)));
    return v;
}
inline Value vfunc(std::shared_ptr<Function> f) {
    Value v; v.type = Value::FUNC;
    new (&v.u.ref) std::shared_ptr<void>(std::move(f));
    return v;
}

struct Function {
    std::string              name;
    std::vector<std::string> params;
    std::vector<StmtPtr>     body;
    std::shared_ptr<Env>     closure;

    bool        isRoute = false;
    std::string routeMethod = "GET";
    std::string routePath   = "/";
    std::string sourceFile;
};

struct Env : std::enable_shared_from_this<Env> {
    std::shared_ptr<Env> parent;
    ValueMap             vars;

    std::shared_ptr<Env> child() {
        auto e = std::make_shared<Env>();
        e->parent = shared_from_this();
        return e;
    }
    Value* find(const std::string& n) {
        auto it = vars.find(n);
        if (it != vars.end()) return &it->second;
        return parent ? parent->find(n) : nullptr;
    }
    void define(const std::string& n, Value v) { vars[n] = std::move(v); }
    void assign(const std::string& n, Value v) {
        Value* p = find(n);
        if (p) *p = std::move(v);
        else   vars[n] = std::move(v);
    }
};

// ============================================================
//  Helpers
// ============================================================
inline std::string typeName(const Value& v) {
    switch (v.type) {
        case Value::NIL:    return "null";
        case Value::INT:    return "int";
        case Value::NUM:    return "number";
        case Value::BOOL:   return "bool";
        case Value::STR:    return "string";
        case Value::ARR:    return "array";
        case Value::MAP:    return "map";
        case Value::FUNC:
        case Value::NATIVE: return "function";
    }
    return "?";
}

inline bool truthy(const Value& v) {
    switch (v.type) {
        case Value::NIL:  return false;
        case Value::BOOL: return v.boolean;
        case Value::INT:  return v.i != 0;
        case Value::NUM:  return v.num != 0;
        case Value::STR:  return v.strLen() != 0;
        default:          return true;
    }
}

inline double toNum(const Value& v) {
    switch (v.type) {
        case Value::NUM:  return v.num;
        case Value::INT:  return (double)v.i;
        case Value::BOOL: return v.boolean ? 1 : 0;
        case Value::NIL:  return 0;
        case Value::STR: {
            auto sv = v.strView();
            try { return std::stod(std::string(sv)); } catch (...) { return 0; }
        }
        default: return 0;
    }
}

inline long long toInt(const Value& v) {
    switch (v.type) {
        case Value::INT:  return v.i;
        case Value::NUM:  return (long long)v.num;
        case Value::BOOL: return v.boolean ? 1 : 0;
        case Value::NIL:  return 0;
        case Value::STR: {
            auto sv = v.strView();
            try { return std::stoll(std::string(sv)); } catch (...) { return 0; }
        }
        default: return 0;
    }
}

inline std::string toStr(const Value& v) {
    switch (v.type) {
        case Value::NIL:  return "null";
        case Value::BOOL: return v.boolean ? "true" : "false";
        case Value::INT:  return std::to_string(v.i);
        case Value::NUM: {
            double d = v.num;
            if (std::fabs(d) < 9.0e15 && d == (double)(long long)d)
                return std::to_string((long long)d);
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.17g", d);
            return buf;
        }
        case Value::STR: return v.strVal();
        case Value::ARR: {
            std::string out = "[";
            const auto& a = *v.arrPtr();
            for (size_t i = 0; i < a.size(); i++) {
                if (i) out += ", ";
                const Value& x = a[i];
                if (x.type == Value::STR) { out += '"'; out += x.strVal(); out += '"'; }
                else                      out += toStr(x);
            }
            return out + "]";
        }
        case Value::MAP: {
            std::string out = "{";
            bool first = true;
            for (auto& kv : *v.mapPtr()) {
                if (!first) out += ", ";
                first = false;
                out += kv.first + ": " + toStr(kv.second);
            }
            return out + "}";
        }
        case Value::FUNC:   return "<fn " + (v.funcPtr() ? v.funcPtr()->name : std::string("?")) + ">";
        case Value::NATIVE: return "<native fn>";
    }
    return "";
}

inline bool valueEquals(const Value& a, const Value& b) {
    // numeric cross-compare
    bool aNum = (a.type == Value::NUM || a.type == Value::INT);
    bool bNum = (b.type == Value::NUM || b.type == Value::INT);
    if (aNum && bNum) {
        if (a.type == Value::INT && b.type == Value::INT) return a.i == b.i;
        return toNum(a) == toNum(b);
    }
    if (a.type != b.type) return false;
    switch (a.type) {
        case Value::NIL:  return true;
        case Value::INT:  return a.i == b.i;
        case Value::NUM:  return a.num == b.num;
        case Value::BOOL: return a.boolean == b.boolean;
        case Value::STR:  return a.strView() == b.strView();
        case Value::ARR: {
            auto& x = *a.arrPtr(); auto& y = *b.arrPtr();
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); i++)
                if (!valueEquals(x[i], y[i])) return false;
            return true;
        }
        case Value::MAP: {
            auto& x = *a.mapPtr(); auto& y = *b.mapPtr();
            if (x.size() != y.size()) return false;
            for (auto& kv : x) {
                auto it = y.find(kv.first);
                if (it == y.end() || !valueEquals(kv.second, it->second)) return false;
            }
            return true;
        }
        default: return a.u.ref == b.u.ref;
    }
}

// ---- deepCopy: value semantics for local bindings ----
// Kept for the `clone()` builtin. NOT called on every `let` anymore.
inline Value deepCopy(const Value& v) {
    switch (v.type) {
        case Value::ARR: {
            const auto& src = *v.arrPtr();
            auto a = std::make_shared<ValueList>();
            a->reserve(src.size());
            for (auto& x : src) a->push_back(deepCopy(x));
            return varr(std::move(a));
        }
        case Value::MAP: {
            auto m = std::make_shared<ValueMap>();
            for (auto& kv : *v.mapPtr()) (*m)[kv.first] = deepCopy(kv.second);
            return vmap(std::move(m));
        }
        default: return v;   // fast path: scalar / function pointer
    }
}

// ============================================================
//  UTF-8 helpers
// ============================================================
namespace utf8 {

inline size_t step(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

inline size_t length(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ) {
        size_t k = step((unsigned char)s[i]);
        if (i + k > s.size()) k = 1;
        i += k;
        n++;
    }
    return n;
}

inline size_t byteOffset(std::string_view s, size_t cp) {
    size_t i = 0, n = 0;
    while (i < s.size() && n < cp) {
        size_t k = step((unsigned char)s[i]);
        if (i + k > s.size()) k = 1;
        i += k;
        n++;
    }
    return i;
}

inline std::string charAt(std::string_view s, long long idx) {
    long long L = (long long)length(s);
    if (idx < 0) idx += L;
    if (idx < 0 || idx >= L) return "";
    size_t b = byteOffset(s, (size_t)idx);
    size_t k = step((unsigned char)s[b]);
    if (b + k > s.size()) k = 1;
    return std::string(s.substr(b, k));
}

inline std::string substr(std::string_view s, long long start, long long len) {
    long long L = (long long)length(s);
    if (start < 0) start += L;
    if (start < 0) start = 0;
    if (start >= L || len < 0) return "";
    long long end = start + len;
    if (end > L) end = L;
    size_t b = byteOffset(s, (size_t)start);
    size_t e = byteOffset(s, (size_t)end);
    return std::string(s.substr(b, e - b));
}

inline std::vector<std::string> chars(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); ) {
        size_t k = step((unsigned char)s[i]);
        if (i + k > s.size()) k = 1;
        out.emplace_back(s.substr(i, k));
        i += k;
    }
    return out;
}

} // namespace utf8

// ============================================================
//  JSON
// ============================================================
inline std::string toJson(const Value& v) {
    switch (v.type) {
        case Value::NIL:  return "null";
        case Value::BOOL: return v.boolean ? "true" : "false";
        case Value::INT:  return std::to_string(v.i);
        case Value::NUM: {
            double d = v.num;
            if (std::fabs(d) < 9.0e15 && d == (double)(long long)d)
                return std::to_string((long long)d);
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.17g", d);
            return buf;
        }
        case Value::STR: {
            std::string out = "\"";
            for (unsigned char c : v.strView()) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (c < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof buf, "\\u%04x", c);
                            out += buf;
                        } else out += (char)c;
                }
            }
            return out + "\"";
        }
        case Value::ARR: {
            std::string out = "[";
            const auto& a = *v.arrPtr();
            for (size_t i = 0; i < a.size(); i++) {
                if (i) out += ",";
                out += toJson(a[i]);
            }
            return out + "]";
        }
        case Value::MAP: {
            std::string out = "{";
            bool first = true;
            for (auto& kv : *v.mapPtr()) {
                if (!first) out += ",";
                first = false;
                out += "\"" + kv.first + "\":" + toJson(kv.second);
            }
            return out + "}";
        }
        default: return "null";
    }
}

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    explicit JsonParser(const std::string& src) : s(src) {}

    void ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++; }
    [[noreturn]] void err(const char* m) { throw std::runtime_error(std::string("JSON: ") + m); }

    void expect(const char* lit) {
        size_t n = std::strlen(lit);
        if (s.compare(i, n, lit) != 0) err("bad literal");
        i += n;
    }

    Value parse() { ws(); Value v = value(); ws(); return v; }

    Value value() {
        ws();
        if (i >= s.size()) err("unexpected end");
        char c = s[i];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return vstr(strv());
        if (c == 't') { expect("true");  return vbool(true); }
        if (c == 'f') { expect("false"); return vbool(false); }
        if (c == 'n') { expect("null");  return vnil(); }
        return number();
    }

    std::string strv() {
        if (s[i] != '"') err("expected string");
        i++;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/';  break;
                    case '"': out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        if (i + 4 > s.size()) err("bad \\u");
                        int cp = (int)std::strtol(s.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += e;
                }
            } else out += c;
        }
        if (i >= s.size()) err("unterminated string");
        i++;
        return out;
    }

    Value number() {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        bool isFloat = false;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i]=='.' ||
                                s[i]=='e' || s[i]=='E' || s[i]=='-' || s[i]=='+')) {
            if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') isFloat = true;
            i++;
        }
        if (start == i) err("bad number");
        std::string tok = s.substr(start, i - start);
        if (!isFloat) {
            try { return vint(std::stoll(tok)); } catch (...) {}
        }
        return vnum(std::stod(tok));
    }

    Value object() {
        i++;
        auto m = std::make_shared<ValueMap>();
        ws();
        if (i < s.size() && s[i] == '}') { i++; return vmap(m); }
        for (;;) {
            ws();
            std::string k = strv();
            ws();
            if (i >= s.size() || s[i] != ':') err("expected ':'");
            i++;
            (*m)[k] = value();
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; break; }
            err("expected ',' or '}'");
        }
        return vmap(m);
    }

    Value array() {
        i++;
        auto a = std::make_shared<ValueList>();
        ws();
        if (i < s.size() && s[i] == ']') { i++; return varr(a); }
        for (;;) {
            a->push_back(value());
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; break; }
            err("expected ',' or ']'");
        }
        return varr(a);
    }
};

inline Value parseJson(const std::string& s) {
    JsonParser p(s);
    return p.parse();
}

} // namespace bi