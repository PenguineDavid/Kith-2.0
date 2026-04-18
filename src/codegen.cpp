#include "codegen.h"
#include "ast.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <set>

// ======================== C type mapping ========================

std::string CodeGen::getCType(const std::string &kt, bool isPointer, bool isArray)
{
    std::string base;
    if (kt == "int")
        base = "int";
    else if (kt == "str")
        base = "char*";
    else if (kt == "float")
        base = "double";
    else if (kt == "bool")
        base = "int";
    else
        base = kt; // struct name or raw C type

    if (isPointer)
        base += "*";
    // isArray handled at declaration site (int arr[] = ...)
    (void)isArray;
    return base;
}

bool CodeGen::isStructType(const std::string &name, Program *program)
{
    for (auto &s : program->structs)
        if (s->name == name)
            return true;
    return false;
}

// ======================== Entry point ========================

std::string CodeGen::generate(Program *program)
{
    std::string code;

    // Standard includes
    code += "#include <stdio.h>\n";
    code += "#include <stdlib.h>\n";
    code += "#include <string.h>\n";
    code += "#include <math.h>\n";
    code += "#include <setjmp.h>\n";

    // User header includes
    for (auto &h : program->headerIncludes)
        code += "#include \"" + h + "\"\n";

    code += "\n";

    // Exception infrastructure
    code += "/* --- Kith exception system --- */\n";
    code += "#define __KITH_EXC_MAX 64\n";
    code += "static jmp_buf __kith_exc_stack[__KITH_EXC_MAX];\n";
    code += "static int     __kith_exc_depth = 0;\n";
    code += "static char*   __kith_exc_msg   = NULL;\n";
    code += "\n";

    // Struct declarations
    for (auto &sd : program->structs)
    {
        code += "typedef struct {\n";
        for (auto &f : sd->fields)
        {
            std::string ctype = getCType(f.type, f.isPointer);
            code += "    " + ctype + " " + f.name + ";\n";
        }
        code += "} " + sd->name + ";\n";
    }
    if (!program->structs.empty())
        code += "\n";

    // Global variables
    // A stub Function is needed so generateExpression can walk the body for type lookup.
    // At global scope only constant expressions are valid in C — gcc will catch violations.
    Function __stubFunc("__global_stub");
    for (auto &g : program->globals)
    {
        // --- Array global ---
        if (g->isArray && g->initExpr)
        {
            std::string ct = getCType(g->type);
            auto *arrLit = dynamic_cast<ArrayLiteralExpr *>(g->initExpr.get());
            if (!arrLit)
                throw std::runtime_error("Global array '" + g->name + "' requires an array literal");
            size_t n = arrLit->elements.size();
            std::string elems;
            for (size_t i = 0; i < n; i++)
            {
                if (i > 0)
                    elems += ", ";
                elems += generateExpression(arrLit->elements[i].get(), &__stubFunc, program);
            }
            code += ct + " " + g->name + "[] = {" + elems + "};\n";
            code += "int " + g->name + "_len = " + std::to_string(n) + ";\n";
            continue;
        }

        // --- Struct global (or any initExpr-based global) ---
        if (g->initExpr)
        {
            std::string ct = getCType(g->type);
            code += ct + " " + g->name + " = " + generateExpression(g->initExpr.get(), &__stubFunc, program) + ";\n";
            continue;
        }

        // --- Scalar globals ---
        if (g->type == "float")
        {
            code += "double " + g->name + " = " + g->value + ";\n";
        }
        else if (g->type == "bool")
        {
            code += "int " + g->name + " = " + g->value + ";\n";
        }
        else if (g->isString)
        {
            if (g->kind == "interp")
                code += "char* " + g->name + " = NULL; /* interpolated global */\n";
            else
                code += "char* " + g->name + " = \"" + escapeString(g->value) + "\";\n";
        }
        else
        {
            code += "int " + g->name + " = " + g->value + ";\n";
        }
    }
    if (!program->globals.empty())
        code += "\n";

    // Forward declarations
    for (auto &func : program->functions)
    {
        code += "int " + func->name + "(";
        for (size_t i = 0; i < func->parameters.size(); i++)
        {
            if (i > 0)
                code += ", ";
            auto &p = func->parameters[i];
            code += getCType(p.first) + " " + p.second;
        }
        code += ");\n";
    }
    code += "\n";

    // Function bodies
    for (auto &func : program->functions)
    {
        code += generateFunction(func.get(), program);
        code += "\n";
    }

    return code;
}

