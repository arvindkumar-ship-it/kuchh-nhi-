// cache + journal + overflow + external sort + EXPLAIN + txn: end-to-end checks
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include "database.h"
#include "executor.h"
#include "pager.h"

using namespace mkdb;
static bool exists_f(const std::string& p) { std::ifstream f(p, std::ios::binary); return (bool)f; }

#define CHECK(c) do { if (!(c)) { std::cout << "FAIL line " << __LINE__ << ": " #c "\n"; return 1; } } while (0)

static void clean(const std::string& p) { std::remove(p.c_str()); std::remove((p + "-journal").c_str()); }
static bool has(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

int main() {
    // 1. clock cache: chhoti cache, zyada pages -> evict hote hain, data sahi
    clean("t_cache.db");
    {
        Pager p("t_cache.db", 4);
        char b[PAGE_SIZE];
        for (int i = 0; i < 20; i++) { PageId id = p.allocate_page(); memset(b, i, PAGE_SIZE); p.write_page(id, b); }
        for (int i = 0; i < 20; i++) { p.read_page(i, b); CHECK((unsigned char)b[100] == i); }
        p.read_page(19, b); p.read_page(19, b);
        CHECK(p.cache_hits() > 0 && p.cache_misses() > 0);
        p.flush();
    }
    { Pager p("t_cache.db", 4); char b[PAGE_SIZE]; p.read_page(7, b); CHECK(b[5] == 7); CHECK(p.page_count() == 20); }
    std::cout << "cache ok\n";

    // 2. rollback: pages + file size wapas
    {
        Pager p("t_cache.db", 2);
        char b[PAGE_SIZE];
        p.begin();
        memset(b, 99, PAGE_SIZE);
        for (int i = 0; i < 20; i++) p.write_page(i, b);  // chhoti cache => disk pe evict bhi hua
        p.allocate_page();
        CHECK(p.rollback());
        CHECK(p.page_count() == 20);
        p.read_page(3, b); CHECK(b[0] == 3);
    }
    std::cout << "rollback ok\n";

    // 3. crash recovery: txn khuli chhodi (destructor commit nahi karta), reopen pe undo
    {
        Pager p("t_cache.db", 2);
        char b[PAGE_SIZE]; memset(b, 77, PAGE_SIZE);
        p.begin();
        for (int i = 0; i < 20; i++) p.write_page(i, b);
        p.allocate_page();
    }  // "crash"
    CHECK(exists_f("t_cache.db-journal"));
    { Pager p("t_cache.db", 2); char b[PAGE_SIZE]; p.read_page(5, b); CHECK(b[0] == 5); CHECK(p.page_count() == 20); }
    CHECK(!exists_f("t_cache.db-journal"));
    std::cout << "crash recovery ok\n";

    // 4. SQL: txn, rollback, overflow, sort, explain
    clean("t_e.db");
    {
        Database db("t_e.db");
        db.set_sort_memory(2000);  // chhoti RAM => disk runs + multi-pass
        db.execute("CREATE TABLE t (id INT PRIMARY KEY, name TEXT)");
        db.execute("BEGIN");
        for (int i = 0; i < 400; i++)
            db.execute("INSERT INTO t VALUES (" + std::to_string(i) + ", 'n" + std::to_string((i * 37) % 400) + "')");
        db.execute("ROLLBACK");
        CHECK(has(db.execute("SELECT * FROM t"), "(0 rows)"));

        db.execute("BEGIN");
        for (int i = 0; i < 400; i++)
            db.execute("INSERT INTO t VALUES (" + std::to_string(i) + ", 'n" + std::to_string((i * 37) % 400) + "')");
        db.execute("COMMIT");
        std::string r = db.execute("SELECT id, name FROM t ORDER BY name DESC LIMIT 3");
        std::cout << r << "\n";
        CHECK(has(r, "(3 rows)") && has(r, "n99"));
        std::string all = db.execute("SELECT * FROM t ORDER BY name");
        CHECK(has(all, "(400 rows)"));

        // overflow: 20000 byte text
        std::string big(20000, 'x');
        db.execute("INSERT INTO t VALUES (1000, '" + big + "')");
        std::string g = db.execute("SELECT name FROM t WHERE id = 1000");
        CHECK(has(g, big));

        std::string ex = db.execute("EXPLAIN SELECT name FROM t WHERE id = 5 ORDER BY name LIMIT 1");
        std::cout << ex << "\n";
        CHECK(has(ex, "PkLookup") && !has(ex, "Sort"));
        CHECK(has(db.execute("EXPLAIN SELECT * FROM t ORDER BY name"), "Sort"));
        CHECK(!has(db.execute("EXPLAIN SELECT * FROM t ORDER BY id"), "Sort"));

        // statement fail => adhura kaam undo (duplicate pk)
        bool threw = false;
        try { db.execute("INSERT INTO t VALUES (5, 'dup')"); } catch (...) { threw = true; }
        CHECK(threw);
        CHECK(has(db.execute("SELECT * FROM t WHERE id = 5"), "(1 rows)"));
    }
    {   // reopen: sab persist
        Database db("t_e.db");
        CHECK(has(db.execute("SELECT * FROM t"), "(401 rows)"));
        CHECK(has(db.execute("SELECT name FROM t WHERE id = 1000"), "xxxx"));
    }
    clean("t_cache.db"); clean("t_e.db");
    std::cout << "ALL OK\n";
    return 0;
}