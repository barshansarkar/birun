#pragma once
#include "token.hpp"
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace bi {

class Lexer {
public:
    Lexer(std::string src, std::string file)
        : src_(std::move(src)), file_(std::move(file)) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        out.reserve(src_.size() / 4 + 8);
        for (;;) {
            skipTrivia();
            if (pos_ >= src_.size()) break;
            int l = line_, c = col_;
            Token t = next();
            t.line = l;
            t.col  = c;
            out.push_back(std::move(t));
        }
        Token e;
        e.type = Tok::End;
        e.line = line_;
        e.col  = col_;
        out.push_back(e);
        return out;
    }

private:
    std::string src_, file_;
    size_t      pos_  = 0;
    int         line_ = 1, col_ = 1;

    char peek(size_t o = 0) const {
        return pos_ + o < src_.size() ? src_[pos_ + o] : '\0';
    }
    char adv() {
        char c = src_[pos_++];
        if (c == '\n') { line_++; col_ = 1; } else col_++;
        return c;
    }
    bool eat(char c) { if (peek() == c) { adv(); return true; } return false; }

    [[noreturn]] void err(const std::string& m) {
        throw std::runtime_error(file_ + ":" + std::to_string(line_) + ":" +
                                 std::to_string(col_) + ": " + m);
    }

    void skipTrivia() {
        for (;;) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { adv(); continue; }
            if (c == '/' && peek(1) == '/') { while (peek() && peek() != '\n') adv(); continue; }
            if (c == '/' && peek(1) == '*') {
                adv(); adv();
                while (peek() && !(peek() == '*' && peek(1) == '/')) adv();
                if (peek()) { adv(); adv(); }
                continue;
            }
            break;
        }
    }

    Token next() {
        char c = peek();
        if (std::isdigit((unsigned char)c)) return number();
        if (c == '"' || c == '\'') return string();
        if (std::isalpha((unsigned char)c) || c == '_') return ident();
        return punct();
    }

    Token number() {
        std::string s;
        while (std::isdigit((unsigned char)peek())) s += adv();
        bool isFloat = false;
        if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
            isFloat = true;
            s += adv();
            while (std::isdigit((unsigned char)peek())) s += adv();
        }
        if (peek() == 'e' || peek() == 'E') {
            isFloat = true;
            s += adv();
            if (peek() == '+' || peek() == '-') s += adv();
            while (std::isdigit((unsigned char)peek())) s += adv();
        }
        Token t;
        t.type = Tok::Number;
        t.text = s;
        if (isFloat) {
            t.isInt = false;
            t.num   = std::stod(s);
        } else {
            try {
                t.inum  = std::stoll(s);
                t.isInt = true;
            } catch (...) {
                t.isInt = false;
                t.num   = std::stod(s);
            }
        }
        return t;
    }

    Token string() {
        char q = adv();
        std::string s;
        while (peek() && peek() != q) {
            char c = adv();
            if (c == '\\') {
                char e = adv();
                switch (e) {
                    case 'n':  s += '\n'; break;
                    case 't':  s += '\t'; break;
                    case 'r':  s += '\r'; break;
                    case '\\': s += '\\'; break;
                    case '"':  s += '"';  break;
                    case '\'': s += '\''; break;
                    case '0':  s += '\0'; break;
                    default:   s += e;    break;
                }
            } else s += c;
        }
        if (!peek()) err("unterminated string literal");
        adv();
        Token t;
        t.type = Tok::String;
        t.text = s;
        return t;
    }

    Token ident() {
        std::string s;
        while (std::isalnum((unsigned char)peek()) || peek() == '_') s += adv();

        static const std::unordered_map<std::string, Tok> kw = {
            {"let", Tok::Let},
            {"fn", Tok::Fn},         {"return", Tok::Return},
            {"if", Tok::If},         {"else", Tok::Else},
            {"while", Tok::While},   {"for", Tok::For},
            {"in", Tok::In},         {"true", Tok::True},
            {"false", Tok::False},   {"null", Tok::Null},
            {"import", Tok::Import}, {"export", Tok::Export},
            {"route", Tok::Route},   {"as", Tok::As},
            {"break", Tok::Break},   {"continue", Tok::Continue},
            {"try", Tok::Try},       {"catch", Tok::Catch},
            {"throw", Tok::Throw},
        };

        Token t;
        auto it = kw.find(s);
        t.type = (it != kw.end()) ? it->second : Tok::Ident;
        t.text = s;
        return t;
    }

    Token punct() {
        char c = adv();
        Token t;
        auto mk = [&](Tok ty, const std::string& s) { t.type = ty; t.text = s; return t; };

        switch (c) {
            case '(': return mk(Tok::LParen,    "(");
            case ')': return mk(Tok::RParen,    ")");
            case '{': return mk(Tok::LBrace,    "{");
            case '}': return mk(Tok::RBrace,    "}");
            case '[': return mk(Tok::LBracket,  "[");
            case ']': return mk(Tok::RBracket,  "]");
            case ',': return mk(Tok::Comma,     ",");
            case '.': return mk(Tok::Dot,       ".");
            case ':': return mk(Tok::Colon,     ":");
            case ';': return mk(Tok::Semicolon, ";");
            case '+': return eat('=') ? mk(Tok::PlusAssign,  "+=") : mk(Tok::Plus,  "+");
            case '-': return eat('=') ? mk(Tok::MinusAssign, "-=") : mk(Tok::Minus, "-");
            case '*': return eat('=') ? mk(Tok::StarAssign,  "*=") : mk(Tok::Star,  "*");
            case '/': return eat('=') ? mk(Tok::SlashAssign, "/=") : mk(Tok::Slash, "/");
            case '%': return mk(Tok::Percent, "%");
            case '=': return eat('=') ? mk(Tok::Eq,  "==") : mk(Tok::Assign, "=");
            case '!': return eat('=') ? mk(Tok::Neq, "!=") : mk(Tok::Not,    "!");
            case '<': return eat('=') ? mk(Tok::Lte, "<=") : mk(Tok::Lt,     "<");
            case '>': return eat('=') ? mk(Tok::Gte, ">=") : mk(Tok::Gt,     ">");
            case '&':
                if (eat('&')) return mk(Tok::And, "&&");
                err("unexpected '&' (did you mean '&&'?)");
            case '|':
                if (eat('|')) return mk(Tok::Or, "||");
                err("unexpected '|' (did you mean '||'?)");
        }
        err(std::string("unexpected character '") + c + "'");
    }
};

} // namespace bi