#include "lexer.h"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace mkdb {

static const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> m = {
        {"CREATE", Tok::Create}, {"TABLE", Tok::Table},
        {"INSERT", Tok::Insert}, {"INTO", Tok::Into},
        {"VALUES", Tok::Values}, {"SELECT", Tok::Select},
        {"FROM", Tok::From},     {"WHERE", Tok::Where},
        {"DELETE", Tok::Delete}, {"UPDATE", Tok::Update},
        {"SET", Tok::Set},       {"AND", Tok::And},
        {"OR", Tok::Or},         {"NOT", Tok::Not},
        {"PRIMARY", Tok::Primary}, {"KEY", Tok::Key},
        {"INT", Tok::Int},       {"TEXT", Tok::Text},
        {"ORDER", Tok::Order},   {"BY", Tok::By},
        {"LIMIT", Tok::Limit},   {"ASC", Tok::Asc},
        {"DESC", Tok::Desc},     {"UNIQUE", Tok::Unique},
        {"INDEX", Tok::Index},   {"ON", Tok::On},
    };
    return m;
}

const char* tok_name(Tok t) {
    switch (t) {
        case Tok::Unique: return "UNIQUE";
        case Tok::Index: return "INDEX";
        case Tok::On: return "ON";
        case Tok::Create: return "CREATE";
        case Tok::Table: return "TABLE";
        case Tok::Insert: return "INSERT";
        case Tok::Into: return "INTO";
        case Tok::Values: return "VALUES";
        case Tok::Select: return "SELECT";
        case Tok::From: return "FROM";
        case Tok::Where: return "WHERE";
        case Tok::Delete: return "DELETE";
        case Tok::Update: return "UPDATE";
        case Tok::Set: return "SET";
        case Tok::And: return "AND";
        case Tok::Or: return "OR";
        case Tok::Not: return "NOT";
        case Tok::Primary: return "PRIMARY";
        case Tok::Key: return "KEY";
        case Tok::Int: return "INT";
        case Tok::Text: return "TEXT";
        case Tok::Order: return "ORDER";
        case Tok::By: return "BY";
        case Tok::Limit: return "LIMIT";
        case Tok::Asc: return "ASC";
        case Tok::Desc: return "DESC";
        case Tok::Ident: return "IDENT";
        case Tok::Number: return "NUMBER";
        case Tok::String: return "STRING";
        case Tok::LParen: return "LPAREN";
        case Tok::RParen: return "RPAREN";
        case Tok::Comma: return "COMMA";
        case Tok::Semicolon: return "SEMICOLON";
        case Tok::Star: return "STAR";
        case Tok::Eq: return "EQ";
        case Tok::Neq: return "NEQ";
        case Tok::Lt: return "LT";
        case Tok::Le: return "LE";
        case Tok::Gt: return "GT";
        case Tok::Ge: return "GE";
        case Tok::Plus: return "PLUS";
        case Tok::Minus: return "MINUS";
        case Tok::Slash: return "SLASH";
        case Tok::End: return "END";
    }
    return "?";
}

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> out;
    size_t i = 0;
    const size_t n = s.size();

    while (i < n) {
        char c = s[i];

        // space, tab, newline chhodo
        if (std::isspace((unsigned char)c)) {
            i++;
            continue;
        }

        // -- se comment, line ke end tak
        if (c == '-' && i + 1 < n && s[i + 1] == '-') {
            while (i < n && s[i] != '\n') i++;
            continue;
        }

        size_t start = i;

        // shabd: keyword ya ident
        if (std::isalpha((unsigned char)c) || c == '_') {
            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
            std::string word = s.substr(start, i - start);

            std::string up = word;
            for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);

            auto it = keywords().find(up);
            if (it != keywords().end()) out.push_back({it->second, up, start});
            else out.push_back({Tok::Ident, word, start});
            continue;
        }

        // number (abhi sirf poore numbers)
        if (std::isdigit((unsigned char)c)) {
            while (i < n && std::isdigit((unsigned char)s[i])) i++;
            out.push_back({Tok::Number, s.substr(start, i - start), start});
            continue;
        }

        // string, single quote me
        if (c == '\'') {
            i++;  // shuru wala quote
            std::string val;
            bool closed = false;
            while (i < n) {
                if (s[i] == '\'') {
                    if (i + 1 < n && s[i + 1] == '\'') {  // '' matlab andar ek quote
                        val += '\'';
                        i += 2;
                        continue;
                    }
                    i++;  // band karne wala quote
                    closed = true;
                    break;
                }
                val += s[i++];
            }
            if (!closed) {
                throw std::runtime_error("lexer: string band nahi hui, position " +
                                         std::to_string(start));
            }
            out.push_back({Tok::String, val, start});
            continue;
        }

        // symbols
        auto add = [&](Tok t, size_t len) {
            out.push_back({t, s.substr(start, len), start});
            i += len;
        };
        char next = (i + 1 < n) ? s[i + 1] : '\0';

        switch (c) {
            case '(': add(Tok::LParen, 1); break;
            case ')': add(Tok::RParen, 1); break;
            case ',': add(Tok::Comma, 1); break;
            case ';': add(Tok::Semicolon, 1); break;
            case '*': add(Tok::Star, 1); break;
            case '+': add(Tok::Plus, 1); break;
            case '-': add(Tok::Minus, 1); break;
            case '/': add(Tok::Slash, 1); break;
            case '=': add(Tok::Eq, 1); break;
            case '<':
                if (next == '=') add(Tok::Le, 2);
                else if (next == '>') add(Tok::Neq, 2);
                else add(Tok::Lt, 1);
                break;
            case '>':
                if (next == '=') add(Tok::Ge, 2);
                else add(Tok::Gt, 1);
                break;
            case '!':
                if (next != '=') {
                    throw std::runtime_error("lexer: ajeeb character '!', position " +
                                             std::to_string(i));
                }
                add(Tok::Neq, 2);
                break;
            default:
                throw std::runtime_error(std::string("lexer: ajeeb character '") + c +
                                         "', position " + std::to_string(i));
        }
    }

    out.push_back({Tok::End, "", n});
    return out;
}

}  // namespace mkdb