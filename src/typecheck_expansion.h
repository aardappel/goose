// Specialization-time structural expansion. The generated trees use the
// ordinary checker, optimizer and code generator; no runtime iterator exists.
#pragma once

namespace goose {

// A body's trailing construct is a statement if any of its branches cannot
// supply a value. Arm fragments count only when their source is nonempty.
// This reads source shapes without checking or expanding expressions: a
// nested block's locals are not in scope yet, and its tail may be unreachable.
inline bool TypeCheck::IsValuelessTail(const Node *n) {
    size_t visited = 0;
    vector<pair<int, TypeExpr *>> bindings;
    function<TypeExpr *(TypeExpr *)> shape = [&](TypeExpr *t) -> TypeExpr * {
        set<TypeExpr *> seen;
        while (t && t->kind == TY_GENERIC) {
            if (!seen.insert(t).second) return nullptr;
            auto id = t->named->expansionbinding;
            if (!id) { t = LookupBindingOuter(t->named->name); continue; }
            auto found = find_if(bindings.rbegin(), bindings.rend(),
                                 [&](auto &b) { return b.first == id; });
            t = found == bindings.rend() ? nullptr : found->second;
        }
        if (t && t->kind == TY_VARIANT && !t->var->variant) {
            auto owner = shape(t->var->adt);
            auto v = owner && owner->kind == TY_ENUM ? owner->enu->en->FindVariant(t->var->name) : nullptr;
            return v ? ast.VariantTypeOf(owner, v, t->line) : nullptr;
        }
        return t;
    };
    function<bool(const Node *)> walk;
    auto arms = [&](const vector<MatchArm> &as) {
        return any_of(as.begin(), as.end(), [&](const MatchArm &a) {
            return walk(a.expansion ? a.expansion : a.body);
        });
    };
    walk = [&](const Node *x) {
        if (!x) return false;
        // Normal expansion diagnoses an oversized reachable source. The
        // shape walk must stay bounded even for a tail never reached.
        if (++visited > ExpansionClone::MAXNODES) return false;
        if (auto fi = Is<IfExpr>(x))
            return !fi->elseb || walk(fi->thenb) || walk(fi->elseb);
        if (auto m = Is<MatchExpr>(x)) return arms(m->arms);
        if (auto cf = Is<ConstFor>(x)) {
            auto entry = [&](TypeExpr *t = nullptr) {
                if (t) bindings.push_back({ cf->bindings.back(), t });
                auto result = arms(cf->arms);
                if (t) bindings.pop_back();
                return result;
            };
            if (cf->source == ES_CONSTANTS) return !cf->constants.empty() && entry();
            if (cf->source == ES_TYPES) {
                for (auto t : cf->types) if (entry(shape(t))) return true;
                return false;
            }
            auto t = shape(cf->types[0]);
            if (!t) return false;
            if (cf->source == ES_FIELDS) {
                auto fields = t->kind == TY_STRUCT ? &t->struc->st->fields
                            : t->kind == TY_VARIANT ? &t->var->variant->fields : nullptr;
                return fields && any_of(fields->begin(), fields->end(),
                                        [](const Field &f) { return !f.ispad; }) && entry();
            }
            if (t->kind == TY_ENUM) {
                for (auto &v : t->enu->en->variants)
                    if (entry(ast.VariantTypeOf(t, &v, cf->line))) return true;
            }
            return false;
        }
        // Conditions are not evaluated here: every branch's arms count, as
        // a runtime if's branches do.
        if (auto ci = Is<ConstIf>(x)) {
            for (auto &br : ci->branches) if (arms(br.arms)) return true;
            return false;
        }
        if (auto l = Is<LoopExpr>(x)) return !BreaksWithValue(l->body);
        if (auto b = Is<Block>(x)) return walk(b->tail);
        if (auto e = Is<EarlyBlock>(x)) return walk(e->body);
        return false;
    };
    return walk(n);
}

// The constant a `const for` source entry or a `const if` operand (`what`)
// is, as a literal node: a typed or adaptable integer, a float, a bool or a
// string (const_for.md §3.3, §11.2).
inline Node *TypeCheck::ExpansionConstant(Node *n, set<VarDecl *> &visiting, const char *what) {
    // Checking a named global may have inserted its ordinary integer-to-
    // float conversion. This is not permission for a source-level cast.
    if (auto a = Is<AsCast>(n); a && a->implicit) {
        auto c = ExpansionConstant(a->child, visiting, what);
        if (auto i = Is<IntLit>(c)) {
            auto f = ast.New<FltLit>(n->line, i->uns ? (double)(uint64_t)i->val : (double)i->val);
            f->fixedtype = a->type;
            if (a->type->fltstorage == FS_F32) f->val = (float)f->val;
            return f;
        }
        Error(n, cat("unsupported conversion in ", what));
    }
    Val v;
    bool literal;
    set<VarDecl *> integers;
    ConstUse use { n, what };
    if (ConstIntValue(n, v, literal, integers, &use)) {
        // `~c` (Val::notconst) stays one, to adapt where the entry is used.
        if (v.notconst)
            return ast.New<Unary>(n->line, T_BITNOT, ast.New<IntLit>(n->line, v.litlo));
        auto i = ast.New<IntLit>(n->line, v.ival, string_view {}, v.uns);
        // An unannotated named literal keeps its ordinary adaptability.
        // Arithmetic still follows ConstIntValue's evaluation widths.
        if (auto id = Is<Ident>(n)) {
            auto g = ast.LookupGlobal(id->name, id->ns);
            if (g && !g->type && g->defs[0]->constlit) literal = true;
        }
        if (!literal) i->fixedtype = v.type;
        return i;
    }
    if (Is<StrLit>(n) || Is<BoolLit>(n) || Is<FltLit>(n)) return n->Clone(ast);
    if (auto u = Is<Unary>(n); u && u->op == T_MINUS) {
        if (auto f = Is<FltLit>(u->child)) return ast.New<FltLit>(n->line, -f->val);
    }
    if (auto id = Is<Ident>(n)) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (g && !g->isvar && g->inits.size() == 1) {
            if (!visiting.insert(g).second) Error(n, cat("cycle in ", what, " constant initializer"));
            use.named = g->defs[0];
            RelyOnConstant(use, g->defs[0]);
            auto c = ExpansionConstant(g->inits[0], visiting, what);
            visiting.erase(g);
            auto t = g->defs[0]->type ? g->defs[0]->type : g->type;
            if (t && t->kind == TY_FLT) {
                if (auto i = Is<IntLit>(c))
                    c = ast.New<FltLit>(n->line, i->uns ? (double)(uint64_t)i->val : (double)i->val);
                auto f = Is<FltLit>(c);
                if (!f) Error(n, cat("named floating constant in ", what,
                                     " requires a numeric constant initializer"));
                if (g->type || !g->defs[0]->constlit) f->fixedtype = ast.PlainOf(t);
                if (t->fltstorage == FS_F32) f->val = (float)f->val;
                return c;
            }
            if (Is<FltLit>(c) && !t) return c;
            if (Is<BoolLit>(c) && (!t || t->kind == TY_BOOL)) return c;
            if (Is<StrLit>(c) && (!t || (t->kind == TY_SLICE && IsU8(t->sub)))) return c;
        }
    }
    Error(n, cat(what, " requires a scalar or string constant, or supported integer constant expression"));
}

