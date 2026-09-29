#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace bi {

class Parser {
public:
    Parser(std::vector<Token> toks, std::string file)
        : t_(std::move(toks)), file_(std::move(file)) {}

    Program parse() {
        Program p;
        while (!check(Tok::End)) {
            if (match(Tok::Semicolon)) continue;
            p.body.push_back(declaration());
        }
        return p;
    }

private:
    std::vector<Token> t_;
    size_t             p_ = 0;
    std::string        file_;

    const Token& cur() const { return t_[p_]; }
    const Token& ahead(size_t o = 1) const {
        size_t i = p_ + o;
        return t_[i < t_.size() ? i : t_.size() - 1];
    }
    bool check(Tok k) const { return cur().type == k; }
    bool match(Tok k) { if (check(k)) { p_++; return true; } return false; }
    const Token& advance() { return t_[p_++]; }

    [[noreturn]] void err(const std::string& m) {
        throw std::runtime_error(file_ + ":" + std::to_string(cur().line) + ":" +
                                 std::to_string(cur().col) + ": " + m);
    }

    Token expect(Tok k, const char* what) {
        if (!check(k))
            err(std::string("expected ") + what + " but found '" +
                (cur().text.empty() ? "<eof>" : cur().text) + "'");
        return advance();
    }

    void semi() { match(Tok::Semicolon); }

    // ============================================================
    //  Constant folding
    // ============================================================
    static bool isNumLit(const ExprPtr& e) {
        return e->kind == EK::Int || e->kind == EK::Num;
    }
    static double litNum(const ExprPtr& e) {
        return e->kind == EK::Int ? (double)e->inum : e->num;
    }

