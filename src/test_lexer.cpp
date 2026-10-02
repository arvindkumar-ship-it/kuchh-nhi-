#include <iostream>
#include <string>
#include "lexer.h"

using namespace mkdb;

static void show(const std::string& sql) {
    std::cout << sql << "\n  ";
    try {
        for (const Token& t : tokenize(sql)) {
            std::cout << tok_name(t.type);
            if (t.type == Tok::Ident || t.type == Tok::Number || t.type == Tok::String) {
                std::cout << "(" << t.text << ")";
            }
            std::cout << " ";
        }
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << "\n";
    }
}

int main() {
    show("SELECT name, age FROM users WHERE age >= 18 AND name != 'Ravi';");
    show("insert into t values (1, 'it''s ok', -5); -- comment");
    show("SELECT * FROM x WHERE a <> 3");
    show("SELECT 'abc");
    show("SELECT @");
    return 0;
}