#pragma once
#include <string>
#include <vector>
#include <utility>
#include "common.h"
#include "pager.h"
#include "btree_node.h"

namespace mkdb {

// is size tak value leaf cell me inline; usse badi overflow pages ki chain me
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

    // key hata do. true agar thi
    bool remove(int64_t key);

    // row-by-row stream: leaf chain pe ek page ek time pe memory me
    class Cursor {
    public:
        explicit Cursor(BTree& t);
        bool next(int64_t* key, std::string* value);
    private:
        BTree& t_;
        PageId pid_;
        uint16_t idx_ = 0;
        char buf_[PAGE_SIZE];
    };

    // saari (key, value) leaf chain se, sorted
    std::vector<std::pair<int64_t, std::string>> scan();

    // saari keys leaf chain se, sorted order me
    std::vector<int64_t> all_keys();

    // kitne level hain (sirf leaf = 1)
    int height();

    PageId root() const { return root_; }

    // poori tree (root, internal, leaf, overflow pages) freelist me wapas. iske baad ye tree use mat karo
    void destroy();

private:
    // child split hua toh ye parent ko batata hai
    struct Split {
        bool happened = false;
        int64_t sep = 0;
        PageId new_page = INVALID_PAGE;
    };

    Pager& pager_;
    PageId root_;

    // leaf me raw bytes: [tag][...]. tag 0 = inline value, 1 = overflow (len4, first_page4)
    std::string encode_value(const std::string& v);
    std::string decode_value(const char* p, size_t len);
    bool find_raw(int64_t key, std::string* raw);

    bool remove_rec(PageId pid, int64_t key);
    void rebalance_child(PageId pid, char* pbuf, uint16_t idx);  // underfull child ko sibling se merge
    void destroy_rec(PageId pid);
    void free_value(const char* p, size_t len);                  // overflow chain wapas freelist me

    InsertResult insert_rec(PageId pid, int64_t key, const std::string& val, Split* split);
};

}  // namespace mkdb