// The kind of the literal ExpansionConstant gives, for diagnostics.
static const char *ConstantKind(const Node *c) {
    return Is<FltLit>(c) ? "float" : Is<BoolLit>(c) ? "bool" : Is<StrLit>(c) ? "string" : "integer";
}
static string AConstantKind(const Node *c) {
    auto k = ConstantKind(c);
    return cat(*k == 'i' ? "an " : "a ", k);
}

static bool CompareResult(TType op, int cmp) {
    switch (op) {
        case T_EQ: return cmp == 0;
        case T_NEQ: return cmp != 0;
        case T_LT: return cmp < 0;
        case T_LTEQ: return cmp <= 0;
        case T_GT: return cmp > 0;
        default: return cmp >= 0;
    }
}

// A comparison in a `const if` condition: two constant operands of one kind
// (const_for.md §11.2). Integers unify as constant arithmetic unifies them
// (ConstIntValue's binary case); an integer beside a float converts (§6.3);
// booleans and strings take `==` and `!=` only.
inline bool TypeCheck::ConstComparison(Binary *b) {
    auto what = constcondition;
    ConstNames(b->left, what);
    ConstNames(b->right, what);
    Val l, r;
    bool llit, rlit;
    set<VarDecl *> visiting;
    ConstUse use { b, what };
    auto lint = ConstIntValue(b->left, l, llit, visiting, &use);
    auto rint = ConstIntValue(b->right, r, rlit, visiting, &use);
    if (lint && rint) {
        auto lf = l, rf = r;
        if (!llit) lf.ck = CK_NONE;
        if (!rlit) rf.ck = CK_NONE;
        auto t = UnifyNumeric(b, b->op, lf, rf, l.type, r.type, true);
        auto cmp = IsUnsigned(t->intstorage)
            ? ((uint64_t)l.ival > (uint64_t)r.ival) - ((uint64_t)l.ival < (uint64_t)r.ival)
            : (l.ival > r.ival) - (l.ival < r.ival);
        return CompareResult(b->op, cmp);
    }
    // A query is a bool constant, as a named one is.
    auto operand = [&](Node *n) -> Node * {
        if (auto q = Is<ConstQuery>(n)) return ast.New<BoolLit>(n->line, ConstQueryValue(q));
        return ExpansionConstant(n, visiting, what);
    };
    auto lc = operand(b->left), rc = operand(b->right);
    if (Is<FltLit>(lc) || Is<FltLit>(rc)) {
        auto value = [&](Node *c, bool isint, const Val &v) -> double {
            if (auto f = Is<FltLit>(c)) return f->val;
            if (isint) return v.uns ? (double)(uint64_t)v.ival : (double)v.ival;
            Error(b, cat(what, " compares a float with ", AConstantKind(c)));
        };
        auto lv = value(lc, lint, l), rv = value(rc, rint, r);
        return CompareResult(b->op, (lv > rv) - (lv < rv));
    }
    auto lk = ConstantKind(lc), rk = ConstantKind(rc);
    if (strcmp(lk, rk) != 0)
        Error(b, cat(what, " compares ", AConstantKind(lc), " with ", AConstantKind(rc)));
    if (b->op != T_EQ && b->op != T_NEQ)
        Error(b, cat(lk, " constants compare with == and != only in a ", what));
    auto equal = Is<StrLit>(lc) ? Is<StrLit>(lc)->val == Is<StrLit>(rc)->val
                                : Is<BoolLit>(lc)->val == Is<BoolLit>(rc)->val;
    return equal == (b->op == T_EQ);
}

