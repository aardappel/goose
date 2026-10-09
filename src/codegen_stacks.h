// Goose compiler -- codegen's data stack accounting (definitions of CodeGen
// members, codegen.h): the static count of data stacks the main program and
// each worker can have in use at once, which the generated program hands the
// runtime to reserve as each thread program starts and for the cap on
// hardware_threads() (gs_rt_init, goose_spec.md 11.2), and the --stacks
// report of it.
#pragma once

namespace goose {

// An emitted function's stacks are gs_sp + k for constants k below its own
// count, and a callee's index starts above what the caller has in use at the
// call (SpTop), so a function's count is the larger of its own and, over its
// calls, the index handed on plus the callee's count. A call into a
// recursive cycle is made with nothing in use (§7.8, NoStackAcrossCycleCall),
// so the members of a cycle share one count, the largest of their own and of
// what they hand to callees outside it, and the count is a constant.

// Tarjan's components, callees' before callers', so a component's count can
// take its external callees' as final.
inline void CodeGen::BoundStacks() {
    unordered_map<FnSpec *, int> index, low;
    vector<FnSpec *> stack;
    unordered_set<FnSpec *> onstack;
    int next = 0;
    function<void(FnSpec *)> visit = [&](FnSpec *v) {
        index[v] = low[v] = next++;
        stack.push_back(v);
        onstack.insert(v);
        if (auto it = stackcalls.find(v); it != stackcalls.end()) {
            for (auto &c : it->second) {
                auto w = c.callee;
                if (!index.count(w)) {
                    visit(w);
                    low[v] = std::min(low[v], low[w]);
                } else if (onstack.count(w)) {
                    low[v] = std::min(low[v], index[w]);
                }
            }
        }
        if (low[v] != index[v]) return;
        vector<FnSpec *> scc;
        for (;;) {
            auto w = stack.back();
            stack.pop_back();
            onstack.erase(w);
            scc.push_back(w);
            if (w == v) break;
        }
        unordered_set<FnSpec *> members(scc.begin(), scc.end());
        int64_t need = 0;
        for (auto m : scc) {
            need = std::max(need, (int64_t)stackown[m]);
            auto it = stackcalls.find(m);
            if (it == stackcalls.end()) continue;
            for (auto &c : it->second) {
                if (members.count(c.callee)) {
                    if (c.offset)
                        Fail(c.line, cat("internal: ", m->sf->name, " holds ", c.offset,
                                         " data stack(s) across its call into the recursive "
                                         "cycle through ", c.callee->sf->name, " (§7.8)"));
                    continue;
                }
                need = std::max(need, c.offset + stackneed[c.callee]);
            }
        }
        for (auto m : scc) stackneed[m] = need;
    };
    for (auto sp : livespecs)
        if (!index.count(sp)) visit(sp);
    if (!index.count(nullptr)) visit(nullptr);
}

// A program's count: over its entry points, and what runs with gs_sp at 0
// (the nullptr function: the global initializers and render functions).
inline int64_t CodeGen::ProgramStacks(const vector<FnSpec *> &roots) {
    auto n = stackneed[nullptr];
    for (auto r : roots) n = std::max(n, stackneed[r]);
    return n;
}

// Where the main program starts: fn main(), and a library's exports.
inline vector<FnSpec *> CodeGen::MainRoots() {
    vector<FnSpec *> roots;
    auto mainsf = ast.MainFunction();
    if (mainsf && !mainsf->specs.empty() && sinfo.count(mainsf->specs[0]))
        roots.push_back(mainsf->specs[0]);
    for (auto sf : ast.functions)
        if (sf->isexport && sf->specs.size() == 1 && sinfo.count(sf->specs[0]))
            roots.push_back(sf->specs[0]);
    return roots;
}

// The thread programs, by their entry's name: the order the generated C
// and the report take them in.
inline vector<pair<string, FnSpec *>> CodeGen::WorkerEntries() {
    vector<pair<string, FnSpec *>> workers;
    for (auto &[sp, name] : thunks) workers.push_back({ string(sp->sf->qname), sp });
    sort(workers.begin(), workers.end());
    return workers;
}

// GS_MAX_STACKS, as the program is configured, is a compile-time limit on a
// thread program's count: the runtime reserves whatever it is told.
inline void CodeGen::CheckStackLimit(const string &what, int64_t stacks, Line ln) {
    if (maxstacks > 0 && stacks > maxstacks)
        Fail(ln, cat(what, " needs ", stacks, " data stacks at once, more than GS_MAX_STACKS (",
                     maxstacks, ") allows; `goose --stacks` shows each function's share"));
}

// A size in the largest unit that holds it exactly.
inline string FmtSize(uint64_t bytes) {
    static const char *const units[] = { "bytes", "KB", "MB", "GB", "TB" };
    int u = 0;
    while (u < 4 && bytes && bytes % 1024 == 0) {
        bytes /= 1024;
        u++;
    }
    return cat(bytes, " ", units[u]);
}

inline void CodeGen::StackReport(FILE *out, const StackConfig &cfg) {
    auto per = cfg.reserve + cfg.gap;
    uint64_t total = per ? cfg.budget / per : 0;
    fprintf(out, "data stacks: %s reserved per stack, %s guard gap, %s budget: %llu regions, "
                 "GS_MAX_STACKS %lld\n",
            FmtSize(cfg.reserve).c_str(), FmtSize(cfg.gap).c_str(), FmtSize(cfg.budget).c_str(),
            (unsigned long long)total, (long long)cfg.maxstacks);
    // One program's line; what it comes to is what gs_rt_init is told.
    auto describe = [&](const string &what, int64_t stacks, int regions, const char *extra) {
        fprintf(out, "%s: %lld data stacks + %d %s = %lld regions\n", what.c_str(),
                (long long)stacks, regions, extra, (long long)(stacks + regions));
        return stacks + regions;
    };
    auto mainregions = describe("main program", ProgramStacks(MainRoots()), globalregions,
                                "global stacks");
    auto workers = WorkerEntries();
    int64_t workerregions = 0;
    for (auto &[name, sp] : workers)
        workerregions = std::max(workerregions,
                                 describe(cat("worker `", name, "`"), ProgramStacks({ sp }),
                                          thunkregions[sp], "argument and global stacks"));
    if (workers.empty()) {
        fprintf(out, "thread cap: none, no workers\n");
    } else {
        // As gs_rt_start computes it.
        auto spare = (int64_t)std::min<uint64_t>(total, INT64_MAX) - mainregions;
        auto cap = spare > workerregions ? spare / workerregions : 1;
        fprintf(out, "thread cap: %lld workers ((%llu - %lld) / %lld), what hardware_threads() "
                     "reports at most\n",
                (long long)cap, (unsigned long long)total, (long long)mainregions,
                (long long)workerregions);
    }
    // Every function with stacks, the heaviest first.
    vector<pair<string, FnSpec *>> fns;
    for (auto sp : livespecs)
        if (stackneed[sp]) fns.push_back({ string(sp->sf->qname), sp });
    sort(fns.begin(), fns.end(), [&](auto &a, auto &b) {
        auto na = stackneed[a.second], nb = stackneed[b.second];
        if (na != nb) return na > nb;
        return a.first < b.first;
    });
    for (auto &[name, sp] : fns)
        fprintf(out, "fn %s: %d own, %lld with callees\n", name.c_str(), stackown[sp],
                (long long)stackneed[sp]);
    if (stackneed[nullptr])
        fprintf(out, "global initializers: %d own, %lld with callees\n", stackown[nullptr],
                (long long)stackneed[nullptr]);
}

}  // namespace goose
