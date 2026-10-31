// Goose compiler — contracts (docs/design/proofs.md, phase 1). The parser
// keeps a function's `requires` and `ensures` clauses and a loop's
// `invariant` clauses beside the code they describe, so that the dump shows
// them as written; LowerContracts, which runs once before typecheck, turns
// each into a check in the body. A check is a call of the B_CONTRACT builtin,
// which codegen emits under `#if GS_CONTRACTS` (on in debug builds, or with
// --check-contracts): every contract is executable, and nothing is proved
// yet, so a release build pays for none of them and the analyses take no
// facts from them.
//
//   requires p      at the top of the body, before anything else runs;
//   ensures q       wherever the function returns: a `return e` returns
//                   `{ let result = e; check(q); result }`, a `return;` runs
//                   the checks first, and so does the end of the body, or
//                   its tail as `result`; `old(e)` is bound on entry;
//   invariant i     at the top of each iteration of its loop.
//
// A `return ... from` that crosses a function does not reach its ensures
// (§3.4), so it is not checked there.
#pragma once

namespace goose {

// The call that checks cond, aborting with "<what> failed: <text>".
inline Node *ContractCheck(Ast &ast, Node *cond, string_view what, const string &text) {
    auto l = cond->line;
    // The builtin's name has a space in it, so no program can call it.
    auto c = ast.New<Call>(l, ast.New<Ident>(l, "contract check"));
    c->args.push_back(cond);
    c->args.push_back(ast.New<StrLit>(l, cat(what, " failed: ", text)));
    return c;
}

struct ContractLowering {
    Ast &ast;
    explicit ContractLowering(Ast &_ast) : ast(_ast) {}

    void Error(Line l, string msg) {
        Diagnostic d;
        d.where = LocOf(l);
        d.message = std::move(msg);
        ast.Report(std::move(d));
    }

    // Every node below n in its body, nested functions excluded (their
    // declarations have no children) and function values included, since a
    // plain return in one leaves the enclosing function (§7.6).
    static void Walk(Node *n, const function<void(Node *)> &f) {
        if (!n) return;
        f(n);
        n->Children([&](Node *c) { Walk(c, f); });
    }

    // A loop's invariants become checks at the top of its body.
    template<typename L> void LowerLoop(L *loop) {
        if (loop->invariants.empty()) return;
        vector<Node *> checks;
        for (size_t i = 0; i < loop->invariants.size(); i++)
            checks.push_back(ContractCheck(ast, loop->invariants[i], "invariant",
                                           loop->invarianttexts[i]));
        auto &stmts = loop->body->stmts;
        stmts.insert(stmts.begin(), checks.begin(), checks.end());
        if (loop->body->brokenat >= 0) loop->body->brokenat += (int)checks.size();
        loop->invariants.clear();
        loop->invarianttexts.clear();
    }

    void LowerLoops(Block *body) {
        Walk(body, [&](Node *n) {
            if (auto w = Is<While>(n)) LowerLoop(w);
            else if (auto f = Is<ForLoop>(n)) LowerLoop(f);
        });
    }

