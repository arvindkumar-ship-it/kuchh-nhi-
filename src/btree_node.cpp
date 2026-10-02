#include "btree_node.h"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace mkdb {

static const uint32_t META_SIZE   = 8;
static const uint32_t OFF_TYPE    = 0;
static const uint32_t OFF_RIGHT   = 4;
static const uint32_t KEY_SIZE    = 8;
static const uint32_t CHILD_SIZE  = 4;

BTreeNode::BTreeNode(char* buf)
    : buf_(buf), sp_(buf + META_SIZE, PAGE_SIZE - META_SIZE) {}

void BTreeNode::init(NodeType t) {
    std::memset(buf_, 0, META_SIZE);
    buf_[OFF_TYPE] = (char)t;
    set_right_ptr(INVALID_PAGE);
    sp_.init();
}

NodeType BTreeNode::type() const {
    return (NodeType)buf_[OFF_TYPE];
}

PageId BTreeNode::right_ptr() const {
    PageId p;
    std::memcpy(&p, buf_ + OFF_RIGHT, sizeof(p));
    return p;
}

void BTreeNode::set_right_ptr(PageId p) {
    std::memcpy(buf_ + OFF_RIGHT, &p, sizeof(p));
}

int64_t BTreeNode::key_at(uint16_t i) const {
    uint16_t len;
    const char* p = sp_.get(i, &len);
    int64_t k;
    std::memcpy(&k, p, KEY_SIZE);
    return k;
}

uint16_t BTreeNode::lower_bound(int64_t key, bool* found) const {
    uint16_t lo = 0;
    uint16_t hi = count();

    // [lo, hi) me dhoondo. beech wali key chhoti hai toh dayein jao, warna bayein
    while (lo < hi) {
        uint16_t mid = (lo + hi) / 2;
        if (key_at(mid) < key) lo = mid + 1;
        else hi = mid;
    }

    *found = (lo < count() && key_at(lo) == key);
    return lo;
}

// ---------- leaf ----------

InsertResult BTreeNode::leaf_insert(int64_t key, const char* val, uint16_t vlen) {
    bool found;
    uint16_t idx = lower_bound(key, &found);
    if (found) return InsertResult::Duplicate;

    // cell = key ke 8 bytes + value
    std::vector<char> cell(KEY_SIZE + vlen);
    std::memcpy(cell.data(), &key, KEY_SIZE);
    std::memcpy(cell.data() + KEY_SIZE, val, vlen);

    if (!sp_.insert_at(idx, cell.data(), (uint16_t)cell.size())) {
        return InsertResult::Full;
    }
    return InsertResult::Ok;
}

const char* BTreeNode::leaf_value(uint16_t i, uint16_t* vlen) const {
    uint16_t len;
    const char* p = sp_.get(i, &len);
    *vlen = len - KEY_SIZE;
    return p + KEY_SIZE;
}

const char* BTreeNode::leaf_get(int64_t key, uint16_t* vlen) const {
    bool found;
    uint16_t idx = lower_bound(key, &found);
    if (!found) return nullptr;
    return leaf_value(idx, vlen);
}

// ---------- internal ----------

uint16_t BTreeNode::find_child_index(int64_t key) const {
    uint16_t lo = 0;
    uint16_t hi = count();

    // pehli key dhoondo jo di hui key se STRICTLY badi ho
    while (lo < hi) {
        uint16_t mid = (lo + hi) / 2;
        if (key_at(mid) <= key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

PageId BTreeNode::child_at(uint16_t i) const {
    if (i >= count()) return right_ptr();

    uint16_t len;
    const char* p = sp_.get(i, &len);
    PageId c;
    std::memcpy(&c, p + KEY_SIZE, CHILD_SIZE);
    return c;
}

void BTreeNode::set_child_at(uint16_t i, PageId pid) {
    if (i >= count()) {
        set_right_ptr(pid);
        return;
    }
    uint16_t len;
    // buf_ apna hi buffer hai, writable hai, isliye const hatana safe
    char* p = const_cast<char*>(sp_.get(i, &len));
    std::memcpy(p + KEY_SIZE, &pid, CHILD_SIZE);
}

InsertResult BTreeNode::internal_insert(uint16_t idx, int64_t key, PageId child) {
    char cell[KEY_SIZE + CHILD_SIZE];
    std::memcpy(cell, &key, KEY_SIZE);
    std::memcpy(cell + KEY_SIZE, &child, CHILD_SIZE);

    if (!sp_.insert_at(idx, cell, (uint16_t)sizeof(cell))) {
        return InsertResult::Full;
    }
    return InsertResult::Ok;
}

// ---------- split ----------

void BTreeNode::truncate(uint16_t n) {
    while (sp_.num_slots() > n) {
        sp_.remove_at(sp_.num_slots() - 1);
    }
    sp_.compact();  // hataye hue cells ki jagah wapas free
}

int64_t BTreeNode::split_leaf(BTreeNode& right) {
    uint16_t n = count();
    if (n < 2) throw std::logic_error("split_leaf: 2 se kam cells");

    // saare cells ka kul size (4 = cell ki length field + slot)
    uint32_t total = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t len;
        sp_.get(i, &len);
        total += len + 4;
    }

    // wahan tak chalo jahan tak aadhe bytes ho jayein
    uint32_t acc = 0;
    uint16_t mid = 0;
    while (mid < n - 1 && acc < total / 2) {
        uint16_t len;
        sp_.get(mid, &len);
        acc += len + 4;
        mid++;
    }

    // mid se aage ke cells right me
    for (uint16_t i = mid; i < n; i++) {
        uint16_t len;
        const char* p = sp_.get(i, &len);
        right.sp_.insert_at(right.count(), p, len);
    }
    int64_t sep = right.key_at(0);

    truncate(mid);
    return sep;
}

int64_t BTreeNode::split_internal(BTreeNode& right) {
    uint16_t n = count();
    if (n < 3) throw std::logic_error("split_internal: 3 se kam cells");

    uint16_t mid = n / 2;

    // beech wala cell: iski key upar jayegi, child left ka right_ptr banega
    uint16_t len;
    const char* p = sp_.get(mid, &len);
    int64_t mkey;
    PageId mchild;
    std::memcpy(&mkey, p, KEY_SIZE);
    std::memcpy(&mchild, p + KEY_SIZE, CHILD_SIZE);

    // mid ke baad wale cells right me
    for (uint16_t i = mid + 1; i < n; i++) {
        const char* q = sp_.get(i, &len);
        right.sp_.insert_at(right.count(), q, len);
    }

    right.set_right_ptr(right_ptr());
    set_right_ptr(mchild);

    truncate(mid);
    return mkey;
}

}  // namespace mkdb