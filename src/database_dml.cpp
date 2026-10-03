#include "database.h"
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include "executor.h"

namespace mkdb {

static std::string show(const Value& v) {
    return v.type == ColType::Int ? std::to_string(v.i) : v.s;
}

std::string Database::do_select(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    const TableSchema& sc = t->schema;

    std::vector<size_t> cols;
    IterPtr plan = build_select_plan(pager_, t->root, sc, s, &cols, defs_for(s.table), sort_mem_);

    std::string out;
    for (size_t i = 0; i < cols.size(); i++) out += (i ? " | " : "") + sc.columns[cols[i]].name;
    size_t n = 0;
    Row r;
    while (plan->next(&r)) {  // row-by-row pull
        out += "\n";
        for (size_t i = 0; i < r.size(); i++) out += (i ? " | " : "") + show(r[i]);
        n++;
    }
    return out + "\n(" + std::to_string(n) + " rows)";
}

std::string Database::do_explain(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    std::vector<size_t> cols;
    IterPtr plan = build_select_plan(pager_, t->root, t->schema, s, &cols, defs_for(s.table), sort_mem_);
    std::string d = plan->describe();
    if (!d.empty() && d.back() == '\n') d.pop_back();
    return d;
}

std::string Database::do_delete(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    BTree tree(pager_, t->root);

    // pehle poora scan + check, phir hatao (scan ke beech tree nahi badalte)
    std::vector<std::pair<int64_t, Row>> dead;
    {
        BTree::Cursor cur(tree);
        int64_t k;
        std::string v;
        while (cur.next(&k, &v)) {
            Row r = decode_row(t->schema, k, v.data(), v.size());
            if (row_matches(s.where.get(), t->schema, r)) dead.emplace_back(k, std::move(r));
        }
    }
    for (auto& dr : dead) {
        tree.remove(dr.first);
        idx_remove_row(s.table, dr.second, dr.first);
    }
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
    // sab naye rows pehle bana lo, taaki beech me error aaye toh kuch na badle
    struct Upd {
        int64_t key;
        Row old_row, new_row;
        std::string payload;
    };
    std::vector<Upd> upd;
    {
        BTree::Cursor cur(tree);
        int64_t k;
        std::string v;
        while (cur.next(&k, &v)) {
            Row r = decode_row(sc, k, v.data(), v.size());
            if (!row_matches(s.where.get(), sc, r)) continue;
            Row nr = r;
            for (size_t i = 0; i < acol.size(); i++) {
                Value nv = eval_expr(*s.assigns[i].value, sc, r);  // purani row pe
                if (nv.type != sc.columns[acol[i]].type)
                    throw std::runtime_error("column '" + sc.columns[acol[i]].name + "' ke liye galat type");
                nr[acol[i]] = nv;
            }
            std::string payload = encode_row(sc, nr);
            upd.push_back(Upd{k, std::move(r), std::move(nr), std::move(payload)});
        }
    }

    // UNIQUE index: badlav ke BAAD ki final state valid honi chahiye
    std::unordered_map<int64_t, size_t> pos;
    for (size_t i = 0; i < upd.size(); i++) pos[upd[i].key] = i;
    for (const Index& ix : indexes_) {
        if (ix.table != s.table || !ix.unique) continue;
        std::unordered_set<std::string> seen;
        for (const Upd& u : upd) {
            const Value& ov = u.old_row[ix.col];
            const Value& nv = u.new_row[ix.col];
            if (value_equal(ov, nv)) continue;
            std::string vk = nv.type == ColType::Int ? "i" + std::to_string(nv.i) : "s" + nv.s;
            if (!seen.insert(vk).second) throw std::runtime_error("UNIQUE index '" + ix.name + "': ye value pehle se hai");
            for (int64_t p : index_lookup(pager_, ix.root, nv)) {
                auto it = pos.find(p);
                bool moving = it != pos.end() &&
                              !value_equal(upd[it->second].old_row[ix.col], upd[it->second].new_row[ix.col]);
                if (!moving) throw std::runtime_error("UNIQUE index '" + ix.name + "': ye value pehle se hai");
            }
        }
    }

    for (const Upd& u : upd) {
        tree.remove(u.key);
        tree.insert(u.key, u.payload);
        for (const Index& ix : indexes_) {
            if (ix.table != s.table) continue;
            if (value_equal(u.old_row[ix.col], u.new_row[ix.col])) continue;
            index_remove(pager_, ix.root, u.old_row[ix.col], u.key);
            index_add(pager_, ix.root, u.new_row[ix.col], u.key);
        }
    }
    pager_.flush();
    return std::to_string(upd.size()) + " row badli";
}

}  // namespace mkdb
