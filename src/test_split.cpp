#include <iostream>
#include <string>
#include "btree_node.h"

using namespace mkdb;

static void print_leaf(const char* name, const BTreeNode& n) {
    std::cout << name << ": count=" << n.count()
              << " first=" << n.key_at(0)
              << " last=" << n.key_at(n.count() - 1)
              << " free=" << n.free_space() << "\n";
}

static void print_keys(const char* name, const BTreeNode& n) {
    std::cout << name << " (" << n.count() << "):";
    for (uint16_t i = 0; i < n.count(); i++) std::cout << " " << n.key_at(i);
    std::cout << " | right_ptr = " << n.right_ptr() << "\n";
}

int main() {
    std::cout << "--- leaf split ---\n";
    char b1[PAGE_SIZE], b2[PAGE_SIZE];
    BTreeNode left(b1), right(b2);
    left.init(NodeType::Leaf);
    right.init(NodeType::Leaf);

    std::string big(100, 'x');
    for (int k = 1; k <= 36; k++) left.leaf_insert(k, big.c_str(), 100);

    int64_t sep = left.split_leaf(right);
    std::cout << "sep = " << sep << "\n";
    print_leaf("left ", left);
    print_leaf("right", right);

    // ab right me jagah bani, naya key jaa sakta hai
    InsertResult r = right.leaf_insert(100, big.c_str(), 100);
    std::cout << "naya key 100 right me = " << (int)r
              << ", right count = " << right.count() << "\n";

    std::cout << "--- internal ---\n";
    char b3[PAGE_SIZE], b4[PAGE_SIZE];
    BTreeNode in(b3), in2(b4);
    in.init(NodeType::Internal);
    in2.init(NodeType::Internal);

    in.internal_insert(0, 10, 100);
    in.internal_insert(1, 20, 101);
    in.internal_insert(2, 30, 102);
    in.internal_insert(3, 40, 103);
    in.internal_insert(4, 50, 104);
    in.set_right_ptr(105);

    int64_t tests[] = {5, 10, 25, 50, 99};
    for (int64_t k : tests) {
        std::cout << "key " << k << " -> child "
                  << in.child_at(in.find_child_index(k)) << "\n";
    }

    std::cout << "--- internal split ---\n";
    int64_t med = in.split_internal(in2);
    std::cout << "median = " << med << "\n";
    print_keys("left ", in);
    print_keys("right", in2);
    return 0;
}