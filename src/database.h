#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "pager.h"
#include "btree.h"
#include "schema.h"
#include "index.h"

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

    // BEGIN ke baad COMMIT/ROLLBACK tak true
    uint32_t page_count() const { return pager_.page_count(); }

    bool in_transaction() const { return pager_.in_txn(); }

    // ORDER BY ke liye RAM limit (chhoti rakho toh disk sort test hota hai)
    void set_sort_memory(size_t bytes) { sort_mem_ = bytes; }

private:
    struct Table {
        int64_t id;
        TableSchema schema;
        PageId root;
    };

    struct Index {
        int64_t id = 0;
        std::string name, table, col_name;
        int col = -1;
        bool unique = false;
        PageId root = INVALID_PAGE;
    };

    Pager pager_;
    std::vector<Index> indexes_;  // catalog me negative keys se
    PageId catalog_root_ = INVALID_PAGE;
    int64_t next_id_ = 1;
    size_t sort_mem_ = 1 << 20;
    std::vector<Table> tables_;  // catalog ki copy memory me

    void load_header();
    void reload_state();  // rollback ke baad memory ki copy dobara disk se
    void save_header();
    void load_catalog();
    Table* find_table(const std::string& name);

    std::string do_create(const Stmt& s);
    std::string do_insert(const Stmt& s);
    std::string do_create_index(const Stmt& s);
    void prepare(Stmt& s);  // analyzer + constant folding
    std::vector<IndexDef> defs_for(const std::string& table) const;
    void check_unique(const std::string& table, const std::vector<Value>& row);
    void idx_add_row(const std::string& table, const std::vector<Value>& row, int64_t pk);
    void idx_remove_row(const std::string& table, const std::vector<Value>& row, int64_t pk);
    std::string do_select(const Stmt& s);
    std::string do_explain(const Stmt& s);
    std::string dispatch(const std::string& sql);
    std::string do_update(const Stmt& s);
    std::string do_delete(const Stmt& s);
};

}  // namespace mkdb