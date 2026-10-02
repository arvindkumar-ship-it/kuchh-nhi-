#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <cstdio>
#include "btree.h"

using namespace mkdb;

// har key ki value 100 bytes ki, key se banti hai, taaki baad me check ho sake
static std::string make_val(int64_t k) {
    std::string v = "val" + std::to_string(k) + "_";
    v.resize(100, '#');
    return v;
}

int main() {
    std::remove("tree.db");
    const int N = 20000;

    // 1..N ki keys ulti seedhi order me
    std::vector<int64_t> keys(N);
    for (int i = 0; i < N; i++) keys[i] = i + 1;
    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    PageId root;
    {
        Pager p("tree.db");
        root = BTree::create(p);
        BTree t(p, root);
        for (size_t i = 0; i < keys.size(); i++) {
            if (!t.insert(keys[i], make_val(keys[i]))) {
                std::cout << "galat duplicate: " << keys[i] << "\n";
                return 1;
            }
        }
        p.flush();
        std::cout << "inserted = " << N << ", height = " << t.height()
                  << ", root page = " << t.root()
                  << ", pages = " << p.page_count() << "\n";
        std::cout << "duplicate 777 -> " << t.insert(777, "x") << "\n";
    }

    // file band karke dobara kholo, sab kuch disk se aana chahiye
    Pager p2("tree.db");
    BTree t2(p2, root);

    std::vector<int64_t> all = t2.all_keys();
    std::cout << "disk se keys = " << all.size()
              << ", sorted = " << std::is_sorted(all.begin(), all.end()) << "\n";

    int bad = 0;
    for (int64_t k = 1; k <= N; k++) {
        std::string v;
        if (!t2.get(k, &v) || v != make_val(k)) bad++;
    }
    std::cout << "galat/missing = " << bad << "\n";

    std::string v;
    std::cout << "key 0 -> " << t2.get(0, &v)
              << ", key 20001 -> " << t2.get(20001, &v) << "\n";

    t2.get(12345, &v);
    std::cout << "key 12345 -> " << v.substr(0, 12) << "\n";
    return 0;
}