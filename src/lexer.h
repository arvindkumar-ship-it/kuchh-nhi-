#pragma once
#include <string>
#include <vector>

namespace mkdb {

enum class Tok {
    // keywords
    Create, Table, Insert, Into, Values, Select, From, Where,
    Delete, Update, Set, And, Or, Not, Primary, Key, Int, Text,
    Order, By, Limit, Asc, Desc, Unique, Index, On,
    // baaki sab
    Ident, Number, String,
    LParen, RParen, Comma, Semicolon, Star,
    Eq, Neq, Lt, Le, Gt, Ge, Plus, Minus, Slash,
    End   // input khatam
};

struct Token {
    Tok type;
    std::string text;  // ident/number/string me asli text, keyword me uppercase naam
    size_t pos;        // query me kahan se shuru hua (error batane ke kaam aata hai)
};

// token type ka naam, debug/print ke liye
const char* tok_name(Tok t);

// poori query ko tokens me todta hai. galat character pe runtime_error
std::vector<Token> tokenize(const std::string& sql);

}  // namespace mkdb