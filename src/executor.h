#pragma once
#include <memory>
#include <string>
#include <vector>
#include "btree.h"
#include "index.h"
#include "schema.h"

namespace mkdb {

using Row = std::vector<Value>;

// expression ko ek row pe chalao / WHERE check (where nullptr = sab pass)
Value eval_expr(const Expr& e, const TableSchema& sc, const Row& row);
bool row_matches(const Expr* where, const TableSchema& sc, const Row& row);

// analyzer: expression ka type static check (galat column/type mel query chalne se pehle pakde)
ColType check_expr_type(const Expr& e, const TableSchema& sc);
// optimizer: jahan dono side constant hon (1+2, 3<4) wahin pehle hi compute
void fold_constants(ExprPtr& e);

// Iterator model: har plan node ek Iter. Upar wala next() bulata hai,
// neeche se ek time pe ek hi row aati hai
class Iter {
public:
    virtual ~Iter() {}
    virtual bool next(Row* out) = 0;
    virtual std::string describe(int depth = 0) const = 0;  // EXPLAIN ke liye
};
using IterPtr = std::unique_ptr<Iter>;

IterPtr make_scan(Pager& p, PageId root, const TableSchema& sc);
IterPtr make_pk_lookup(Pager& p, PageId root, const TableSchema& sc, int64_t key);
IterPtr make_index_lookup(Pager& p, PageId root, const TableSchema& sc, const IndexDef& ix, const Value& v);
IterPtr make_filter(IterPtr child, const TableSchema& sc, const Expr* where);
// mem_limit_bytes se zyada rows RAM me nahi: baaki disk pe sorted runs + k-way merge
IterPtr make_sort(IterPtr child, int col, bool desc, size_t mem_limit_bytes);
IterPtr make_limit(IterPtr child, int64_t n);
IterPtr make_project(IterPtr child, std::vector<size_t> cols);

// SELECT statement -> plan tree. output rows sirf select kiye columns ki hongi.
// out_cols me wahi column indexes. galti pe runtime_error
IterPtr build_select_plan(Pager& p, PageId root, const TableSchema& sc, const Stmt& s,
                          std::vector<size_t>* out_cols, const std::vector<IndexDef>& indexes,
                          size_t sort_mem = 1 << 20);

}  // namespace mkdb