inline bool TypeCheck::ConstQueryValue(ConstQuery *q) {
    if (q->query == Q_HAS_DEFAULT) {
        if (!q->owner)
            Error(q, cat("has_default<", q->name, "> requires ", q->name,
                         " to be a fields<T> accessor binding in scope"));
        // Only a written `= ...` counts, whatever the field's type defaults
        // to on its own; the expression itself is not evaluated.
        auto owner = ast.PlainOf(q->owner);
        auto &fields = owner->kind == TY_VARIANT ? owner->var->variant->fields
                                                 : GetStructInst(owner)->st->fields;
        for (auto &f : fields) if (f.name == q->field) return f.defaultval != nullptr;
        Error(q, cat("internal: has_default of unknown field ", q->field));
    }
    auto what = q->query == Q_SAME ? "same" : "holds_own";
    TypeExpr *ts[2];
    for (size_t i = 0; i < q->types.size(); i++) {
        auto raw = q->types[i];
        ConstNamesIn(raw);
        auto t = Subst(raw);
        // Validation reports an unknown name before it could pass as a generic.
        if (t->kind != TY_FN) ValidateType(t, q->line, VT_POINTEE);
        if (HasGenerics(t)) Error(q, cat(what, " requires concrete types, got ", TypeStr(t)));
        ts[i] = t;
    }
    // Whether a value of the type carries an obligation, or has a part that
    // does (docs/design/ownership.md 4.1): what a copy of it, or storage
    // reused under it, would lose track of.
    if (q->query == Q_HOLDS_OWN) return ts[0]->kind != TY_FN && HoldsOwn(ts[0]);
    return TypeEq(ts[0], ts[1]);
}

// A `const if` condition's value in this specialization (const_for.md
// §11.2): boolean constants and queries, `!`, `&&`, `||`, and comparisons of
// constant operands. Anything else is an error, never a runtime branch.
inline bool TypeCheck::ConstCondition(Node *n) {
    if (auto b = Is<BoolLit>(n)) return b->val;
    if (auto q = Is<ConstQuery>(n)) return ConstQueryValue(q);
    if (auto u = Is<Unary>(n); u && u->op == T_NOT) return !ConstCondition(u->child);
    if (auto b = Is<Binary>(n)) {
        switch (b->op) {
            case T_ANDAND: case T_OROR: {
                // Both operands are conditions, whichever decides the result.
                auto l = ConstCondition(b->left);
                auto r = ConstCondition(b->right);
                return b->op == T_ANDAND ? l && r : l || r;
            }
            case T_EQ: case T_NEQ: case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:
                return ConstComparison(b);
            default: break;
        }
    }
    auto what = constcondition;
    if (Is<Ident>(n) || Is<IntLit>(n) || Is<FltLit>(n) || Is<StrLit>(n) || Is<Unary>(n) ||
        Is<Binary>(n)) {
        ConstNames(n, what);
        set<VarDecl *> visiting;
        auto c = ExpansionConstant(n, visiting, what);
        if (auto bl = Is<BoolLit>(c)) return bl->val;
        Error(n, cat(what, " requires a bool, not ", AConstantKind(c), ": ", ExprStr(n)));
    }
    Error(n, cat(what, " requires a constant: ", ExprStr(n), " is not one"));
}

