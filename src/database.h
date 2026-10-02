#pragma once
#include <string>
#include <vector>
#include "pager.h"
#include "btree.h"
#include "schema.h"

namespace mkdb {

class Database {
public:
    // file nahi hai toh nayi database bana deta hai
    explicit Database(const std::string& path);

    // CREATE TABLE ya INSERT chalao, kya hua uska chhota message deta hai.
    // galti pe runtime_error
    std::string execute(const std::string& sql);

    // pk se ek row dhoondo
    bool get_row(const std::string& table, int64_t key, std::vector<Value>* out);

    std::vector<std::string> table_names() const;

private:
    struct Table {
        int64_t id;
        TableSchema schema;
        PageId root;
    };

    Pager pager_;
    PageId catalog_root_ = INVALID_PAGE;
    int64_t next_id_ = 1;
    std::vector<Table> tables_;  // catalog ki copy memory me

    void save_header();
    void load_catalog();
    Table* find_table(const std::string& name);

    std::string do_create(const Stmt& s);
    std::string do_insert(const Stmt& s);
};

}  // namespace mkdb