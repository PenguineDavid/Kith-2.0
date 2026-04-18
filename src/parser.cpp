#include "parser.h"
#include "lexer.h"
#include <stdexcept>
#include <memory>
#include <fstream>
#include <sstream>

Parser::Parser(const std::vector<Token> &t, const std::string &dir)
    : tokens(t), position(0), sourceDir(dir) {}

Token &Parser::current() { return tokens[position]; }
void Parser::advance()
{
    if (position < tokens.size())
        position++;
}
bool Parser::check(TokenType t) { return current().type == t; }
Token Parser::previous() { return tokens[position - 1]; }

bool Parser::match(TokenType t)
{
    if (current().type == t)
    {
        advance();
        return true;
    }
    return false;
}

// Returns true if the current token starts a variable/parameter type
bool Parser::isTypeToken() const
{
    switch (tokens[position].type)
    {
    case TokenType::INT_TYPE:
    case TokenType::STR_TYPE:
    case TokenType::FLOAT_TYPE:
    case TokenType::BOOL_TYPE:
        return true;
    case TokenType::IDENTIFIER:
        return structNames.count(tokens[position].value) > 0;
    default:
        return false;
    }
}

// Consume a type spec (keyword or struct name) including optional trailing * or []
// Returns the base type string; sets isPointer/isArray via out params.
// This helper is only used where we need the raw type string but not the flags.
std::string Parser::consumeType()
{
    std::string t = current().value;
    advance();
    return t;
}

// ======================== Top-level ========================

std::unique_ptr<Program> Parser::parse()
{
    auto program = std::make_unique<Program>();

    while (current().type != TokenType::END_OF_FILE)
    {
        // include "file"
        if (current().type == TokenType::INCLUDE)
        {
            advance();
            if (current().type != TokenType::STRING)
                throw std::runtime_error("Expected string after 'include'");
            std::string path = current().value;
            advance();

            bool isKith = path.size() >= 5 && path.substr(path.size() - 5) == ".kith";
            if (isKith)
            {
                // Resolve relative to sourceDir
                std::string fullPath = sourceDir + "/" + path;
                std::ifstream f(fullPath);
                if (!f.is_open())
                { // try plain path
                    f.open(path);
                    if (!f.is_open())
                        throw std::runtime_error("Cannot open include: " + path);
                }
                std::stringstream buf;
                buf << f.rdbuf();
                Lexer subLex(buf.str());
                Parser subParser(subLex.tokenize(), sourceDir);
                subParser.structNames = structNames;
                auto sub = subParser.parse();
                structNames = subParser.structNames; // bring back any new structs
                for (auto &s : sub->structs)
                    program->structs.push_back(std::move(s));
                for (auto &g : sub->globals)
                    program->globals.push_back(std::move(g));
                for (auto &fn : sub->functions)
                    program->functions.push_back(std::move(fn));
            }
            else
            {
                program->headerIncludes.push_back(path);
            }
            continue;
        }

        // struct declaration
        if (current().type == TokenType::STRUCT)
        {
            auto sd = parseStructDecl();
            structNames.insert(sd->name);
            program->structs.push_back(std::move(sd));
            continue;
        }

        // function
        if (current().type == TokenType::FUNC)
        {
            program->functions.push_back(parseFunction());
            continue;
        }

        // global variable: built-in types OR struct types
        if (current().type == TokenType::INT_TYPE ||
            current().type == TokenType::STR_TYPE ||
            current().type == TokenType::FLOAT_TYPE ||
            current().type == TokenType::BOOL_TYPE ||
            isTypeToken())
        {
            program->globals.push_back(parseGlobalDeclaration());
            continue;
        }

        throw std::runtime_error("Unexpected top-level token: '" + current().value + "'");
    }
    return program;
}

