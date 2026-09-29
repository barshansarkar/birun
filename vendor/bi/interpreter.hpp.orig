#pragma once
#include "ast.hpp"
#include "builtins.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "value.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace bi {

// ReturnSignal is kept ONLY for the top-level "return outside function" error
// and for API compatibility with main.cpp / repl.hpp. It is NOT thrown inside
// function bodies anymore.
struct ReturnSignal : ControlSignal {
    Value value;
    explicit ReturnSignal(Value v) : value(std::move(v)) {}
};
struct BreakSignal    : ControlSignal {};
struct ContinueSignal : ControlSignal {};
struct ThrowSignal    : ControlSignal {
    Value value;
    explicit ThrowSignal(Value v) : value(std::move(v)) {}
};

class Interpreter {
public:
    static constexpr size_t kMaxDepth = 2000;

    Interpreter() {
        global_ = std::make_shared<Env>();
        g_callFn = [this](const Value& f, ValueList& a) { return call(f, a); };
        frames_.reserve(kMaxDepth);
        registerBuiltins(global_);
    }

    std::shared_ptr<Env> globals() const { return global_; }
    const std::vector<std::shared_ptr<Function>>& routes() const { return routes_; }
    std::string baseDir() const { return baseDir_; }
    const std::string& currentFile() const { return currentFile_; }
    int currentLine() const { return currentLine_; }
    int currentCol()  const { return currentCol_; }
    const std::vector<Frame>& frames() const { return frames_; }

