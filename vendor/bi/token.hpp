#pragma once
#include <string>

namespace bi {

enum class Tok {
    End, Number, String, Ident,
    Let, Fn, Return, If, Else, While, For, In, True, False, Null,
    Import, Export, Route, As, Break, Continue,
    Try, Catch, Throw,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Dot, Colon, Semicolon,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Eq, Neq, Lt, Lte, Gt, Gte,
    And, Or, Not,
};

struct Token {
    Tok         type = Tok::End;
    std::string text;
    double      num  = 0.0;
    long long   inum = 0;
    bool        isInt = false;
    int         line = 1;
    int         col  = 1;
};

} // namespace bi