inline ConstIfBranch *TypeCheck::SelectBranch(ConstIf *ci) {
    for (auto &br : ci->branches)
        if (!br.cond || ConstCondition(br.cond)) return &br;
    return nullptr;
}

// A `const if` in a statement list: the chosen branch's block, in a block
// standing for the selection as ExpandStatements's stands for an expansion,
// and empty where no branch is chosen. The block is a plain scope, not an
// expansion boundary: `break` and `continue` leave it as they leave an
// `if`'s branch. The branch not chosen is never checked.
inline Block *TypeCheck::ExpandSelection(ConstIf *ci) {
    auto out = ast.New<Block>(ci->line);
    out->origin = ci->Origin();
    out->expansiontrace = ci->expansiontrace;
    if (auto br = SelectBranch(ci)) out->stmts.push_back(br->body);
    return out;
}

// A `const assert` in this specialization (const_for.md §11.7): nothing,
// where its condition holds, and the error its message words where it does
// not, which the instantiation trace takes to the call that made it so.
inline Block *TypeCheck::ExpandAssertion(ConstAssert *ca) {
    Saved<const char *> what(constcondition, "const assert condition");
    if (!ConstCondition(ca->cond)) Error(ca, ca->msg->val);
    auto out = ast.New<Block>(ca->line);
    out->origin = ca->Origin();
    out->expansiontrace = ca->expansiontrace;
    return out;
}

inline void TypeCheck::ExpandConstFor(ConstFor *cf, const function<void(ExpansionClone &)> &emit) {
    // Resolve the complete source before creating any fragment instances.
    vector<vector<ExpansionArg>> entries;
    vector<string> descriptions;
    if (cf->source == ES_CONSTANTS) {
        for (size_t i = 0; i < cf->constants.size(); i++) {
            auto n = cf->constants[i];
            ConstNames(n, "const for source");
            set<VarDecl *> visiting;
            auto value = ExpansionConstant(n, visiting);
            entries.push_back({ { cf->bindings[0], value } });
            descriptions.push_back(cat("constant entry ", i + 1, " (", ExprStr(n), ")"));
        }
    } else {
        vector<TypeExpr *> types;
        for (auto raw : cf->types) {
            ConstNamesIn(raw);
            auto t = Subst(raw);
            if (HasGenerics(t)) Error(cf, cat("const for source requires a concrete type, got ", TypeStr(t)));
            if (cf->source == ES_FIELDS && t->kind != TY_STRUCT && t->kind != TY_VARIANT)
                Error(cf, cat("fields requires a concrete struct or variant type, got ", TypeStr(t)));
            if (cf->source == ES_VARIANTS && ast.PlainOf(t)->kind != TY_ENUM)
                Error(cf, cat("variants requires a concrete ADT type, got ", TypeStr(t)));
            if (t->kind != TY_FN) ValidateType(t, cf->line, VT_POINTEE);
            types.push_back(t);
        }
        if (cf->source == ES_FIELDS) {
            // A variant type's payload is a struct-like run of fields (§3.5),
            // reached as a struct's are, on a value of that variant type.
            auto t = ast.PlainOf(types[0]);
            auto &fields = t->kind == TY_VARIANT ? t->var->variant->fields : GetStructInst(t)->st->fields;
            for (auto &f : fields) {
                if (f.ispad) continue;
                entries.push_back({ { cf->bindings[0], ast.New<StrLit>(cf->line, string(f.name)) },
                                    { cf->bindings[1], nullptr, nullptr, t, f.name } });
                descriptions.push_back(cat("field ", TypeStr(t), ".", f.name));
            }
        } else if (cf->source == ES_VARIANTS) {
            // Every variant in declaration order: its name, and the variant as
            // a type, a pattern, a constructor and, payload-less, a value.
            auto t = ast.PlainOf(types[0]);
            for (auto &v : t->enu->en->variants) {
                auto vt = ast.VariantTypeOf(t, &v, cf->line);
                Node *value = nullptr;
                if (none_of(v.fields.begin(), v.fields.end(), [](const Field &f) { return !f.ispad; }))
                    value = ast.New<StructLit>(cf->line, vt);
                ExpansionArg variant { cf->bindings[1], value, vt };
                variant.variant = v.name;
                entries.push_back({ { cf->bindings[0], ast.New<StrLit>(cf->line, string(v.name)) }, variant });
                descriptions.push_back(cat("variant ", TypeStr(t), ".", v.name));
            }
        } else {
            for (auto t : types) {
                entries.push_back({ { cf->bindings[0], nullptr, t } });
                descriptions.push_back(cat("type ", TypeStr(t)));
            }
        }
    }
    for (size_t i = 0; i < entries.size(); i++) {
        ExpansionClone ctx;
        ctx.args = std::move(entries[i]);
        ctx.trace = ast.Intern(cat("\n  in const for ", descriptions[i], " at ", Where(cf->line),
                                  cf->expansiontrace));
        ctx.size = &expansionsize;
        ctx.error = [&](Line line, const string &msg) {
            ErrorIn(LocOf(line), msg, Chain(ctx.trace));
        };
        auto saved = ast.expansionclone;
        ast.expansionclone = &ctx;
        // Cloning is the only operation using this context. Recursive
        // expansion and ordinary checking take place after it is restored.
        try { emit(ctx); } catch (...) { ast.expansionclone = saved; throw; }
        ast.expansionclone = saved;
    }
}