// ---- Struct declaration ----
std::unique_ptr<StructDecl> Parser::parseStructDecl()
{
    match(TokenType::STRUCT);
    if (current().type != TokenType::IDENTIFIER)
        throw std::runtime_error("Expected struct name");
    auto sd = std::make_unique<StructDecl>();
    sd->name = current().value;
    advance();

    if (!match(TokenType::LBRACE))
        throw std::runtime_error("Expected '{' after struct name");

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE))
    {
        StructField sf;
        if (!isTypeToken())
            throw std::runtime_error("Expected type in struct field");
        sf.type = consumeType();
        sf.isPointer = match(TokenType::STAR);
        if (current().type != TokenType::IDENTIFIER)
            throw std::runtime_error("Expected field name");
        sf.name = current().value;
        advance();
        sd->fields.push_back(std::move(sf));
    }
    if (!match(TokenType::RBRACE))
        throw std::runtime_error("Expected '}' after struct body");
    return sd;
}

// ---- Global variable declaration ----
std::unique_ptr<GlobalVarDeclaration> Parser::parseGlobalDeclaration()
{
    std::string type = current().value;
    bool isStr = (current().type == TokenType::STR_TYPE);
    bool isBool = (current().type == TokenType::BOOL_TYPE);
    bool isStructT = (current().type == TokenType::IDENTIFIER); // struct type
    advance();

    // Optional [] for array types  (e.g.  int[]  float[]  str[])
    bool isArray = false;
    if (check(TokenType::LBRACKET))
    {
        advance();
        if (!match(TokenType::RBRACKET))
            throw std::runtime_error("Expected ']' after '[' in global array type");
        isArray = true;
    }

    if (current().type != TokenType::IDENTIFIER)
        throw std::runtime_error("Expected global variable name after type '" + type + "'");
    std::string name = current().value;
    advance();

    if (!match(TokenType::ASSIGN))
        throw std::runtime_error("Expected '=' in global declaration of '" + name + "'");

    auto decl = std::make_unique<GlobalVarDeclaration>(type, name, "", isStr);
    decl->isArray = isArray;

    // ---- Array initializer: [elem, ...] or {elem, ...} ----
    if (isArray)
    {
        bool useBrace = check(TokenType::LBRACE);
        if (!useBrace && !check(TokenType::LBRACKET))
            throw std::runtime_error("Expected '[' or '{' for global array initializer '" + name + "'");
        advance(); // consume [ or {
        TokenType close = useBrace ? TokenType::RBRACE : TokenType::RBRACKET;

        auto arr = std::make_unique<ArrayLiteralExpr>();
        if (!check(close))
        {
            do
            {
                arr->elements.push_back(parseExpression());
            } while (match(TokenType::COMMA));
        }
        if (!match(close))
            throw std::runtime_error("Expected closing bracket for global array '" + name + "'");
        decl->initExpr = std::move(arr);
        return decl;
    }

    // ---- Struct constructor: StructName(field, ...) ----
    if (isStructT)
    {
        decl->initExpr = parseExpression(); // will parse StructName(...)
        return decl;
    }

    // ---- Scalar initializers (int / float / bool / str) ----
    std::string value;
    std::string kind = "normal";

    if (isBool)
    {
        if (match(TokenType::TRUE))
            value = "1";
        else if (match(TokenType::FALSE))
            value = "0";
        else
            throw std::runtime_error("Expected true/false for bool global '" + name + "'");
    }
    else if (isStr)
    {
        if (current().type != TokenType::STRING)
            throw std::runtime_error("Expected string literal for global '" + name + "'");
        std::string raw = current().value;
        if (raw.rfind("r:", 0) == 0)
        {
            kind = "raw";
            raw = raw.substr(2);
        }
        else if (raw.rfind("$:", 0) == 0)
        {
            kind = "interp";
            raw = raw.substr(2);
        }
        else if (raw.rfind("m:", 0) == 0)
        {
            kind = "multi";
            raw = raw.substr(2);
        }
        value = raw;
        advance();
    }
    else // int or float
    {
        bool neg = match(TokenType::MINUS);
        if (current().type != TokenType::NUMBER)
            throw std::runtime_error("Expected numeric literal for global '" + name +
                                     "' (expressions referencing other variables not allowed at global scope)");
        value = (neg ? "-" : "") + current().value;
        advance();
    }

    decl->value = value;
    decl->isString = isStr;
    decl->kind = kind;
    return decl;
}

