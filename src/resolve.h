// Goose compiler — post-parse type name resolution, possible only once every
// file is in (top-level declarations are order-independent). Every parse-time
// TY_UNRESOLVED is rewritten in place: struct and enum names become TY_STRUCT /
// TY_ENUM, alias uses are substituted with the aliased type, and anything else
// becomes TY_GENERIC — a generic type parameter, or an unknown name, which
// typecheck decides since it knows scopes. A qualified name (`ns::Name`) can
// only be a declaration, so an unknown one is an error here. After this pass
// no TY_UNRESOLVED remains and aliases do not exist as types.
#pragma once

namespace goose {

inline void ResolveTypeNames(Ast &ast) {
    auto ErrorAt = [&](const TypeExpr *t, const string &msg) {
        throw CompileError {
            cat(ast.sources[t->line.fileidx].first, ":", t->line.line, ": error: ", msg)
        };
    };

    // Classify every parsed name that is not an alias use.
    for (auto t : ast.alltypes) {
        if (t->kind != TY_UNRESOLVED) continue;
        auto nm = t->named;
        if (auto s = ast.LookupStruct(nm->name, nm->ns)) {
            if (nm->varmode)
                ErrorAt(t, cat("struct ", nm->name, " has no variable mode (.. applies to enums)"));
            auto d = ast.NewDetail<TypeStruct>();
            d->st = s;
            d->args = std::move(nm->args);
            t->kind = TY_STRUCT;
            t->struc = d;
        } else if (auto e = ast.LookupEnum(nm->name, nm->ns)) {
            auto d = ast.NewDetail<TypeEnum>();
            d->en = e;
            d->args = std::move(nm->args);
            d->varmode = nm->varmode;
            t->kind = TY_ENUM;
            t->enu = d;
        } else if (!ast.LookupAlias(nm->name, nm->ns)) {
            if (SplitName(nm->name, nm->ns).qualified)
                ErrorAt(t, cat("unknown type: ", nm->name));
            t->kind = TY_GENERIC;  // Keeps its TypeName detail, varmode included.
        }
        // Alias uses stay TY_UNRESOLVED for the substitution loop below.
    }

    // Aliases expand structurally, so a cycle is invalid even when it passes
    // through a reference, array, function signature or generic argument.
    // Check before substitution can turn such a cycle into a recursive
    // TypeExpr graph that later tree walks cannot traverse. Nominal struct
    // and enum declarations are boundaries: their fields may refer back to
    // the declaration legally, and are checked by the layout/lifetime passes.
    unordered_map<SAlias *, int> aliasstate;  // 1 visiting, 2 complete.
    function<void(TypeExpr *)> checkaliases = [&](TypeExpr *t) {
        auto each = [&](const vector<TypeExpr *> &ts) {
            for (auto child : ts) checkaliases(child);
        };
        switch (t->kind) {
            case TY_UNRESOLVED: {
                auto a = ast.LookupAlias(t->named->name, t->named->ns);
                if (aliasstate[a] == 1)
                    ErrorAt(t, cat("type alias cycle involving: ", t->named->name));
                if (aliasstate[a] == 2) break;
                aliasstate[a] = 1;
                checkaliases(a->type);
                aliasstate[a] = 2;
                break;
            }
            case TY_ARRAY: checkaliases(t->arr->sub); break;
            case TY_REF: checkaliases(t->ref->sub); break;
            case TY_SLICE: checkaliases(t->sub); break;
            case TY_VARIANT: checkaliases(t->var->adt); break;
            case TY_STRUCT: each(t->struc->args); break;
            case TY_ENUM: each(t->enu->args); break;
            case TY_GENERIC: each(t->named->args); break;
            case TY_FN: each(t->fn->args); each(t->fn->rets); break;
            default: break;
        }
    };
    for (auto a : ast.aliases) checkaliases(a->type);

    // Substitute alias uses: the use-site node becomes a copy of the target
    // type (sharing its detail), keeping its own source line for diagnostics.
    // A `..` on the use gets a fresh detail with the flag set instead.
    for (auto t : ast.alltypes) {
        if (t->kind != TY_UNRESOLVED) continue;
        auto use = t->named;
        if (!use->args.empty())
            ErrorAt(t, cat("type alias ", use->name, " takes no type arguments"));
        auto target = t;
        for (auto depth = 0; target->kind == TY_UNRESOLVED; depth++) {
            if (depth > (int)ast.aliases.size())
                ErrorAt(t, cat("type alias cycle involving: ", use->name));
            target = ast.LookupAlias(target->named->name, target->named->ns)->type;
        }
        if (use->varmode) {
            switch (target->kind) {
                case TY_ENUM: {
                    if (target->enu->varmode) ErrorAt(t, "type is already variable-mode");
                    auto d = ast.NewDetail<TypeEnum>();
                    d->en = target->enu->en;
                    d->args = target->enu->args;
                    d->varmode = true;
                    t->kind = TY_ENUM;
                    t->enu = d;
                    break;
                }
                case TY_GENERIC: {
                    auto d = ast.NewDetail<TypeName>();
                    d->name = target->named->name;
                    d->ns = target->named->ns;
                    d->args = target->named->args;
                    d->varmode = true;
                    t->kind = TY_GENERIC;
                    t->named = d;
                    break;
                }
                default:
                    ErrorAt(t, "variable mode (..) requires an ADT type");
            }
        } else {
            auto line = t->line;
            auto cq = t->cq;
            *t = *target;
            t->line = line;
            t->cq = t->cq || cq;   // `const Alias` keeps its qualifier.
        }
    }

    // Variant lookup runs last, when every ADT name is resolved.
    for (auto t : ast.alltypes) {
        if (t->kind != TY_VARIANT) continue;
        auto adt = t->var->adt;
        if (adt->kind != TY_ENUM) continue;  // E.g. a generic param; typecheck decides.
        auto name = t->var->name;
        SVariant *found = nullptr;
        for (auto &v : adt->enu->en->variants)
            if (v.name == name) { found = &v; break; }
        if (!found)
            ErrorAt(t, cat("enum ", adt->enu->en->name, " has no variant named ", name));
        t->var->variant = found;  // Alias/const copies may share this detail.
    }
}

}  // namespace goose