    void runFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open file: " + path);
        std::stringstream ss; ss << f.rdbuf();
        auto slash = path.find_last_of("/\\");
        baseDir_ = (slash == std::string::npos) ? "." : path.substr(0, slash);
        runSource(ss.str(), path);
    }

    void runSource(const std::string& src, const std::string& file) {
        currentFile_ = file;
        Lexer  lx(src, file);
        Parser ps(lx.run(), file);
        Program prog = ps.parse();

        hasRet_ = false;
        for (auto& s : prog.body) {
            exec(s, global_);
            if (hasRet_) {
                // Top-level `return` — surface it as the classic error.
                Value v = std::move(retVal_);
                hasRet_ = false;
                retVal_ = vnil();
                throw ReturnSignal{ std::move(v) };
            }
        }
    }

    std::pair<bool, Value> runSourceRepl(const std::string& src, const std::string& file) {
        currentFile_ = file;
        Lexer  lx(src, file);
        Parser ps(lx.run(), file);
        Program prog = ps.parse();

        bool hasValue = false;
        Value last = vnil();
        hasRet_ = false;
        for (size_t i = 0; i < prog.body.size(); i++) {
            auto& s = prog.body[i];
            if (s->kind == SK::Expr && i + 1 == prog.body.size()) {
                last = eval(s->expr, global_);
                hasValue = true;
            } else {
                exec(s, global_);
                if (hasRet_) {
                    Value v = std::move(retVal_);
                    hasRet_ = false;
                    retVal_ = vnil();
                    throw ReturnSignal{ std::move(v) };
                }
            }
        }
        return { hasValue, last };
    }

    // ---- calls ----
    Value call(const Value& fn, ValueList& args) {
        if (fn.type == Value::NATIVE) {
            return (*fn.nativePtr())(args);
        }
        if (fn.type == Value::FUNC) {
            auto f = fn.funcPtr();
            if (frames_.size() >= kMaxDepth) {
                auto trace = frames_;
                throw BiError("stack overflow (max depth " + std::to_string(kMaxDepth) + ")",
                              currentLine_, currentCol_, currentFile_, std::move(trace));
            }
            frames_.push_back({ f->name, currentFile_, currentLine_, currentCol_ });
            try {
                Value r = callFunction(f, args);
                frames_.pop_back();
                return r;
            } catch (...) {
                frames_.pop_back();
                throw;
            }
        }
        throw std::runtime_error("attempt to call a " + typeName(fn));
    }

    Value callFunction(std::shared_ptr<Function> f, ValueList& args) {
        auto env = f->closure->child();
        const size_t np = f->params.size();
        const size_t na = args.size();

        // Move args into params — safe: caller's `args` isn't reused after.
        for (size_t i = 0; i < np; i++)
            env->define(f->params[i], i < na ? std::move(args[i]) : vnil());

        if (np > 0 && na > np) {
            auto rest = std::make_shared<ValueList>();
            rest->reserve(na - np);
            for (size_t i = np; i < na; i++) rest->push_back(std::move(args[i]));
            env->define("args", varr(std::move(rest)));
        }

        return callFunctionBody(f->body, env);
    }

    // Public so http.hpp / future callers can drive a body without exceptions.
    Value callFunctionBody(const std::vector<StmtPtr>& body, std::shared_ptr<Env> env) {
        bool  savedHasRet = hasRet_;
        Value savedRet    = std::move(retVal_);
        hasRet_ = false;
        retVal_ = vnil();

        try {
            for (auto& s : body) {
                exec(s, env);
                if (hasRet_) break;
            }
        } catch (...) {
            hasRet_ = savedHasRet;
            retVal_ = std::move(savedRet);
            throw;
        }

        Value result = hasRet_ ? std::move(retVal_) : vnil();
        hasRet_ = savedHasRet;
        retVal_ = std::move(savedRet);
        return result;
    }

    // ---- exec / eval ----
    void exec(StmtPtr s, std::shared_ptr<Env> env) {
        int prevLine = currentLine_;
        int prevCol  = currentCol_;
        currentLine_ = s->line;
        currentCol_  = s->col;
        try {
            execImpl(s, env);
            currentLine_ = prevLine;
            currentCol_  = prevCol;
        } catch (BiError&) {
            currentLine_ = prevLine; currentCol_ = prevCol; throw;
        } catch (ControlSignal&) {
            currentLine_ = prevLine; currentCol_ = prevCol; throw;
        } catch (std::exception& ex) {
            int ln = s->line, cl = s->col;
            auto trace = frames_;
            auto file = currentFile_;
            currentLine_ = prevLine; currentCol_ = prevCol;
            throw BiError(ex.what(), ln, cl, file, std::move(trace));
        }
    }

    Value eval(ExprPtr e, std::shared_ptr<Env> env) {
        int prevLine = currentLine_;
        int prevCol  = currentCol_;
        currentLine_ = e->line;
        currentCol_  = e->col;
        try {
            Value v = evalImpl(e, env);
            currentLine_ = prevLine;
            currentCol_  = prevCol;
            return v;
        } catch (BiError&) {
            currentLine_ = prevLine; currentCol_ = prevCol; throw;
        } catch (ControlSignal&) {
            currentLine_ = prevLine; currentCol_ = prevCol; throw;
        } catch (std::exception& ex) {
            int ln = e->line, cl = e->col;
            auto trace = frames_;
            auto file = currentFile_;
            currentLine_ = prevLine; currentCol_ = prevCol;
            throw BiError(ex.what(), ln, cl, file, std::move(trace));
        }
    }

    void execBlock(const std::vector<StmtPtr>& body, std::shared_ptr<Env> env) {
        auto scope = env->child();
        for (auto& s : body) {
            exec(s, scope);
            if (hasRet_) return;
        }
    }

    ValueMap* exportSink_ = nullptr;

