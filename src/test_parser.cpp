#include <iostream>
#include <string>
#include "parser.h"

using namespace mkdb;

static void show(const std::string& sql) {
    std::cout << sql << "\n  ";
    try {
        Stmt s = parse(sql);
        std::cout << stmt_to_string(s) << "\n";
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << "\n";
    }
}

int main() {
    show("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);");
    show("insert into users (id, name) values (1, 'it''s ok');");
    show("INSERT INTO users VALUES (2, 'x', -5)");
    show("SELECT name, age FROM users WHERE age >= 18 AND name != 'Ravi' ORDER BY age DESC LIMIT 5;");
    show("SELECT * FROM t WHERE a = 1 OR b = 2 AND c = 3");
    show("SELECT * FROM t WHERE NOT a = 1 AND b");
    show("SELECT * FROM t WHERE 1 + 2 * 3 - 4 > -x");
    show("SELECT * FROM t WHERE (1 + 2) * 3 = 9");
    show("UPDATE users SET age = age + 1, name = 'z' WHERE id = 7");
    show("DELETE FROM users WHERE id < 10");
    std::cout << "--- galat queries ---\n";
    show("SELECT FROM t");
    show("SELECT * FROM t WHERE");
    show("SELECT * FROM t extra");
    show("DROP TABLE t");
    show("SELECT * FROM t WHERE (a = 1");
    return 0;
}