#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "parser.h"

namespace mkdb {

// ek cell ki value: ya number ya text
struct Value {
    ColType type = ColType::Int;
    int64_t i = 0;
    std::string s;

    static Value Int(int64_t v) {
        Value x;
        x.type = ColType::Int;
        x.i = v;
        return x;
    }
    static Value Text(const std::string& v) {
        Value x;
        x.type = ColType::Text;
        x.s = v;
        return x;
    }
};

struct TableSchema {
    std::string name;
    std::vector<ColumnDef> columns;
    int pk = -1;  // primary key wale column ka index

    // naam se column ka index, nahi mila toh -1
    int find_column(const std::string& n) const;
};

// CREATE statement se schema banao aur rules check karo
TableSchema schema_from_create(const Stmt& s);

// row = columns ke order me values (pk bhi inme hai)
int64_t row_key(const TableSchema& t, const std::vector<Value>& row);

// pk ko chhodke baaki columns ke bytes
std::string encode_row(const TableSchema& t, const std::vector<Value>& row);

// key + payload se wapas poori row
std::vector<Value> decode_row(const TableSchema& t, int64_t key,
                              const char* data, size_t len);

// schema ko bytes me (catalog me rakhne ke liye) aur wapas
std::string encode_schema(const TableSchema& t);
TableSchema decode_schema(const char* data, size_t len);

}  // namespace mkdb