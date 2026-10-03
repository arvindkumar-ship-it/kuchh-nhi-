#include "index.h"
#include <cstring>
#include <stdexcept>

namespace mkdb {

namespace {
struct Entry {
    std::string v;
    int64_t pk;
};

std::string venc(const Value& v) {
    if (v.type == ColType::Int) return std::string((const char*)&v.i, 8);
    return v.s;
}

std::vector<Entry> parse_bucket(const std::string& b) {
    std::vector<Entry> out;
    size_t p = 0;
    while (p < b.size()) {
        if (p + 4 > b.size()) throw std::runtime_error("index bucket kharab hai");
        uint32_t n;
        std::memcpy(&n, b.data() + p, 4);
        p += 4;
        if (p + n + 8 > b.size()) throw std::runtime_error("index bucket kharab hai");
        Entry e;
        e.v.assign(b.data() + p, n);
        p += n;
        std::memcpy(&e.pk, b.data() + p, 8);
        p += 8;
        out.push_back(e);
    }
    return out;
}

std::string dump_bucket(const std::vector<Entry>& es) {
    std::string b;
    for (const Entry& e : es) {
        uint32_t n = (uint32_t)e.v.size();
        b.append((const char*)&n, 4);
        b += e.v;
        b.append((const char*)&e.pk, 8);
    }
    return b;
}

void store(BTree& t, int64_t key, const std::vector<Entry>& es) {
    t.remove(key);
    if (!es.empty()) t.insert(key, dump_bucket(es));
}
}  // namespace

int64_t index_key(const Value& v) {
    if (v.type == ColType::Int) return v.i;
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char c : v.s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return (int64_t)h;
}

bool value_equal(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    return a.type == ColType::Int ? a.i == b.i : a.s == b.s;
}

std::vector<int64_t> index_lookup(Pager& p, PageId root, const Value& v) {
    BTree t(p, root);
    std::vector<int64_t> pks;
    std::string raw;
    if (!t.get(index_key(v), &raw)) return pks;
    std::string want = venc(v);
    for (const Entry& e : parse_bucket(raw)) {
        if (e.v == want) pks.push_back(e.pk);  // collision wale alag value ke entries chhodo
    }
    return pks;
}

void index_add(Pager& p, PageId root, const Value& v, int64_t pk) {
    BTree t(p, root);
    int64_t key = index_key(v);
    std::vector<Entry> es;
    std::string raw;
    if (t.get(key, &raw)) es = parse_bucket(raw);
    es.push_back(Entry{venc(v), pk});
    store(t, key, es);
}

void index_remove(Pager& p, PageId root, const Value& v, int64_t pk) {
    BTree t(p, root);
    int64_t key = index_key(v);
    std::string raw;
    if (!t.get(key, &raw)) return;
    std::vector<Entry> es = parse_bucket(raw), keep;
    std::string want = venc(v);
    for (const Entry& e : es) {
        if (!(e.v == want && e.pk == pk)) keep.push_back(e);
    }
    store(t, key, keep);
}

}  // namespace mkdb
