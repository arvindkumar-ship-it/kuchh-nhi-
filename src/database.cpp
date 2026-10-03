#include "database.h"
#include "executor.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace mkdb {

// page 0: magic (4) | catalog ka root (4) | agla table id (8)
static const uint32_t MAGIC = 0x4D4B4442;  // "MKDB"

// INSERT ki VALUES me abhi sirf seedhi values: number, text, -number
static Value eval_const(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Number:
            return Value::Int(e.num);
        case ExprKind::String:
            return Value::Text(e.text);
        case ExprKind::Unary:
            if (e.op == Tok::Minus && e.lhs->kind == ExprKind::Number) {
                return Value::Int(-e.lhs->num);
            }
            break;
        default:
            break;
    }
    throw std::runtime_error("VALUES me abhi sirf number ya text chalta hai");
}

Database::Database(const std::string& path) : pager_(path) {
    pager_.enable_freelist();  // page 0 header hamara hai, freelist head uske bytes 16..19 me
    if (pager_.page_count() == 0) {
        pager_.allocate_page();  // page 0 = header
        catalog_root_ = BTree::create(pager_);
        save_header();
        pager_.flush();
        return;
    }

    load_header();
}

void Database::load_header() {
    char buf[PAGE_SIZE];
    pager_.read_page(0, buf);

    uint32_t magic;
    std::memcpy(&magic, buf, 4);
    if (magic != MAGIC) throw std::runtime_error("ye mkdb file nahi lagti (magic galat)");

    std::memcpy(&catalog_root_, buf + 4, 4);
    std::memcpy(&next_id_, buf + 8, 8);
    load_catalog();
}

void Database::save_header() {
    char buf[PAGE_SIZE] = {0};
    if (pager_.page_count() > 0) pager_.read_page(0, buf);  // freelist head (bytes 16..19) na mitao
    uint32_t magic = MAGIC;
    std::memcpy(buf, &magic, 4);
    std::memcpy(buf + 4, &catalog_root_, 4);
    std::memcpy(buf + 8, &next_id_, 8);
    pager_.write_page(0, buf);
}

void Database::reload_state() {
    tables_.clear();
    indexes_.clear();
    load_header();
}

// index catalog entry: root(4) | unique(1) | len+name | len+table | len+col   (key = -id)
static void put_s(std::string& o, const std::string& s) {
    uint16_t n = (uint16_t)s.size();
    o.append((const char*)&n, 2);
    o += s;
}
static std::string get_s(const std::string& v, size_t* p) {
    uint16_t n;
    if (*p + 2 > v.size()) throw std::runtime_error("catalog kharab hai");
    std::memcpy(&n, v.data() + *p, 2);
    *p += 2;
    if (*p + n > v.size()) throw std::runtime_error("catalog kharab hai");
    std::string s = v.substr(*p, n);
    *p += n;
    return s;
}

void Database::load_catalog() {
    BTree cat(pager_, catalog_root_);
    for (int64_t id : cat.all_keys()) {
        std::string v;
        cat.get(id, &v);
        if (v.size() < 4) throw std::runtime_error("catalog kharab hai");

        if (id < 0) {  // secondary index
            Index ix;
            ix.id = -id;
            std::memcpy(&ix.root, v.data(), 4);
            ix.unique = v.size() > 4 && v[4] != 0;
            size_t p = 5;
            ix.name = get_s(v, &p);
            ix.table = get_s(v, &p);
            ix.col_name = get_s(v, &p);
            indexes_.push_back(ix);
            continue;
        }

        Table t;
        t.id = id;
        std::memcpy(&t.root, v.data(), 4);
        t.schema = decode_schema(v.data() + 4, v.size() - 4);
        tables_.push_back(t);
    }
    for (Index& ix : indexes_) {  // column index ab resolve (tables load ho chuki)
        Table* t = find_table(ix.table);
        if (!t) throw std::runtime_error("catalog kharab: index ki table nahi mili");
        ix.col = t->schema.find_column(ix.col_name);
        if (ix.col < 0) throw std::runtime_error("catalog kharab: index ka column nahi mila");
    }
}

Database::Table* Database::find_table(const std::string& name) {
    for (Table& t : tables_) {
        if (t.schema.name == name) return &t;
    }
    return nullptr;
}

std::vector<std::string> Database::table_names() const {
    std::vector<std::string> names;
    for (const Table& t : tables_) names.push_back(t.schema.name);
    return names;
}