inline Block *TypeCheck::ExpandStatements(ConstFor *cf) {
    auto out = ast.New<Block>(cf->line);
    out->origin = cf->Origin();
    out->expansiontrace = cf->expansiontrace;
    ExpandConstFor(cf, [&](ExpansionClone &) {
        auto body = (Block *)cf->body->Clone(ast);
        body->expansionboundary = true;
        out->stmts.push_back(body);
    });
    return out;
}

// A match's arm list with each `const for` of arms replaced by its arms, one
// set per entry, and each `const if` by its chosen branch's arms, in place:
// the ordinary match checks then see every arm.
inline void TypeCheck::ExpandArms(vector<MatchArm> &arms) {
    if (none_of(arms.begin(), arms.end(), [](const MatchArm &a) { return a.expansion != nullptr; }))
        return;
    vector<MatchArm> flat;
    for (auto &arm : arms) {
        vector<MatchArm> generated;
        if (auto cf = Is<ConstFor>(arm.expansion)) {
            ExpandConstFor(cf, [&](ExpansionClone &) {
                for (auto &a : cf->arms) {
                    generated.push_back(CloneArm(ast, a));
                    generated.back().generated = true;
                }
            });
        } else if (auto ci = Is<ConstIf>(arm.expansion)) {
            // Arms chosen inside a const for's arms are generated ones too.
            if (auto br = SelectBranch(ci))
                for (auto &a : br->arms) {
                    generated.push_back(a);
                    generated.back().generated |= arm.generated;
                }
        } else {
            flat.push_back(arm);
            continue;
        }
        ExpandArms(generated);
        flat.insert(flat.end(), generated.begin(), generated.end());
    }
    arms = std::move(flat);
}

inline void TypeCheck::ExpandInitializers(vector<FieldInit> &inits) {
    auto expansion = [](const FieldInit &fi) { return Is<ConstFor>(fi.val) || Is<ConstIf>(fi.val); };
    if (none_of(inits.begin(), inits.end(), expansion)) return;
    vector<FieldInit> flat;
    for (auto &fi : inits) {
        vector<FieldInit> generated;
        if (auto cf = Is<ConstFor>(fi.val)) {
            ExpandConstFor(cf, [&](ExpansionClone &) {
                for (auto &entry : cf->inits) generated.push_back(ast.CloneFieldInit(entry));
            });
        } else if (auto ci = Is<ConstIf>(fi.val)) {
            if (auto br = SelectBranch(ci))
                for (auto &entry : br->inits) generated.push_back(entry);
        } else {
            flat.push_back(fi);
            continue;
        }
        ExpandInitializers(generated);
        flat.insert(flat.end(), generated.begin(), generated.end());
    }
    inits = std::move(flat);
}

} // namespace goose
