#include "schema.h"
#include <cstring>
#include <stdexcept>

namespace mkdb {

namespace {

// ---------- bytes likhne ke helpers ----------
// (x86/ARM dono little-endian hain, memcpy se seedha likh dete hain)

void put_u16(std::string& out, uint16_t v) {
    char b[2];
    std::memcpy(b, &v, 2);
    out.append(b, 2);
}

void put_i64(std::string& out, int64_t v) {
    char b[8];
    std::memcpy(b, &v, 8);
    out.append(b, 8);
}

void put_str(std::string& out, const std::string& s) {
    if (s.size() > 65535) throw std::runtime_error("text 65535 bytes se bada nahi ho sakta");
    put_u16(out, (uint16_t)s.size());
    out.append(s);
}

// ---------- bytes padhne ka helper, har step pe bounds check ----------

class Reader {
public:
    Reader(const char* d, size_t n) : d_(d), n_(n), pos_(0) {}

    uint8_t u8() {
        need(1);
        return (uint8_t)d_[pos_++];
    }
    uint16_t u16() {
        need(2);
        uint16_t v;
        std::memcpy(&v, d_ + pos_, 2);
        pos_ += 2;
        return v;
    }
    int64_t i64() {
        need(8);
        int64_t v;
        std::memcpy(&v, d_ + pos_, 8);
        pos_ += 8;
        return v;
    }
    std::string str() {
        uint16_t len = u16();
        need(len);
        std::string s(d_ + pos_, len);
        pos_ += len;
        return s;
    }
    bool done() const { return pos_ == n_; }

private:
    void need(size_t k) const {
        if (pos_ + k > n_) throw std::runtime_error("decode: data adhoora ya kharab hai");
    }
    const char* d_;
    size_t n_;
    size_t pos_;
};

void check_row(const TableSchema& t, const std::vector<Value>& row) {
    if (row.size() != t.columns.size()) {
        throw std::runtime_error("row me columns ki ginti galat hai");
    }
    for (size_t i = 0; i < row.size(); i++) {
        if (row[i].type != t.columns[i].type) {
            throw std::runtime_error("column '" + t.columns[i].name + "' ka type galat hai");
        }
    }
}

}  // namespace

// ---------- schema ----------

int TableSchema::find_column(const std::string& n) const {
    for (size_t i = 0; i < columns.size(); i++) {
        if (columns[i].name == n) return (int)i;
    }
    return -1;
}

TableSchema schema_from_create(const Stmt& s) {
    if (s.kind != StmtKind::Create) {
        throw std::logic_error("schema_from_create: CREATE statement chahiye");
    }

    TableSchema t;
    t.name = s.table;
    t.columns = s.columns;

    if (t.columns.empty()) throw std::runtime_error("schema: table me column nahi hain");

    for (size_t i = 0; i < t.columns.size(); i++) {
        for (size_t j = 0; j < i; j++) {
            if (t.columns[i].name == t.columns[j].name) {
                throw std::runtime_error("schema: column ka naam do baar aaya: " +
                                         t.columns[i].name);
            }
        }
        if (t.columns[i].primary) {
            if (t.pk != -1) throw std::runtime_error("schema: ek se zyada PRIMARY KEY");
            t.pk = (int)i;
        }
    }

    if (t.pk == -1) throw std::runtime_error("schema: PRIMARY KEY chahiye");
    if (t.columns[t.pk].type != ColType::Int) {
        throw std::runtime_error("schema: PRIMARY KEY INT honi chahiye");
    }
    return t;
}

// ---------- row ----------

int64_t row_key(const TableSchema& t, const std::vector<Value>& row) {
    check_row(t, row);
    return row[t.pk].i;
}

std::string encode_row(const TableSchema& t, const std::vector<Value>& row) {
    check_row(t, row);

    std::string out;
    for (size_t i = 0; i < row.size(); i++) {
        if ((int)i == t.pk) continue;  // pk B-Tree ki key me hai

        if (t.columns[i].type == ColType::Int) put_i64(out, row[i].i);
        else put_str(out, row[i].s);
    }
    return out;
}

std::vector<Value> decode_row(const TableSchema& t, int64_t key,
                              const char* data, size_t len) {
    Reader r(data, len);
    std::vector<Value> row(t.columns.size());

    for (size_t i = 0; i < row.size(); i++) {
        if ((int)i == t.pk) {
            row[i] = Value::Int(key);
        } else if (t.columns[i].type == ColType::Int) {
            row[i] = Value::Int(r.i64());
        } else {
            row[i] = Value::Text(r.str());
        }
    }

    if (!r.done()) throw std::runtime_error("decode: row me fazool bytes bache");
    return row;
}

// ---------- schema ko bytes me ----------
// format: naam, column ginti, phir har column: naam, type (1 byte), primary (1 byte)

std::string encode_schema(const TableSchema& t) {
    std::string out;
    put_str(out, t.name);
    put_u16(out, (uint16_t)t.columns.size());
    for (const ColumnDef& c : t.columns) {
        put_str(out, c.name);
        out.push_back(c.type == ColType::Int ? 0 : 1);
        out.push_back(c.primary ? 1 : 0);
    }
    return out;
}

TableSchema decode_schema(const char* data, size_t len) {
    Reader r(data, len);
    TableSchema t;
    t.name = r.str();

    uint16_t n = r.u16();
    for (uint16_t i = 0; i < n; i++) {
        ColumnDef c;
        c.name = r.str();

        uint8_t type = r.u8();
        if (type == 0) c.type = ColType::Int;
        else if (type == 1) c.type = ColType::Text;
        else throw std::runtime_error("decode: column ka type samajh nahi aaya");

        c.primary = (r.u8() != 0);
        if (c.primary) t.pk = (int)i;
        t.columns.push_back(c);
    }

    if (!r.done()) throw std::runtime_error("decode: schema me fazool bytes bache");
    if (t.pk == -1) throw std::runtime_error("decode: schema me PRIMARY KEY nahi hai");
    return t;
}

}  // namespace mkdb