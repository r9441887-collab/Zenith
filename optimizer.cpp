#include "optimizer.h"
#include <algorithm>

bool Optimizer::isUserFunc(const std::string& name, const Program& prog) {
    for (auto& f : prog.functions) {
        if (f->name == name && !f->isExtern) return true;
    }
    return false;
}

bool Optimizer::isGlobal(const std::string& name, const Program& prog) {
    for (auto& g : prog.globals) {
        if (g->name == name) return true;
    }
    return false;
}

void Optimizer::collectFuncRefsInExpr(Expr* expr, std::unordered_set<std::string>& refs, const Program& prog) {
    if (!expr) return;
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        refs.insert(call->name);
        collectFuncRefsInExpr(call->receiver.get(), refs, prog);
        for (auto& arg : call->args) {
            collectFuncRefsInExpr(arg.get(), refs, prog);
        }
    } else if (auto id = dynamic_cast<IdentExpr*>(expr)) {
        if (isUserFunc(id->name, prog)) {
            refs.insert(id->name);
        }
    } else if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
        collectFuncRefsInExpr(bin->left.get(), refs, prog);
        collectFuncRefsInExpr(bin->right.get(), refs, prog);
    } else if (auto unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectFuncRefsInExpr(unary->operand.get(), refs, prog);
    } else if (auto memb = dynamic_cast<MemberExpr*>(expr)) {
        collectFuncRefsInExpr(memb->object.get(), refs, prog);
    } else if (auto arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
        collectFuncRefsInExpr(arr->array.get(), refs, prog);
        collectFuncRefsInExpr(arr->index.get(), refs, prog);
    } else if (auto deref = dynamic_cast<DerefExpr*>(expr)) {
        collectFuncRefsInExpr(deref->ptr.get(), refs, prog);
    }
}

void Optimizer::collectFuncRefsInStmt(Stmt* stmt, std::unordered_set<std::string>& refs, const Program& prog) {
    if (!stmt) return;
    if (auto varDecl = dynamic_cast<VarDecl*>(stmt)) {
        collectFuncRefsInExpr(varDecl->init.get(), refs, prog);
    } else if (auto ret = dynamic_cast<ReturnStmt*>(stmt)) {
        collectFuncRefsInExpr(ret->value.get(), refs, prog);
    } else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectFuncRefsInExpr(exprStmt->expr.get(), refs, prog);
    } else if (auto assign = dynamic_cast<AssignStmt*>(stmt)) {
        collectFuncRefsInExpr(assign->value.get(), refs, prog);
        collectFuncRefsInExpr(assign->indexExpr.get(), refs, prog);
        if (isUserFunc(assign->name, prog)) {
            refs.insert(assign->name);
        }
    } else if (auto ptrAssign = dynamic_cast<PtrAssignStmt*>(stmt)) {
        collectFuncRefsInExpr(ptrAssign->ptr.get(), refs, prog);
        collectFuncRefsInExpr(ptrAssign->value.get(), refs, prog);
    } else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectFuncRefsInExpr(ifStmt->condition.get(), refs, prog);
        collectFuncRefsInBlock(ifStmt->thenBlock, refs, prog);
        collectFuncRefsInBlock(ifStmt->elseBlock, refs, prog);
    } else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectFuncRefsInExpr(whileStmt->condition.get(), refs, prog);
        collectFuncRefsInBlock(whileStmt->body, refs, prog);
    } else if (auto loopStmt = dynamic_cast<LoopStmt*>(stmt)) {
        collectFuncRefsInBlock(loopStmt->body, refs, prog);
    } else if (auto switchStmt = dynamic_cast<SwitchStmt*>(stmt)) {
        collectFuncRefsInExpr(switchStmt->condition.get(), refs, prog);
        for (auto& sc : switchStmt->cases) {
            collectFuncRefsInExpr(sc.condition.get(), refs, prog);
            collectFuncRefsInBlock(sc.body, refs, prog);
        }
    } else if (auto forStmt = dynamic_cast<ForStmt*>(stmt)) {
        collectFuncRefsInExpr(forStmt->start.get(), refs, prog);
        collectFuncRefsInExpr(forStmt->end.get(), refs, prog);
        collectFuncRefsInExpr(forStmt->step.get(), refs, prog);
        collectFuncRefsInBlock(forStmt->body, refs, prog);
    } else if (dynamic_cast<AsmStmt*>(stmt)) {
        // AsmStmt may reference any function/global — conservatively mark nothing
    }
}

