#include "btree.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace mkdb {

static const char TAG_INLINE = 0, TAG_OVERFLOW = 1;
// overflow page: next(4) | chunk_len(2) | data
static const uint32_t OVF_HDR = 6, OVF_CAP = PAGE_SIZE - OVF_HDR;

std::string BTree::encode_value(const std::string& v) {
    if (v.size() <= MAX_VALUE_SIZE) return std::string(1, TAG_INLINE) + v;

    size_t n = (v.size() + OVF_CAP - 1) / OVF_CAP;
    std::vector<PageId> ids(n);
    for (size_t i = 0; i < n; i++) ids[i] = pager_.allocate_page();
    for (size_t i = 0; i < n; i++) {
        char buf[PAGE_SIZE] = {0};
        PageId next = (i + 1 < n) ? ids[i + 1] : INVALID_PAGE;
        uint16_t len = (uint16_t)std::min<size_t>(OVF_CAP, v.size() - i * OVF_CAP);
        std::memcpy(buf, &next, 4);
        std::memcpy(buf + 4, &len, 2);
        std::memcpy(buf + OVF_HDR, v.data() + i * OVF_CAP, len);
        pager_.write_page(ids[i], buf);
    }
    std::string cell(1, TAG_OVERFLOW);
    uint32_t total = (uint32_t)v.size();
    cell.append((const char*)&total, 4);
    cell.append((const char*)&ids[0], 4);
    return cell;
}

std::string BTree::decode_value(const char* p, size_t len) {
    if (len < 1) throw std::runtime_error("btree: khali cell");
    if (p[0] == TAG_INLINE) return std::string(p + 1, len - 1);
    if (p[0] != TAG_OVERFLOW || len < 9) throw std::runtime_error("btree: cell kharab hai");

    uint32_t total;
    PageId pid;
    std::memcpy(&total, p + 1, 4);
    std::memcpy(&pid, p + 5, 4);
    std::string out;
    out.reserve(total);
    while (pid != INVALID_PAGE && out.size() < total) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        uint16_t clen;
        std::memcpy(&pid, buf, 4);
        std::memcpy(&clen, buf + 4, 2);
        out.append(buf + OVF_HDR, clen);
    }
    if (out.size() != total) throw std::runtime_error("btree: overflow chain adhuri hai");
    return out;
}

PageId BTree::create(Pager& pager) {
    PageId pid = pager.allocate_page();
    char buf[PAGE_SIZE];
    BTreeNode n(buf);
    n.init(NodeType::Leaf);
    pager.write_page(pid, buf);
    return pid;
}

BTree::BTree(Pager& pager, PageId root) : pager_(pager), root_(root) {
    if (root == INVALID_PAGE || root >= pager.page_count()) {
        throw std::runtime_error("btree: root page galat hai");
    }
}

bool BTree::insert(int64_t key, const std::string& value) {
    // duplicate pe overflow pages waste na ho, isliye badi value ke liye pehle check
    if (value.size() > MAX_VALUE_SIZE) {
        std::string tmp;
        if (find_raw(key, &tmp)) return false;
    }
    std::string cell = encode_value(value);

    Split sp;
    InsertResult r = insert_rec(root_, key, cell, &sp);
    if (r == InsertResult::Duplicate) return false;

    // root hi split ho gaya. root ka page number badalna nahi hai,
    // isliye root ka (left wala) content nayi page me, aur root page ko
    // naye internal node se overwrite
    if (sp.happened) {
        char old[PAGE_SIZE];
        pager_.read_page(root_, old);
        PageId left = pager_.allocate_page();
        pager_.write_page(left, old);

        char buf[PAGE_SIZE];
        BTreeNode n(buf);
        n.init(NodeType::Internal);
        n.internal_insert(0, sp.sep, left);  // sep se chhoti keys left me
        n.set_right_ptr(sp.new_page);        // baaki nayi page me
        pager_.write_page(root_, buf);
    }
    return true;
}