    // Try to fold `a OP b` where both are numeric literals.
    // Returns nullptr if not foldable.
    static ExprPtr tryFoldNum(const std::string& op,
                              const ExprPtr& a, const ExprPtr& b,
                              int line, int col) {
        if (!isNumLit(a) || !isNumLit(b)) return nullptr;
        bool aInt = a->kind == EK::Int;
        bool bInt = b->kind == EK::Int;

        // Pure integer arithmetic
        if (aInt && bInt) {
            long long x = a->inum, y = b->inum;
            if (op == "+") { auto e = mkExpr(EK::Int, line, col); e->inum = x + y; return e; }
            if (op == "-") { auto e = mkExpr(EK::Int, line, col); e->inum = x - y; return e; }
            if (op == "*") { auto e = mkExpr(EK::Int, line, col); e->inum = x * y; return e; }
            if (op == "/") {
                if (y == 0) return nullptr;   // let runtime raise
                auto e = mkExpr(EK::Int, line, col); e->inum = x / y; return e;
            }
            if (op == "%") {
                if (y == 0) return nullptr;
                auto e = mkExpr(EK::Int, line, col); e->inum = x % y; return e;
            }
            if (op == "<")  { auto e = mkExpr(EK::Bool, line, col); e->boolean = x <  y; return e; }
            if (op == "<=") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x <= y; return e; }
            if (op == ">")  { auto e = mkExpr(EK::Bool, line, col); e->boolean = x >  y; return e; }
            if (op == ">=") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x >= y; return e; }
            if (op == "==") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x == y; return e; }
            if (op == "!=") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x != y; return e; }
            return nullptr;
        }

        // Float arithmetic
        double x = litNum(a), y = litNum(b);
        if (op == "+") { auto e = mkExpr(EK::Num, line, col); e->num = x + y; return e; }
        if (op == "-") { auto e = mkExpr(EK::Num, line, col); e->num = x - y; return e; }
        if (op == "*") { auto e = mkExpr(EK::Num, line, col); e->num = x * y; return e; }
        if (op == "/") {
            if (y == 0) return nullptr;
            auto e = mkExpr(EK::Num, line, col); e->num = x / y; return e;
        }
        if (op == "<")  { auto e = mkExpr(EK::Bool, line, col); e->boolean = x <  y; return e; }
        if (op == "<=") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x <= y; return e; }
        if (op == ">")  { auto e = mkExpr(EK::Bool, line, col); e->boolean = x >  y; return e; }
        if (op == ">=") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x >= y; return e; }
        if (op == "==") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x == y; return e; }
        if (op == "!=") { auto e = mkExpr(EK::Bool, line, col); e->boolean = x != y; return e; }
        return nullptr;
    }

    // ============================================================
    //  Declarations
    // ============================================================
    std::vector<StmtPtr> blockBody() {
        expect(Tok::LBrace, "'{'");
        std::vector<StmtPtr> out;
        while (!check(Tok::RBrace) && !check(Tok::End)) {
            if (match(Tok::Semicolon)) continue;
            out.push_back(declaration());
        }
        expect(Tok::RBrace, "'}'");
        return out;
    }

    StmtPtr declaration() {
        bool exp = false;
        if (match(Tok::Export)) exp = true;

        if (check(Tok::Let)) {
            int line = cur().line, col = cur().col;
            advance();
            auto s = mkStmt(SK::Let, line, col);
            s->exported = exp;
            s->name = expect(Tok::Ident, "variable name").text;
            if (match(Tok::Assign)) s->expr = expression();
            semi();
            return s;
        }
        if (check(Tok::Fn) && ahead().type == Tok::Ident) return fnDecl(exp);
        if (check(Tok::Import)) return importStmt();
        if (exp) err("'export' must be followed by 'let' or 'fn'");
        return statement();
    }

    StmtPtr fnDecl(bool exp) {
        int line = cur().line, col = cur().col;
        expect(Tok::Fn, "'fn'");
        auto s = mkStmt(SK::Func, line, col);
        s->exported = exp;
        s->name = expect(Tok::Ident, "function name").text;
        expect(Tok::LParen, "'('");
        if (!check(Tok::RParen)) {
            do { s->params.push_back(expect(Tok::Ident, "parameter name").text); }
            while (match(Tok::Comma));
        }
        expect(Tok::RParen, "')'");
        s->body = blockBody();
        return s;
    }

    StmtPtr importStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::Import, "'import'");
        auto s = mkStmt(SK::Import, line, col);
        s->name = expect(Tok::String, "module path").text;
        if (match(Tok::As)) s->alias = expect(Tok::Ident, "alias").text;
        semi();
        return s;
    }

    StmtPtr statement() {
        if (check(Tok::LBrace)) {
            auto s = mkStmt(SK::Block, cur().line, cur().col);
            s->body = blockBody();
            return s;
        }
        if (check(Tok::If))     return ifStmt();
        if (check(Tok::While))  return whileStmt();
        if (check(Tok::For))    return forStmt();
        if (check(Tok::Return)) return returnStmt();
        if (check(Tok::Route))  return routeStmt();
        if (check(Tok::Try))    return tryStmt();
        if (check(Tok::Throw))  return throwStmt();
        if (check(Tok::Break))    { int l = cur().line, c = cur().col; advance(); semi(); return mkStmt(SK::Break, l, c); }
        if (check(Tok::Continue)) { int l = cur().line, c = cur().col; advance(); semi(); return mkStmt(SK::Continue, l, c); }
        if (check(Tok::Fn) && ahead().type == Tok::Ident) return fnDecl(false);

        auto s = mkStmt(SK::Expr, cur().line, cur().col);
        s->expr = expression();
        semi();
        return s;
    }

    StmtPtr ifStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::If, "'if'");
        auto s = mkStmt(SK::If, line, col);
        expect(Tok::LParen, "'('");
        s->expr = expression();
        expect(Tok::RParen, "')'");
        s->body = blockBody();
        if (match(Tok::Else)) {
            if (check(Tok::If)) s->alt.push_back(ifStmt());
            else                s->alt = blockBody();
        }
        return s;
    }

    StmtPtr whileStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::While, "'while'");
        auto s = mkStmt(SK::While, line, col);
        expect(Tok::LParen, "'('");
        s->expr = expression();
        expect(Tok::RParen, "')'");
        s->body = blockBody();
        return s;
    }

    StmtPtr forStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::For, "'for'");

        if (check(Tok::Let) && ahead().type == Tok::Ident && ahead(2).type == Tok::In) {
            advance();
            auto s = mkStmt(SK::ForIn, line, col);
            s->name = advance().text;
            advance();
            s->expr = expression();
            s->body = blockBody();
            return s;
        }
        if (check(Tok::Ident) && ahead().type == Tok::In) {
            auto s = mkStmt(SK::ForIn, line, col);
            s->name = advance().text;
            advance();
            s->expr = expression();
            s->body = blockBody();
            return s;
        }

        expect(Tok::LParen, "'('");
        auto s = mkStmt(SK::For, line, col);
        if (!check(Tok::Semicolon)) s->init = forInit();
        expect(Tok::Semicolon, "';'");
        if (!check(Tok::Semicolon)) s->cond = expression();
        expect(Tok::Semicolon, "';'");
        if (!check(Tok::RParen)) s->step = expression();
        expect(Tok::RParen, "')'");
        s->body = blockBody();
        return s;
    }

    StmtPtr forInit() {
        if (check(Tok::Let)) {
            advance();
            auto d = mkStmt(SK::Let, cur().line, cur().col);
            d->name = expect(Tok::Ident, "variable name").text;
            if (match(Tok::Assign)) d->expr = expression();
            return d;
        }
        auto e = mkStmt(SK::Expr, cur().line, cur().col);
        e->expr = expression();
        return e;
    }

    StmtPtr returnStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::Return, "'return'");
        auto s = mkStmt(SK::Return, line, col);
        if (!check(Tok::Semicolon) && !check(Tok::RBrace) && !check(Tok::End))
            s->expr = expression();
        semi();
        return s;
    }

    StmtPtr routeStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::Route, "'route'");
        auto s = mkStmt(SK::Route, line, col);
        s->name = "GET";
        if (check(Tok::Ident) && ahead().type == Tok::String) {
            s->name = advance().text;
            for (auto& c : s->name) c = (char)std::toupper((unsigned char)c);
        }
        s->expr = expression();
        s->body = blockBody();
        return s;
    }

    StmtPtr tryStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::Try, "'try'");
        auto s = mkStmt(SK::Try, line, col);
        s->body = blockBody();
        expect(Tok::Catch, "'catch'");
        expect(Tok::LParen, "'('");
        s->name = expect(Tok::Ident, "catch variable name").text;
        expect(Tok::RParen, "')'");
        s->alt = blockBody();
        return s;
    }

    StmtPtr throwStmt() {
        int line = cur().line, col = cur().col;
        expect(Tok::Throw, "'throw'");
        auto s = mkStmt(SK::Throw, line, col);
        if (!check(Tok::Semicolon) && !check(Tok::RBrace) && !check(Tok::End))
            s->expr = expression();
        semi();
        return s;
    }

    // ============================================================
    //  Expressions (with constant folding)
    // ============================================================
    ExprPtr expression() { return assignment(); }

    ExprPtr assignment() {
        auto left = logicalOr();
        if (check(Tok::Assign) || check(Tok::PlusAssign) || check(Tok::MinusAssign) ||
            check(Tok::StarAssign) || check(Tok::SlashAssign)) {
            int line = cur().line, col = cur().col;
            std::string op = advance().text;
            auto right = assignment();
            auto e = mkExpr(EK::Assign, line, col);
            e->op = op;
            e->a  = left;
            e->b  = right;
            return e;
        }
        return left;
    }

    ExprPtr logicalOr() {
        auto e = logicalAnd();
        while (check(Tok::Or)) {
            int line = cur().line, col = cur().col; advance();
            auto r = mkExpr(EK::Binary, line, col);
            r->op = "||"; r->a = e; r->b = logicalAnd();
            e = r;
        }
        return e;
    }

    ExprPtr logicalAnd() {
        auto e = equality();
        while (check(Tok::And)) {
            int line = cur().line, col = cur().col; advance();
            auto r = mkExpr(EK::Binary, line, col);
            r->op = "&&"; r->a = e; r->b = equality();
            e = r;
        }
        return e;
    }

    ExprPtr equality() {
        auto e = comparison();
        while (check(Tok::Eq) || check(Tok::Neq)) {
            int line = cur().line, col = cur().col;
            std::string op = advance().text;
            auto rhs = comparison();
            if (auto f = tryFoldNum(op, e, rhs, line, col)) { e = f; continue; }
            auto r = mkExpr(EK::Binary, line, col);
            r->op = op; r->a = e; r->b = rhs;
            e = r;
        }
        return e;
    }

    ExprPtr comparison() {
        auto e = term();
        while (check(Tok::Lt) || check(Tok::Lte) || check(Tok::Gt) || check(Tok::Gte)) {
            int line = cur().line, col = cur().col;
            std::string op = advance().text;
            auto rhs = term();
            if (auto f = tryFoldNum(op, e, rhs, line, col)) { e = f; continue; }
            auto r = mkExpr(EK::Binary, line, col);
            r->op = op; r->a = e; r->b = rhs;
            e = r;
        }
        return e;
    }

    ExprPtr term() {
        auto e = factor();
        while (check(Tok::Plus) || check(Tok::Minus)) {
            int line = cur().line, col = cur().col;
            std::string op = advance().text;
            auto rhs = factor();
            if (auto f = tryFoldNum(op, e, rhs, line, col)) { e = f; continue; }
            auto r = mkExpr(EK::Binary, line, col);
            r->op = op; r->a = e; r->b = rhs;
            e = r;
        }
        return e;
    }

    ExprPtr factor() {
        auto e = unary();
        while (check(Tok::Star) || check(Tok::Slash) || check(Tok::Percent)) {
            int line = cur().line, col = cur().col;
            std::string op = advance().text;
            auto rhs = unary();
            if (auto f = tryFoldNum(op, e, rhs, line, col)) { e = f; continue; }
            auto r = mkExpr(EK::Binary, line, col);
            r->op = op; r->a = e; r->b = rhs;
            e = r;
        }
        return e;
    }

    ExprPtr unary() {
        if (check(Tok::Not) || check(Tok::Minus)) {
            int line = cur().line, col = cur().col;
            std::string op = advance().text;
            auto inner = unary();
            // Fold unary minus on numeric literals
            if (op == "-" && inner->kind == EK::Int) {
                auto e = mkExpr(EK::Int, line, col);
                e->inum = -inner->inum;
                return e;
            }
            if (op == "-" && inner->kind == EK::Num) {
                auto e = mkExpr(EK::Num, line, col);
                e->num = -inner->num;
                return e;
            }
            auto e = mkExpr(EK::Unary, line, col);
            e->op = op;
            e->a  = inner;
            return e;
        }
        return postfix();
    }

    ExprPtr postfix() {
        auto e = primary();
        for (;;) {
            if (match(Tok::LParen)) {
                auto c = mkExpr(EK::Call, e->line, e->col);
                c->a = e;
                if (!check(Tok::RParen)) {
                    do { c->items.push_back(expression()); } while (match(Tok::Comma));
                }
                expect(Tok::RParen, "')'");
                e = c;
            } else if (match(Tok::Dot)) {
                auto m = mkExpr(EK::Member, e->line, e->col);
                m->a   = e;
                m->str = expect(Tok::Ident, "property name").text;
                e = m;
            } else if (match(Tok::LBracket)) {
                auto i = mkExpr(EK::Index, e->line, e->col);
                i->a = e;
                i->b = expression();
                expect(Tok::RBracket, "']'");
                e = i;
            } else break;
        }
        return e;
    }

    ExprPtr primary() {
        int line = cur().line, col = cur().col;

        if (check(Tok::Number)) {
            advance();
            const Token& tk = t_[p_ - 1];
            if (tk.isInt) {
                auto e = mkExpr(EK::Int, line, col);
                e->inum = tk.inum;
                return e;
            }
            auto e = mkExpr(EK::Num, line, col);
            e->num = tk.num;
            return e;
        }
        if (match(Tok::String)) { auto e = mkExpr(EK::Str, line, col);  e->str = t_[p_-1].text; return e; }
        if (match(Tok::True))   { auto e = mkExpr(EK::Bool, line, col); e->boolean = true;      return e; }
        if (match(Tok::False))  { auto e = mkExpr(EK::Bool, line, col); e->boolean = false;     return e; }
        if (match(Tok::Null))   return mkExpr(EK::Nil, line, col);
        if (match(Tok::Ident))  { auto e = mkExpr(EK::Ident, line, col); e->str = t_[p_-1].text; return e; }

        if (match(Tok::LParen)) {
            auto e = expression();
            expect(Tok::RParen, "')'");
            return e;
        }

        if (match(Tok::LBracket)) {
            auto e = mkExpr(EK::Array, line, col);
            if (!check(Tok::RBracket)) {
                do { e->items.push_back(expression()); } while (match(Tok::Comma));
            }
            expect(Tok::RBracket, "']'");
            return e;
        }

        if (match(Tok::LBrace)) {
            auto e = mkExpr(EK::Map, line, col);
            if (!check(Tok::RBrace)) {
                do {
                    std::string key;
                    if (check(Tok::String) || check(Tok::Ident)) key = advance().text;
                    else err("expected map key");
                    expect(Tok::Colon, "':'");
                    e->fields.emplace_back(key, expression());
                } while (match(Tok::Comma));
            }
            expect(Tok::RBrace, "'}'");
            return e;
        }

        if (match(Tok::Fn)) {
            auto e = mkExpr(EK::Func, line, col);
            expect(Tok::LParen, "'('");
            if (!check(Tok::RParen)) {
                do { e->params.push_back(expect(Tok::Ident, "parameter name").text); }
                while (match(Tok::Comma));
            }
            expect(Tok::RParen, "')'");
            e->body = blockBody();
            return e;
        }

        err("unexpected token '" + (cur().text.empty() ? "<eof>" : cur().text) + "'");
    }
};

} // namespace bi