void Optimizer::collectFuncRefsInBlock(const Block& block, std::unordered_set<std::string>& refs, const Program& prog) {
    for (auto& stmt : block.stmts) {
        collectFuncRefsInStmt(stmt.get(), refs, prog);
    }
}

void Optimizer::collectGlobalRefsInExpr(Expr* expr, std::unordered_set<std::string>& refs, const Program& prog) {
    if (!expr) return;
    if (auto id = dynamic_cast<IdentExpr*>(expr)) {
        if (isGlobal(id->name, prog)) {
            refs.insert(id->name);
        }
    } else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        collectGlobalRefsInExpr(call->receiver.get(), refs, prog);
        for (auto& arg : call->args) {
            collectGlobalRefsInExpr(arg.get(), refs, prog);
        }
    } else if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
        collectGlobalRefsInExpr(bin->left.get(), refs, prog);
        collectGlobalRefsInExpr(bin->right.get(), refs, prog);
    } else if (auto unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectGlobalRefsInExpr(unary->operand.get(), refs, prog);
    } else if (auto memb = dynamic_cast<MemberExpr*>(expr)) {
        collectGlobalRefsInExpr(memb->object.get(), refs, prog);
    } else if (auto arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
        collectGlobalRefsInExpr(arr->array.get(), refs, prog);
        collectGlobalRefsInExpr(arr->index.get(), refs, prog);
    } else if (auto deref = dynamic_cast<DerefExpr*>(expr)) {
        collectGlobalRefsInExpr(deref->ptr.get(), refs, prog);
    }
}

void Optimizer::collectGlobalRefsInStmt(Stmt* stmt, std::unordered_set<std::string>& refs, const Program& prog) {
    if (!stmt) return;
    if (auto varDecl = dynamic_cast<VarDecl*>(stmt)) {
        collectGlobalRefsInExpr(varDecl->init.get(), refs, prog);
    } else if (auto ret = dynamic_cast<ReturnStmt*>(stmt)) {
        collectGlobalRefsInExpr(ret->value.get(), refs, prog);
    } else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectGlobalRefsInExpr(exprStmt->expr.get(), refs, prog);
    } else if (auto assign = dynamic_cast<AssignStmt*>(stmt)) {
        collectGlobalRefsInExpr(assign->value.get(), refs, prog);
        collectGlobalRefsInExpr(assign->indexExpr.get(), refs, prog);
        if (isGlobal(assign->name, prog)) {
            refs.insert(assign->name);
        }
    } else if (auto ptrAssign = dynamic_cast<PtrAssignStmt*>(stmt)) {
        collectGlobalRefsInExpr(ptrAssign->ptr.get(), refs, prog);
        collectGlobalRefsInExpr(ptrAssign->value.get(), refs, prog);
    } else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectGlobalRefsInExpr(ifStmt->condition.get(), refs, prog);
        collectGlobalRefsInBlock(ifStmt->thenBlock, refs, prog);
        collectGlobalRefsInBlock(ifStmt->elseBlock, refs, prog);
    } else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectGlobalRefsInExpr(whileStmt->condition.get(), refs, prog);
        collectGlobalRefsInBlock(whileStmt->body, refs, prog);
    } else if (auto loopStmt = dynamic_cast<LoopStmt*>(stmt)) {
        collectGlobalRefsInBlock(loopStmt->body, refs, prog);
    } else if (auto switchStmt = dynamic_cast<SwitchStmt*>(stmt)) {
        collectGlobalRefsInExpr(switchStmt->condition.get(), refs, prog);
        for (auto& sc : switchStmt->cases) {
            collectGlobalRefsInExpr(sc.condition.get(), refs, prog);
            collectGlobalRefsInBlock(sc.body, refs, prog);
        }
    } else if (auto forStmt = dynamic_cast<ForStmt*>(stmt)) {
        collectGlobalRefsInExpr(forStmt->start.get(), refs, prog);
        collectGlobalRefsInExpr(forStmt->end.get(), refs, prog);
        collectGlobalRefsInExpr(forStmt->step.get(), refs, prog);
        collectGlobalRefsInBlock(forStmt->body, refs, prog);
    } else if (dynamic_cast<AsmStmt*>(stmt)) {
        // AsmStmt may reference any function/global — conservatively mark nothing
    }
}

