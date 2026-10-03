// freelist + B-Tree merge/rebalance + fsync'd commit/reopen
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include "btree.h"
#include "database.h"
#include "pager.h"

using namespace mkdb;
#define CHECK(c) do { if (!(c)) { std::cout << "FAIL line " << __LINE__ << ": " #c "\n"; return 1; } } while (0)
static void clean(const std::string& p) { std::remove(p.c_str()); std::remove((p + "-journal").c_str()); }
static bool same(const std::vector<std::pair<int64_t, std::string>>& s, const std::map<int64_t, std::string>& m) {
    if (s.size() != m.size()) return false;
    size_t i = 0;
    for (auto& kv : m) {
        if (s[i].first != kv.first || s[i].second != kv.second) return false;
        i++;
    }
    return true;
}
static bool has(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

int main() {
    // 1. freelist: free hui page wapas milti hai
    clean("t_s1.db");
    {
        Pager p("t_s1.db");
        p.allocate_page();  // page 0 = header jaisa
        p.enable_freelist();
        PageId a = p.allocate_page(), b = p.allocate_page(), c = p.allocate_page();
        uint32_t n = p.page_count();
        p.free_page(b);
        p.free_page(a);
        CHECK(p.allocate_page() == a);   // LIFO
        CHECK(p.allocate_page() == b);
        CHECK(p.page_count() == n);      // file badhi nahi
        CHECK(p.allocate_page() == n);   // freelist khali => end me
        (void)c;
        p.flush();
    }
    std::cout << "freelist ok\n";

    // 2. B-Tree: bahut delete ke baad merge, height kam, pages wapas, scan sahi
    clean("t_s2.db");
    {
        Pager p("t_s2.db", 32);
        p.allocate_page();
        p.enable_freelist();
        PageId root = BTree::create(p);
        BTree t(p, root);
        std::map<int64_t, std::string> ref;
        const int N = 30000;
        for (int i = 0; i < N; i++) {
            std::string v = "value-" + std::to_string(i) + std::string(40, 'x');
            CHECK(t.insert(i, v));
            ref[i] = v;
        }
        int h0 = t.height();
        uint32_t pages0 = p.page_count();
        CHECK(h0 >= 3);

        srand(7);
        std::vector<int> keys;
        for (int i = 0; i < N; i++) keys.push_back(i);
        for (int i = N - 1; i > 0; i--) std::swap(keys[i], keys[rand() % (i + 1)]);
        for (int i = 0; i < N - 20; i++) {
            CHECK(t.remove(keys[i]));
            ref.erase(keys[i]);
            if (i % 7000 == 0) {  // beech me bhi tree sahi
                auto s = t.scan();
                CHECK(same(s, ref));
            }
        }
        auto s = t.scan();
        CHECK(s.size() == 20 && same(s, ref));
        CHECK(t.height() < h0);
        CHECK(!t.remove(keys[0]));  // pehle hati hui

        for (int i = N - 20; i < N; i++) CHECK(t.remove(keys[i]));
        CHECK(t.scan().empty());
        CHECK(t.height() == 1);

        // dobara bharo: freelist se pages aate hain, file nahi badhti
        for (int i = 0; i < N; i++) CHECK(t.insert(i, "value-" + std::to_string(i) + std::string(40, 'x')));
        CHECK(p.page_count() <= pages0 + pages0 / 10);
        CHECK(t.scan().size() == (size_t)N);
        p.flush();
    }
    {   // reopen
        Pager p("t_s2.db", 32);
        BTree t(p, 1);
        CHECK(t.scan().size() == 30000);
    }
    std::cout << "btree merge ok\n";

    // 3. Database: overflow rows delete/update => pages reuse
    clean("t_s3.db");
    {
        Database db("t_s3.db");
        db.execute("CREATE TABLE b (id INT PRIMARY KEY, v TEXT)");
        std::string big(20000, 'q');
        for (int i = 0; i < 20; i++) db.execute("INSERT INTO b VALUES (" + std::to_string(i) + ", '" + big + "')");
        uint32_t full = db.page_count();
        db.execute("DELETE FROM b WHERE id >= 0");
        CHECK(has(db.execute("SELECT * FROM b"), "(0 rows)"));
        for (int i = 0; i < 20; i++) db.execute("INSERT INTO b VALUES (" + std::to_string(i) + ", '" + big + "')");
        CHECK(db.page_count() <= full + 2);
        for (int r = 0; r < 5; r++) db.execute("UPDATE b SET v = '" + std::string(20000, (char)('a' + r)) + "' WHERE id >= 0");
        CHECK(db.page_count() <= full + 30);   // update ke baar-baar chakkar me file nahi phoolti
        CHECK(has(db.execute("SELECT v FROM b WHERE id = 3"), std::string(20000, 'e')));
    }
    {
        Database db("t_s3.db");  // freelist head header me bacha raha
        CHECK(has(db.execute("SELECT * FROM b"), "(20 rows)"));
        uint32_t n = db.page_count();
        db.execute("DELETE FROM b WHERE id >= 0");
        db.execute("INSERT INTO b VALUES (1, 'small')");
        CHECK(db.page_count() == n);
    }
    clean("t_s1.db"); clean("t_s2.db"); clean("t_s3.db");
    std::cout << "STORAGE OK\n";
    return 0;
}
