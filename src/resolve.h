// Goose compiler — post-parse type name resolution, possible only once every
// file is in (top-level declarations are order-independent). Every parse-time
// TY_UNRESOLVED is rewritten in place: struct and enum names become TY_STRUCT /
// TY_ENUM, alias uses are substituted with the aliased type, and anything else
// becomes TY_GENERIC — a generic type parameter, or an unknown name, which
// typecheck decides since it knows scopes. A qualified name (`ns::Name`) can
// only be a declaration, so an unknown one is an error here, as is a name in
// a struct's, an enum's or an alias's declaration that is not one of its type
// parameters. After this pass no TY_UNRESOLVED remains and aliases do not
// exist as types.
//
// An error here is reported (Ast::Report) and the pass goes on: the name is
// given a type that keeps the analysis going. An unknown one, or one this
// pass cannot make a type of, stands for a struct with no fields that names
// nothing (SStruct::unknown, Ast::unknowntypes), which the checker takes for
// a consequence of the error wherever it meets a value of it.
#pragma once
#include "expansion_bind.h"

namespace goose {

inline void ResolveTypeNames(Ast &ast) {
    BindExpansionNames(ast);
    // Once per name as written, however many nodes share its detail (a
    // `const` copy, an alias's uses): `detail` is that detail. Reported in
    // the order of the source at the end, rather than of the steps below.
    set<const void *> reported;
    vector<Diagnostic> errors;
    auto ErrorAt = [&](const TypeExpr *t, const void *detail, const string &msg,
                       string_view written = {}) {
        if (!reported.insert(detail).second) return;
        Diagnostic d;
        d.where = LocOf(t->line);
        if (!written.empty() && t->line.col > 0) {
            d.where.endline = t->line.line;
            d.where.endcol = t->line.col + (int)written.size();
        }
        d.message = msg;
        errors.push_back(std::move(d));
    };
    // Makes t the struct standing for a name that names nothing, one per
    // detail, so that every copy of the use is the same type.
    map<const void *, SStruct *> unknowns;
    auto MakeUnknown = [&](TypeExpr *t, const void *detail, string_view name) {
        auto &st = unknowns[detail];
        if (!st) {
            st = new SStruct();
            st->name = st->qname = ast.Intern(string(name));
            st->line = st->end = t->line;
            st->broken = st->unknown = true;
            ast.unknowntypes.push_back(st);
        }
        auto d = ast.NewDetail<TypeStruct>();
        d->st = st;
        t->kind = TY_STRUCT;
        t->struc = d;
    };

    // Classify every parsed name that is not an alias use. A name's detail
    // may be shared with a `const` copy of its node (Ast::ConstOf), which is
    // classified on its own, so the arguments are copied out of it.
    for (auto t : ast.alltypes) {
        if (t->kind != TY_UNRESOLVED) continue;
        auto nm = t->named;
        int native = -1;
        for (int k = NK_OWNER; k <= NK_ATOMIC_U64; k++)
            if (nm->name == NativeName(k)) native = k;
        if (native >= 0) {
            if (!nm->args.empty() || nm->varmode)
                ErrorAt(t, nm, "native capabilities take no type arguments or variable mode");
            t->kind = TY_NATIVE;
            t->nativekind = (NativeKind)native;
        } else if (auto s = ast.LookupStruct(nm->name, nm->ns)) {
            if (nm->varmode)
                ErrorAt(t, nm, cat("struct ", nm->name,
                                   " has no variable mode (.. applies to enums)"));
            auto d = ast.NewDetail<TypeStruct>();
            d->st = s;
            d->args = nm->args;
            t->kind = TY_STRUCT;
            t->struc = d;
        } else if (auto e = ast.LookupEnum(nm->name, nm->ns)) {
            auto d = ast.NewDetail<TypeEnum>();
            d->en = e;
            d->args = nm->args;
            d->varmode = nm->varmode;
            t->kind = TY_ENUM;
            t->enu = d;
        } else if (!ast.LookupAlias(nm->name, nm->ns)) {
            if (SplitName(nm->name, nm->ns).qualified) {
                ErrorAt(t, nm, cat("unknown type: ", nm->name), nm->name);
                MakeUnknown(t, nm, nm->name);
            } else {
                t->kind = TY_GENERIC;  // Keeps its TypeName detail, varmode included.
            }
        }
        // Alias uses stay TY_UNRESOLVED for the substitution loop below.
    }

    // A declaration's types can name no generic but its own type
    // parameters (Parser::BindGenericNames), and an alias's none: any
    // other name is unknown, which the declaration's validation would only
    // find at the checker's start, ending the analysis there. A generic of
    // a const for expansion is bound by name only in a body.
    function<void(TypeExpr *, const vector<GenericParam> *)> unknownin =
        [&](TypeExpr *t, const vector<GenericParam> *generics) {
            if (!t) return;
            auto each = [&](const vector<TypeExpr *> &ts) {
                for (auto child : ts) unknownin(child, generics);
            };
            switch (t->kind) {
                case TY_GENERIC: {
                    auto nm = t->named;
                    each(nm->args);
                    if (nm->expansionbinding) break;
                    if (generics)
                        for (auto &g : *generics)
                            if (g.name == nm->name) return;
                    ErrorAt(t, nm, cat("unknown type: ", nm->name), nm->name);
                    MakeUnknown(t, nm, nm->name);
                    break;
                }
                case TY_ARRAY: unknownin(t->arr->sub, generics); break;
                case TY_REF: unknownin(t->ref->sub, generics); break;
                case TY_SLICE: unknownin(t->sub, generics); break;
                case TY_VARIANT: unknownin(t->var->adt, generics); break;
                case TY_STRUCT: each(t->struc->args); break;
                case TY_ENUM: each(t->enu->args); break;
                case TY_UNRESOLVED: each(t->named->args); break;
                case TY_FN: each(t->fn->args); each(t->fn->rets); break;
                default: break;
            }
        };
    for (auto st : ast.structs)
        for (auto &f : st->fields) unknownin(f.type, &st->generics);
    for (auto en : ast.enums)
        for (auto &v : en->variants)
            for (auto &f : v.fields) unknownin(f.type, &en->generics);
    for (auto a : ast.aliases) unknownin(a->type, nullptr);

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
                if (aliasstate[a] == 1) {
                    // The use closing the cycle stands for nothing, which
                    // ends the cycle there.
                    ErrorAt(t, t->named, cat("type alias cycle involving: ", t->named->name));
                    MakeUnknown(t, t->named, t->named->name);
                    break;
                }
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
            case TY_FN: each(t->fn->args); each(t->fn->rets); break;
            default: break;
        }
    };
    for (auto a : ast.aliases) checkaliases(a->type);

    // Substitute alias uses: the use-site node becomes a copy of the target
    // type (sharing its detail), keeping its own source line for diagnostics.
    // A `..` on the use gets a fresh detail with the flag set instead. Either
    // way the node is marked as an alias use (TypeExpr::aliasuse).
    for (auto t : ast.alltypes) {
        if (t->kind != TY_UNRESOLVED) continue;
        auto use = t->named;
        // The use names the alias, whatever else it says wrongly.
        if (!use->args.empty())
            ErrorAt(t, use, cat("type alias ", use->name, " takes no type arguments"));
        auto target = t;
        for (auto depth = 0; target->kind == TY_UNRESOLVED; depth++) {
            assert(depth <= (int)ast.aliases.size());   // checkaliases ended every cycle.
            target = ast.LookupAlias(target->named->name, target->named->ns)->type;
        }
        auto varmode = use->varmode;
        if (varmode && target->kind == TY_ENUM && target->enu->varmode) {
            ErrorAt(t, use, "type is already variable-mode");
            varmode = false;
        } else if (varmode && target->kind != TY_ENUM) {
            // An alias's type names no generic (unknownin): a name in it
            // that names nothing is the struct standing for it, whose `..`
            // is a consequence of the error reported for the name.
            if (!(target->kind == TY_STRUCT && target->struc->st->unknown))
                ErrorAt(t, use, "variable mode (..) requires an ADT type");
            varmode = false;
        }
        if (varmode) {
            auto d = ast.NewDetail<TypeEnum>();
            d->en = target->enu->en;
            d->args = target->enu->args;
            d->varmode = true;
            t->kind = TY_ENUM;
            t->enu = d;
        } else {
            auto line = t->line;
            auto cq = t->cq;
            *t = *target;
            t->line = line;
            t->cq = t->cq || cq;   // `const Alias` keeps its qualifier.
        }
        t->aliasuse = true;
    }

    // Variant lookup runs last, when every ADT name is resolved.
    for (auto t : ast.alltypes) {
        if (t->kind != TY_VARIANT) continue;
        auto adt = t->var->adt;
        if (adt->kind == TY_STRUCT && adt->struc->st->unknown) {
            MakeUnknown(t, t->var, cat(adt->struc->st->name, ".", t->var->name));
            continue;
        }
        if (adt->kind != TY_ENUM) continue;  // E.g. a generic param; typecheck decides.
        auto name = t->var->name;
        auto en = adt->enu->en;
        auto found = en->FindVariant(name);
        if (!found) {
            // The variant a broken enum lacks is a consequence of its error.
            if (!en->broken)
                ErrorAt(t, t->var, cat("enum ", en->name, " has no variant named ", name));
            MakeUnknown(t, t->var, cat(en->qname, ".", name));
            continue;
        }
        t->var->variant = found;  // Alias/const copies may share this detail.
    }

    std::stable_sort(errors.begin(), errors.end(), [](const Diagnostic &a, const Diagnostic &b) {
        return tie(a.where.fileidx, a.where.line, a.where.col) <
               tie(b.where.fileidx, b.where.line, b.where.col);
    });
    for (auto &d : errors) ast.Report(std::move(d));
}

}  // namespace goose