// ======================== Function ========================

std::string CodeGen::generateFunction(Function *func, Program *program)
{
    std::string code = "int " + func->name + "(";
    for (size_t i = 0; i < func->parameters.size(); i++)
    {
        if (i > 0)
            code += ", ";
        auto &p = func->parameters[i];
        code += getCType(p.first) + " " + p.second;
    }
    code += ") {\n";

    for (auto &stmt : func->body)
        code += generateStatement(stmt.get(), func, program);

    code += "    return 0;\n}\n";
    return code;
}

// ======================== Helpers ========================

std::string CodeGen::escapeString(const std::string &s)
{
    std::string out;
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::vector<std::pair<bool, std::string>> CodeGen::parseInterpolated(const std::string &s)
{
    std::vector<std::pair<bool, std::string>> parts;
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t b = s.find('{', pos);
        if (b == std::string::npos)
        {
            parts.push_back({false, s.substr(pos)});
            break;
        }
        if (b > pos)
            parts.push_back({false, s.substr(pos, b - pos)});
        size_t e = s.find('}', b + 1);
        if (e == std::string::npos)
        {
            parts.push_back({false, s.substr(pos)});
            break;
        }
        parts.push_back({true, s.substr(b + 1, e - b - 1)});
        pos = e + 1;
    }
    return parts;
}

std::string CodeGen::buildInterpolatedStr(const std::string &tmpVar,
                                          const std::string &src,
                                          Function *func, Program *program)
{
    auto parts = parseInterpolated(src);
    std::string fmt;
    std::vector<std::pair<std::string, std::string>> vars;

    for (auto &p : parts)
    {
        if (p.first)
        {
            std::string vt = getVariableType(p.second, func, program);
            vars.push_back({p.second, vt});
            fmt += (vt == "str") ? "%s" : (vt == "float") ? "%f"
                                                          : "%d";
        }
        else
        {
            for (char c : p.second)
                fmt += (c == '%') ? "%%" : std::string(1, c);
        }
    }

    std::string lenExpr;
    bool first = true;
    for (auto &p : parts)
    {
        if (!first)
            lenExpr += " + ";
        first = false;
        if (p.first)
        {
            std::string vt = getVariableType(p.second, func, program);
            if (vt == "str")
                lenExpr += "strlen(" + p.second + ")";
            else if (vt == "float")
                lenExpr += "snprintf(NULL, 0, \"%f\", " + p.second + ")";
            else
                lenExpr += "snprintf(NULL, 0, \"%d\", " + p.second + ")";
        }
        else
        {
            lenExpr += std::to_string(p.second.size());
        }
    }
    if (lenExpr.empty())
        lenExpr = "0";

    std::string code;
    code += "        int __len_" + tmpVar + " = " + lenExpr + " + 1;\n";
    code += "        char* " + tmpVar + " = (char*)malloc(__len_" + tmpVar + ");\n";
    code += "        if (" + tmpVar + ") {\n";
    code += "            snprintf(" + tmpVar + ", __len_" + tmpVar + ", \"" + fmt + "\"";
    for (auto &v : vars)
        code += ", " + v.first;
    code += ");\n        }\n";
    return code;
}

