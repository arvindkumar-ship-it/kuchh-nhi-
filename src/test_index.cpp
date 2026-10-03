// secondary index + constant folding + static type check
#include <cstdio>
#include <fstream>
#include <iostream>
#include "database.h"

using namespace mkdb;

#define CHECK(c) do { if (!(c)) { std::cout << "FAIL line " << __LINE__ << ": " #c "\n"; return 1; } } while (0)
static bool has(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }
static bool fails(Database& db, const std::string& q) { try { db.execute(q); } catch (...) { return true; } return false; }
static void clean(const std::string& p) { std::remove(p.c_str()); std::remove((p + "-journal").c_str()); }

int main() {
    clean("t_i.db");
    {
        Database db("t_i.db");
        db.execute("CREATE TABLE u (id INT PRIMARY KEY, email TEXT, age INT)");
        for (int i = 1; i <= 200; i++)
            db.execute("INSERT INTO u VALUES (" + std::to_string(i) + ", 'm" + std::to_string(i) + "@x.com', " + std::to_string(i % 5) + ")");

        // backfill wala index
        db.execute("CREATE UNIQUE INDEX ix_email ON u (email)");
        db.execute("CREATE INDEX ix_age ON u (age)");
        std::string ex = db.execute("EXPLAIN SELECT * FROM u WHERE email = 'm77@x.com'");
        std::cout << ex << "\n";
        CHECK(has(ex, "IndexLookup"));
        CHECK(has(db.execute("SELECT id FROM u WHERE email = 'm77@x.com'"), "77"));
        CHECK(has(db.execute("SELECT * FROM u WHERE email = 'nope'"), "(0 rows)"));
        CHECK(has(db.execute("SELECT * FROM u WHERE age = 3"), "(40 rows)"));         // non-unique bucket
        CHECK(has(db.execute("SELECT * FROM u WHERE age = 3 AND id > 100"), "(20 rows)"));
        CHECK(has(db.execute("EXPLAIN SELECT * FROM u WHERE age = 3 AND id > 100"), "IndexLookup"));
        CHECK(!has(db.execute("EXPLAIN SELECT * FROM u WHERE id = 3"), "IndexLookup"));  // pk ko pk lookup

        // unique enforcement
        CHECK(fails(db, "INSERT INTO u VALUES (500, 'm5@x.com', 1)"));
        CHECK(has(db.execute("SELECT * FROM u"), "(200 rows)"));
        CHECK(fails(db, "CREATE UNIQUE INDEX ix_age2 ON u (age)"));   // duplicates hain
        CHECK(fails(db, "CREATE INDEX ix_email ON u (age)"));         // naam repeat
        CHECK(fails(db, "CREATE INDEX bad ON u (id)"));               // pk pe nahi

        // update indexed column
        db.execute("UPDATE u SET email = 'new@x.com' WHERE id = 10");
        CHECK(has(db.execute("SELECT id FROM u WHERE email = 'new@x.com'"), "10"));
        CHECK(has(db.execute("SELECT * FROM u WHERE email = 'm10@x.com'"), "(0 rows)"));
        CHECK(fails(db, "UPDATE u SET email = 'm11@x.com' WHERE id = 12"));   // dusre ki value
        CHECK(fails(db, "UPDATE u SET email = 'same' WHERE id < 5"));         // batch me duplicate
        CHECK(has(db.execute("SELECT * FROM u WHERE email = 'same'"), "(0 rows)"));
        db.execute("UPDATE u SET age = 9 WHERE id <= 4");
        CHECK(has(db.execute("SELECT * FROM u WHERE age = 9"), "(4 rows)"));

        // delete index se hatata hai
        db.execute("DELETE FROM u WHERE id = 20");
        CHECK(has(db.execute("SELECT * FROM u WHERE email = 'm20@x.com'"), "(0 rows)"));
        db.execute("INSERT INTO u VALUES (20, 'm20@x.com', 0)");  // value dobara free

        // txn rollback index bhi undo
        db.execute("BEGIN");
        db.execute("INSERT INTO u VALUES (900, 'tx@x.com', 1)");
        db.execute("ROLLBACK");
        CHECK(has(db.execute("SELECT * FROM u WHERE email = 'tx@x.com'"), "(0 rows)"));

        // constant folding + static types
        db.execute("INSERT INTO u VALUES (1000 + 1, 'f@x.com', 2 * 3)");
        CHECK(has(db.execute("SELECT age FROM u WHERE id = 1001"), "6"));
        CHECK(has(db.execute("EXPLAIN SELECT * FROM u WHERE id = 500 + 501"), "PkLookup"));
        CHECK(fails(db, "SELECT * FROM u WHERE id = 'x'"));
        CHECK(fails(db, "SELECT * FROM u WHERE email + 1 = 2"));
        CHECK(fails(db, "UPDATE u SET age = 'old' WHERE id = 1"));
        CHECK(fails(db, "SELECT * FROM u WHERE 1 / 0 = 1"));  // folding chhodta hai, runtime error
    }
    {   // reopen: index catalog se wapas
        Database db("t_i.db");
        CHECK(has(db.execute("EXPLAIN SELECT * FROM u WHERE email = 'f@x.com'"), "IndexLookup"));
        CHECK(has(db.execute("SELECT id FROM u WHERE email = 'f@x.com'"), "1001"));
        CHECK(fails(db, "INSERT INTO u VALUES (2000, 'f@x.com', 1)"));
    }
    clean("t_i.db");
    std::cout << "INDEX OK\n";
    return 0;
}
