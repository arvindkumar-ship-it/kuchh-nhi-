#include <iostream>
#include <string>
#include <vector>
#include "schema.h"

using namespace mkdb;

static TableSchema make(const std::string& sql) {
    return schema_from_create(parse(sql));
}

static void try_schema(const std::string& sql) {
    std::cout << sql << "\n  ";
    try {
        TableSchema t = make(sql);
        std::cout << "ok, pk = " << t.columns[t.pk].name << "\n";
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << "\n";
    }
}

static void print_row(const TableSchema& t, const std::vector<Value>& row) {
    for (size_t i = 0; i < row.size(); i++) {
        std::cout << t.columns[i].name << "=";
        if (row[i].type == ColType::Int) std::cout << row[i].i;
        else std::cout << "'" << row[i].s << "'";
        std::cout << " ";
    }
    std::cout << "\n";
}

int main() {
    TableSchema t = make("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)");
    std::cout << "pk index = " << t.pk << ", age index = " << t.find_column("age")
              << ", xyz = " << t.find_column("xyz") << "\n";

    // row -> bytes -> row
    std::vector<Value> row = {Value::Int(7), Value::Text("ravi"), Value::Int(21)};
    std::string bytes = encode_row(t, row);
    std::cout << "key = " << row_key(t, row) << ", payload bytes = " << bytes.size() << "\n";

    std::vector<Value> back = decode_row(t, row_key(t, row), bytes.data(), bytes.size());
    std::cout << "wapas: ";
    print_row(t, back);

    // schema -> bytes -> schema
    std::string sb = encode_schema(t);
    TableSchema t2 = decode_schema(sb.data(), sb.size());
    std::cout << "schema bytes = " << sb.size() << ", wapas: " << t2.name
              << " cols=" << t2.columns.size() << " pk=" << t2.pk << "\n";

    std::cout << "--- galat schema ---\n";
    try_schema("CREATE TABLE a (x INT)");
    try_schema("CREATE TABLE a (x INT PRIMARY KEY, y INT PRIMARY KEY)");
    try_schema("CREATE TABLE a (x TEXT PRIMARY KEY)");
    try_schema("CREATE TABLE a (x INT PRIMARY KEY, x TEXT)");

    std::cout << "--- galat row ---\n";
    try {
        std::vector<Value> bad = {Value::Int(1), Value::Int(5), Value::Int(2)};
        encode_row(t, bad);
    } catch (const std::exception& e) {
        std::cout << "wrong type: " << e.what() << "\n";
    }
    try {
        decode_row(t, 7, bytes.data(), bytes.size() - 3);
    } catch (const std::exception& e) {
        std::cout << "adhoora data: " << e.what() << "\n";
    }
    return 0;
}