// ---- Function ----
std::unique_ptr<Function> Parser::parseFunction()
{
    match(TokenType::FUNC);
    if (current().type != TokenType::IDENTIFIER)
        throw std::runtime_error("Expected function name");
    auto func = std::make_unique<Function>(current().value);
    advance();

    if (!match(TokenType::LPAREN))
        throw std::runtime_error("Expected '(' after function name");

    if (!check(TokenType::RPAREN))
    {
        do
        {
            if (!isTypeToken())
                throw std::runtime_error("Expected parameter type");
            std::string ptype = consumeType();
            bool pptr = match(TokenType::STAR);
            if (pptr)
                ptype += "*";
            if (current().type != TokenType::IDENTIFIER)
                throw std::runtime_error("Expected parameter name");
            std::string pname = current().value;
            advance();
            func->parameters.push_back({ptype, pname});
        } while (match(TokenType::COMMA));
    }

    if (!match(TokenType::RPAREN))
        throw std::runtime_error("Expected ')' after parameters");
    if (!match(TokenType::LBRACE))
        throw std::runtime_error("Expected '{' to open function body");

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE))
        func->body.push_back(parseStatement());

    if (!match(TokenType::RBRACE))
        throw std::runtime_error("Expected '}' to close function");
    return func;
}

// ======================== Statements ========================

