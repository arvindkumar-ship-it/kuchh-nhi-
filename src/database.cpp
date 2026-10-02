#include "database.h"
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
    if (pager_.page_count() == 0) {
        pager_.allocate_page();  // page 0 = header
        catalog_root_ = BTree::create(pager_);
        save_header();
        pager_.flush();
        return;
    }

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
    uint32_t magic = MAGIC;
    std::memcpy(buf, &magic, 4);
    std::memcpy(buf + 4, &catalog_root_, 4);
    std::memcpy(buf + 8, &next_id_, 8);
    pager_.write_page(0, buf);
}

void Database::load_catalog() {
    BTree cat(pager_, catalog_root_);
    for (int64_t id : cat.all_keys()) {
        std::string v;
        cat.get(id, &v);
        if (v.size() < 4) throw std::runtime_error("catalog kharab hai");

        Table t;
        t.id = id;
        std::memcpy(&t.root, v.data(), 4);
        t.schema = decode_schema(v.data() + 4, v.size() - 4);
        tables_.push_back(t);
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

std::string Database::execute(const std::string& sql) {
    Stmt s = parse(sql);
    switch (s.kind) {
        case StmtKind::Create: return do_create(s);
        case StmtKind::Insert: return do_insert(s);
        case StmtKind::Select: return do_select(s);
        case StmtKind::Update: return do_update(s);
        case StmtKind::Delete: return do_delete(s);
        default: break;
    }
    throw std::runtime_error("statement samajh nahi aaya");
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

    BTree tree(pager_, t->root);
    if (!tree.insert(key, payload)) {
        throw std::runtime_error("primary key " + std::to_string(key) + " pehle se hai");
    }
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