#include "parser.h"
#include <stdexcept>
#include <utility>

namespace mkdb {

namespace {

class Parser {
public:
    explicit Parser(std::vector<Token> t) : toks_(std::move(t)) {}

    Stmt statement() {
        Stmt s;
        switch (peek().type) {
            case Tok::Create: next(); s.kind = StmtKind::Create; parse_create(s); break;
            case Tok::Insert: next(); s.kind = StmtKind::Insert; parse_insert(s); break;
            case Tok::Select: next(); s.kind = StmtKind::Select; parse_select(s); break;
            case Tok::Delete: next(); s.kind = StmtKind::Delete; parse_delete(s); break;
            case Tok::Update: next(); s.kind = StmtKind::Update; parse_update(s); break;
            default:
                fail("statement CREATE/INSERT/SELECT/DELETE/UPDATE se shuru karo");
        }

        accept(Tok::Semicolon);
        if (peek().type != Tok::End) fail("query ke baad kuch extra hai");
        return s;
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;

    // ---------- token ka kaam ----------

    const Token& peek() const { return toks_[pos_]; }

    Token next() {
        Token t = toks_[pos_];
        if (pos_ + 1 < toks_.size()) pos_++;  // End token ke aage nahi jaate
        return t;
    }

    bool accept(Tok t) {
        if (peek().type == t) {
            next();
            return true;
        }
        return false;
    }

    [[noreturn]] void fail(const std::string& msg) {
        const Token& t = peek();
        std::string got = (t.type == Tok::End) ? "query khatam" : "'" + t.text + "'";
        throw std::runtime_error("parser: " + msg + ", mila " + got +
                                 " position " + std::to_string(t.pos));
    }

    Token expect(Tok t, const char* what) {
        if (peek().type != t) fail(std::string("chahiye ") + what);
        return next();
    }

    std::string ident(const char* what) {
        return expect(Tok::Ident, what).text;
    }

    // number token ko int64 me. bahut bada ho toh error
    int64_t to_int(const Token& t) {
        try {
            return std::stoll(t.text);
        } catch (const std::out_of_range&) {
            fail("number bahut bada hai");
        }
    }

    // ---------- expression (Pratt) ----------

    static int infix_prec(Tok t) {
        switch (t) {
            case Tok::Or: return 1;
            case Tok::And: return 2;
            case Tok::Eq: case Tok::Neq: case Tok::Lt:
            case Tok::Le: case Tok::Gt: case Tok::Ge: return 4;
            case Tok::Plus: case Tok::Minus: return 5;
            case Tok::Star: case Tok::Slash: return 6;
            default: return 0;  // operator nahi
        }
    }

    ExprPtr expr(int rbp = 0) {
        ExprPtr left = prefix();

        while (infix_prec(peek().type) > rbp) {
            int prec = infix_prec(peek().type);
            Tok op = next().type;
            ExprPtr right = expr(prec);

            ExprPtr node(new Expr());
            node->kind = ExprKind::Binary;
            node->op = op;
            node->lhs = std::move(left);
            node->rhs = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    // wo cheezein jo expression ki shuruat me aa sakti hain
    ExprPtr prefix() {
        Token t = peek();
        ExprPtr e(new Expr());

        switch (t.type) {
            case Tok::Number:
                e->kind = ExprKind::Number;
                e->num = to_int(t);   // error ho toh yahi token dikhega
                next();
                return e;
            case Tok::String:
                next();
                e->kind = ExprKind::String;
                e->text = t.text;
                return e;
            case Tok::Ident:
                next();
                e->kind = ExprKind::Column;
                e->text = t.text;
                return e;
            case Tok::LParen: {
                next();
                ExprPtr inner = expr(0);
                expect(Tok::RParen, "')'");
                return inner;
            }
            case Tok::Minus:
            case Tok::Not:
                next();
                e->kind = ExprKind::Unary;
                e->op = t.type;
                e->lhs = expr(t.type == Tok::Not ? 3 : 7);
                return e;
            default:
                fail("expression chahiye");
        }
    }

    // ---------- statements ----------

    void parse_where(Stmt& s) {
        if (accept(Tok::Where)) s.where = expr();
    }

    void parse_create(Stmt& s) {
        bool uniq = accept(Tok::Unique);
        if (accept(Tok::Index)) {
            s.kind = StmtKind::CreateIndex;
            s.index_unique = uniq;
            s.index_name = ident("index ka naam");
            expect(Tok::On, "ON");
            s.table = ident("table ka naam");
            expect(Tok::LParen, "'('");
            s.index_col = ident("column ka naam");
            expect(Tok::RParen, "')'");
            return;
        }
        if (uniq) fail("UNIQUE ke baad INDEX chahiye");
        expect(Tok::Table, "TABLE");
        s.table = ident("table ka naam");
        expect(Tok::LParen, "'('");
        do {
            ColumnDef c;
            c.name = ident("column ka naam");
            if (accept(Tok::Int)) c.type = ColType::Int;
            else if (accept(Tok::Text)) c.type = ColType::Text;
            else fail("column ka type (INT ya TEXT) chahiye");

            if (accept(Tok::Primary)) {
                expect(Tok::Key, "KEY");
                c.primary = true;
            }
            s.columns.push_back(c);
        } while (accept(Tok::Comma));
        expect(Tok::RParen, "')'");
    }

    void parse_insert(Stmt& s) {
        expect(Tok::Into, "INTO");
        s.table = ident("table ka naam");

        if (accept(Tok::LParen)) {
            do {
                s.insert_cols.push_back(ident("column ka naam"));
            } while (accept(Tok::Comma));
            expect(Tok::RParen, "')'");
        }

        expect(Tok::Values, "VALUES");
        expect(Tok::LParen, "'('");
        do {
            s.values.push_back(expr());
        } while (accept(Tok::Comma));
        expect(Tok::RParen, "')'");
    }

    void parse_select(Stmt& s) {
        if (accept(Tok::Star)) {
            s.select_all = true;
        } else {
            do {
                s.select_cols.push_back(ident("column ka naam"));
            } while (accept(Tok::Comma));
        }

        expect(Tok::From, "FROM");
        s.table = ident("table ka naam");
        parse_where(s);

        if (accept(Tok::Order)) {
            expect(Tok::By, "BY");
            s.order_col = ident("column ka naam");
            if (accept(Tok::Desc)) s.order_desc = true;
            else accept(Tok::Asc);
        }

        if (accept(Tok::Limit)) {
            Token n = expect(Tok::Number, "LIMIT ke baad number");
            s.has_limit = true;
            s.limit = to_int(n);
        }
    }

    void parse_delete(Stmt& s) {
        expect(Tok::From, "FROM");
        s.table = ident("table ka naam");
        parse_where(s);
    }

    void parse_update(Stmt& s) {
        s.table = ident("table ka naam");
        expect(Tok::Set, "SET");
        do {
            Assignment a;
            a.col = ident("column ka naam");
            expect(Tok::Eq, "'='");
            a.value = expr();
            s.assigns.push_back(std::move(a));
        } while (accept(Tok::Comma));
        parse_where(s);
    }
};

// ---------- text me dikhane ke helpers ----------

const char* op_str(Tok t) {
    switch (t) {
        case Tok::Or: return "OR";
        case Tok::And: return "AND";
        case Tok::Eq: return "=";
        case Tok::Neq: return "!=";
        case Tok::Lt: return "<";
        case Tok::Le: return "<=";
        case Tok::Gt: return ">";
        case Tok::Ge: return ">=";
        case Tok::Plus: return "+";
        case Tok::Minus: return "-";
        case Tok::Star: return "*";
        case Tok::Slash: return "/";
        default: return "?";
    }
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i > 0) out += sep;
        out += v[i];
    }
    return out;
}

}  // namespace

Stmt parse(const std::string& sql) {
    Parser p(tokenize(sql));
    return p.statement();
}

std::string expr_to_string(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Number:
            return std::to_string(e.num);
        case ExprKind::String: {
            std::string out = "'";
            for (char c : e.text) {
                if (c == '\'') out += "''";
                else out += c;
            }
            return out + "'";
        }
        case ExprKind::Column:
            return e.text;
        case ExprKind::Unary:
            return std::string("(") + (e.op == Tok::Not ? "NOT " : "-") +
                   expr_to_string(*e.lhs) + ")";
        case ExprKind::Binary:
            return "(" + expr_to_string(*e.lhs) + " " + op_str(e.op) + " " +
                   expr_to_string(*e.rhs) + ")";
    }
    return "?";
}

std::string stmt_to_string(const Stmt& s) {
    std::string out;
    switch (s.kind) {
        case StmtKind::Create: {
            std::vector<std::string> cols;
            for (const ColumnDef& c : s.columns) {
                std::string d = c.name + (c.type == ColType::Int ? " INT" : " TEXT");
                if (c.primary) d += " PRIMARY KEY";
                cols.push_back(d);
            }
            out = "CREATE TABLE " + s.table + " (" + join(cols, ", ") + ")";
            break;
        }
        case StmtKind::CreateIndex:
            out = std::string("CREATE ") + (s.index_unique ? "UNIQUE " : "") + "INDEX " + s.index_name +
                  " ON " + s.table + " (" + s.index_col + ")";
            break;
        case StmtKind::Insert: {
            out = "INSERT " + s.table;
            if (!s.insert_cols.empty()) out += " (" + join(s.insert_cols, ", ") + ")";
            std::vector<std::string> vals;
            for (const ExprPtr& e : s.values) vals.push_back(expr_to_string(*e));
            out += " VALUES [" + join(vals, ", ") + "]";
            break;
        }
        case StmtKind::Select: {
            out = "SELECT " + (s.select_all ? std::string("*") : join(s.select_cols, ", ")) +
                  " FROM " + s.table;
            if (s.where) out += " WHERE " + expr_to_string(*s.where);
            if (!s.order_col.empty()) {
                out += " ORDER BY " + s.order_col + (s.order_desc ? " DESC" : " ASC");
            }
            if (s.has_limit) out += " LIMIT " + std::to_string(s.limit);
            break;
        }
        case StmtKind::Delete: {
            out = "DELETE FROM " + s.table;
            if (s.where) out += " WHERE " + expr_to_string(*s.where);
            break;
        }
        case StmtKind::Update: {
            std::vector<std::string> sets;
            for (const Assignment& a : s.assigns) {
                sets.push_back(a.col + " = " + expr_to_string(*a.value));
            }
            out = "UPDATE " + s.table + " SET " + join(sets, ", ");
            if (s.where) out += " WHERE " + expr_to_string(*s.where);
            break;
        }
    }
    return out;
}

}  // namespace mkdb