#pragma once
#include <cstdint>
#include "common.h"
#include "slotted_page.h"

namespace mkdb {

enum class NodeType : uint8_t { Leaf = 1, Internal = 2 };

enum class InsertResult { Ok = 0, Duplicate = 1, Full = 2 };

// ek page ko B-Tree node ki tarah dekhta hai.
// pehle 8 bytes meta, uske baad slotted page
class BTreeNode {
public:
    explicit BTreeNode(char* buf);

    void init(NodeType t);

    NodeType type() const;
    bool is_leaf() const { return type() == NodeType::Leaf; }

    PageId right_ptr() const;
    void set_right_ptr(PageId p);

    uint16_t count() const { return sp_.num_slots(); }
    uint16_t free_space() const { return sp_.free_space(); }

    // i-th cell ki key
    int64_t key_at(uint16_t i) const;

    // binary search: pehla index jahan key >= di hui key.
    // found me bata deta hai barabar mila ya nahi
    uint16_t lower_bound(int64_t key, bool* found) const;

    // ---------- leaf ----------

    // key sorted jagah pe daalo
    InsertResult leaf_insert(int64_t key, const char* val, uint16_t vlen);

    // i-th cell ki value
    const char* leaf_value(uint16_t i, uint16_t* vlen) const;

    // key se value dhoondo, nahi mili toh nullptr
    const char* leaf_get(int64_t key, uint16_t* vlen) const;

    // ---------- internal ----------
    // cell = key(8 bytes) + child page(4 bytes)
    // child[i] me wo keys jo key[i] se chhoti hon.
    // right_ptr me wo keys jo sabse badi key se barabar ya badi hon

    // di hui key kis child me jayegi uska index (count() matlab right_ptr)
    uint16_t find_child_index(int64_t key) const;

    // i-th child ka page number. i == count() pe right_ptr
    PageId child_at(uint16_t i) const;
    void set_child_at(uint16_t i, PageId p);

    InsertResult internal_insert(uint16_t idx, int64_t key, PageId child);

    // ---------- split ----------

    // upar ka aadha hissa right (khali, init kiya hua leaf) me bhej do.
    // separator key (right ki pehli key) wapas deta hai.
    // right_ptr ka link caller jodta hai
    int64_t split_leaf(BTreeNode& right);

    // beech ki key upar nikal ke wapas deta hai. us key ka child
    // left ka right_ptr ban jaata hai. right_ptr khud sambhal leta hai
    int64_t split_internal(BTreeNode& right);

private:
    char* buf_;
    SlottedPage sp_;

    // sirf pehle n cells rakho, baaki hata do
    void truncate(uint16_t n);
};

}  // namespace mkdb