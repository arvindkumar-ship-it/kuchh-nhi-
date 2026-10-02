#pragma once
#include <string>
#include <vector>
#include "common.h"
#include "pager.h"
#include "btree_node.h"

namespace mkdb {

// ek value is se badi nahi (badi values ke liye overflow pages baad me)
const uint16_t MAX_VALUE_SIZE = 1000;

class BTree {
public:
    // nayi khali tree: ek khali leaf banata hai, uska page number deta hai.
    // ye page number (root) tree ki poori zindagi me wahi rehta hai
    static PageId create(Pager& pager);

    BTree(Pager& pager, PageId root);

    // false agar key pehle se hai
    bool insert(int64_t key, const std::string& value);

    // mil gayi toh true aur value out me
    bool get(int64_t key, std::string* out);

    // saari keys leaf chain se, sorted order me
    std::vector<int64_t> all_keys();

    // kitne level hain (sirf leaf = 1)
    int height();

    PageId root() const { return root_; }

private:
    // child split hua toh ye parent ko batata hai
    struct Split {
        bool happened = false;
        int64_t sep = 0;
        PageId new_page = INVALID_PAGE;
    };

    Pager& pager_;
    PageId root_;

    InsertResult insert_rec(PageId pid, int64_t key, const std::string& val, Split* split);
};

}  // namespace mkdb