InsertResult BTree::insert_rec(PageId pid, int64_t key, const std::string& val, Split* split) {
    char buf[PAGE_SIZE];
    pager_.read_page(pid, buf);
    BTreeNode node(buf);

    // ---------- leaf ----------
    if (node.is_leaf()) {
        InsertResult r = node.leaf_insert(key, val.data(), (uint16_t)val.size());
        if (r != InsertResult::Full) {
            if (r == InsertResult::Ok) pager_.write_page(pid, buf);
            return r;  // Ok ya Duplicate
        }

        // jagah nahi, split karo
        PageId new_pid = pager_.allocate_page();
        char rbuf[PAGE_SIZE];
        BTreeNode right(rbuf);
        right.init(NodeType::Leaf);

        int64_t sep = node.split_leaf(right);

        // leaf chain: node -> right -> (node ka purana agla)
        right.set_right_ptr(node.right_ptr());
        node.set_right_ptr(new_pid);

        // nayi key jis taraf ki hai wahin daalo
        BTreeNode& target = (key >= sep) ? right : node;
        if (target.leaf_insert(key, val.data(), (uint16_t)val.size()) != InsertResult::Ok) {
            throw std::logic_error("split ke baad bhi leaf me jagah nahi");
        }

        pager_.write_page(pid, buf);
        pager_.write_page(new_pid, rbuf);

        split->happened = true;
        split->sep = sep;
        split->new_page = new_pid;
        return InsertResult::Ok;
    }

    // ---------- internal ----------
    uint16_t idx = node.find_child_index(key);
    PageId child = node.child_at(idx);

    Split cs;
    InsertResult r = insert_rec(child, key, val, &cs);
    if (!cs.happened) return r;

    // child split hua: (sep -> child) cell idx pe daalo,
    // aur purani cell ka child ab nayi page
    InsertResult ir = node.internal_insert(idx, cs.sep, child);
    if (ir == InsertResult::Ok) {
        node.set_child_at(idx + 1, cs.new_page);
        pager_.write_page(pid, buf);
        return InsertResult::Ok;
    }

    // ye node bhi full, ise bhi split karo
    PageId new_pid = pager_.allocate_page();
    char rbuf[PAGE_SIZE];
    BTreeNode right(rbuf);
    right.init(NodeType::Internal);

    int64_t median = node.split_internal(right);

    // jis aadhe me sep aata hai wahin entry daalo
    BTreeNode& t = (cs.sep < median) ? node : right;
    uint16_t i2 = t.find_child_index(cs.sep);
    if (t.internal_insert(i2, cs.sep, child) != InsertResult::Ok) {
        throw std::logic_error("split ke baad bhi internal me jagah nahi");
    }
    t.set_child_at(i2 + 1, cs.new_page);

    pager_.write_page(pid, buf);
    pager_.write_page(new_pid, rbuf);

    split->happened = true;
    split->sep = median;
    split->new_page = new_pid;
    return InsertResult::Ok;
}

bool BTree::find_raw(int64_t key, std::string* out) {
    PageId pid = root_;
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);

        if (n.is_leaf()) {
            uint16_t vlen;
            const char* v = n.leaf_get(key, &vlen);
            if (!v) return false;
            out->assign(v, vlen);
            return true;
        }
        pid = n.child_at(n.find_child_index(key));
    }
    return false;
}

bool BTree::get(int64_t key, std::string* out) {
    std::string raw;
    if (!find_raw(key, &raw)) return false;
    *out = decode_value(raw.data(), raw.size());
    return true;
}

void BTree::free_value(const char* p, size_t len) {
    if (len < 9 || p[0] != TAG_OVERFLOW) return;
    PageId pid;
    std::memcpy(&pid, p + 5, 4);
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        PageId next;
        std::memcpy(&next, buf, 4);
        pager_.free_page(pid);
        pid = next;
    }
}

void BTree::destroy_rec(PageId pid) {
    char buf[PAGE_SIZE];
    pager_.read_page(pid, buf);
    BTreeNode n(buf);
    if (n.is_leaf()) {
        for (uint16_t i = 0; i < n.count(); i++) {
            uint16_t len;
            const char* v = n.leaf_value(i, &len);
            free_value(v, len);
        }
    } else {
        for (uint16_t i = 0; i <= n.count(); i++) destroy_rec(n.child_at(i));
    }
    pager_.free_page(pid);
}

void BTree::destroy() { destroy_rec(root_); }

static uint16_t node_capacity() {
    char b[PAGE_SIZE];
    BTreeNode n(b);
    n.init(NodeType::Leaf);
    return n.free_space();
}

