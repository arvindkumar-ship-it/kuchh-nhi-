#pragma once
#include <string>
#include <vector>
#include "btree.h"
#include "schema.h"

namespace mkdb {

// ek secondary index ki jaankari (executor/planner ke liye)
struct IndexDef {
    std::string name;
    int col = -1;        // table ka kaun sa column
    bool unique = false;
    PageId root = INVALID_PAGE;
};

// Secondary index = alag B-Tree: key = column value ka int64 (INT: value, TEXT: FNV-1a hash),
// value = bucket [(value bytes, primary key)...]. Bucket se hash collision aur
// non-unique (duplicate values) dono sambhalte hain.
int64_t index_key(const Value& v);
std::vector<int64_t> index_lookup(Pager& p, PageId root, const Value& v);  // matching pks
void index_add(Pager& p, PageId root, const Value& v, int64_t pk);
void index_remove(Pager& p, PageId root, const Value& v, int64_t pk);
bool value_equal(const Value& a, const Value& b);

}  // namespace mkdb
