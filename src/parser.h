#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "lexer.h"

namespace mkdb {

// ---------- expression ----------

enum class ExprKind { Number, String, Column, Unary, Binary };

struct Expr {
    ExprKind kind = ExprKind::Number;
    int64_t num = 0;           // Number
    std::string text;          // String ki value ya Column ka naam
    Tok op = Tok::End;         // Unary/Binary ka operator
    std::unique_ptr<Expr> lhs; // Binary ka left, Unary ka operand
    std::unique_ptr<Expr> rhs; // Binary ka right
};
using ExprPtr = std::unique_ptr<Expr>;

// ---------- statement ----------

enum class ColType { Int, Text };

struct ColumnDef {
    std::string name;
    ColType type = ColType::Int;
    bool primary = false;
};

struct Assignment {
    std::string col;
    ExprPtr value;
};

enum class StmtKind { Create, Insert, Select, Delete, Update };

// ek struct sab statements ke liye, kind se pata chalta hai kaun si fields kaam ki hain
struct Stmt {
    StmtKind kind = StmtKind::Select;
    std::string table;

    // CREATE
    std::vector<ColumnDef> columns;

    // INSERT
    std::vector<std::string> insert_cols;  // khali = saare columns order me
    std::vector<ExprPtr> values;

    // SELECT
    bool select_all = false;
    std::vector<std::string> select_cols;
    std::string order_col;                 // khali = ORDER BY nahi
    bool order_desc = false;
    bool has_limit = false;
    int64_t limit = 0;

    // UPDATE
    std::vector<Assignment> assigns;

    // SELECT / UPDATE / DELETE
    ExprPtr where;                         // nullptr = WHERE nahi
};

// ek query ko parse karo. galti pe runtime_error
Stmt parse(const std::string& sql);

// debug/test ke liye: tree ko text me, har operator ke saath bracket
std::string expr_to_string(const Expr& e);
std::string stmt_to_string(const Stmt& s);

}  // namespace mkdb