// parent (pbuf) ke idx-wale child ko, agar aadhe se kam bhara ho, sibling se jodo
void BTree::rebalance_child(PageId pid, char* pbuf, uint16_t idx) {
    static const uint16_t CAP = node_capacity();
    BTreeNode parent(pbuf);

    char cbuf[PAGE_SIZE];
    pager_.read_page(parent.child_at(idx), cbuf);
    BTreeNode c(cbuf);
    if (c.live_bytes() * 2 >= CAP) return;  // theek bhara hai

    // pehle right sibling ke saath, na ho paye toh left ke saath
    for (int pass = 0; pass < 2; pass++) {
        int i = (pass == 0) ? (int)idx : (int)idx - 1;  // (i, i+1) ko merge karenge
        if (i < 0 || i >= (int)parent.count()) continue;

        PageId lp = parent.child_at((uint16_t)i), rp = parent.child_at((uint16_t)i + 1);
        char lbuf[PAGE_SIZE], rbuf[PAGE_SIZE];
        pager_.read_page(lp, lbuf);
        pager_.read_page(rp, rbuf);
        BTreeNode l(lbuf), r(rbuf);
        if (l.live_bytes() + r.live_bytes() > CAP) continue;  // ek page me nahi aayenge

        bool ok = true;
        if (l.is_leaf()) {
            for (uint16_t k = 0; k < r.count() && ok; k++) {
                uint16_t len;
                const char* v = r.leaf_value(k, &len);
                ok = l.leaf_insert(r.key_at(k), v, len) == InsertResult::Ok;
            }
            if (ok) l.set_right_ptr(r.right_ptr());  // leaf chain: l -> (r ka agla)
        } else {
            int64_t sep = parent.key_at((uint16_t)i);
            ok = l.internal_insert(l.count(), sep, l.child_at(l.count())) == InsertResult::Ok;
            for (uint16_t k = 0; k < r.count() && ok; k++)
                ok = l.internal_insert(l.count(), r.key_at(k), r.child_at(k)) == InsertResult::Ok;
            if (ok) l.set_right_ptr(r.right_ptr());
        }
        if (!ok) continue;  // jagah nahi nikli, l/r ki copies hi badli thi, disk safe

        pager_.write_page(lp, lbuf);
        parent.set_child_at((uint16_t)i + 1, lp);  // r ki jagah ab l
        parent.internal_remove((uint16_t)i);
        pager_.write_page(pid, pbuf);
        pager_.free_page(rp);
        return;
    }
}

bool BTree::remove_rec(PageId pid, int64_t key) {
    char buf[PAGE_SIZE];
    pager_.read_page(pid, buf);
    BTreeNode n(buf);

    if (n.is_leaf()) {
        bool found;
        uint16_t i = n.lower_bound(key, &found);
        if (!found) return false;
        uint16_t len;
        const char* v = n.leaf_value(i, &len);
        free_value(v, len);  // buf me v abhi valid hai
        n.leaf_remove(i);
        pager_.write_page(pid, buf);
        return true;
    }

    uint16_t idx = n.find_child_index(key);
    if (!remove_rec(n.child_at(idx), key)) return false;
    rebalance_child(pid, buf, idx);
    return true;
}

bool BTree::remove(int64_t key) {
    if (!remove_rec(root_, key)) return false;

    // root me sirf ek hi child bacha toh tree ek level chhoti. root ka page number wahi rehna chahiye,
    // isliye child ka content root page me copy karke child page free
    while (true) {
        char buf[PAGE_SIZE];
        pager_.read_page(root_, buf);
        BTreeNode n(buf);
        if (n.is_leaf() || n.count() > 0) break;
        PageId child = n.right_ptr();
        char cbuf[PAGE_SIZE];
        pager_.read_page(child, cbuf);
        pager_.write_page(root_, cbuf);
        pager_.free_page(child);
    }
    return true;
}

BTree::Cursor::Cursor(BTree& t) : t_(t), pid_(t.root_) {
    while (pid_ != INVALID_PAGE) {
        t_.pager_.read_page(pid_, buf_);
        BTreeNode n(buf_);
        if (n.is_leaf()) return;
        pid_ = n.child_at(0);
    }
}

bool BTree::Cursor::next(int64_t* key, std::string* value) {
    while (pid_ != INVALID_PAGE) {
        BTreeNode n(buf_);
        if (idx_ < n.count()) {
            uint16_t len;
            const char* v = n.leaf_value(idx_, &len);
            *key = n.key_at(idx_);
            *value = t_.decode_value(v, len);
            idx_++;
            return true;
        }
        pid_ = n.right_ptr();
        idx_ = 0;
        if (pid_ != INVALID_PAGE) t_.pager_.read_page(pid_, buf_);
    }
    return false;
}

std::vector<std::pair<int64_t, std::string>> BTree::scan() {
    std::vector<std::pair<int64_t, std::string>> out;
    Cursor c(*this);
    int64_t k;
    std::string v;
    while (c.next(&k, &v)) out.emplace_back(k, v);
    return out;
}

std::vector<int64_t> BTree::all_keys() {
    std::vector<int64_t> keys;

    // sabse left wale leaf tak utro
    PageId pid = root_;
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);
        if (n.is_leaf()) break;
        pid = n.child_at(0);
    }

    // phir leaf chain pe right_ptr follow karte jao
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);
        for (uint16_t i = 0; i < n.count(); i++) keys.push_back(n.key_at(i));
        pid = n.right_ptr();
    }
    return keys;
}

int BTree::height() {
    int h = 0;
    PageId pid = root_;
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);
        h++;
        if (n.is_leaf()) break;
        pid = n.child_at(0);
    }
    return h;
}

}  // namespace mkdb