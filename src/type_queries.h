// Concrete type graphs that will no longer change during typechecking.
#pragma once

namespace goose {

// The checker owns this for one compilation. Construction/qualification of
// fresh nodes precedes queries. The later mutations of published types are
// pending-array inference, size evaluation, pool resolution and instance
// completion; all are excluded here. This walk never instantiates a type.
struct StableTypeGraph {
    unordered_set<TypeExpr *> known;

    bool Ready(TypeExpr *t) {
        if (!t) return false;
        if (known.count(t)) return true;
        vector<TypeExpr *> seen;
        if (!Visit(t, seen)) return false;
        // Cache only the root after the WHOLE graph succeeded. A descendant
        // in a cycle may have depended on an ancestor that later fails.
        known.insert(t);
        return true;
    }

private:
    bool Visit(TypeExpr *t, vector<TypeExpr *> &seen) {
        if (!t) return false;
        if (known.count(t) || find(seen.begin(), seen.end(), t) != seen.end()) return true;
        seen.push_back(t);
        auto all = [&](const vector<TypeExpr *> &ts) {
            for (auto x : ts) if (x && !Visit(x, seen)) return false;
            return true;
        };
        switch (t->kind) {
            case TY_INT: case TY_FLT: case TY_BOOL: case TY_VOID: case TY_NATIVE: return true;
            case TY_SLICE: return Visit(t->sub, seen);
            case TY_REF:
                return (t->ref->poolname.empty() || t->ref->pool) && Visit(t->ref->sub, seen);
            case TY_ARRAY:
                if (t->arr->akind == A_GROW && t->arr->sub->kind == TY_VOID) return false;
                if (t->arr->akind == A_FIXED && t->arr->size < 0) return false;
                if (t->arr->sizeexpr && t->arr->size < 0) return false;
                return Visit(t->arr->sub, seen);
            case TY_STRUCT: {
                auto i = t->struc->inst;
                return i && i->validated && all(t->struc->args) && all(i->ftypes);
            }
            case TY_ENUM: {
                auto i = t->enu->inst;
                if (!i || !i->validated || !all(t->enu->args)) return false;
                for (auto &fs : i->vftypes) if (!all(fs)) return false;
                return true;
            }
            case TY_VARIANT: return t->var->variant && Visit(t->var->adt, seen);
            default: return false;   // Unresolved, generic and function placeholders.
        }
    }
};

}  // namespace goose