std::unique_ptr<Statement> Parser::parseStatement()
{
    // ---- Variable declaration: type [*] [[] name = expr ----
    if (isTypeToken())
    {
        std::string type = consumeType();
        bool isPointer = false, isArray = false;
        if (match(TokenType::STAR))
        {
            isPointer = true;
        }
        else if (check(TokenType::LBRACKET))
        {
            advance();
            if (!match(TokenType::RBRACKET))
                throw std::runtime_error("Expected ']'");
            isArray = true;
        }
        if (current().type != TokenType::IDENTIFIER)
            throw std::runtime_error("Expected variable name after type");
        std::string name = current().value;
        advance();
        if (!match(TokenType::ASSIGN))
            throw std::runtime_error("Expected '=' in declaration of '" + name + "'");
        auto init = parseExpression();
        return std::make_unique<VarDeclaration>(type, name, std::move(init), isPointer, isArray);
    }

    // ---- Deref assignment: *ptr = expr ----
    if (check(TokenType::STAR) &&
        position + 1 < tokens.size() && tokens[position + 1].type == TokenType::IDENTIFIER &&
        position + 2 < tokens.size() && tokens[position + 2].type == TokenType::ASSIGN)
    {
        advance(); // *
        std::string name = current().value;
        advance(); // name
        advance(); // =
        return std::make_unique<Assignment>(name, parseExpression(), true);
    }

    // ---- Index assignment: identifier[expr] = expr ----
    if (check(TokenType::IDENTIFIER) &&
        position + 1 < tokens.size() && tokens[position + 1].type == TokenType::LBRACKET)
    {
        // Peek ahead for the = after the ]
        // We parse the array expression and check for assignment
        size_t saved = position;
        std::string arrName = current().value;
        advance(); // identifier
        advance(); // [
        auto idx = parseExpression();
        if (match(TokenType::RBRACKET) && check(TokenType::ASSIGN))
        {
            advance(); // =
            auto val = parseExpression();
            return std::make_unique<IndexAssignment>(
                std::make_unique<VariableExpr>(arrName),
                std::move(idx), std::move(val));
        }
        // Not an index assignment — backtrack
        position = saved;
    }

    // ---- Member assignment: obj.member = expr  or  ptr->member = expr ----
    // We speculatively parse the LHS as a postfix expression, then check for =.
    // If it resolves to a MemberAccessExpr followed by ASSIGN, emit MemberAssignment.
    // Otherwise backtrack and fall through to expression-statement.
    if (check(TokenType::IDENTIFIER))
    {
        size_t savedPos = position;
        auto lhs = parsePostfix();
        if (check(TokenType::ASSIGN))
        {
            if (auto ma = dynamic_cast<MemberAccessExpr *>(lhs.get()))
            {
                advance(); // consume =
                auto val = parseExpression();
                // Need to move object out of ma; lhs still owns it
                return std::make_unique<MemberAssignment>(
                    std::move(ma->object), ma->member, ma->isArrow, std::move(val));
            }
        }
        // Not a member assignment — backtrack
        position = savedPos;
    }

    // ---- Assignment: identifier = expr ----
    if (check(TokenType::IDENTIFIER) &&
        position + 1 < tokens.size() && tokens[position + 1].type == TokenType::ASSIGN)
    {
        std::string name = current().value;
        advance();
        advance();
        return std::make_unique<Assignment>(name, parseExpression());
    }

    // ---- print(expr) ----
    if (check(TokenType::IDENTIFIER) && current().value == "print")
    {
        advance();
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after print");
        auto expr = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after print arg");
        return std::make_unique<PrintStatement>(std::move(expr));
    }

    // ---- return expr ----
    if (match(TokenType::RETURN))
        return std::make_unique<ReturnStatement>(parseExpression());

    // ---- stop ----
    if (match(TokenType::STOP))
        return std::make_unique<StopStatement>();

    // ---- break ----
    if (match(TokenType::BREAK))
        return std::make_unique<BreakStatement>();

    // ---- throw expr ----
    if (match(TokenType::THROW))
        return std::make_unique<ThrowStatement>(parseExpression());

    // ---- try ----
    if (match(TokenType::TRY))
    {
        auto ts = std::make_unique<TryStatement>();
        ts->tryBlock = parseBlock();
        if (!match(TokenType::CATCH))
            throw std::runtime_error("Expected 'catch' after try block");
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after catch");
        if (current().type != TokenType::IDENTIFIER)
            throw std::runtime_error("Expected catch variable name");
        ts->catchVar = current().value;
        advance();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after catch variable");
        ts->catchBlock = parseBlock();
        if (match(TokenType::FINALLY))
            ts->finallyBlock = parseBlock();
        return ts;
    }

    // ---- if ----
    if (match(TokenType::IF))
    {
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after if");
        auto cond = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after if condition");
        auto thenB = parseBlock();
        std::unique_ptr<Block> elseB;
        if (match(TokenType::ELSE))
            elseB = parseBlock();
        return std::make_unique<IfStatement>(std::move(cond), std::move(thenB), std::move(elseB));
    }

    // ---- while ----
    if (match(TokenType::WHILE))
    {
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after while");
        auto cond = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after while condition");
        return std::make_unique<WhileStatement>(std::move(cond), parseBlock());
    }

    // ---- do { } while (cond) ----
    if (match(TokenType::DO))
    {
        auto body = parseBlock();
        if (!match(TokenType::WHILE))
            throw std::runtime_error("Expected 'while' after do block");
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '('");
        auto cond = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')'");
        return std::make_unique<DoWhileStatement>(std::move(body), std::move(cond));
    }

    // ---- for (init; cond; update) ----
    if (match(TokenType::FOR))
    {
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after for");
        auto fs = std::make_unique<ForStatement>();

        // init (optional)
        if (!check(TokenType::SEMICOLON))
            fs->init = parseStatement();
        if (!match(TokenType::SEMICOLON))
            throw std::runtime_error("Expected ';' in for loop");

        // condition (optional)
        if (!check(TokenType::SEMICOLON))
            fs->condition = parseExpression();
        if (!match(TokenType::SEMICOLON))
            throw std::runtime_error("Expected ';' in for loop");

        // update (optional)
        if (!check(TokenType::RPAREN))
            fs->update = parseStatement();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after for header");

        fs->body = parseBlock();
        return fs;
    }

    // ---- foreach (type name in array) ----
    if (match(TokenType::FOREACH))
    {
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after foreach");
        auto fe = std::make_unique<ForeachStatement>();
        if (!isTypeToken())
            throw std::runtime_error("Expected type in foreach");
        fe->varType = consumeType();
        if (current().type != TokenType::IDENTIFIER)
            throw std::runtime_error("Expected variable name in foreach");
        fe->varName = current().value;
        advance();
        if (!match(TokenType::IN_KW))
            throw std::runtime_error("Expected 'in' in foreach");
        fe->array = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after foreach");
        fe->body = parseBlock();
        return fe;
    }

    // ---- switch ----
    if (match(TokenType::SWITCH))
    {
        if (!match(TokenType::LPAREN))
            throw std::runtime_error("Expected '(' after switch");
        auto sw = std::make_unique<SwitchStatement>();
        sw->subject = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')' after switch expr");
        if (!match(TokenType::LBRACE))
            throw std::runtime_error("Expected '{'");
        while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE))
        {
            SwitchCase sc;
            if (match(TokenType::CASE))
                sc.value = parseExpression();
            else if (match(TokenType::DEFAULT))
                sc.value = nullptr;
            else
                throw std::runtime_error("Expected 'case' or 'default' in switch");
            sc.body = parseBlock();
            sw->cases.push_back(std::move(sc));
        }
        if (!match(TokenType::RBRACE))
            throw std::runtime_error("Expected '}'");
        return sw;
    }

    // ---- expression statement (function calls, ++, etc.) ----
    return std::make_unique<ExpressionStatement>(parseExpression());
}

