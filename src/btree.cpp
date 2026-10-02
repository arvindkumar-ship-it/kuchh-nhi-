#include "btree.h"
#include <stdexcept>

namespace mkdb {

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
    if (value.size() > MAX_VALUE_SIZE) {
        throw std::runtime_error("value bahut badi hai");
    }

    Split sp;
    InsertResult r = insert_rec(root_, key, value, &sp);
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

bool BTree::get(int64_t key, std::string* out) {
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

bool BTree::remove(int64_t key) {
    PageId pid = root_;
    while (true) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);
        if (!n.is_leaf()) {
            pid = n.child_at(n.find_child_index(key));
            continue;
        }
        bool found;
        uint16_t i = n.lower_bound(key, &found);
        if (!found) return false;
        n.leaf_remove(i);
        pager_.write_page(pid, buf);
        return true;
    }
}

std::vector<std::pair<int64_t, std::string>> BTree::scan() {
    std::vector<std::pair<int64_t, std::string>> out;
    PageId pid = root_;
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);
        if (n.is_leaf()) break;
        pid = n.child_at(0);
    }
    while (pid != INVALID_PAGE) {
        char buf[PAGE_SIZE];
        pager_.read_page(pid, buf);
        BTreeNode n(buf);
        for (uint16_t i = 0; i < n.count(); i++) {
            uint16_t len;
            const char* v = n.leaf_value(i, &len);
            out.emplace_back(n.key_at(i), std::string(v, len));
        }
        pid = n.right_ptr();
    }
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