std::string CodeGen::getVarTypeInBlock(const std::string &name,
                                       const std::vector<std::unique_ptr<Statement>> &stmts)
{
    for (auto &s : stmts)
    {
        if (auto v = dynamic_cast<VarDeclaration *>(s.get()))
            if (v->name == name)
                return v->type;
        if (auto ifs = dynamic_cast<IfStatement *>(s.get()))
        {
            auto r = getVarTypeInBlock(name, ifs->thenBlock->statements);
            if (!r.empty())
                return r;
            if (ifs->elseBlock)
            {
                r = getVarTypeInBlock(name, ifs->elseBlock->statements);
                if (!r.empty())
                    return r;
            }
        }
        if (auto ws = dynamic_cast<WhileStatement *>(s.get()))
        {
            auto r = getVarTypeInBlock(name, ws->body->statements);
            if (!r.empty())
                return r;
        }
        if (auto dw = dynamic_cast<DoWhileStatement *>(s.get()))
        {
            auto r = getVarTypeInBlock(name, dw->body->statements);
            if (!r.empty())
                return r;
        }
        if (auto fs = dynamic_cast<ForStatement *>(s.get()))
        {
            auto r = getVarTypeInBlock(name, fs->body->statements);
            if (!r.empty())
                return r;
        }
        if (auto fe = dynamic_cast<ForeachStatement *>(s.get()))
            if (fe->varName == name)
                return fe->varType;
        if (auto ts = dynamic_cast<TryStatement *>(s.get()))
        {
            // catch variable is always str (it holds __kith_exc_msg which is char*)
            if (ts->catchVar == name)
                return "str";
            auto r = getVarTypeInBlock(name, ts->tryBlock->statements);
            if (!r.empty())
                return r;
            r = getVarTypeInBlock(name, ts->catchBlock->statements);
            if (!r.empty())
                return r;
            if (ts->finallyBlock)
            {
                r = getVarTypeInBlock(name, ts->finallyBlock->statements);
                if (!r.empty())
                    return r;
            }
        }
    }
    return "";
}

std::string CodeGen::getVariableType(const std::string &name, Function *func, Program *program)
{
    for (auto &p : func->parameters)
        if (p.second == name)
            return p.first;
    auto r = getVarTypeInBlock(name, func->body);
    if (!r.empty())
        return r;
    for (auto &g : program->globals)
        if (g->name == name)
            return g->type;
    return "int";
}

// Generate a statement for use inside a for(;;) clause —
// strips leading indent spaces and trailing ;\n
std::string CodeGen::generateForClause(Statement *stmt, Function *func, Program *program)
{
    std::string s = generateStatement(stmt, func, program);
    // Strip leading spaces
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
        start++;
    s = s.substr(start);
    // Strip trailing newlines and semicolons
    while (!s.empty() && (s.back() == '\n' || s.back() == ';' || s.back() == ' '))
        s.pop_back();
    return s;
}

// ======================== Expressions ========================