    void LowerFunction(SFunction *sf) {
        auto body = sf->body;
        if (sf->requires_.empty() && sf->ensures.empty()) return;
        if (!sf->ensures.empty() && sf->rets.size() > 1) {
            Error(sf->line, cat("ensures on function ", sf->name, ", which has several results, "
                                "is not supported yet (docs/design/proofs.md §14)"));
            return;
        }
        if (!sf->ensures.empty() && !sf->has_rets && body->tail) {
            Error(body->tail->line,
                  cat("function ", sf->name, " has ensures and ends in a value: write its result "
                      "type (-> T), or end the statement with ';'"));
            return;
        }
        // `result` and `old(e)` become names a program cannot write, bound
        // where the checks run.
        auto resultname = ast.Intern("result#");
        vector<Node *> prelude;
        int olds = 0;
        for (auto q : sf->ensures) {
            Walk(q, [&](Node *n) {
                auto id = Is<Ident>(n);
                if (!id) return;
                if (id->oldof) {
                    id->name = ast.Intern(cat("old#", olds++));
                    auto vd = ast.New<VarDecl>(id->line, false);
                    vd->names.push_back(id->name);
                    vd->inits.push_back(id->oldof->Clone(ast));
                    prelude.push_back(vd);
                    id->oldof = nullptr;
                } else if (id->name == "result") {
                    id->name = resultname;
                }
            });
        }
        vector<Node *> pre;
        for (size_t i = 0; i < sf->requires_.size(); i++)
            pre.push_back(ContractCheck(ast, sf->requires_[i], "requires", sf->requirestexts[i]));
        pre.insert(pre.end(), prelude.begin(), prelude.end());
        // The ensures checks, with `result` naming the variable `named`.
        auto checks = [&](Ident *named) {
            vector<Node *> cs;
            for (size_t i = 0; i < sf->ensures.size(); i++) {
                auto q = sf->ensures[i]->Clone(ast);
                if (named)
                    Walk(q, [&](Node *n) {
                        if (auto id = Is<Ident>(n); id && id->name == resultname) {
                            id->name = named->name;
                            id->ns = named->ns;
                        }
                    });
                cs.push_back(ContractCheck(ast, q, "ensures", sf->ensurestexts[i]));
            }
            return cs;
        };
        // The statements that check a result v before it is returned, and
        // what then returns it. A variable is returned as itself, in place
        // (§7.3), and anything else is first bound to `result`, where it is
        // built. Either way nothing is copied, as a nonfixed result needs.
        auto checking = [&](Node *v, vector<Node *> &stmts) -> Node * {
            if (auto id = Is<Ident>(v); id && !id->oldof) {
                for (auto c : checks(id)) stmts.push_back(c);
                return v;
            }
            auto vd = ast.New<VarDecl>(v->line, false);
            vd->names.push_back(resultname);
            vd->inits.push_back(v);
            stmts.push_back(vd);
            for (auto c : checks(nullptr)) stmts.push_back(c);
            return ast.New<Ident>(v->line, resultname);
        };
        if (!sf->ensures.empty()) {
            vector<Return *> returns;
            Walk(body, [&](Node *n) {
                if (auto r = Is<Return>(n); r && r->from.empty()) returns.push_back(r);
            });
            // A return that is a statement or a tail of a block becomes a
            // block that checks, then returns.
            Walk(body, [&](Node *n) {
                auto b = Is<Block>(n);
                if (!b) return;
                auto swap = [&](Node *&slot) {
                    auto r = Is<Return>(slot);
                    auto it = r ? find(returns.begin(), returns.end(), r) : returns.end();
                    if (it == returns.end() || r->vals.size() > 1) return;
                    returns.erase(it);
                    auto g = ast.New<Block>(r->line);
                    if (r->vals.empty()) g->stmts = checks(nullptr);
                    else r->vals[0] = checking(r->vals[0], g->stmts);
                    g->tail = r;
                    slot = g;
                };
                for (auto &s : b->stmts) swap(s);
                if (b->tail) swap(b->tail);
            });
            // One in the middle of an expression (`x ?? return 0`) returns
            // `{ let result = v; checks; result }`, which a fixed-size value can.
            for (auto r : returns) {
                if (r->vals.size() != 1) {
                    Error(r->line, cat("this return in function ", sf->name, ", which has "
                                       "ensures, is not in a block: write it as { return; }"));
                    continue;
                }
                auto g = ast.New<Block>(r->line);
                g->tail = checking(r->vals[0], g->stmts);
                r->vals[0] = g;
            }
            // The end of the body returns too.
            if (body->tail) body->tail = checking(body->tail, body->stmts);
            else if (sf->rets.empty())
                for (auto c : checks(nullptr)) body->stmts.push_back(c);
        }
        body->stmts.insert(body->stmts.begin(), pre.begin(), pre.end());
        if (body->brokenat >= 0) body->brokenat += (int)pre.size();
    }

    void Run() {
        for (auto sf : ast.functions) {
            if (!sf->body || sf->brokensig) continue;
            LowerFunction(sf);
            LowerLoops(sf->body);
            sf->requires_.clear();
            sf->ensures.clear();
        }
    }
};

inline void LowerContracts(Ast &ast) { ContractLowering(ast).Run(); }

}  // namespace goose
