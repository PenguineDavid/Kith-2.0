#pragma once
#include <string>
#include <vector>
#include <memory>

struct Expression;
struct Statement;

struct ASTNode
{
    virtual ~ASTNode() = default;
};

// -------------------- Expressions --------------------
struct Expression : ASTNode
{
    virtual ~Expression() = default;
};

struct LiteralExpr : Expression
{
    std::string value;
    bool isString;
    std::string kind; // "normal"|"raw"|"interp"|"multi"
    LiteralExpr(const std::string &v, bool s = false, const std::string &k = "normal")
        : value(v), isString(s), kind(k) {}
};

struct VariableExpr : Expression
{
    std::string name;
    VariableExpr(const std::string &n) : name(n) {}
};

struct CallExpr : Expression
{
    std::string callee;
    std::vector<std::unique_ptr<Expression>> arguments;
    CallExpr(const std::string &c) : callee(c) {}
};

struct BinaryExpr : Expression
{
    std::unique_ptr<Expression> left;
    std::string op;
    std::unique_ptr<Expression> right;
    BinaryExpr(std::unique_ptr<Expression> l, const std::string &o, std::unique_ptr<Expression> r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

// op: "not","!","-","~","&"(addr),"*"(deref),
//     "typeof","delete","post++","post--","pre++","pre--"
struct UnaryExpr : Expression
{
    std::string op;
    std::unique_ptr<Expression> expr;
    UnaryExpr(const std::string &o, std::unique_ptr<Expression> e)
        : op(o), expr(std::move(e)) {}
};

struct IndexExpr : Expression
{
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    IndexExpr(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i)
        : array(std::move(a)), index(std::move(i)) {}
};

struct ArrayLiteralExpr : Expression
{
    std::vector<std::unique_ptr<Expression>> elements;
};

// TypeName::member (static/namespace-style access)
struct StaticMemberAccessExpr : Expression
{
    std::string typeName;
    std::string member;
    StaticMemberAccessExpr(const std::string &t, const std::string &m)
        : typeName(t), member(m) {}
};

struct MemberAccessExpr : Expression
{
    std::unique_ptr<Expression> object;
    std::string member;
    bool isArrow;
    MemberAccessExpr(std::unique_ptr<Expression> o, const std::string &m, bool a)
        : object(std::move(o)), member(m), isArrow(a) {}
};

struct TernaryExpr : Expression
{
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> thenExpr;
    std::unique_ptr<Expression> elseExpr;
    TernaryExpr(std::unique_ptr<Expression> c,
                std::unique_ptr<Expression> t,
                std::unique_ptr<Expression> e)
        : condition(std::move(c)), thenExpr(std::move(t)), elseExpr(std::move(e)) {}
};

// -------------------- Statements --------------------
struct Statement : ASTNode
{
};

struct ExpressionStatement : Statement
{
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
};

struct PrintStatement : Statement
{
    std::unique_ptr<Expression> expr;
    PrintStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
};

struct ReturnStatement : Statement
{
    std::unique_ptr<Expression> value;
    ReturnStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
};

struct StopStatement : Statement
{
    StopStatement() = default;
};
struct BreakStatement : Statement
{
    BreakStatement() = default;
};

struct VarDeclaration : Statement
{
    std::string type; // "int","str","float","bool", or a struct name
    std::string name;
    std::unique_ptr<Expression> initExpr;
    bool isPointer = false;
    bool isArray = false;
    VarDeclaration(const std::string &t, const std::string &n,
                   std::unique_ptr<Expression> e,
                   bool ptr = false, bool arr = false)
        : type(t), name(n), initExpr(std::move(e)), isPointer(ptr), isArray(arr) {}
};

struct Assignment : Statement
{
    std::string name;
    std::unique_ptr<Expression> value;
    bool deref = false;
    Assignment(const std::string &n, std::unique_ptr<Expression> v, bool d = false)
        : name(n), value(std::move(v)), deref(d) {}
};

// obj.member = value  or  ptr->member = value
struct MemberAssignment : Statement
{
    std::unique_ptr<Expression> object;
    std::string member;
    bool isArrow;
    std::unique_ptr<Expression> value;
    MemberAssignment(std::unique_ptr<Expression> o, const std::string &m,
                     bool a, std::unique_ptr<Expression> v)
        : object(std::move(o)), member(m), isArrow(a), value(std::move(v)) {}
};

// arr[index] = value
struct IndexAssignment : Statement
{
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;
    IndexAssignment(std::unique_ptr<Expression> a,
                    std::unique_ptr<Expression> i,
                    std::unique_ptr<Expression> v)
        : array(std::move(a)), index(std::move(i)), value(std::move(v)) {}
};

struct Block : Statement
{
    std::vector<std::unique_ptr<Statement>> statements;
};

struct IfStatement : Statement
{
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> thenBlock;
    std::unique_ptr<Block> elseBlock;
    IfStatement(std::unique_ptr<Expression> c,
                std::unique_ptr<Block> t,
                std::unique_ptr<Block> e = nullptr)
        : condition(std::move(c)), thenBlock(std::move(t)), elseBlock(std::move(e)) {}
};

struct WhileStatement : Statement
{
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;
    WhileStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> b)
        : condition(std::move(c)), body(std::move(b)) {}
};

struct DoWhileStatement : Statement
{
    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> condition;
    DoWhileStatement(std::unique_ptr<Block> b, std::unique_ptr<Expression> c)
        : body(std::move(b)), condition(std::move(c)) {}
};

// for (init; cond; update) { }
struct ForStatement : Statement
{
    std::unique_ptr<Statement> init;       // may be null
    std::unique_ptr<Expression> condition; // null → 1
    std::unique_ptr<Statement> update;     // may be null
    std::unique_ptr<Block> body;
};

// foreach (type varName in array) { }
struct ForeachStatement : Statement
{
    std::string varType;
    std::string varName;
    std::unique_ptr<Expression> array;
    std::unique_ptr<Block> body;
};

struct SwitchCase
{
    std::unique_ptr<Expression> value; // null = default
    std::unique_ptr<Block> body;
    SwitchCase() = default;
    SwitchCase(SwitchCase &&) = default;
    SwitchCase &operator=(SwitchCase &&) = default;
};

struct SwitchStatement : Statement
{
    std::unique_ptr<Expression> subject;
    std::vector<SwitchCase> cases;
};

struct ThrowStatement : Statement
{
    std::unique_ptr<Expression> value;
    ThrowStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
};

struct TryStatement : Statement
{
    std::unique_ptr<Block> tryBlock;
    std::string catchVar;
    std::unique_ptr<Block> catchBlock;
    std::unique_ptr<Block> finallyBlock; // null if absent
};

// -------------------- Global declarations --------------------
struct GlobalVarDeclaration : ASTNode
{
    std::string type; // "int","str","float","bool", or a struct name
    std::string name;
    std::string value; // scalar literal; empty when initExpr is used
    bool isString = false;
    std::string kind; // "normal"|"raw"|"interp"|"multi"
    bool isArray = false;
    bool isPointer = false;
    // Set for array literals and struct constructor inits
    std::unique_ptr<Expression> initExpr;

    GlobalVarDeclaration(const std::string &t, const std::string &n,
                         const std::string &v, bool s, const std::string &k = "normal")
        : type(t), name(n), value(v), isString(s), kind(k) {}
};

struct StructField
{
    std::string type;
    std::string name;
    bool isPointer = false;
};

struct StructDecl : ASTNode
{
    std::string name;
    std::vector<StructField> fields;
};

struct Function : ASTNode
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> parameters; // {type,name}
    std::vector<std::unique_ptr<Statement>> body;
    Function(const std::string &n) : name(n) {}
};

struct Program : ASTNode
{
    std::vector<std::string> headerIncludes;
    std::vector<std::unique_ptr<StructDecl>> structs;
    std::vector<std::unique_ptr<GlobalVarDeclaration>> globals;
    std::vector<std::unique_ptr<Function>> functions;
};