std::string CodeGen::generateExpression(Expression *expr, Function *func, Program *program)
{
    // Literal
    if (auto lit = dynamic_cast<LiteralExpr *>(expr))
    {
        if (!lit->isString)
            return lit->value;
        return "\"" + escapeString(lit->value) + "\"";
    }

    // Variable
    if (auto var = dynamic_cast<VariableExpr *>(expr))
        return var->name;

    // Call / built-ins
    if (auto call = dynamic_cast<CallExpr *>(expr))
    {
        auto arg0 = [&]() -> std::string
        {
            if (call->arguments.empty())
                throw std::runtime_error(call->callee + "() needs arguments");
            return generateExpression(call->arguments[0].get(), func, program);
        };

        // --- built-in casts ---
        if (call->callee == "int" && call->arguments.size() == 1)
            return "(int)(" + arg0() + ")";
        if (call->callee == "str" && call->arguments.size() == 1)
            return "(char*)(" + arg0() + ")";
        if (call->callee == "float" && call->arguments.size() == 1)
            return "(double)(" + arg0() + ")";
        if (call->callee == "bool" && call->arguments.size() == 1)
            return "(!!(" + arg0() + "))";

        // --- len(arr) ---
        if (call->callee == "len" && call->arguments.size() == 1)
        {
            if (auto v = dynamic_cast<VariableExpr *>(call->arguments[0].get()))
                return v->name + "_len";
            throw std::runtime_error("len() argument must be an array variable");
        }

        // --- free(x) ---
        if (call->callee == "free" && call->arguments.size() == 1)
            return "free(" + arg0() + ")";

        // --- input() — reads a line, caller must free ---
        if (call->callee == "input")
        {
            return "({ char* __inp=(char*)malloc(1024); "
                   "if(__inp){fgets(__inp,1024,stdin); "
                   "size_t __il=strlen(__inp); "
                   "if(__il>0&&__inp[__il-1]=='\\n')__inp[__il-1]='\\0';} __inp; })";
        }

        // --- toStr(int) ---
        if (call->callee == "toStr" && call->arguments.size() == 1)
        {
            std::string a = arg0();
            return "({ char* __ts=(char*)malloc(32); if(__ts)snprintf(__ts,32,\"%d\",(int)(" + a + ")); __ts; })";
        }

        // --- toInt(str) ---
        if (call->callee == "toInt" && call->arguments.size() == 1)
            return "atoi(" + arg0() + ")";

        // --- toFloat(str) ---
        if (call->callee == "toFloat" && call->arguments.size() == 1)
            return "atof(" + arg0() + ")";

        // --- math ---
        if (call->callee == "sqrt" && call->arguments.size() == 1)
            return "sqrt(" + arg0() + ")";
        if (call->callee == "abs" && call->arguments.size() == 1)
            return "abs(" + arg0() + ")";
        if (call->callee == "floor" && call->arguments.size() == 1)
            return "floor(" + arg0() + ")";
        if (call->callee == "ceil" && call->arguments.size() == 1)
            return "ceil(" + arg0() + ")";
        if (call->callee == "min" && call->arguments.size() == 2)
        {
            std::string a = generateExpression(call->arguments[0].get(), func, program);
            std::string b = generateExpression(call->arguments[1].get(), func, program);
            return "((" + a + ") < (" + b + ") ? (" + a + ") : (" + b + "))";
        }
        if (call->callee == "max" && call->arguments.size() == 2)
        {
            std::string a = generateExpression(call->arguments[0].get(), func, program);
            std::string b = generateExpression(call->arguments[1].get(), func, program);
            return "((" + a + ") > (" + b + ") ? (" + a + ") : (" + b + "))";
        }

        // Struct constructor: StructName(fields...)
        if (isStructType(call->callee, program))
        {
            std::string code = "(" + call->callee + "){";
            for (size_t i = 0; i < call->arguments.size(); i++)
            {
                if (i > 0)
                    code += ", ";
                code += generateExpression(call->arguments[i].get(), func, program);
            }
            return code + "}";
        }

        // Regular function call
        std::string code = call->callee + "(";
        for (size_t i = 0; i < call->arguments.size(); i++)
        {
            if (i > 0)
                code += ", ";
            code += generateExpression(call->arguments[i].get(), func, program);
        }
        return code + ")";
    }

    // Binary
    if (auto bin = dynamic_cast<BinaryExpr *>(expr))
    {
        std::string left = generateExpression(bin->left.get(), func, program);
        std::string right = generateExpression(bin->right.get(), func, program);
        std::string op = bin->op;

        if (op == "&&" || op == "and")
            return "(" + left + " && " + right + ")";
        if (op == "||" || op == "or")
            return "(" + left + " || " + right + ")";
        // Logical XOR: !!(a) ^ !!(b)
        if (op == "lxor" || op == "xor")
            return "(!!(" + left + ") ^ !!(" + right + "))";
        if (op == "**")
            return "pow(" + left + ", " + right + ")";
        if (op == ">>>")
            return "((unsigned int)(" + left + ") >> (" + right + "))";
        if (op == ">>")
            return "(" + left + " >> " + right + ")";
        if (op == "<<")
            return "(" + left + " << " + right + ")";
        // Strict equality — same as == in C (no type coercion)
        if (op == "===" || op == "==")
            return "(" + left + " == " + right + ")";
        if (op == "!==" || op == "!=")
            return "(" + left + " != " + right + ")";
        // NCO
        if (op == "??")
            return "(" + left + " != NULL ? " + left + " : " + right + ")";
        // instanceof — compile-time type check
        if (op == "instanceof")
        {
            // right is a LiteralExpr holding the type name
            if (auto litR = dynamic_cast<LiteralExpr *>(bin->right.get()))
            {
                if (auto varL = dynamic_cast<VariableExpr *>(bin->left.get()))
                    return (getVariableType(varL->name, func, program) == litR->value) ? "1" : "0";
            }
            return "0";
        }
        // in — linear search in array (requires arr_len companion variable)
        if (op == "in")
        {
            if (auto varR = dynamic_cast<VariableExpr *>(bin->right.get()))
            {
                std::string lenVar = varR->name + "_len";
                return "({ int __in_r=0; for(int __in_i=0;__in_i<" + lenVar +
                       ";__in_i++) if(" + right + "[__in_i]==" + left +
                       "){__in_r=1;break;} __in_r; })";
            }
            return "0"; // unknown array, can't determine size
        }
        // Everything else passes through as-is
        return "(" + left + " " + op + " " + right + ")";
    }

    // Unary
    if (auto un = dynamic_cast<UnaryExpr *>(expr))
    {
        std::string sub = generateExpression(un->expr.get(), func, program);
        std::string op = un->op;
        if (op == "!")
            return "(!" + sub + ")";
        if (op == "not")
            return "(!" + sub + ")";
        if (op == "-")
            return "(-" + sub + ")";
        if (op == "~")
            return "(~" + sub + ")";
        if (op == "&")
            return "(&" + sub + ")";
        if (op == "*")
            return "(*" + sub + ")";
        if (op == "post++")
            return "(" + sub + "++)";
        if (op == "post--")
            return "(" + sub + "--)";
        if (op == "pre++")
            return "(++" + sub + ")";
        if (op == "pre--")
            return "(" + sub + "--)"; // note: --sub in C
        if (op == "delete")
            return "free(" + sub + ")";
        if (op == "typeof")
        {
            if (auto v = dynamic_cast<VariableExpr *>(un->expr.get()))
                return "\"" + getVariableType(v->name, func, program) + "\"";
            return "\"unknown\"";
        }
        return "(" + op + sub + ")";
    }

    // Index
    if (auto idx = dynamic_cast<IndexExpr *>(expr))
    {
        std::string arr = generateExpression(idx->array.get(), func, program);
        std::string idxS = generateExpression(idx->index.get(), func, program);
        if (boundsCheck)
        {
            // Derive _len variable name from the array expression if it's a simple variable
            std::string lenVar;
            if (auto ve = dynamic_cast<VariableExpr *>(idx->array.get()))
                lenVar = ve->name + "_len";
            if (!lenVar.empty())
            {
                // Emit as a GNU statement-expression: ({ bounds-check; arr[i]; })
                return "({ int __idx = " + idxS + "; " + "if (__idx < 0 || __idx >= " + lenVar + ") { " + "fprintf(stderr, \"Bounds check failed: index %d out of range [0,%d)\\n\", __idx, " + lenVar + "); " + "exit(1); } " + arr + "[__idx]; })";
            }
        }
        return arr + "[" + idxS + "]";
    }

    // Array literal — used in declarations, emitted as compound literal
    if (auto arr = dynamic_cast<ArrayLiteralExpr *>(expr))
    {
        std::string s = "{";
        for (size_t i = 0; i < arr->elements.size(); i++)
        {
            if (i > 0)
                s += ", ";
            s += generateExpression(arr->elements[i].get(), func, program);
        }
        return s + "}";
    }

    // Static member access: TypeName::member
    // In generated C this is a global variable named TypeName__member.
    if (auto sma = dynamic_cast<StaticMemberAccessExpr *>(expr))
        return sma->typeName + "__" + sma->member;

    // Instance member access: obj.member  or  ptr->member
    if (auto ma = dynamic_cast<MemberAccessExpr *>(expr))
    {
        std::string obj = generateExpression(ma->object.get(), func, program);
        return obj + (ma->isArrow ? "->" : ".") + ma->member;
    }

    // Ternary
    if (auto tern = dynamic_cast<TernaryExpr *>(expr))
    {
        std::string c = generateExpression(tern->condition.get(), func, program);
        std::string t = generateExpression(tern->thenExpr.get(), func, program);
        std::string e = generateExpression(tern->elseExpr.get(), func, program);
        return "(" + c + " ? " + t + " : " + e + ")";
    }

    throw std::runtime_error("Unknown expression node in codegen");
}