std::unique_ptr<Block> Parser::parseBlock()
{
    if (!match(TokenType::LBRACE))
        throw std::runtime_error("Expected '{'");
    auto block = std::make_unique<Block>();
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE))
        block->statements.push_back(parseStatement());
    if (!match(TokenType::RBRACE))
        throw std::runtime_error("Expected '}'");
    return block;
}

// ======================== Expressions ========================
//
// Precedence (low → high):
//   parseTernary      ? :
//   parseNCO          ??
//   parseLogicalOr    or ||
//   parseLogicalXor   xor ^^
//   parseLogicalAnd   and &&
//   parseBitwiseOr    |
//   parseBitwiseXor   ^
//   parseBitwiseAnd   &
//   parseEquality     == != === !==
//   parseComparison   < > <= >= in instanceof
//   parseShift        << >> >>>
//   parseAdditive     + -
//   parseMultiplicative * / % **
//   parseUnary        - not ! ~ & * typeof delete pre++ pre--
//   parsePostfix      [] . -> post++ post--
//   parsePrimary      literals, calls, (...)

std::unique_ptr<Expression> Parser::parseExpression() { return parseTernary(); }

std::unique_ptr<Expression> Parser::parseTernary()
{
    auto expr = parseNCO();
    if (match(TokenType::QUESTION))
    {
        auto thenE = parseTernary();
        if (!match(TokenType::COLON))
            throw std::runtime_error("Expected ':' in ternary");
        auto elseE = parseTernary();
        return std::make_unique<TernaryExpr>(std::move(expr), std::move(thenE), std::move(elseE));
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parseNCO()
{
    auto expr = parseLogicalOr();
    while (match(TokenType::NCO))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "??", parseLogicalOr());
    return expr;
}

std::unique_ptr<Expression> Parser::parseLogicalOr()
{
    auto expr = parseLogicalXor();
    while (match(TokenType::OR) || match(TokenType::LOGICAL_OR))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "||", parseLogicalXor());
    return expr;
}

std::unique_ptr<Expression> Parser::parseLogicalXor()
{
    auto expr = parseLogicalAnd();
    while (match(TokenType::XOR) || match(TokenType::LOGICAL_XOR))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "lxor", parseLogicalAnd());
    return expr;
}

std::unique_ptr<Expression> Parser::parseLogicalAnd()
{
    auto expr = parseBitwiseOr();
    while (match(TokenType::AND) || match(TokenType::LOGICAL_AND))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "&&", parseBitwiseOr());
    return expr;
}

std::unique_ptr<Expression> Parser::parseBitwiseOr()
{
    auto expr = parseBitwiseXor();
    while (match(TokenType::BIT_OR))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "|", parseBitwiseXor());
    return expr;
}

