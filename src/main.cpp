#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"

static void printUsage()
{
    std::cout << "Usage: kith [options] <input.kith> [output_exe]\n"
              << "\n"
              << "Options:\n"
              << "  --keep-c     Keep the intermediate output.c file after compilation\n"
              << "  --bounds     Emit runtime bounds checks for all array accesses\n"
              << "\n"
              << "  output_exe defaults to 'output'\n";
}

int main(int argc, char **argv)
{
    bool keepC = false;
    bool boundsCheck = false;
    std::string inputPath;
    std::string outputExe;

    // Parse flags — anything starting with -- is a flag, others are positional
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--keep-c")
        {
            keepC = true;
        }
        else if (arg == "--bounds")
        {
            boundsCheck = true;
        }
        else if (arg == "--help")
        {
            printUsage();
            return 0;
        }
        else if (arg[0] == '-')
        {
            std::cerr << "Unknown flag: " << arg << "\n";
            printUsage();
            return 1;
        }
        else if (inputPath.empty())
        {
            inputPath = arg;
        }
        else if (outputExe.empty())
        {
            outputExe = arg;
        }
        else
        {
            std::cerr << "Unexpected argument: " << arg << "\n";
            return 1;
        }
    }

    if (inputPath.empty())
    {
        printUsage();
        return 1;
    }
    if (outputExe.empty())
        outputExe = "output";

    std::ifstream file(inputPath);
    if (!file.is_open())
    {
        std::cerr << "Error: cannot open '" << inputPath << "'\n";
        return 1;
    }

    // Source directory for resolving relative includes
    std::string sourceDir = ".";
    size_t slash = inputPath.find_last_of("/\\");
    if (slash != std::string::npos)
        sourceDir = inputPath.substr(0, slash);

    std::stringstream buf;
    buf << file.rdbuf();
    std::string source = buf.str();

    // Lex
    Lexer lexer(source);
    std::vector<Token> tokens;
    try
    {
        tokens = lexer.tokenize();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Lexer error: " << e.what() << "\n";
        return 1;
    }

    // Parse
    Parser parser(tokens, sourceDir);
    std::unique_ptr<Program> ast;
    try
    {
        ast = parser.parse();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 1;
    }

    // Codegen
    CodeGen generator;
    generator.boundsCheck = boundsCheck;
    std::string cCode;
    try
    {
        cCode = generator.generate(ast.get());
    }
    catch (const std::exception &e)
    {
        std::cerr << "Codegen error: " << e.what() << "\n";
        return 1;
    }

    // Write intermediate C
    const std::string cFile = "output.c";
    {
        std::ofstream out(cFile);
        if (!out.is_open())
        {
            std::cerr << "Error: cannot write " << cFile << "\n";
            return 1;
        }
        out << cCode;
    }

    // Compile
    std::string cmd = "gcc " + cFile + " -o " + outputExe + " -lm 2>&1";
    int result = std::system(cmd.c_str());

    // Delete intermediate C unless --keep-c
    if (!keepC)
        std::remove(cFile.c_str());
    else
        std::cout << "Kept intermediate C: " << cFile << "\n";

    if (result != 0)
    {
        std::cerr << "Compilation failed.\n";
        return 1;
    }

    std::cout << "Compiled: ./" << outputExe << "\n";
    return 0;
}