// ======================== Statements ========================

std::string CodeGen::generateStatement(Statement *stmt, Function *func, Program *program)
{
    // ---- print ----
    if (auto ps = dynamic_cast<PrintStatement *>(stmt))
    {
        // Interpolated string — needs temp buffer
        if (auto lit = dynamic_cast<LiteralExpr *>(ps->expr.get()))
        {
            if (lit->isString && lit->kind == "interp")
            {
                std::string code = "    {\n";
                code += buildInterpolatedStr("__print_tmp", lit->value, func, program);
                code += "        printf(\"%s\\n\", __print_tmp);\n";
                code += "        free(__print_tmp);\n";
                code += "    }\n";
                return code;
            }
            if (lit->isString)
                return "    printf(\"" + escapeString(lit->value) + "\\n\");\n";
            return "    printf(\"%d\\n\", " + lit->value + ");\n";
        }
        if (auto var = dynamic_cast<VariableExpr *>(ps->expr.get()))
        {
            std::string type = getVariableType(var->name, func, program);
            if (type == "str")
                return "    printf(\"%s\\n\", " + var->name + ");\n";
            if (type == "float")
                return "    printf(\"%g\\n\", " + var->name + ");\n";
            return "    printf(\"%d\\n\", " + var->name + ");\n";
        }
        // General expression — try to infer type for format string
        std::string e = generateExpression(ps->expr.get(), func, program);
        std::string fmt = "%d";

        // IndexExpr: look up the array variable's element type
        if (auto idx = dynamic_cast<IndexExpr *>(ps->expr.get()))
        {
            if (auto arrVar = dynamic_cast<VariableExpr *>(idx->array.get()))
            {
                std::string arrType = getVariableType(arrVar->name, func, program);
                if (arrType == "str" || arrType == "str[]")
                    fmt = "%s";
                else if (arrType == "float" || arrType == "float[]")
                    fmt = "%g";
            }
        }
        // CallExpr returning str (toStr, input, etc.)
        if (auto call = dynamic_cast<CallExpr *>(ps->expr.get()))
        {
            if (call->callee == "toStr" || call->callee == "input")
                fmt = "%s";
        }

        return "    printf(\"" + fmt + "\\n\", " + e + ");\n";
    }

    // ---- return ----
    if (auto ret = dynamic_cast<ReturnStatement *>(stmt))
        return "    return " + generateExpression(ret->value.get(), func, program) + ";\n";

    // ---- stop ----
    if (dynamic_cast<StopStatement *>(stmt))
        return "    exit(0);\n";

    // ---- break ----
    if (dynamic_cast<BreakStatement *>(stmt))
        return "    break;\n";

    // ---- throw ----
    if (auto th = dynamic_cast<ThrowStatement *>(stmt))
    {
        std::string code = "    {\n";
        // If the thrown value is a string literal, use it directly
        if (auto lit = dynamic_cast<LiteralExpr *>(th->value.get()))
        {
            if (lit->isString)
                code += "        __kith_exc_msg = \"" + escapeString(lit->value) + "\";\n";
            else
            {
                // int — convert to a static string via snprintf into a heap buffer
                code += "        { char* __em=(char*)malloc(32);\n";
                code += "          if(__em) snprintf(__em,32,\"%d\"," + lit->value + ");\n";
                code += "          __kith_exc_msg=__em; }\n";
            }
        }
        else
        {
            std::string val = generateExpression(th->value.get(), func, program);
            code += "        __kith_exc_msg = (char*)(" + val + ");\n";
        }
        code += "        if (__kith_exc_depth > 0)\n";
        code += "            longjmp(__kith_exc_stack[__kith_exc_depth-1], 1);\n";
        code += "        else { fprintf(stderr, \"Uncaught exception: %s\\n\", __kith_exc_msg); exit(1); }\n";
        code += "    }\n";
        return code;
    }

    // ---- try / catch / finally ----
    if (auto ts = dynamic_cast<TryStatement *>(stmt))
    {
        std::string code;
        code += "    if (setjmp(__kith_exc_stack[__kith_exc_depth++]) == 0) {\n";
        for (auto &s : ts->tryBlock->statements)
            code += "    " + generateStatement(s.get(), func, program);
        code += "        __kith_exc_depth--;\n";
        if (ts->finallyBlock)
            for (auto &s : ts->finallyBlock->statements)
                code += "    " + generateStatement(s.get(), func, program);
        code += "    } else {\n";
        code += "        __kith_exc_depth--;\n";
        code += "        char* " + ts->catchVar + " = __kith_exc_msg;\n";
        for (auto &s : ts->catchBlock->statements)
            code += "    " + generateStatement(s.get(), func, program);
        if (ts->finallyBlock)
            for (auto &s : ts->finallyBlock->statements)
                code += "    " + generateStatement(s.get(), func, program);
        code += "    }\n";
        return code;
    }

    // ---- var declaration ----
    if (auto var = dynamic_cast<VarDeclaration *>(stmt))
    {
        if (var->isPointer)
        {
            std::string ct = getCType(var->type, true);
            return "    " + ct + " " + var->name + " = " + generateExpression(var->initExpr.get(), func, program) + ";\n";
        }
        if (var->isArray)
        {
            std::string ct = getCType(var->type);
            if (auto arrLit = dynamic_cast<ArrayLiteralExpr *>(var->initExpr.get()))
            {
                std::string elems = "{";
                for (size_t i = 0; i < arrLit->elements.size(); i++)
                {
                    if (i > 0)
                        elems += ", ";
                    elems += generateExpression(arrLit->elements[i].get(), func, program);
                }
                elems += "}";
                std::string code = "    " + ct + " " + var->name + "[] = " + elems + ";\n";
                // Companion length variable
                code += "    int " + var->name + "_len = " + std::to_string(arrLit->elements.size()) + ";\n";
                return code;
            }
            throw std::runtime_error("Array '" + var->name + "' requires an array literal []");
        }
        if (var->type == "str")
        {
            if (auto lit = dynamic_cast<LiteralExpr *>(var->initExpr.get()))
            {
                if (lit->kind == "interp")
                {
                    std::string code = "    char* " + var->name + " = NULL;\n";
                    code += "    {\n";
                    code += buildInterpolatedStr(var->name + "_tmp", lit->value, func, program);
                    code += "        " + var->name + " = " + var->name + "_tmp;\n";
                    code += "    }\n";
                    return code;
                }
                return "    char* " + var->name + " = \"" + escapeString(lit->value) + "\";\n";
            }
            return "    char* " + var->name + " = " + generateExpression(var->initExpr.get(), func, program) + ";\n";
        }
        if (var->type == "float")
        {
            return "    double " + var->name + " = " + generateExpression(var->initExpr.get(), func, program) + ";\n";
        }
        if (var->type == "bool")
        {
            return "    int " + var->name + " = " + generateExpression(var->initExpr.get(), func, program) + ";\n";
        }
        // Struct type
        if (isStructType(var->type, program))
        {
            return "    " + var->type + " " + var->name + " = " + generateExpression(var->initExpr.get(), func, program) + ";\n";
        }
        // int (default)
        return "    int " + var->name + " = " + generateExpression(var->initExpr.get(), func, program) + ";\n";
    }

    // ---- assignment ----
    if (auto assign = dynamic_cast<Assignment *>(stmt))
    {
        std::string val = generateExpression(assign->value.get(), func, program);
        if (assign->deref)
            return "    *" + assign->name + " = " + val + ";\n";
        return "    " + assign->name + " = " + val + ";\n";
    }

    // ---- index assignment ----
    if (auto ia = dynamic_cast<IndexAssignment *>(stmt))
    {
        std::string arr = generateExpression(ia->array.get(), func, program);
        std::string idx = generateExpression(ia->index.get(), func, program);
        std::string val = generateExpression(ia->value.get(), func, program);
        return "    " + arr + "[" + idx + "] = " + val + ";\n";
    }

    // ---- member assignment: obj.member = val  or  ptr->member = val ----
    if (auto ma = dynamic_cast<MemberAssignment *>(stmt))
    {
        std::string obj = generateExpression(ma->object.get(), func, program);
        std::string val = generateExpression(ma->value.get(), func, program);
        std::string op = ma->isArrow ? "->" : ".";
        return "    " + obj + op + ma->member + " = " + val + ";\n";
    }

    // ---- if ----
    if (auto ifs = dynamic_cast<IfStatement *>(stmt))
    {
        std::string code = "    if (" + generateExpression(ifs->condition.get(), func, program) + ") {\n";
        for (auto &s : ifs->thenBlock->statements)
            code += generateStatement(s.get(), func, program);
        if (ifs->elseBlock)
        {
            code += "    } else {\n";
            for (auto &s : ifs->elseBlock->statements)
                code += generateStatement(s.get(), func, program);
        }
        code += "    }\n";
        return code;
    }

    // ---- while ----
    if (auto ws = dynamic_cast<WhileStatement *>(stmt))
    {
        std::string code = "    while (" + generateExpression(ws->condition.get(), func, program) + ") {\n";
        for (auto &s : ws->body->statements)
            code += generateStatement(s.get(), func, program);
        code += "    }\n";
        return code;
    }

    // ---- do-while ----
    if (auto dw = dynamic_cast<DoWhileStatement *>(stmt))
    {
        std::string code = "    do {\n";
        for (auto &s : dw->body->statements)
            code += generateStatement(s.get(), func, program);
        code += "    } while (" + generateExpression(dw->condition.get(), func, program) + ");\n";
        return code;
    }

    // ---- for ----
    if (auto fs = dynamic_cast<ForStatement *>(stmt))
    {
        std::string init = fs->init ? generateForClause(fs->init.get(), func, program) : "";
        std::string cond = fs->condition ? generateExpression(fs->condition.get(), func, program) : "1";
        std::string update = fs->update ? generateForClause(fs->update.get(), func, program) : "";
        std::string code = "    for (" + init + "; " + cond + "; " + update + ") {\n";
        for (auto &s : fs->body->statements)
            code += generateStatement(s.get(), func, program);
        code += "    }\n";
        return code;
    }

    // ---- foreach ----
    if (auto fe = dynamic_cast<ForeachStatement *>(stmt))
    {
        std::string ct = getCType(fe->varType);
        std::string arrCode = generateExpression(fe->array.get(), func, program);
        std::string lenVar;
        // Try to derive companion _len variable from named array
        if (auto ve = dynamic_cast<VariableExpr *>(fe->array.get()))
            lenVar = ve->name + "_len";
        else
            throw std::runtime_error("foreach requires a named array variable (for len tracking)");

        std::string iVar = "__fe_i_" + fe->varName;
        std::string code = "    for (int " + iVar + " = 0; " + iVar + " < " + lenVar + "; " + iVar + "++) {\n";
        code += "        " + ct + " " + fe->varName + " = " + arrCode + "[" + iVar + "];\n";
        for (auto &s : fe->body->statements)
            code += generateStatement(s.get(), func, program);
        code += "    }\n";
        return code;
    }

    // ---- switch ----
    if (auto sw = dynamic_cast<SwitchStatement *>(stmt))
    {
        // Determine if the switch subject is a string type so we can use strcmp
        bool isStrSwitch = false;
        if (auto ve = dynamic_cast<VariableExpr *>(sw->subject.get()))
            isStrSwitch = (getVariableType(ve->name, func, program) == "str");

        std::string subjCode = generateExpression(sw->subject.get(), func, program);

        if (isStrSwitch)
        {
            // Emit as if/else if chain using strcmp
            std::string code;
            bool first = true;
            for (auto &sc : sw->cases)
            {
                if (sc.value == nullptr) // default
                {
                    code += (first ? "    if (1)" : " else");
                    code += " {\n";
                }
                else
                {
                    std::string caseVal = generateExpression(sc.value.get(), func, program);
                    if (first)
                        code += "    if (strcmp(" + subjCode + ", " + caseVal + ") == 0) {\n";
                    else
                        code += " else if (strcmp(" + subjCode + ", " + caseVal + ") == 0) {\n";
                }
                for (auto &s : sc.body->statements)
                    code += generateStatement(s.get(), func, program);
                code += "    }";
                first = false;
            }
            code += "\n";
            return code;
        }

        // Integer switch (original path)
        std::string code = "    switch (" + subjCode + ") {\n";
        for (auto &sc : sw->cases)
        {
            if (sc.value)
                code += "        case " + generateExpression(sc.value.get(), func, program) + ": {\n";
            else
                code += "        default: {\n";
            for (auto &s : sc.body->statements)
                code += "    " + generateStatement(s.get(), func, program);
            code += "            break;\n        }\n";
        }
        code += "    }\n";
        return code;
    }

    // ---- expression statement ----
    if (auto es = dynamic_cast<ExpressionStatement *>(stmt))
        return "    " + generateExpression(es->expr.get(), func, program) + ";\n";

    return "";
}