std::unique_ptr<Expression> Parser::parseBitwiseXor()
{
    auto expr = parseBitwiseAnd();
    while (match(TokenType::BIT_XOR))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "^", parseBitwiseAnd());
    return expr;
}

std::unique_ptr<Expression> Parser::parseBitwiseAnd()
{
    auto expr = parseEquality();
    while (match(TokenType::BIT_AND))
        expr = std::make_unique<BinaryExpr>(std::move(expr), "&", parseEquality());
    return expr;
}

std::unique_ptr<Expression> Parser::parseEquality()
{
    auto expr = parseComparison();
    while (true)
    {
        if (match(TokenType::STRICT_EQ))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "===", parseComparison());
        else if (match(TokenType::STRICT_NE))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "!==", parseComparison());
        else if (match(TokenType::EQ))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "==", parseComparison());
        else if (match(TokenType::NE))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "!=", parseComparison());
        else
            break;
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parseComparison()
{
    auto expr = parseShift();
    while (true)
    {
        if (match(TokenType::LT))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "<", parseShift());
        else if (match(TokenType::GT))
            expr = std::make_unique<BinaryExpr>(std::move(expr), ">", parseShift());
        else if (match(TokenType::LE))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "<=", parseShift());
        else if (match(TokenType::GE))
            expr = std::make_unique<BinaryExpr>(std::move(expr), ">=", parseShift());
        else if (match(TokenType::IN_KW))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "in", parseShift());
        else if (match(TokenType::INSTANCEOF))
        {
            // RHS is a type name (identifier)
            if (current().type != TokenType::IDENTIFIER)
                throw std::runtime_error("Expected type name after instanceof");
            auto typeNameExpr = std::make_unique<LiteralExpr>(current().value, false);
            advance();
            expr = std::make_unique<BinaryExpr>(std::move(expr), "instanceof", std::move(typeNameExpr));
        }
        else
            break;
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parseShift()
{
    auto expr = parseAdditive();
    while (true)
    {
        if (match(TokenType::URSH))
            expr = std::make_unique<BinaryExpr>(std::move(expr), ">>>", parseAdditive());
        else if (match(TokenType::RSH))
            expr = std::make_unique<BinaryExpr>(std::move(expr), ">>", parseAdditive());
        else if (match(TokenType::LSH))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "<<", parseAdditive());
        else
            break;
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parseAdditive()
{
    auto expr = parseMultiplicative();
    while (true)
    {
        if (match(TokenType::PLUS))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "+", parseMultiplicative());
        else if (match(TokenType::MINUS))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "-", parseMultiplicative());
        else
            break;
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parseMultiplicative()
{
    auto expr = parseUnary();
    while (true)
    {
        // Don't consume * if it's the start of a deref-assignment (*ptr = val)
        if (check(TokenType::STAR) &&
            position + 1 < tokens.size() && tokens[position + 1].type == TokenType::IDENTIFIER &&
            position + 2 < tokens.size() && tokens[position + 2].type == TokenType::ASSIGN)
            break;

        if (match(TokenType::POWER))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "**", parseUnary());
        else if (match(TokenType::STAR))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "*", parseUnary());
        else if (match(TokenType::SLASH))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "/", parseUnary());
        else if (match(TokenType::PERCENT))
            expr = std::make_unique<BinaryExpr>(std::move(expr), "%", parseUnary());
        else
            break;
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parseUnary()
{
    if (match(TokenType::NOT) || match(TokenType::LOGICAL_NOT))
        return std::make_unique<UnaryExpr>("!", parseUnary());
    if (match(TokenType::MINUS))
        return std::make_unique<UnaryExpr>("-", parseUnary());
    if (match(TokenType::BIT_NOT))
        return std::make_unique<UnaryExpr>("~", parseUnary());
    if (match(TokenType::BIT_AND))
        return std::make_unique<UnaryExpr>("&", parseUnary());
    if (match(TokenType::STAR))
        return std::make_unique<UnaryExpr>("*", parseUnary());
    if (match(TokenType::TYPEOF))
        return std::make_unique<UnaryExpr>("typeof", parseUnary());
    if (match(TokenType::DELETE_KW))
        return std::make_unique<UnaryExpr>("delete", parseUnary());
    if (match(TokenType::INC))
        return std::make_unique<UnaryExpr>("pre++", parseUnary());
    if (match(TokenType::DEC))
        return std::make_unique<UnaryExpr>("pre--", parseUnary());
    return parsePostfix();
}

