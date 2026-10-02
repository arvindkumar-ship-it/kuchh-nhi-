#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include "database.h"

using namespace mkdb;

static void run(Database& db, const std::string& sql) {
    std::cout << sql << "\n  ";
    try {
        std::cout << db.execute(sql) << "\n";
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << "\n";
    }
}

static void show(Database& db, const std::string& table, int64_t key) {
    std::cout << table << "[" << key << "] = ";
    std::vector<Value> row;
    if (!db.get_row(table, key, &row)) {
        std::cout << "nahi mili\n";
        return;
    }
    for (const Value& v : row) {
        if (v.type == ColType::Int) std::cout << v.i << " ";
        else std::cout << "'" << v.s << "' ";
    }
    std::cout << "\n";
}

int main() {
    std::remove("sql.db");

    {
        Database db("sql.db");
        run(db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)");
        run(db, "CREATE TABLE users (id INT PRIMARY KEY)");
        run(db, "CREATE TABLE posts (pid INT PRIMARY KEY, title TEXT)");

        run(db, "INSERT INTO users VALUES (1, 'ravi', 21)");
        run(db, "INSERT INTO users (name, id, age) VALUES ('sita', 2, 30)");
        run(db, "INSERT INTO users VALUES (1, 'dup', 5)");
        run(db, "INSERT INTO users VALUES (3, 'x')");
        run(db, "INSERT INTO users VALUES (3, 5, 'x')");
        run(db, "INSERT INTO users (id, name) VALUES (4, 'y')");
        run(db, "INSERT INTO nope VALUES (1)");
        run(db, "INSERT INTO users VALUES (-5, 'neg', -1)");
        run(db, "INSERT INTO posts VALUES (10, 'hello')");
        run(db, "SELECT * FROM users");

        // posts me kaafi rows, taaki B-Tree split ho aur root badle (page nahi)
        for (int i = 1000; i < 2000; i++) {
            db.execute("INSERT INTO posts VALUES (" + std::to_string(i) +
                       ", 'post number " + std::to_string(i) + "')");
        }
        std::cout << "posts me 1000 row daali (pid 1000..1999)\n";
    }

    std::cout << "--- file band, dobara khuli ---\n";
    Database db("sql.db");

    std::cout << "tables:";
    for (const std::string& n : db.table_names()) std::cout << " " << n;
    std::cout << "\n";

    show(db, "users", 1);
    show(db, "users", 2);
    show(db, "users", -5);
    show(db, "users", 99);
    show(db, "posts", 10);
    show(db, "posts", 1500);
    show(db, "posts", 999);
    return 0;
}