static std::string norm(const std::string& sql) {
    size_t b = 0, e = sql.size();
    while (b < e && std::isspace((unsigned char)sql[b])) b++;
    while (e > b && (std::isspace((unsigned char)sql[e - 1]) || sql[e - 1] == ';')) e--;
    std::string s = sql.substr(b, e - b);
    for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

std::string Database::execute(const std::string& sql) {
    std::string up = norm(sql);
    if (up == "BEGIN") {
        pager_.begin();
        return "transaction shuru";
    }
    if (up == "COMMIT") {
        pager_.commit();
        return "commit ho gaya";
    }
    if (up == "ROLLBACK") {
        if (pager_.rollback()) reload_state();
        return "rollback ho gaya";
    }
    try {
        return dispatch(sql);
    } catch (const std::exception& e) {
        // statement beech me fail hua toh adhura kaam undo. transaction ke andar
        // bhi: aadhi badli hui state commit na ho, isliye poori transaction rollback
        bool was_txn = pager_.in_txn();
        if (pager_.rollback()) reload_state();
        if (was_txn) {
            throw std::runtime_error(std::string(e.what()) + " (transaction rollback ho gayi)");
        }
        throw;
    } catch (...) {
        if (pager_.rollback()) reload_state();
        throw;
    }
}

std::string Database::dispatch(const std::string& sql) {
    if (norm(sql).rfind("EXPLAIN ", 0) == 0) {
        size_t p = sql.find_first_not_of(" \t\r\n");
        Stmt s = parse(sql.substr(p + 8));
        prepare(s);
        if (s.kind != StmtKind::Select) throw std::runtime_error("EXPLAIN sirf SELECT pe chalta hai");
        return do_explain(s);
    }
    Stmt s = parse(sql);
    prepare(s);
    switch (s.kind) {
        case StmtKind::Create: return do_create(s);
        case StmtKind::CreateIndex: return do_create_index(s);
        case StmtKind::Insert: return do_insert(s);
        case StmtKind::Select: return do_select(s);
        case StmtKind::Update: return do_update(s);
        case StmtKind::Delete: return do_delete(s);
        default: break;
    }
    throw std::runtime_error("statement samajh nahi aaya");
}

void Database::prepare(Stmt& s) {
    if (s.kind == StmtKind::Insert) {
        for (ExprPtr& e : s.values) fold_constants(e);  // INSERT me 1+2 bhi chalega
        return;
    }
    if (s.kind != StmtKind::Select && s.kind != StmtKind::Update && s.kind != StmtKind::Delete) return;
    fold_constants(s.where);
    for (Assignment& a : s.assigns) fold_constants(a.value);
    Table* t = find_table(s.table);
    if (!t) return;  // table ka error aage wahi dega
    const TableSchema& sc = t->schema;
    if (s.where && check_expr_type(*s.where, sc) != ColType::Int)
        throw std::runtime_error("WHERE me condition chahiye");
    for (Assignment& a : s.assigns) {
        int c = sc.find_column(a.col);
        if (c < 0) throw std::runtime_error("column nahi mila: " + a.col);
        if (check_expr_type(*a.value, sc) != sc.columns[c].type)
            throw std::runtime_error("column '" + a.col + "' ke liye galat type");
    }
}

std::vector<IndexDef> Database::defs_for(const std::string& table) const {
    std::vector<IndexDef> out;
    for (const Index& ix : indexes_) {
        if (ix.table != table) continue;
        IndexDef d;
        d.name = ix.name;
        d.col = ix.col;
        d.unique = ix.unique;
        d.root = ix.root;
        out.push_back(d);
    }
    return out;
}

void Database::check_unique(const std::string& table, const std::vector<Value>& row) {
    for (const Index& ix : indexes_) {
        if (ix.table != table || !ix.unique) continue;
        if (!index_lookup(pager_, ix.root, row[ix.col]).empty())
            throw std::runtime_error("UNIQUE index '" + ix.name + "': ye value pehle se hai");
    }
}

void Database::idx_add_row(const std::string& table, const std::vector<Value>& row, int64_t pk) {
    for (const Index& ix : indexes_)
        if (ix.table == table) index_add(pager_, ix.root, row[ix.col], pk);
}

void Database::idx_remove_row(const std::string& table, const std::vector<Value>& row, int64_t pk) {
    for (const Index& ix : indexes_)
        if (ix.table == table) index_remove(pager_, ix.root, row[ix.col], pk);
}

std::string Database::do_create_index(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    int c = t->schema.find_column(s.index_col);
    if (c < 0) throw std::runtime_error("column nahi mila: " + s.index_col);
    if (c == t->schema.pk) throw std::runtime_error("primary key pe alag index ki zarurat nahi (wo pehle se index hai)");
    for (const Index& x : indexes_)
        if (x.name == s.index_name) throw std::runtime_error("index pehle se hai: " + s.index_name);

    Index ix;
    ix.name = s.index_name;
    ix.table = s.table;
    ix.col_name = s.index_col;
    ix.col = c;
    ix.unique = s.index_unique;
    ix.root = BTree::create(pager_);

    // maujooda rows se index bharo
    {
        BTree tree(pager_, t->root);
        BTree::Cursor cur(tree);
        int64_t k;
        std::string v;
        while (cur.next(&k, &v)) {
            std::vector<Value> r = decode_row(t->schema, k, v.data(), v.size());
            if (ix.unique && !index_lookup(pager_, ix.root, r[c]).empty())
                throw std::runtime_error("UNIQUE index nahi ban sakta: column me duplicate values hain");
            index_add(pager_, ix.root, r[c], k);
        }
    }

    std::string cv(4, '\0');
    std::memcpy(&cv[0], &ix.root, 4);
    cv.push_back(ix.unique ? 1 : 0);
    put_s(cv, ix.name);
    put_s(cv, ix.table);
    put_s(cv, ix.col_name);

    ix.id = next_id_;
    BTree cat(pager_, catalog_root_);
    cat.insert(-ix.id, cv);
    next_id_++;
    save_header();
    pager_.flush();
    indexes_.push_back(ix);
    return std::string("index ban gaya: ") + ix.name;
}

std::string Database::do_create(const Stmt& s) {
    TableSchema schema = schema_from_create(s);
    if (find_table(schema.name)) {
        throw std::runtime_error("table pehle se hai: " + schema.name);
    }

    // catalog ki value: root (4 bytes) + schema. size pehle check, page baad me banao
    std::string sbytes = encode_schema(schema);
    if (sbytes.size() + 4 > MAX_VALUE_SIZE) {
        throw std::runtime_error("table ka schema bahut bada hai");
    }

    PageId root = BTree::create(pager_);
    std::string val(4, '\0');
    std::memcpy(&val[0], &root, 4);
    val += sbytes;

    int64_t id = next_id_;
    BTree cat(pager_, catalog_root_);
    cat.insert(id, val);

    next_id_++;
    save_header();
    pager_.flush();

    Table t{id, schema, root};
    tables_.push_back(t);
    return "table ban gayi: " + schema.name;
}

std::string Database::do_insert(const Stmt& s) {
    Table* t = find_table(s.table);
    if (!t) throw std::runtime_error("table nahi mili: " + s.table);
    const TableSchema& sc = t->schema;
    size_t ncols = sc.columns.size();

    // values[i] kis column me jaayegi
    std::vector<size_t> target;
    if (s.insert_cols.empty()) {
        if (s.values.size() != ncols) {
            throw std::runtime_error("values ki ginti columns se alag hai");
        }
        for (size_t i = 0; i < ncols; i++) target.push_back(i);
    } else {
        if (s.insert_cols.size() != s.values.size()) {
            throw std::runtime_error("columns aur values ki ginti alag hai");
        }
        for (const std::string& name : s.insert_cols) {
            int c = sc.find_column(name);
            if (c < 0) throw std::runtime_error("column nahi mila: " + name);
            for (size_t prev : target) {
                if (prev == (size_t)c) {
                    throw std::runtime_error("column do baar likha hai: " + name);
                }
            }
            target.push_back((size_t)c);
        }
    }

    std::vector<Value> row(ncols);
    std::vector<bool> filled(ncols, false);
    for (size_t i = 0; i < target.size(); i++) {
        size_t c = target[i];
        Value v = eval_const(*s.values[i]);
        if (v.type != sc.columns[c].type) {
            throw std::runtime_error("column '" + sc.columns[c].name + "' ke liye galat type");
        }
        row[c] = v;
        filled[c] = true;
    }
    for (size_t c = 0; c < ncols; c++) {
        if (!filled[c]) {
            throw std::runtime_error("column '" + sc.columns[c].name +
                                     "' ki value nahi di (NULL abhi nahi chalta)");
        }
    }

    int64_t key = row_key(sc, row);
    std::string payload = encode_row(sc, row);

    check_unique(s.table, row);  // koi bhi badlav se pehle
    BTree tree(pager_, t->root);
    if (!tree.insert(key, payload)) {
        throw std::runtime_error("primary key " + std::to_string(key) + " pehle se hai");
    }
    idx_add_row(s.table, row, key);
    pager_.flush();
    return "1 row daali";
}

bool Database::get_row(const std::string& table, int64_t key, std::vector<Value>* out) {
    Table* t = find_table(table);
    if (!t) return false;

    BTree tree(pager_, t->root);
    std::string v;
    if (!tree.get(key, &v)) return false;

    *out = decode_row(t->schema, key, v.data(), v.size());
    return true;
}

}  // namespace mkdb