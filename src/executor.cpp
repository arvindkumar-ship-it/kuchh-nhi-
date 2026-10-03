#include "executor.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <queue>
#include <stdexcept>

namespace mkdb {

// ---------- expression eval ----------

static Value truth(bool b) { return Value::Int(b ? 1 : 0); }

Value eval_expr(const Expr& e, const TableSchema& sc, const Row& row) {
    switch (e.kind) {
        case ExprKind::Number: return Value::Int(e.num);
        case ExprKind::String: return Value::Text(e.text);
        case ExprKind::Column: {
            int c = sc.find_column(e.text);
            if (c < 0) throw std::runtime_error("column nahi mila: " + e.text);
            return row[c];
        }
        case ExprKind::Unary: {
            Value v = eval_expr(*e.lhs, sc, row);
            if (v.type != ColType::Int) throw std::runtime_error("number chahiye");
            if (e.op == Tok::Minus) return Value::Int(-v.i);
            return truth(v.i == 0);  // NOT
        }
        case ExprKind::Binary: {
            Value a = eval_expr(*e.lhs, sc, row), b = eval_expr(*e.rhs, sc, row);
            if (e.op == Tok::And || e.op == Tok::Or) {
                if (a.type != ColType::Int || b.type != ColType::Int)
                    throw std::runtime_error("AND/OR me number chahiye");
                return truth(e.op == Tok::And ? (a.i && b.i) : (a.i || b.i));
            }
            if (a.type != b.type) throw std::runtime_error("number aur text ka mel nahi");
            bool txt = a.type == ColType::Text;
            int cmp = txt ? a.s.compare(b.s) : (a.i < b.i ? -1 : a.i > b.i ? 1 : 0);
            switch (e.op) {
                case Tok::Eq:  return truth(cmp == 0);
                case Tok::Neq: return truth(cmp != 0);
                case Tok::Lt:  return truth(cmp < 0);
                case Tok::Le:  return truth(cmp <= 0);
                case Tok::Gt:  return truth(cmp > 0);
                case Tok::Ge:  return truth(cmp >= 0);
                default: break;
            }
            if (txt) throw std::runtime_error("text pe arithmetic nahi chalta");
            switch (e.op) {
                case Tok::Plus:  return Value::Int(a.i + b.i);
                case Tok::Minus: return Value::Int(a.i - b.i);
                case Tok::Star:  return Value::Int(a.i * b.i);
                case Tok::Slash:
                    if (b.i == 0) throw std::runtime_error("zero se divide");
                    return Value::Int(a.i / b.i);
                default: break;
            }
        }
    }
    throw std::runtime_error("expression samajh nahi aaya");
}

bool row_matches(const Expr* where, const TableSchema& sc, const Row& row) {
    if (!where) return true;
    Value v = eval_expr(*where, sc, row);
    if (v.type != ColType::Int) throw std::runtime_error("WHERE me condition chahiye");
    return v.i != 0;
}

ColType check_expr_type(const Expr& e, const TableSchema& sc) {
    switch (e.kind) {
        case ExprKind::Number: return ColType::Int;
        case ExprKind::String: return ColType::Text;
        case ExprKind::Column: {
            int c = sc.find_column(e.text);
            if (c < 0) throw std::runtime_error("column nahi mila: " + e.text);
            return sc.columns[c].type;
        }
        case ExprKind::Unary:
            if (check_expr_type(*e.lhs, sc) != ColType::Int) throw std::runtime_error("number chahiye");
            return ColType::Int;
        case ExprKind::Binary: {
            ColType a = check_expr_type(*e.lhs, sc), b = check_expr_type(*e.rhs, sc);
            if (e.op == Tok::And || e.op == Tok::Or) {
                if (a != ColType::Int || b != ColType::Int) throw std::runtime_error("AND/OR me number chahiye");
                return ColType::Int;
            }
            if (a != b) throw std::runtime_error("number aur text ka mel nahi");
            switch (e.op) {
                case Tok::Eq: case Tok::Neq: case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge:
                    return ColType::Int;
                default: break;
            }
            if (a == ColType::Text) throw std::runtime_error("text pe arithmetic nahi chalta");
            return ColType::Int;
        }
    }
    throw std::runtime_error("expression samajh nahi aaya");
}

static bool is_const(const Expr& e) { return e.kind == ExprKind::Number || e.kind == ExprKind::String; }

void fold_constants(ExprPtr& e) {
    if (!e) return;
    if (e->lhs) fold_constants(e->lhs);
    if (e->rhs) fold_constants(e->rhs);
    bool un = e->kind == ExprKind::Unary && is_const(*e->lhs);
    bool bi = e->kind == ExprKind::Binary && is_const(*e->lhs) && is_const(*e->rhs);
    if (!un && !bi) return;
    try {
        Value v = eval_expr(*e, TableSchema(), Row());
        if (v.type != ColType::Int) return;
        ExprPtr n(new Expr);
        n->kind = ExprKind::Number;
        n->num = v.i;
        e = std::move(n);
    } catch (const std::runtime_error&) {
        // zero se divide jaisa: jaisa hai waisa chhodo, runtime pe wahi error aayega
    }
}

static std::string pad(int d) { return std::string((size_t)d * 2, ' '); }

// ---------- scan / lookup / filter / limit / project ----------

namespace {

class ScanIter : public Iter {
public:
    ScanIter(Pager& p, PageId root, const TableSchema& sc) : tree_(p, root), cur_(tree_), sc_(sc) {}
    bool next(Row* out) override {
        int64_t k;
        std::string v;
        if (!cur_.next(&k, &v)) return false;
        *out = decode_row(sc_, k, v.data(), v.size());
        return true;
    }
    std::string describe(int d) const override { return pad(d) + "Scan(" + sc_.name + ")\n"; }
private:
    BTree tree_;
    BTree::Cursor cur_;
    const TableSchema& sc_;
};

class PkLookupIter : public Iter {
public:
    PkLookupIter(Pager& p, PageId root, const TableSchema& sc, int64_t key)
        : tree_(p, root), sc_(sc), key_(key) {}
    bool next(Row* out) override {
        if (done_) return false;
        done_ = true;
        std::string v;
        if (!tree_.get(key_, &v)) return false;
        *out = decode_row(sc_, key_, v.data(), v.size());
        return true;
    }
    std::string describe(int d) const override {
        return pad(d) + "PkLookup(" + sc_.name + ", key=" + std::to_string(key_) + ")\n";
    }
private:
    BTree tree_;
    const TableSchema& sc_;
    int64_t key_;
    bool done_ = false;
};

class IndexLookupIter : public Iter {
public:
    IndexLookupIter(Pager& p, PageId root, const TableSchema& sc, const IndexDef& ix, const Value& v)
        : p_(p), tree_(p, root), sc_(sc), ix_(ix), v_(v) {}
    bool next(Row* out) override {
        if (!loaded_) {
            pks_ = index_lookup(p_, ix_.root, v_);
            loaded_ = true;
        }
        while (pos_ < pks_.size()) {
            int64_t pk = pks_[pos_++];
            std::string val;
            if (!tree_.get(pk, &val)) continue;
            *out = decode_row(sc_, pk, val.data(), val.size());
            return true;
        }
        return false;
    }
    std::string describe(int d) const override {
        return pad(d) + "IndexLookup(" + sc_.name + "." + sc_.columns[ix_.col].name + " via " + ix_.name + ")\n";
    }
private:
    Pager& p_;
    BTree tree_;
    const TableSchema& sc_;
    IndexDef ix_;
    Value v_;
    std::vector<int64_t> pks_;
    size_t pos_ = 0;
    bool loaded_ = false;
};

class FilterIter : public Iter {
public:
    FilterIter(IterPtr c, const TableSchema& sc, const Expr* w) : c_(std::move(c)), sc_(sc), w_(w) {}
    bool next(Row* out) override {
        while (c_->next(out)) {
            if (row_matches(w_, sc_, *out)) return true;
        }
        return false;
    }
    std::string describe(int d) const override { return pad(d) + "Filter\n" + c_->describe(d + 1); }
private:
    IterPtr c_;
    const TableSchema& sc_;
    const Expr* w_;
};

class LimitIter : public Iter {
public:
    LimitIter(IterPtr c, int64_t n) : c_(std::move(c)), n_(n) {}
    bool next(Row* out) override {
        if (n_ <= 0) return false;  // child ko aur pull nahi karte
        if (!c_->next(out)) return false;
        n_--;
        return true;
    }
    std::string describe(int d) const override { return pad(d) + "Limit\n" + c_->describe(d + 1); }
private:
    IterPtr c_;
    int64_t n_;
};

class ProjectIter : public Iter {
public:
    ProjectIter(IterPtr c, std::vector<size_t> cols) : c_(std::move(c)), cols_(std::move(cols)) {}
    bool next(Row* out) override {
        Row r;
        if (!c_->next(&r)) return false;
        out->clear();
        for (size_t i : cols_) out->push_back(std::move(r[i]));
        return true;
    }
    std::string describe(int d) const override { return pad(d) + "Project\n" + c_->describe(d + 1); }
private:
    IterPtr c_;
    std::vector<size_t> cols_;
};

// ---------- external merge sort ----------

int cmp_col(const Row& a, const Row& b, int col, bool desc) {
    int c;
    if (a[col].type == ColType::Int) c = a[col].i < b[col].i ? -1 : a[col].i > b[col].i ? 1 : 0;
    else c = a[col].s.compare(b[col].s);
    c = c < 0 ? -1 : c > 0 ? 1 : 0;
    return desc ? -c : c;
}

size_t row_bytes(const Row& r) {
    size_t n = 32;
    for (const Value& v : r) n += 24 + v.s.size();
    return n;
}

void write_row(std::ostream& o, const Row& r) {
    for (const Value& v : r) {
        char t = v.type == ColType::Int ? 0 : 1;
        o.write(&t, 1);
        if (t == 0) {
            o.write((const char*)&v.i, 8);
        } else {
            uint32_t n = (uint32_t)v.s.size();
            o.write((const char*)&n, 4);
            o.write(v.s.data(), n);
        }
    }
}

bool read_row(std::istream& in, size_t ncols, Row* r) {
    r->clear();
    for (size_t c = 0; c < ncols; c++) {
        char t;
        if (!in.read(&t, 1)) return false;
        if (t == 0) {
            int64_t i;
            if (!in.read((char*)&i, 8)) return false;
            r->push_back(Value::Int(i));
        } else {
            uint32_t n;
            if (!in.read((char*)&n, 4)) return false;
            std::string s(n, '\0');
            if (n && !in.read(&s[0], n)) return false;
            r->push_back(Value::Text(s));
        }
    }
    return true;
}

// k sorted run files ka merge, ek time pe ek row
class Merger {
public:
    Merger(const std::vector<std::string>& files, size_t ncols, int col, bool desc)
        : ncols_(ncols), col_(col), desc_(desc), heap_(Worse{this}) {
        for (size_t i = 0; i < files.size(); i++) {
            in_.emplace_back(new std::ifstream(files[i], std::ios::binary));
            Entry e;
            e.run = i;
            if (read_row(*in_[i], ncols_, &e.row)) heap_.push(std::move(e));
        }
    }
    bool pop(Row* out) {
        if (heap_.empty()) return false;
        Entry e = std::move(const_cast<Entry&>(heap_.top()));
        heap_.pop();
        *out = std::move(e.row);
        Entry n;
        n.run = e.run;
        if (read_row(*in_[e.run], ncols_, &n.row)) heap_.push(std::move(n));
        return true;
    }
private:
    struct Entry { Row row; size_t run = 0; };
    struct Worse {
        Merger* m;
        bool operator()(const Entry& a, const Entry& b) const {
            int c = cmp_col(a.row, b.row, m->col_, m->desc_);
            return c != 0 ? c > 0 : a.run > b.run;  // barabar pe pehli run pehle (stable)
        }
    };
    size_t ncols_;
    int col_;
    bool desc_;
    std::vector<std::unique_ptr<std::ifstream>> in_;
    std::priority_queue<Entry, std::vector<Entry>, Worse> heap_;
};

class SortIter : public Iter {
public:
    SortIter(IterPtr c, int col, bool desc, size_t mem) : c_(std::move(c)), col_(col), desc_(desc), mem_(mem) {}
    ~SortIter() override {
        merger_.reset();
        for (auto& f : runs_) std::remove(f.c_str());
    }

