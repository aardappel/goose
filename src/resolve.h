// Goose compiler — post-parse type name resolution, possible only once every
// file is in (top-level declarations are order-independent). Every parse-time
// TY_UNRESOLVED is rewritten in place: struct and enum names become TY_STRUCT /
// TY_ENUM, alias uses are substituted with the aliased type, and anything else
// becomes TY_GENERIC — a generic type parameter, or an unknown name, which
// typecheck decides since it knows scopes. After this pass no TY_UNRESOLVED
// remains and aliases do not exist as types.
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
        if (auto s = ast.structmap.find(nm->name); s != ast.structmap.end()) {
            if (nm->varmode)
                ErrorAt(t, cat("struct ", nm->name, " has no variable mode (.. applies to enums)"));
            auto d = ast.NewDetail<TypeStruct>();
            d->st = s->second;
            d->args = std::move(nm->args);
            t->kind = TY_STRUCT;
            t->struc = d;
        } else if (auto e = ast.enummap.find(nm->name); e != ast.enummap.end()) {
            auto d = ast.NewDetail<TypeEnum>();
            d->en = e->second;
            d->args = std::move(nm->args);
            d->varmode = nm->varmode;
            t->kind = TY_ENUM;
            t->enu = d;
        } else if (!ast.aliasmap.count(nm->name)) {
            t->kind = TY_GENERIC;  // Keeps its TypeName detail, varmode included.
        }
        // Alias uses stay TY_UNRESOLVED for the substitution loop below.
    }

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
            target = ast.aliasmap.find(target->named->name)->second->type;
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
            *t = *target;
            t->line = line;
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
        t->var->variant = found;  // Replaces the union's name member.
    }
}

}  // namespace goose