std::unique_ptr<Expression> Parser::parsePostfix()
{
    auto expr = parsePrimary();
    while (true)
    {
        if (check(TokenType::LBRACKET))
        {
            advance();
            auto idx = parseExpression();
            if (!match(TokenType::RBRACKET))
                throw std::runtime_error("Expected ']'");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(idx));
        }
        else if (check(TokenType::DOT) || check(TokenType::ARROW))
        {
            bool arrow = check(TokenType::ARROW);
            advance();
            if (current().type != TokenType::IDENTIFIER)
                throw std::runtime_error("Expected member name after '.' or '->'");
            std::string member = current().value;
            advance();
            expr = std::make_unique<MemberAccessExpr>(std::move(expr), member, arrow);
        }
        else if (match(TokenType::INC))
        {
            expr = std::make_unique<UnaryExpr>("post++", std::move(expr));
        }
        else if (match(TokenType::DEC))
        {
            expr = std::make_unique<UnaryExpr>("post--", std::move(expr));
        }
        else
            break;
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parsePrimary()
{
    // Number literal
    if (match(TokenType::NUMBER))
        return std::make_unique<LiteralExpr>(previous().value, false);

    // String literal (all variants)
    if (match(TokenType::STRING))
    {
        std::string raw = previous().value;
        std::string kind = "normal";
        if (raw.rfind("r:", 0) == 0)
        {
            kind = "raw";
            raw = raw.substr(2);
        }
        else if (raw.rfind("$:", 0) == 0)
        {
            kind = "interp";
            raw = raw.substr(2);
        }
        else if (raw.rfind("m:", 0) == 0)
        {
            kind = "multi";
            raw = raw.substr(2);
        }
        return std::make_unique<LiteralExpr>(raw, true, kind);
    }

    // Boolean
    if (match(TokenType::TRUE))
        return std::make_unique<LiteralExpr>("1", false);
    if (match(TokenType::FALSE))
        return std::make_unique<LiteralExpr>("0", false);

    // Parenthesised expression
    if (match(TokenType::LPAREN))
    {
        auto expr = parseExpression();
        if (!match(TokenType::RPAREN))
            throw std::runtime_error("Expected ')'");
        return expr;
    }

    // Array literal [elem, ...]
    if (check(TokenType::LBRACKET))
    {
        advance();
        auto arr = std::make_unique<ArrayLiteralExpr>();
        if (!check(TokenType::RBRACKET))
        {
            do
            {
                arr->elements.push_back(parseExpression());
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RBRACKET))
            throw std::runtime_error("Expected ']'");
        return arr;
    }

    // Identifier — variable or function call
    if (match(TokenType::IDENTIFIER))
    {
        std::string name = previous().value;
        // TypeName::member  (static member / constant access)
        if (match(TokenType::COLONCOLON))
        {
            if (current().type != TokenType::IDENTIFIER)
                throw std::runtime_error("Expected member name after '::'");
            std::string member = current().value;
            advance();
            return std::make_unique<StaticMemberAccessExpr>(name, member);
        }
        if (check(TokenType::LPAREN))
        {
            advance();
            auto call = std::make_unique<CallExpr>(name);
            if (!check(TokenType::RPAREN))
            {
                do
                {
                    call->arguments.push_back(parseExpression());
                } while (match(TokenType::COMMA));
            }
            if (!match(TokenType::RPAREN))
                throw std::runtime_error("Expected ')' after args to '" + name + "'");
            return call;
        }
        return std::make_unique<VariableExpr>(name);
    }

    throw std::runtime_error("Unexpected token in expression: '" + current().value + "'");
}