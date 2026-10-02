#include <iostream>
#include <string>
#include "btree_node.h"

using namespace mkdb;

int main() {
    char buf[PAGE_SIZE];
    BTreeNode n(buf);
    n.init(NodeType::Leaf);

    std::cout << "type leaf = " << (int)n.type()
              << ", right = " << n.right_ptr() << "\n";

    // keys ulti seedhi order me daalo, node khud sorted rakhega
    int64_t keys[] = {50, 10, 30, 20, 40};
    for (int64_t k : keys) {
        std::string v = "v" + std::to_string(k);
        n.leaf_insert(k, v.c_str(), (uint16_t)v.size());
    }

    std::cout << "keys:";
    for (uint16_t i = 0; i < n.count(); i++) std::cout << " " << n.key_at(i);
    std::cout << "\n";

    bool found;
    uint16_t idx = n.lower_bound(30, &found);
    std::cout << "find 30 -> found=" << found << " idx=" << idx << "\n";
    idx = n.lower_bound(35, &found);
    std::cout << "find 35 -> found=" << found << " idx=" << idx << "\n";

    InsertResult r = n.leaf_insert(30, "x", 1);
    std::cout << "duplicate 30 -> " << (int)r << "\n";

    uint16_t vlen;
    const char* v = n.leaf_get(20, &vlen);
    std::cout << "value of 20 = " << std::string(v, vlen) << "\n";

    // ek naya leaf bharo jab tak full na ho
    char buf2[PAGE_SIZE];
    BTreeNode m(buf2);
    m.init(NodeType::Leaf);
    std::string big(100, 'x');
    int count = 0;
    while (m.leaf_insert(1000 + count, big.c_str(), 100) == InsertResult::Ok) count++;
    std::cout << "fill: " << count << " records, free = " << m.free_space() << "\n";

    n.set_right_ptr(7);
    std::cout << "right_ptr set karke = " << n.right_ptr() << "\n";
    return 0;
}