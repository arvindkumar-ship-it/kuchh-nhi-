#include <iostream>
#include <cstdio>
#include "database.h"
using namespace mkdb;
static void run(Database& db, const std::string& sql) {
    std::cout << sql << "\n";
    try { std::cout << db.execute(sql) << "\n\n"; }
    catch (const std::exception& e) { std::cout << "error: " << e.what() << "\n\n"; }
}
int main() {
    std::remove("dml.db");
    {
        Database db("dml.db");
        run(db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)");
        run(db, "INSERT INTO users VALUES (1, 'ravi', 21)");
        run(db, "INSERT INTO users VALUES (2, 'sita', 30)");
        run(db, "INSERT INTO users VALUES (3, 'amit', 25)");
        run(db, "INSERT INTO users VALUES (4, 'neha', 30)");
        run(db, "SELECT * FROM users");
        run(db, "SELECT name, age FROM users WHERE age >= 25 AND NOT name = 'amit' ORDER BY name DESC");
        run(db, "SELECT id FROM users ORDER BY age DESC LIMIT 2");
        run(db, "SELECT * FROM users WHERE age + 5 > 30");
        run(db, "SELECT * FROM users WHERE name > 5");
        run(db, "UPDATE users SET age = age + 1 WHERE name = 'ravi'");
        run(db, "UPDATE users SET id = 9");
        run(db, "DELETE FROM users WHERE age = 30");
        run(db, "SELECT * FROM users");
        run(db, "CREATE TABLE big (k INT PRIMARY KEY, v INT)");
        for (int i = 1; i <= 3000; i++)
            db.execute("INSERT INTO big VALUES (" + std::to_string(i) + ", " + std::to_string(i % 10) + ")");
        run(db, "DELETE FROM big WHERE v = 3");
        run(db, "UPDATE big SET v = v * 100 WHERE k <= 5");
        run(db, "SELECT * FROM big WHERE k < 8 ORDER BY k");
        run(db, "SELECT k FROM big WHERE v = 3");
    }
    std::cout << "--- file band, dobara khuli ---\n";
    Database db("dml.db");
    run(db, "SELECT * FROM users");
    run(db, "SELECT k FROM big WHERE k >= 2995 ORDER BY k DESC LIMIT 3");
    run(db, "INSERT INTO big VALUES (3, 3)");
    run(db, "INSERT INTO big VALUES (3, 4)");
}