    bool next(Row* out) override {
        if (!built_) build();
        if (merger_) return merger_->pop(out);
        if (pos_ >= rows_.size()) return false;
        *out = std::move(rows_[pos_++]);
        return true;
    }
    std::string describe(int d) const override {
        return pad(d) + "Sort(external merge, mem=" + std::to_string(mem_) + "B)\n" + c_->describe(d + 1);
    }

private:
    static const size_t FANIN = 8;
    IterPtr c_;
    int col_;
    bool desc_;
    size_t mem_;
    size_t ncols_ = 0;
    bool built_ = false;
    std::vector<Row> rows_;
    size_t pos_ = 0;
    std::vector<std::string> runs_;
    std::unique_ptr<Merger> merger_;

    std::string temp_name() {
        static std::atomic<unsigned> ctr{0};
        auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        return "mkdb_sort_" + std::to_string((long long)t) + "_" + std::to_string(ctr++) + ".run";
    }
    void sort_chunk() {
        std::stable_sort(rows_.begin(), rows_.end(),
                         [&](const Row& a, const Row& b) { return cmp_col(a, b, col_, desc_) < 0; });
    }
    void spill() {
        sort_chunk();
        std::string name = temp_name();
        runs_.push_back(name);  // pehle register, taaki exception pe bhi delete ho
        std::ofstream o(name, std::ios::binary);
        for (const Row& r : rows_) write_row(o, r);
        if (!o) throw std::runtime_error("sort: temp file likh nahi payi");
        rows_.clear();
    }
    void build() {
        built_ = true;
        size_t bytes = 0;
        Row r;
        while (c_->next(&r)) {
            ncols_ = r.size();
            bytes += row_bytes(r);
            rows_.push_back(std::move(r));
            if (bytes > mem_) { spill(); bytes = 0; }
        }
        if (runs_.empty()) { sort_chunk(); return; }  // sab RAM me aa gaya
        if (!rows_.empty()) spill();

        // runs bahut ho gayi toh multi-pass: FANIN-FANIN ke group merge
        while (runs_.size() > FANIN) {
            std::vector<std::string> next_runs;
            for (size_t i = 0; i < runs_.size(); i += FANIN) {
                size_t e = std::min(runs_.size(), i + FANIN);
                std::vector<std::string> grp(runs_.begin() + i, runs_.begin() + e);
                std::string name = temp_name();
                next_runs.push_back(name);
                {
                    Merger m(grp, ncols_, col_, desc_);
                    std::ofstream o(name, std::ios::binary);
                    Row x;
                    while (m.pop(&x)) write_row(o, x);
                }
                for (auto& f : grp) std::remove(f.c_str());
            }
            runs_ = next_runs;
        }
        merger_.reset(new Merger(runs_, ncols_, col_, desc_));
    }
};

}  // namespace

IterPtr make_scan(Pager& p, PageId root, const TableSchema& sc) { return IterPtr(new ScanIter(p, root, sc)); }
IterPtr make_pk_lookup(Pager& p, PageId root, const TableSchema& sc, int64_t key) {
    return IterPtr(new PkLookupIter(p, root, sc, key));
}
IterPtr make_index_lookup(Pager& p, PageId root, const TableSchema& sc, const IndexDef& ix, const Value& v) {
    return IterPtr(new IndexLookupIter(p, root, sc, ix, v));
}
IterPtr make_filter(IterPtr c, const TableSchema& sc, const Expr* w) {
    return IterPtr(new FilterIter(std::move(c), sc, w));
}
IterPtr make_sort(IterPtr c, int col, bool desc, size_t mem) {
    return IterPtr(new SortIter(std::move(c), col, desc, mem));
}
IterPtr make_limit(IterPtr c, int64_t n) { return IterPtr(new LimitIter(std::move(c), n)); }
IterPtr make_project(IterPtr c, std::vector<size_t> cols) {
    return IterPtr(new ProjectIter(std::move(c), std::move(cols)));
}

// ---------- planner / analyzer / optimizer ----------

// WHERE pk = <number> (ya <number> = pk) ho toh key nikalo
static bool pk_equality(const Expr* w, const TableSchema& sc, int64_t* key) {
    if (!w || w->kind != ExprKind::Binary || w->op != Tok::Eq) return false;
    const Expr *a = w->lhs.get(), *b = w->rhs.get();
    if (a->kind == ExprKind::Number) std::swap(a, b);
    if (a->kind != ExprKind::Column || b->kind != ExprKind::Number) return false;
    if (sc.find_column(a->text) != sc.pk) return false;
    *key = b->num;
    return true;
}

// WHERE ke AND-chain me "indexed column = constant" dhoondo
static bool index_equality(const Expr* w, const TableSchema& sc, const std::vector<IndexDef>& ixs,
                           const IndexDef** found, Value* val) {
    if (!w || w->kind != ExprKind::Binary) return false;
    if (w->op == Tok::And) {
        return index_equality(w->lhs.get(), sc, ixs, found, val) ||
               index_equality(w->rhs.get(), sc, ixs, found, val);
    }
    if (w->op != Tok::Eq) return false;
    const Expr *a = w->lhs.get(), *b = w->rhs.get();
    if (a->kind != ExprKind::Column) std::swap(a, b);
    if (a->kind != ExprKind::Column || !is_const(*b)) return false;
    int c = sc.find_column(a->text);
    if (c < 0) return false;
    for (const IndexDef& ix : ixs) {
        if (ix.col != c) continue;
        Value v = b->kind == ExprKind::Number ? Value::Int(b->num) : Value::Text(b->text);
        if (v.type != sc.columns[c].type) return false;
        *found = &ix;
        *val = v;
        return true;
    }
    return false;
}

IterPtr build_select_plan(Pager& p, PageId root, const TableSchema& sc, const Stmt& s,
                          std::vector<size_t>* out_cols, const std::vector<IndexDef>& indexes,
                          size_t sort_mem) {
    // analyzer: columns exist karte hain?
    out_cols->clear();
    if (s.select_all) {
        for (size_t i = 0; i < sc.columns.size(); i++) out_cols->push_back(i);
    } else {
        for (const std::string& n : s.select_cols) {
            int c = sc.find_column(n);
            if (c < 0) throw std::runtime_error("column nahi mila: " + n);
            out_cols->push_back((size_t)c);
        }
    }
    int oc = -1;
    if (!s.order_col.empty()) {
        oc = sc.find_column(s.order_col);
        if (oc < 0) throw std::runtime_error("column nahi mila: " + s.order_col);
    }

    // optimizer: pk = const => seedha B-Tree lookup, sort bhi bekaar
    IterPtr it;
    bool single = false, sorted_by_pk = false;
    int64_t key;
    if (pk_equality(s.where.get(), sc, &key)) {
        it = make_pk_lookup(p, root, sc, key);
        single = true;
    } else {
        const IndexDef* ix = nullptr;
        Value iv;
        if (index_equality(s.where.get(), sc, indexes, &ix, &iv)) {
            it = make_index_lookup(p, root, sc, *ix, iv);  // secondary index se seedha pks
        } else {
            it = make_scan(p, root, sc);
            sorted_by_pk = true;  // leaf chain pk ke order me hi hai
        }
        if (s.where) it = make_filter(std::move(it), sc, s.where.get());
    }
    if (oc >= 0 && !single && !(sorted_by_pk && oc == sc.pk && !s.order_desc)) {
        it = make_sort(std::move(it), oc, s.order_desc, sort_mem);
    }
    if (s.has_limit) it = make_limit(std::move(it), s.limit);
    return make_project(std::move(it), *out_cols);
}

}  // namespace mkdb
