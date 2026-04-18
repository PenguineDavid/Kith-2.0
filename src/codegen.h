#pragma once
#include "ast.h"
#include <string>
#include <vector>
#include <utility>

class CodeGen
{
public:
    bool boundsCheck = false; // set by --bounds flag
    std::string generate(Program *program);

private:
    // C type from Kith type string
    static std::string getCType(const std::string &kithType,
                                bool isPointer = false,
                                bool isArray = false);

    std::string generateFunction(Function *func, Program *program);
    std::string generateStatement(Statement *stmt, Function *func, Program *program);
    std::string generateExpression(Expression *expr, Function *func, Program *program);

    // For clause: generate a statement inline (no leading indent, no trailing newline)
    std::string generateForClause(Statement *stmt, Function *func, Program *program);

    // Interpolated string helpers
    std::string escapeString(const std::string &s);
    std::vector<std::pair<bool, std::string>> parseInterpolated(const std::string &s);
    std::string buildInterpolatedStr(const std::string &tmpVar,
                                     const std::string &src,
                                     Function *func, Program *program);

    // Type inference
    std::string getVariableType(const std::string &name, Function *func, Program *program);
    std::string getVarTypeInBlock(const std::string &name,
                                  const std::vector<std::unique_ptr<Statement>> &stmts);

    // Check if a name is a known struct type
    bool isStructType(const std::string &name, Program *program);
};