private:
    std::shared_ptr<Env> global_;
    std::string          baseDir_ = ".";
    std::string          currentFile_;
    int                  currentLine_ = 0;
    int                  currentCol_  = 0;
    std::vector<Frame>   frames_;
    std::vector<std::shared_ptr<Function>> routes_;
    std::map<std::string, Value> moduleCache_;

    // Return propagation without exceptions.
    Value retVal_;
    bool  hasRet_ = false;

    Value evalImpl(ExprPtr e, std::shared_ptr<Env> env) {
        switch (e->kind) {
            case EK::Int:  return vint(e->inum);
            case EK::Num:  return vnum(e->num);
            case EK::Str:  return vstr(e->str);
            case EK::Bool: return vbool(e->boolean);
            case EK::Nil:  return vnil();

            case EK::Ident: {
                Value* p = env->find(e->str);
                if (!p) throw std::runtime_error("undefined variable '" + e->str + "'");
                return *p;
            }

            case EK::Array: {
                auto list = std::make_shared<ValueList>();
                list->reserve(e->items.size());
                for (auto& it : e->items) list->push_back(eval(it, env));
                return varr(std::move(list));
            }

            case EK::Map: {
                auto m = std::make_shared<ValueMap>();
                for (auto& kv : e->fields) (*m)[kv.first] = eval(kv.second, env);
                return vmap(std::move(m));
            }

            case EK::Unary: {
                Value a = eval(e->a, env);
                if (e->op == "!") return vbool(!truthy(a));
                if (e->op == "-") {
                    if (a.type == Value::INT) return vint(-a.i);
                    return vnum(-toNum(a));
                }
                throw std::runtime_error("bad unary operator " + e->op);
            }

            case EK::Binary:  return evalBinary(e, env);
            case EK::Assign:  return evalAssign(e, env);
            case EK::Member:  return getMember(eval(e->a, env), e->str);

            case EK::Index: {
                Value o = eval(e->a, env);
                Value k = eval(e->b, env);
                return index(o, k);
            }

            case EK::Call: {
                if (e->a->kind == EK::Member) {
                    Value obj = eval(e->a->a, env);
                    const NativeFn* m = lookupMethod(obj, e->a->str);
                    if (m) {
                        ValueList args;
                        args.reserve(1 + e->items.size());
                        args.push_back(std::move(obj));
                        for (auto& a : e->items) args.push_back(eval(a, env));
                        return (*m)(args);
                    }
                    Value mv = getMember(obj, e->a->str);
                    if (mv.type == Value::NIL) {
                        throw std::runtime_error(
                            "no method or property '" + e->a->str +
                            "' on value of type " + typeName(obj));
                    }
                    ValueList args;
                    args.reserve(e->items.size());
                    for (auto& a : e->items) args.push_back(eval(a, env));
                    return call(mv, args);
                }
                Value callee = eval(e->a, env);
                ValueList args;
                args.reserve(e->items.size());
                for (auto& a : e->items) args.push_back(eval(a, env));
                return call(callee, args);
            }

            case EK::Func: {
                auto f = std::make_shared<Function>();
                f->name       = "lambda";
                f->params     = e->params;
                f->body       = e->body;
                f->closure    = env;
                f->sourceFile = currentFile_;
                return vfunc(std::move(f));
            }
        }
        return vnil();
    }

    void execImpl(StmtPtr s, std::shared_ptr<Env> env) {
        switch (s->kind) {

            case SK::Let: {
                Value v = s->expr ? eval(s->expr, env) : vnil();
                env->define(s->name, std::move(v));
                if (s->exported && exportSink_) (*exportSink_)[s->name] = *env->find(s->name);
                break;
            }

            case SK::Expr: eval(s->expr, env); break;
            case SK::Block: execBlock(s->body, env); break;

            case SK::If: {
                if (truthy(eval(s->expr, env))) execBlock(s->body, env);
                else if (!s->alt.empty())       execBlock(s->alt, env);
                break;
            }

            case SK::While: {
                while (truthy(eval(s->expr, env))) {
                    try { execBlock(s->body, env); }
                    catch (BreakSignal&)    { break; }
                    catch (ContinueSignal&) { continue; }
                    if (hasRet_) break;
                }
                break;
            }

            case SK::For: {
                auto scope = env->child();
                if (s->init) exec(s->init, scope);
                for (;;) {
                    if (hasRet_) break;
                    if (s->cond && !truthy(eval(s->cond, scope))) break;
                    bool brk = false;
                    try { execBlock(s->body, scope); }
                    catch (BreakSignal&)    { brk = true; }
                    catch (ContinueSignal&) {}
                    if (brk)   break;
                    if (hasRet_) break;
                    if (s->step) eval(s->step, scope);
                }
                break;
            }

            case SK::ForIn: {
                Value it = eval(s->expr, env);
                auto scope = env->child();
                auto iter = [&](const Value& v) {
                    scope->define(s->name, v);
                    try { execBlock(s->body, scope); return true; }
                    catch (BreakSignal&)    { return false; }
                    catch (ContinueSignal&) { return true; }
                };
                if (it.type == Value::ARR) {
                    for (auto& x : *it.arrPtr()) { if (!iter(x)) break; if (hasRet_) break; }
                } else if (it.type == Value::MAP) {
                    for (auto& kv : *it.mapPtr()) { if (!iter(vstr(kv.first))) break; if (hasRet_) break; }
                } else if (it.type == Value::STR) {
                    for (auto& c : utf8::chars(it.strView())) { if (!iter(vstr(c))) break; if (hasRet_) break; }
                }
                break;
            }

            case SK::Return:
                retVal_ = s->expr ? eval(s->expr, env) : vnil();
                hasRet_ = true;
                return;   // <-- no throw

            case SK::Break:    throw BreakSignal{};
            case SK::Continue: throw ContinueSignal{};

            case SK::Func: {
                auto f = std::make_shared<Function>();
                f->name       = s->name;
                f->params     = s->params;
                f->body       = s->body;
                f->closure    = env;
                f->sourceFile = currentFile_;
                Value v = vfunc(std::move(f));
                env->define(s->name, v);
                if (s->exported && exportSink_) (*exportSink_)[s->name] = v;
                break;
            }

            case SK::Route: {
                auto f = std::make_shared<Function>();
                f->name        = "route " + s->name;
                f->body        = s->body;
                f->closure     = env;
                f->isRoute     = true;
                f->routeMethod = s->name;
                f->sourceFile  = currentFile_;
                Value pv = eval(s->expr, env);
                f->routePath = (pv.type == Value::STR) ? std::string(pv.strView()) : toStr(pv);
                routes_.push_back(std::move(f));
                break;
            }

            case SK::Import: {
                Value mod = importModule(s->name);
                std::string bind = s->alias.empty() ? moduleBase(s->name) : s->alias;
                env->define(bind, std::move(mod));
                break;
            }

            case SK::Try: {
                try {
                    execBlock(s->body, env);
                } catch (ThrowSignal& t) {
                    auto scope = env->child();
                    scope->define(s->name, std::move(t.value));
                    for (auto& st : s->alt) {
                        exec(st, scope);
                        if (hasRet_) return;
                    }
                } catch (BiError& e) {
                    auto scope = env->child();
                    scope->define(s->name, vstr(e.what()));
                    for (auto& st : s->alt) {
                        exec(st, scope);
                        if (hasRet_) return;
                    }
                }
                break;
            }

            case SK::Throw:
                throw ThrowSignal{ s->expr ? eval(s->expr, env) : vnil() };
        }
    }

    Value evalBinary(ExprPtr e, std::shared_ptr<Env> env) {
        const std::string& op = e->op;

        if (op == "&&") {
            Value a = eval(e->a, env);
            if (!truthy(a)) return vbool(false);
            return vbool(truthy(eval(e->b, env)));
        }
        if (op == "||") {
            Value a = eval(e->a, env);
            if (truthy(a)) return vbool(true);
            return vbool(truthy(eval(e->b, env)));
        }

        Value a = eval(e->a, env);
        Value b = eval(e->b, env);

        // Fast path: int-int
        if (a.type == Value::INT && b.type == Value::INT) {
            long long x = a.i, y = b.i;
            if (op == "+") return vint(x + y);
            if (op == "-") return vint(x - y);
            if (op == "*") return vint(x * y);
            if (op == "/") {
                if (y == 0) throw std::runtime_error("division by zero");
                return vint(x / y);
            }
            if (op == "%") {
                if (y == 0) throw std::runtime_error("modulo by zero");
                return vint(x % y);
            }
            if (op == "<")  return vbool(x <  y);
            if (op == "<=") return vbool(x <= y);
            if (op == ">")  return vbool(x >  y);
            if (op == ">=") return vbool(x >= y);
            if (op == "==") return vbool(x == y);
            if (op == "!=") return vbool(x != y);
        }

        if (op == "==") return vbool(valueEquals(a, b));
        if (op == "!=") return vbool(!valueEquals(a, b));

        if (op == "+") {
            if (a.type == Value::STR && b.type == Value::STR) {
                std::string s;
                s.reserve(a.strLen() + b.strLen());
                s += a.strView();
                s += b.strView();
                return vstr(std::move(s));
            }
            if (a.type == Value::STR || b.type == Value::STR)
                return vstr(std::string(toStr(a)) + toStr(b));
            if (a.type == Value::ARR && b.type == Value::ARR) {
                auto out = std::make_shared<ValueList>(*a.arrPtr());
                for (auto& x : *b.arrPtr()) out->push_back(x);
                return varr(std::move(out));
            }
            return vnum(toNum(a) + toNum(b));
        }
        if (op == "-") return vnum(toNum(a) - toNum(b));
        if (op == "*") {
            if (a.type == Value::STR && (b.type == Value::INT || b.type == Value::NUM)) {
                long long n = toInt(b);
                if (n <= 0) return vstr("");
                std::string out;
                out.reserve(a.strLen() * (size_t)n);
                for (long long i = 0; i < n; i++) out += a.strView();
                return vstr(std::move(out));
            }
            return vnum(toNum(a) * toNum(b));
        }
        if (op == "/") {
            double d = toNum(b);
            if (d == 0) throw std::runtime_error("division by zero");
            return vnum(toNum(a) / d);
        }
        if (op == "%") {
            double d = toNum(b);
            if (d == 0) throw std::runtime_error("modulo by zero");
            return vnum(std::fmod(toNum(a), d));
        }

        if (a.type == Value::STR && b.type == Value::STR) {
            int cmp = a.strView().compare(b.strView());
            if (op == "<")  return vbool(cmp <  0);
            if (op == "<=") return vbool(cmp <= 0);
            if (op == ">")  return vbool(cmp >  0);
            if (op == ">=") return vbool(cmp >= 0);
        }

        double x = toNum(a), y = toNum(b);
        if (op == "<")  return vbool(x <  y);
        if (op == "<=") return vbool(x <= y);
        if (op == ">")  return vbool(x >  y);
        if (op == ">=") return vbool(x >= y);

        throw std::runtime_error("unknown binary operator " + op);
    }

    static Value compound(const std::string& op, const Value& cur, const Value& b) {
        bool aI = cur.type == Value::INT;
        bool bI = b.type == Value::INT;
        if (op == "+=") {
            if (cur.type == Value::STR && b.type == Value::STR) {
                std::string s;
                s.reserve(cur.strLen() + b.strLen());
                s += cur.strView();
                s += b.strView();
                return vstr(std::move(s));
            }
            if (cur.type == Value::STR || b.type == Value::STR)
                return vstr(std::string(toStr(cur)) + toStr(b));
            if (cur.type == Value::ARR && b.type == Value::ARR) {
                auto out = std::make_shared<ValueList>(*cur.arrPtr());
                for (auto& x : *b.arrPtr()) out->push_back(x);
                return varr(std::move(out));
            }
            if (aI && bI) return vint(cur.i + b.i);
            return vnum(toNum(cur) + toNum(b));
        }
        if (op == "-=") {
            if (aI && bI) return vint(cur.i - b.i);
            return vnum(toNum(cur) - toNum(b));
        }
        if (op == "*=") {
            if (aI && bI) return vint(cur.i * b.i);
            return vnum(toNum(cur) * toNum(b));
        }
        if (op == "/=") {
            if (aI && bI) {
                if (b.i == 0) throw std::runtime_error("division by zero");
                return vint(cur.i / b.i);
            }
            if (toNum(b) == 0) throw std::runtime_error("division by zero");
            return vnum(toNum(cur) / toNum(b));
        }
        throw std::runtime_error("unknown compound operator " + op);
    }

    Value assignTo(ExprPtr target, Value rhs, std::shared_ptr<Env> env) {
        if (target->kind == EK::Ident) {
            env->assign(target->str, std::move(rhs));
            return rhs;
        }
        if (target->kind == EK::Member) {
            Value obj = eval(target->a, env);
            if (obj.type == Value::MAP) {
                (*obj.mapPtr())[target->str] = std::move(rhs);
                return rhs;
            }
            throw std::runtime_error("cannot assign property on " + typeName(obj));
        }
        if (target->kind == EK::Index) {
            Value obj = eval(target->a, env);
            Value k   = eval(target->b, env);
            if (obj.type == Value::ARR) {
                long long i = toInt(k);
                long long n = (long long)obj.arrPtr()->size();
                if (i < 0) i += n;
                if (i < 0 || i >= n)
                    throw std::runtime_error("array index out of range: " + std::to_string(i));
                (*obj.arrPtr())[(size_t)i] = std::move(rhs);
                return rhs;
            }
            if (obj.type == Value::MAP) {
                (*obj.mapPtr())[std::string(k.strView())] = std::move(rhs);
                return rhs;
            }
            throw std::runtime_error("cannot index-assign on " + typeName(obj));
        }
        throw std::runtime_error("invalid assignment target");
    }

    Value evalAssign(ExprPtr e, std::shared_ptr<Env> env) {
        if (e->op == "=") {
            Value rhs = eval(e->b, env);
            return assignTo(e->a, std::move(rhs), env);
        }
        if (e->a->kind == EK::Ident) {
            Value* p = env->find(e->a->str);
            if (!p) throw std::runtime_error("undefined variable '" + e->a->str + "'");
            Value b = eval(e->b, env);
            Value rhs = compound(e->op, *p, b);
            *p = std::move(rhs);
            return rhs;
        }
        if (e->a->kind == EK::Member) {
            Value obj = eval(e->a->a, env);
            if (obj.type != Value::MAP)
                throw std::runtime_error("cannot assign property on " + typeName(obj));
            Value cur = getMember(obj, e->a->str);
            Value b   = eval(e->b, env);
            Value rhs = compound(e->op, cur, b);
            (*obj.mapPtr())[e->a->str] = rhs;
            return rhs;
        }
        if (e->a->kind == EK::Index) {
            Value obj = eval(e->a->a, env);
            Value k   = eval(e->a->b, env);
            Value cur = index(obj, k);
            Value b   = eval(e->b, env);
            Value rhs = compound(e->op, cur, b);
            if (obj.type == Value::ARR) {
                long long i = toInt(k);
                long long n = (long long)obj.arrPtr()->size();
                if (i < 0) i += n;
                if (i < 0 || i >= n)
                    throw std::runtime_error("array index out of range: " + std::to_string(i));
                (*obj.arrPtr())[(size_t)i] = rhs;
                return rhs;
            }
            if (obj.type == Value::MAP) {
                (*obj.mapPtr())[std::string(k.strView())] = rhs;
                return rhs;
            }
            throw std::runtime_error("cannot index-assign on " + typeName(obj));
        }
        throw std::runtime_error("invalid assignment target");
    }

    Value index(const Value& o, const Value& k) {
        if (o.type == Value::ARR) {
            long long i = toInt(k);
            long long n = (long long)o.arrPtr()->size();
            if (i < 0) i += n;
            if (i < 0 || i >= n) return vnil();
            return (*o.arrPtr())[(size_t)i];
        }
        if (o.type == Value::MAP) {
            auto& m = *o.mapPtr();
            auto it = m.find(std::string(k.strView()));
            return it != m.end() ? it->second : vnil();
        }
        if (o.type == Value::STR) {
            return vstr(utf8::charAt(o.strView(), toInt(k)));
        }
        return vnil();
    }

    static std::string moduleBase(const std::string& path) {
        std::string p = path;
        auto slash = p.find_last_of('/');
        if (slash != std::string::npos) p = p.substr(slash + 1);
        if (p.size() > 3 && p.substr(p.size() - 3) == ".bi") p = p.substr(0, p.size() - 3);
        return p;
    }

    std::string resolveModule(const std::string& name) {
        if (name.rfind("./", 0) == 0 || name.rfind("../", 0) == 0) {
            std::string p = baseDir_ + "/" + name;
            if (p.size() < 3 || p.substr(p.size() - 3) != ".bi") p += ".bi";
            return p;
        }
        if (!name.empty() && name[0] == '/') {
            std::string p = name;
            if (p.size() < 3 || p.substr(p.size() - 3) != ".bi") p += ".bi";
            return p;
        }
        const std::string candidates[] = {
            "bi_modules/" + name + "/src/main.bi",
            "bi_modules/" + name + "/index.bi",
            "bi_modules/" + name + "/main.bi",
            "bi_modules/" + name + "/" + name + ".bi",
            name + ".bi",
        };
        for (auto& c : candidates) {
            std::ifstream f(c);
            if (f.good()) return c;
        }
        throw std::runtime_error("cannot resolve module '" + name + "'");
    }

    Value importModule(const std::string& name) {
        std::string path = resolveModule(name);
        auto cached = moduleCache_.find(path);
        if (cached != moduleCache_.end()) return cached->second;

        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open module: " + path);
        std::stringstream ss; ss << f.rdbuf();

        Lexer  lx(ss.str(), path);
        Parser ps(lx.run(), path);
        Program prog = ps.parse();

        auto mEnv = global_->child();
        auto exports = std::make_shared<ValueMap>();

        auto* prevSink = exportSink_;
        exportSink_ = exports.get();
        try {
            for (auto& s : prog.body) {
                exec(s, mEnv);
                if (hasRet_) { hasRet_ = false; retVal_ = vnil(); }
            }
        } catch (...) {
            exportSink_ = prevSink;
            throw;
        }
        exportSink_ = prevSink;

        Value mod = vmap(exports);
        moduleCache_[path] = mod;
        return mod;
    }
};

} // namespace bi