void Optimizer::collectGlobalRefsInBlock(const Block& block, std::unordered_set<std::string>& refs, const Program& prog) {
    for (auto& stmt : block.stmts) {
        collectGlobalRefsInStmt(stmt.get(), refs, prog);
    }
}

void Optimizer::findReachable(const std::string& funcName,
                               std::unordered_set<std::string>& reachable,
                               const Program& prog) {
    if (reachable.count(funcName)) return;
    if (!isUserFunc(funcName, prog)) return;
    reachable.insert(funcName);

    for (auto& func : prog.functions) {
        if (func->name == funcName && !func->isExtern) {
            std::unordered_set<std::string> refs;
            collectFuncRefsInBlock(func->body, refs, prog);
            for (auto& callee : refs) {
                findReachable(callee, reachable, prog);
            }
            break;
        }
    }
}

OptResult Optimizer::optimize(Program& prog) {
    OptResult result;

    if (prog.isLibrary) return result;

    std::string entryFunc;
    for (auto& f : prog.functions) {
        if (f->name == "main" && !f->isExtern) {
            entryFunc = "main";
            break;
        }
    }
    if (entryFunc.empty()) {
        for (auto& f : prog.functions) {
            if (!f->isExtern) {
                entryFunc = f->name;
                break;
            }
        }
    }
    if (entryFunc.empty()) return result;

    std::unordered_set<std::string> reachable;
    findReachable(entryFunc, reachable, prog);

    // Functions referenced from global initializers must be kept as well
    // (e.g. `var x: int = compute()`) — otherwise the init code would call
    // into a function that got stripped.
    for (auto& g : prog.globals) {
        std::unordered_set<std::string> refs;
        collectFuncRefsInExpr(g->init.get(), refs, prog);
        for (auto& r : refs) {
            findReachable(r, reachable, prog);
        }
    }

    std::unordered_set<std::string> usedExterns;
    for (auto& func : prog.functions) {
        if (!func->isExtern && reachable.count(func->name)) {
            std::unordered_set<std::string> refs;
            collectFuncRefsInBlock(func->body, refs, prog);
            for (auto& name : refs) {
                if (!isUserFunc(name, prog)) {
                    usedExterns.insert(name);
                }
            }
        }
    }

    auto fit = std::remove_if(prog.functions.begin(), prog.functions.end(),
        [&](const std::unique_ptr<FunctionDecl>& func) {
            if (func->isExtern) {
                if (!usedExterns.count(func->name)) {
                    result.warnings.push_back("Warning: unused extern function '" + func->name + "'");
                    result.removedFunctions++;
                    return true;
                }
                return false;
            }
            if (!reachable.count(func->name)) {
                result.warnings.push_back("Warning: unused function '" + func->name + "'");
                result.removedFunctions++;
                return true;
            }
            return false;
        });
    prog.functions.erase(fit, prog.functions.end());

    std::unordered_set<std::string> usedGlobals;
    for (auto& func : prog.functions) {
        if (!func->isExtern && reachable.count(func->name)) {
            collectGlobalRefsInBlock(func->body, usedGlobals, prog);
        }
    }

    // A global referenced from another global's initializer must be kept too
    // (e.g. `var b: int = a` — `a` is only reachable through `b`'s init).
    bool globalChanged;
    do {
        globalChanged = false;
        for (auto& g : prog.globals) {
            if (usedGlobals.count(g->name)) {
                std::unordered_set<std::string> refs;
                collectGlobalRefsInExpr(g->init.get(), refs, prog);
                for (auto& r : refs) {
                    if (!usedGlobals.count(r)) {
                        usedGlobals.insert(r);
                        globalChanged = true;
                    }
                }
            }
        }
    } while (globalChanged);

    auto git = std::remove_if(prog.globals.begin(), prog.globals.end(),
        [&](const std::unique_ptr<VarDecl>& var) {
            if (!usedGlobals.count(var->name)) {
                result.warnings.push_back("Warning: unused global variable '" + var->name + "'");
                result.removedGlobals++;
                return true;
            }
            return false;
        });
    prog.globals.erase(git, prog.globals.end());

    return result;
}
