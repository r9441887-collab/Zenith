#pragma once
#include "ast.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>

struct OptResult {
    std::vector<std::string> warnings;
    int removedFunctions = 0;
    int removedGlobals = 0;
};

class Optimizer {
public:
    OptResult optimize(Program& prog);

private:
    void findReachable(const std::string& funcName,
                       std::unordered_set<std::string>& reachable,
                       const Program& prog);

    void collectFuncRefsInExpr(Expr* expr, std::unordered_set<std::string>& refs, const Program& prog);
    void collectFuncRefsInStmt(Stmt* stmt, std::unordered_set<std::string>& refs, const Program& prog);
    void collectFuncRefsInBlock(const Block& block, std::unordered_set<std::string>& refs, const Program& prog);

    void collectGlobalRefsInExpr(Expr* expr, std::unordered_set<std::string>& refs, const Program& prog);
    void collectGlobalRefsInStmt(Stmt* stmt, std::unordered_set<std::string>& refs, const Program& prog);
    void collectGlobalRefsInBlock(const Block& block, std::unordered_set<std::string>& refs, const Program& prog);

    bool isUserFunc(const std::string& name, const Program& prog);
    bool isGlobal(const std::string& name, const Program& prog);
};
