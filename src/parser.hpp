#pragma once
#include "task.hpp"

#include <cctype>
#include <stdexcept>
#include <string>

namespace birun {

struct ParseError : std::runtime_error {
    int line, col;
    ParseError(const std::string& m, int l, int c)
        : std::runtime_error(m), line(l), col(c) {}
};

class Parser {
public:
    explicit Parser(std::string src) : src_(std::move(src)) {}

    TaskMap parse() {
        TaskMap tasks;
        for (;;) {
            skipWs();
            if (eof()) break;

            int l = line_, c = col_;
            std::string kw = ident();
            if (kw != "task")
                throw ParseError("expected 'task', got '" + kw + "'", l, c);

            Task t;
            t.name = stringLit();

            if (tasks.count(t.name))
                throw ParseError("duplicate task '" + t.name + "'", l, c);

            for (;;) {
                skipWs();
                if (peek() == '{') break;
                int ml = line_, mc = col_;
                std::string kw2 = ident();
                if (kw2 == "desc") {
                    t.desc = stringLit();
                } else if (kw2 == "depends") {
                    parseDepends(t);
                } else {
                    throw ParseError(
                        "expected 'desc', 'depends' or '{', got '" + kw2 + "'",
                        ml, mc);
                }
            }

            expect('{');

            for (;;) {
                skipWs();
                if (peek() == '}') { adv(); break; }
                if (eof())
                    throw ParseError("unterminated task body", line_, col_);

                int bl = line_, bc = col_;
                std::string key = ident();
                if (key != "run")
                    throw ParseError(
                        "expected 'run' in task body, got '" + key + "'",
                        bl, bc);
                expect(':');
                t.runs.push_back(stringLit());
            }

            tasks.emplace(t.name, std::move(t));
        }
        return tasks;
    }

private:
    std::string src_;
    size_t      i_    = 0;
    int         line_ = 1;
    int         col_  = 1;

    bool eof() const { return i_ >= src_.size(); }

    char peek(size_t o = 0) const {
        return i_ + o < src_.size() ? src_[i_ + o] : '\0';
    }

    char adv() {
        char c = src_[i_++];
        if (c == '\n') { line_++; col_ = 1; } else col_++;
        return c;
    }

    void skipWs() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { adv(); continue; }
            if (c == '/' && peek(1) == '/') {
                while (!eof() && peek() != '\n') adv();
                continue;
            }
            if (c == '#') {
                while (!eof() && peek() != '\n') adv();
                continue;
            }
            break;
        }
    }

    void expect(char c) {
        skipWs();
        if (peek() != c)
            throw ParseError(std::string("expected '") + c + "'", line_, col_);
        adv();
    }

    std::string ident() {
        skipWs();
        char c = peek();
        if (!(std::isalpha((unsigned char)c) || c == '_'))
            throw ParseError("expected identifier", line_, col_);
        std::string s;
        while (std::isalnum((unsigned char)peek()) || peek() == '_')
            s += adv();
        return s;
    }

    std::string stringLit() {
        skipWs();
        char q = peek();
        if (q != '"' && q != '\'')
            throw ParseError("expected string literal", line_, col_);
        adv();
        std::string s;
        while (!eof() && peek() != q) {
            char c = adv();
            if (c == '\\' && !eof()) {
                char e = adv();
                switch (e) {
                    case 'n':  s += '\n'; break;
                    case 't':  s += '\t'; break;
                    case 'r':  s += '\r'; break;
                    case '\\': s += '\\'; break;
                    case '"':  s += '"';  break;
                    case '\'': s += '\''; break;
                    default:   s += e;    break;
                }
            } else s += c;
        }
        if (eof())
            throw ParseError("unterminated string literal", line_, col_);
        adv();
        return s;
    }

    void parseDepends(Task& t) {
        for (;;) {
            skipWs();
            char c = peek();
            if (c == '"' || c == '\'') {
                t.depends.push_back(stringLit());
                skipWs();
                if (peek() == ',') adv();
                continue;
            }
            break;
        }
    }
};

} // namespace birun
