// Goose compiler -- codegen's merging of identical functions (definitions of
// CodeGen members, codegen.h). A function is checked once per
// specialization, and specializations that differ only in their key (a
// slice argument rooted at a global in one and at a local in the other, say)
// often come out as the same C, but for the names of the specializations
// they call and the numbers of their long-distance returns. MergeSpecs keeps
// one function of each group of those, and points every reference to the
// others at it. What is checked, and how, is unchanged; only the C written
// is.
#pragma once

namespace goose {

// Two functions are the same when they are specializations of one function
// (both element-run twins, or neither), their C is the same once every
// reference in it is blanked, and each blank refers to the same group in
// both. Only a function written more than once can merge, so only those are
// read: a program with none pays for a count. The references are the names
// of those functions (their own included), and the numbers of their
// long-distance return targets, which stand for the target's function: the
// one function that catches the number (EmitRfCheck). Any other function's
// name or number stays in the text, where it matches only itself. The groups
// start as "the same blanked text" and split until nothing splits, as
// minimizing an automaton does, so that copies calling themselves or each
// other (a recursive descent parser's) merge as well.
//
// The kept function of a group is its first; in the kept functions and in
// the rest of the program's C, a reference to any function of a group names
// the kept one, and a target's number becomes the number of the kept copy of
// its target. That is sound because merging changes no control flow, and
// because a return to target t never passes a frame of another function of
// t's group, which would now catch t's number. t's group holds only
// specializations of t's function, and a return passes none but t. For
// `return ... from`, the innermost call of t's function on every path the
// return takes is t (CheckReturn, TypeCheck::NeedOnPath, §7.9). A plain
// return in a function value's body exits the specialization that wrote the
// body (FrameOfSpec); another copy of that function between them would have
// to run the body, so be keyed on it, and write a body of its own to key the
// next copy on, without end, which MAXNESTEDSPECS refuses (§7.8).
inline void CodeGen::MergeSpecs() {
    auto n = (int)spectexts.size();
    // Each function (and its element-run twins apart), and how many copies
    // of it were written: the candidates are the copies of one written more
    // than once.
    map<pair<SFunction *, bool>, int> fnids;
    vector<int> fn(n), copies, cands;
    for (int u = 0; u < n; u++) {
        auto &st = spectexts[u];
        assert(!u || st.begin >= spectexts[u - 1].end);
        auto [it, fresh] = fnids.try_emplace({ st.sp->sf, st.er }, (int)copies.size());
        if (fresh) copies.push_back(0);
        copies[fn[u] = it->second]++;
    }
    for (int u = 0; u < n; u++)
        if (copies[fn[u]] > 1) cands.push_back(u);
    if (cands.empty()) return;
    // The candidates' names, and the candidate catching each target number.
    unordered_map<string_view, pair<int, int>> names;
    vector<int> rfunits(fromids.size() + 1, -1);
    for (auto u : cands) {
        auto &st = spectexts[u];
        for (size_t k = 0; k < st.names.size(); k++) names[st.names[k]] = { u, (int)k };
        if (auto it = fromids.find(st.sp); it != fromids.end() && !st.er) rfunits[it->second] = u;
    }
    auto catcher = [&](FnSpec *t) {
        auto it = fromids.find(t);
        return it == fromids.end() ? -1 : rfunits[it->second];
    };
    auto isid = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    auto digits = [](string_view s, size_t i) {
        auto j = i;
        while (j < s.size() && isdigit((unsigned char)s[j])) j++;
        return j - i;
    };
    // The references in C text s, in order: f(at, len, unit, k, num) for
    // s[at, at + len), which is either the k-th name of candidate `unit` or
    // (num) the number of the candidate target `unit`. A function's names
    // are taken with Unique, which no local, type or global takes, and a
    // struct member's name follows `.` or `->`. A number is the K of a
    // channel gs_lret_K_J, gs_fdst_K_J or gs_fval_K_J (FromChannels), or the
    // operand gs_rf is compared with or set to (EmitRfCheck,
    // PropagateReturn). Literals and comments hold none.
    auto scan = [&](string_view s, auto f) {
        auto number = [&](size_t at, size_t len) {
            size_t id = 0;
            for (auto c : s.substr(at, len)) id = id * 10 + (size_t)(c - '0');
            if (!id) return;   // gs_rf = 0: nothing in flight.
            if (id >= rfunits.size()) Fail(Line {}, cat("internal: unknown target number ", id));
            if (rfunits[id] >= 0) f(at, len, rfunits[id], 0, true);
        };
        for (size_t i = 0; i < s.size();) {
            auto c = s[i];
            if (c == '"' || c == '\'') {
                for (i++; i < s.size() && s[i] != c; i++)
                    if (s[i] == '\\') i++;
                i++;
                continue;
            }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                auto e = s.find("*/", i + 2);
                i = e == string_view::npos ? s.size() : e + 2;
                continue;
            }
            if (isdigit((unsigned char)c)) {
                while (i < s.size() && (isid(s[i]) || s[i] == '.')) i++;
                continue;
            }
            if (!isid(c)) {
                i++;
                continue;
            }
            auto b = i;
            while (i < s.size() && isid(s[i])) i++;
            auto id = s.substr(b, i - b);
            if (b && (s[b - 1] == '.' || (s[b - 1] == '>' && b > 1 && s[b - 2] == '-'))) continue;
            if (auto it = names.find(id); it != names.end()) {
                f(b, id.size(), it->second.first, it->second.second, false);
            } else if (id.starts_with("gs_lret_") || id.starts_with("gs_fdst_") ||
                       id.starts_with("gs_fval_")) {
                auto k = digits(id, 8);
                if (k && 9 + k < id.size() && id[8 + k] == '_' && digits(id, 9 + k) == id.size() - 9 - k)
                    number(b + 8, k);
            } else if (id == "gs_rf") {
                auto j = i;
                while (j < s.size() && s[j] == ' ') j++;
                auto op = j;
                while (j < s.size() && (s[j] == '=' || s[j] == '!' || s[j] == '<' || s[j] == '>')) j++;
                if (j == op) continue;
                while (j < s.size() && s[j] == ' ') j++;
                if (auto k = digits(s, j)) {
                    number(j, k);
                    i = j + k;
                }
            }
        }
    };
    // Each candidate's text with its references blanked, and what they refer
    // to, in order. The text leads with the candidate's function, so that a
    // group holds copies of one, and a target only ever stands with
    // targets, whose numbers its blanks may become.
    auto text = [&](int u) {
        return string_view(code).substr(spectexts[u].begin, spectexts[u].end - spectexts[u].begin);
    };
    vector<string> blanked(n);
    vector<vector<int>> refs(n);
    for (auto u : cands) {
        auto s = text(u);
        auto &bl = blanked[u];
        bl = cat(fn[u], !spectexts[u].er && fromids.count(spectexts[u].sp) ? "T" : "F");
        size_t p = 0;
        scan(s, [&](size_t at, size_t len, int v, int k, bool num) {
            bl.append(s.substr(p, at - p));
            bl += num ? '\x02' : '\x01';
            bl += (char)('0' + k);
            refs[u].push_back(v);
            p = at + len;
        });
        bl.append(s.substr(p));
    }
    // A function that is no candidate is in no group, and kept.
    vector<int> group(n, -1);
    int ngroups = 0;
    {
        unordered_map<string_view, int> first;
        for (auto u : cands) {
            auto [it, fresh] = first.try_emplace(blanked[u], ngroups);
            if (fresh) ngroups++;
            group[u] = it->second;
        }
    }
    for (;;) {
        map<vector<int>, int> ids;
        vector<int> next(n, -1);
        for (auto u : cands) {
            vector<int> key { group[u] };
            for (auto r : refs[u]) key.push_back(group[r]);
            auto id = (int)ids.size();
            next[u] = ids.try_emplace(std::move(key), id).first->second;
        }
        if ((int)ids.size() == ngroups) break;
        group = std::move(next);
        ngroups = (int)ids.size();
    }
    // A frame a return to t passes compares gs_rf with its own number where
    // it catches returns too, and would catch t's once t's number is its
    // own; the checker keeps every such frame out of t's group (see above).
    for (auto u : cands) {
        for (auto t : spectexts[u].sp->needs) {
            auto v = catcher(t);
            if (v < 0 || v == u || group[v] != group[u]) continue;
            Fail(spectexts[u].sp->sf->line,
                 cat("internal: ", spectexts[u].names[0], " lies on the way of a long-distance ",
                     "return to ", spectexts[v].names[0], ", which it would merge with"));
        }
    }
    // The text is grouped; what is written is rewritten from `code`.
    vector<string>().swap(blanked);
    vector<vector<int>>().swap(refs);
    if (ngroups == (int)cands.size()) return;
    vector<int> kept(ngroups, -1);
    for (auto u : cands)
        if (kept[group[u]] < 0) kept[group[u]] = u;
    auto keep = [&](int u) { return group[u] < 0 || kept[group[u]] == u; };
    // A merged target's channels are the kept one's: the same C, as the
    // functions writing and reading them are.
    for (auto u : cands) {
        auto &st = spectexts[u], &k = spectexts[kept[group[u]]];
        if (keep(u) || st.er || !fromids.count(st.sp)) continue;
        auto chans = FromChannels(st.sp, "K");
        if (chans != FromChannels(k.sp, "K"))
            Fail(st.sp->sf->line, cat("internal: ", st.names[0], " merged with ", k.names[0],
                                      ", whose long-distance return channels differ"));
    }
    auto rewrite = [&](string_view s, string &out) {
        size_t p = 0;
        scan(s, [&](size_t at, size_t len, int v, int k, bool num) {
            out.append(s.substr(p, at - p));
            auto &kv = spectexts[kept[group[v]]];
            auto id = fromids.find(kv.sp);
            // A target stands only with targets, and a name with functions
            // that define the same names.
            if (num ? id == fromids.end() : (size_t)k >= kv.names.size())
                Fail(kv.sp->sf->line, cat("internal: ", kv.names[0], " stands for ",
                                          spectexts[v].names[0], " but lacks its reference"));
            if (num) Append(out, id->second);
            else out += kv.names[(size_t)k];
            p = at + len;
        });
        out.append(s.substr(p));
    };
    string merged;
    merged.reserve(code.size());
    size_t p = 0;
    for (int u = 0; u < n; u++) {
        rewrite(string_view(code).substr(p, spectexts[u].begin - p), merged);
        if (keep(u)) rewrite(text(u), merged);
        p = spectexts[u].end;
    }
    rewrite(string_view(code).substr(p), merged);
    code = std::move(merged);
    // A prototype names the function it declares, and a target's channels
    // its number, and nothing else does: a line naming a function that is
    // not kept goes.
    auto droplines = [&](string &s) {
        string out;
        for (size_t b = 0; b < s.size();) {
            auto e = s.find('\n', b);
            e = e == string::npos ? s.size() : e + 1;
            auto line = string_view(s).substr(b, e - b);
            auto drop = false;
            scan(line, [&](size_t, size_t, int v, int, bool) { drop = drop || !keep(v); });
            if (!drop) out.append(line);
            b = e;
        }
        s = std::move(out);
    };
    droplines(protos);
    droplines(data);
}

}  // namespace goose
