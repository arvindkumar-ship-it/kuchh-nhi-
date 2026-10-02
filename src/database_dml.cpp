#include "database.h"
#include <algorithm>
#include <stdexcept>

namespace mkdb {

static Value truth(bool b) { return Value::Int(b ? 1 : 0); }

// expression ko ek row pe chalao
static Value eval(const Expr& e, const TableSchema& sc, const std::vector<Value>& row) {
    switch (e.kind) {
        case ExprKind::Number: return Value::Int(e.num);
        case ExprKind::String: return Value::Text(e.text);
        case ExprKind::Column: {
            int c = sc.find_column(e.text);
            if (c < 0) throw std::runtime_error("column nahi mila: " + e.text);
            return row[c];
        }
        case ExprKind::Unary: {
            Value v = eval(*e.lhs, sc, row);
            if (v.type != ColType::Int) throw std::runtime_error("number chahiye");
            if (e.op == Tok::Minus) return Value::Int(-v.i);
            return truth(v.i == 0);  // NOT
        }
        case ExprKind::Binary: {
            Value a = eval(*e.lhs, sc, row), b = eval(*e.rhs, sc, row);
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

static bool matches(const Stmt& s, const TableSchema& sc, const std::vector<Value>& row) {
    if (!s.where) return true;
    Value v = eval(*s.where, sc, row);
    if (v.type != ColType::Int) throw std::runtime_error("WHERE me condition chahiye");
    return v.i != 0;
}

static std::string show(const Value& v) {
    return v.type == ColType::Int ? std::to_string(v.i) : v.s;
}

std::string Database::do_select(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    const TableSchema& sc = t->schema;

    std::vector<size_t> cols;
    if (s.select_all) {
        for (size_t i = 0; i < sc.columns.size(); i++) cols.push_back(i);
    } else {
        for (const std::string& n : s.select_cols) {
            int c = sc.find_column(n);
            if (c < 0) throw std::runtime_error("column nahi mila: " + n);
            cols.push_back((size_t)c);
        }
    }
    int oc = -1;
    if (!s.order_col.empty()) {
        oc = sc.find_column(s.order_col);
        if (oc < 0) throw std::runtime_error("column nahi mila: " + s.order_col);
    }

    BTree tree(pager_, t->root);
    std::vector<std::vector<Value>> rows;
    for (auto& kv : tree.scan()) {
        std::vector<Value> r = decode_row(sc, kv.first, kv.second.data(), kv.second.size());
        if (matches(s, sc, r)) rows.push_back(std::move(r));
    }
    if (oc >= 0) {
        std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) {
            bool lt = a[oc].type == ColType::Int ? a[oc].i < b[oc].i : a[oc].s < b[oc].s;
            bool gt = a[oc].type == ColType::Int ? a[oc].i > b[oc].i : a[oc].s > b[oc].s;
            return s.order_desc ? gt : lt;
        });
    }
    if (s.has_limit && (int64_t)rows.size() > s.limit) rows.resize((size_t)std::max<int64_t>(s.limit, 0));

    std::string out;
    for (size_t i = 0; i < cols.size(); i++) out += (i ? " | " : "") + sc.columns[cols[i]].name;
    for (auto& r : rows) {
        out += "\n";
        for (size_t i = 0; i < cols.size(); i++) out += (i ? " | " : "") + show(r[cols[i]]);
    }
    return out + "\n(" + std::to_string(rows.size()) + " rows)";
}

std::string Database::do_delete(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    BTree tree(pager_, t->root);

    // pehle poora scan + check, phir hatao (scan ke beech tree nahi badalte)
    std::vector<int64_t> dead;
    for (auto& kv : tree.scan()) {
        auto r = decode_row(t->schema, kv.first, kv.second.data(), kv.second.size());
        if (matches(s, t->schema, r)) dead.push_back(kv.first);
    }
    for (int64_t k : dead) tree.remove(k);
    pager_.flush();
    return std::to_string(dead.size()) + " row hati";
}

std::string Database::do_update(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    const TableSchema& sc = t->schema;

    std::vector<int> acol;
    for (const Assignment& a : s.assigns) {
        int c = sc.find_column(a.col);
        if (c < 0) throw std::runtime_error("column nahi mila: " + a.col);
        if (c == sc.pk) throw std::runtime_error("primary key abhi update nahi hoti");
        acol.push_back(c);
    }

    BTree tree(pager_, t->root);
    // sab naye payload pehle bana lo, taaki beech me error aaye toh kuch na badle
    std::vector<std::pair<int64_t, std::string>> upd;
    for (auto& kv : tree.scan()) {
        auto r = decode_row(sc, kv.first, kv.second.data(), kv.second.size());
        if (!matches(s, sc, r)) continue;
        std::vector<Value> nr = r;
        for (size_t i = 0; i < acol.size(); i++) {
            Value v = eval(*s.assigns[i].value, sc, r);  // purani row pe
            if (v.type != sc.columns[acol[i]].type)
                throw std::runtime_error("column '" + sc.columns[acol[i]].name + "' ke liye galat type");
            nr[acol[i]] = v;
        }
        upd.emplace_back(kv.first, encode_row(sc, nr));
    }
    for (auto& u : upd) {
        tree.remove(u.first);
        tree.insert(u.first, u.second);
    }
    pager_.flush();
    return std::to_string(upd.size()) + " row badli";
}

}  // namespace mkdb