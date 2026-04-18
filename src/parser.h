#pragma once
#include "lexer.h"
#include "ast.h"
#include <vector>
#include <memory>
#include <set>
#include <string>

class Parser
{
public:
    Parser(const std::vector<Token> &tokens, const std::string &sourceDir = ".");
    std::unique_ptr<Program> parse();

    std::set<std::string> structNames; // shared when sub-parsing includes

private:
    std::vector<Token> tokens;
    size_t position;
    std::string sourceDir;

    Token &current();
    void advance();
    bool match(TokenType type);
    bool check(TokenType type);
    Token previous();

    // Top-level
    std::unique_ptr<StructDecl> parseStructDecl();
    std::unique_ptr<Function> parseFunction();
    std::unique_ptr<GlobalVarDeclaration> parseGlobalDeclaration();

    // Statements
    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<Block> parseBlock();

    // Expressions - ordered low -> high precedence
    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseTernary();
    std::unique_ptr<Expression> parseNCO();
    std::unique_ptr<Expression> parseLogicalOr();
    std::unique_ptr<Expression> parseLogicalXor();
    std::unique_ptr<Expression> parseLogicalAnd();
    std::unique_ptr<Expression> parseBitwiseOr();
    std::unique_ptr<Expression> parseBitwiseXor();
    std::unique_ptr<Expression> parseBitwiseAnd();
    std::unique_ptr<Expression> parseEquality();
    std::unique_ptr<Expression> parseComparison(); // handles in / instanceof
    std::unique_ptr<Expression> parseShift();
    std::unique_ptr<Expression> parseAdditive();
    std::unique_ptr<Expression> parseMultiplicative();
    std::unique_ptr<Expression> parseUnary();
    std::unique_ptr<Expression> parsePostfix(); // [], ., ->, ++, --
    std::unique_ptr<Expression> parsePrimary();

    // Helpers for isTypeToken / struct-typed declarations
    bool isTypeToken() const;
    std::string consumeType(); // consumes type + optional * []
};