// Semantic checking is separate from emission. This is an experimental
// bootstrap checker; the fixture's scope is described in README.md.
struct Value {
    ty: i64 = 0,
    root: i64 = -1,
    place: i64 = -1,
    readonly: bool = false,
    growshrink: bool = false,
    inexact: bool = false,
    known: bool = false,
    bits: u64 = 0,
    held: i64 = -1,
    fields: i64 = -1,
}
struct Specialization {
    symbol: i64,
    body: i64,
    scope: i64,
    result: i64,
    state: i64 = 0,
    result_root: i64 = -1,
    parameters: i64 = -1,
    shrinks: i64 = -1,
    caller: i64 = -1,
    in_cycle: bool = false,
    return_seen: bool = false,
    result_fields: i64 = -1,
    result_readonly: bool = false,
    result_growshrink: bool = false,
    result_inexact: bool = false,
    result_used: bool = false,
    visit: i64 = 0,
}
struct CheckedCall { node: i64, scope: i64, callee: i64 }
struct Shrink { root: i64, balanced: bool }

fn value_of(ctx: Compiler&, n: i64) -> Value {
    let x = ctx.nodes[n];
    return Value { ty: x.ty, root: x.root, place: x.place, readonly: x.readonly, growshrink: x.growshrink, inexact: x.inexact, known: x.known, bits: x.bits, held: x.held, fields: x.fields };
}
fn annotate(ctx: Compiler&, n: i64, v: Value) -> Value {
    ctx.nodes[n].ty = v.ty; ctx.nodes[n].root = v.root; ctx.nodes[n].place = v.place;
    ctx.nodes[n].readonly = v.readonly; ctx.nodes[n].known = v.known; ctx.nodes[n].bits = v.bits;
    ctx.nodes[n].held = v.held;
    ctx.nodes[n].fields = v.fields;
    ctx.nodes[n].growshrink = v.growshrink;
    ctx.nodes[n].inexact = v.inexact;
    return v;
}
fn decay(ctx: Compiler&, v: Value) -> Value {
    var r = v;
    if ctx.types[r.ty].kind == T_REFERENCE { r.ty = ctx.types[r.ty].element; }
    return r;
}
fn numeric(ctx: Compiler&, ty: i64) -> bool { return ctx.types[ty].kind == T_INTEGER || ctx.types[ty].kind == T_FLOAT; }
fn boolean_type(ctx: Compiler&) -> i64 { return builtin_type(ctx, "bool"); }
fn default_integer(ctx: Compiler&) -> i64 { return integer_type(ctx, 8, false); }
fn root_depth(ctx: Compiler&, root: i64) -> i64 {
    if root < 0 { return 0; }
    return ctx.scopes[ctx.symbols[root].scope].depth;
}
fn root_outlives(ctx: Compiler&, source: i64, bound: i64) -> bool {
    if source < 0 || source == bound { return true; }
    if bound < 0 { return false; }
    return scope_contains(ctx, ctx.symbols[source].scope, ctx.symbols[bound].scope);
}
recursive fn contains_view(ctx: Compiler&, ty: i64) -> bool {
    let t = ctx.types[ty];
    if t.kind == T_REFERENCE || t.kind == T_SLICE { return true; }
    if t.kind == T_ARRAY { return contains_view(ctx, t.element); }
    if t.kind == T_STRUCT {
        var e = t.first;
        while e >= 0 { if contains_view(ctx, ctx.fielddefs[ctx.edges[e].value].ty) { return true; } e = ctx.edges[e].next; }
    }
    return false;
}
fn store_lifetime(ctx: Compiler&, v: Value, destination: i64, token: i64) {
    let root = content_root(ctx, v);
    if contains_view(ctx, v.ty) && root_depth(ctx, root) > root_depth(ctx, destination) { error_at(ctx, token, "stored reference would outlive its owner"); }
    if destination >= 0 && root_depth(ctx, root) >= root_depth(ctx, ctx.symbols[destination].held) { ctx.symbols[destination].held = root; }
}
fn content_root(ctx: Compiler&, v: Value) -> i64 {
    if ctx.types[v.ty].kind == T_REFERENCE || ctx.types[v.ty].kind == T_SLICE { return v.root; }
    return v.held;
}
fn received(ctx: Compiler&, v: Value, target: i64) -> Value {
    var result = v; result.ty = target; return result;
}
fn scope_contains(ctx: Compiler&, outer: i64, inner: i64) -> bool {
    var s = inner;
    while s >= 0 { if s == outer { return true; } s = ctx.scopes[s].parent; }
    return false;
}
fn holds_view_of(ctx: Compiler&, ty: i64, element: i64) -> bool {
    ctx.visit++;
    return holds_view_walk(ctx, ty, element, ctx.visit);
}
recursive fn holds_view_walk(ctx: Compiler&, ty: i64, element: i64, visit: i64) -> bool {
    if ctx.types[ty].view_visit == visit { return false; }
    ctx.types[ty].view_visit = visit;
    let t = ctx.types[ty];
    if t.kind == T_SLICE { return t.element == element; }
    if t.kind == T_REFERENCE { return t.element == element || (ctx.types[t.element].kind == T_STRUCT && holds_view_walk(ctx, t.element, element, visit)); }
    if t.kind == T_ARRAY { return holds_view_walk(ctx, t.element, element, visit); }
    if t.kind == T_STRUCT {
        var e = t.first;
        while e >= 0 { if holds_view_walk(ctx, ctx.fielddefs[ctx.edges[e].value].ty, element, visit) { return true; } e = ctx.edges[e].next; }
    }
    return false;
}
fn check_stored_view(ctx: Compiler&, v: Value, token: i64) {
    if v.growshrink { error_at(ctx, token, "a value that may refer into a grow-shrink array cannot be stored in a container"); }
    let root = content_root(ctx, v);
    if root >= 0 && contains_view(ctx, v.ty) {
        let t = ctx.types[decay(ctx, Value { ty: ctx.symbols[root].ty }).ty];
        if t.kind == T_ARRAY && t.flags == 4 && holds_view_of(ctx, v.ty, t.element) { error_at(ctx, token, "a reference into a grow-shrink array cannot be stored in a container"); }
    }
}
fn store_in_array(ctx: Compiler&, value: Value, array: Value, scope: i64, token: i64) {
    check_stored_view(ctx, value, token);
    store_lifetime(ctx, value, array.root, token);
    if array.inexact && contains_view(ctx, value.ty) {
        // A borrowed field's bound is not an identity guarantee. A reference
        // parameter to an array of the same shape can reach that storage too.
        for s in ctx.symbols {
            if s.kind == 2 && s.decl >= 0 && ctx.nodes[s.decl].tag == N_PARAM && s.ty >= 0 && scope_contains(ctx, s.scope, scope) && ctx.types[s.ty].kind == T_REFERENCE && ctx.types[s.ty].element == array.ty {
                store_lifetime(ctx, value, s.root, token);
            }
        }
    }
}
fn check_shrink(ctx: Compiler&, root: i64, element: i64, scope: i64, token: i64, balanced: bool, at_call: bool) {
    if root < 0 { error_at(ctx, token, "not implemented yet: shrink through an unknown root"); }
    for s in ctx.symbols {
        if balanced && at_call { continue; }
        if s.kind != 2 || s.state != 2 || s.ty < 0 || !scope_contains(ctx, s.scope, scope) { continue; }
        let shape = ctx.types[s.ty];
        let direct_view = (shape.kind == T_SLICE || shape.kind == T_REFERENCE) && shape.element == element;
        if (direct_view && s.root == root) || (s.held >= 0 && holds_view_of(ctx, s.ty, element) && root_depth(ctx, s.held) >= root_depth(ctx, root)) {
            error_at(ctx, token, str("shrink may invalidate '", ctx.tokens[s.token].text, "'; this checker conservatively keeps views live to scope exit"));
        }
    }
    if ctx.checking_function >= 0 && ctx.scopes[ctx.symbols[root].scope].function != ctx.checking_function {
        var e = ctx.specializations[ctx.checking_function].shrinks;
        while e >= 0 {
            let effect = ctx.edges[e].value;
            if ctx.shrinks[effect].root == root {
                if !balanced && ctx.shrinks[effect].balanced { ctx.shrinks[effect].balanced = false; ctx.effect_revision++; }
                return;
            }
            e = ctx.edges[e].next;
        }
        let effect = ctx.shrinks.len; ctx.shrinks.push(Shrink { root: root, balanced: balanced });
        let id = ctx.edges.len; ctx.edges.push(Edge { value: effect, next: ctx.specializations[ctx.checking_function].shrinks });
        ctx.specializations[ctx.checking_function].shrinks = id;
        ctx.effect_revision++;
    }
}
fn shrink_array(ctx: Compiler&, array: Value, scope: i64, token: i64, balanced: bool) {
    let element = element_type(ctx, array.ty);
    check_shrink(ctx, array.root, element, scope, token, balanced, false);
    if array.inexact {
        // A merged root is an outlives bound, not a unique storage identity.
        // Keep effects for every compatible owned array inside that bound.
        let limit = ctx.symbols.len;
        for i in limit {
            let owner = ctx.symbols[i];
            if i != array.root && owner.kind == 2 && owner.ty == array.ty && root_depth(ctx, i) <= root_depth(ctx, array.root) {
                check_shrink(ctx, i, element, scope, token, balanced, false);
            }
        }
    }
}
fn integer_fits(ctx: Compiler&, bits: u64, ty: i64) -> bool {
    let t = ctx.types[ty];
    if t.flags & 2 != 0 {
        if t.bytes == 8 { return true; }
        return bits < (1 as u64) << (t.bytes * 8);
    }
    let signed = bits as! i64;
    if t.bytes == 8 { return true; }
    let bound = (1 as i64) << (t.bytes * 8 - 1);
    return signed >= -bound && signed < bound;
}
fn fits(ctx: Compiler&, v: Value, target: i64) -> bool {
    if target == v.ty || ctx.types[v.ty].kind == T_BOTTOM { return true; }
    let a = ctx.types[v.ty]; let b = ctx.types[target];
    if b.kind == T_REFERENCE { return v.place >= 0 && decay(ctx, v).ty == b.element && (!v.readonly || b.flags & 1 != 0); }
    if a.kind == T_REFERENCE { return fits_plain(ctx, decay(ctx, v), target); }
    return fits_plain(ctx, v, target);
}
fn fits_plain(ctx: Compiler&, v: Value, target: i64) -> bool {
    let a = ctx.types[v.ty]; let b = ctx.types[target];
    if v.ty == target { return true; }
    if a.kind == T_INTEGER && b.kind == T_INTEGER {
        if v.known {
            if a.flags & 2 == 0 && (v.bits as! i64) < 0 && b.flags & 2 != 0 { return false; }
            if a.flags & 2 != 0 && v.bits > 9223372036854775807 && b.flags & 2 == 0 { return false; }
            return integer_fits(ctx, v.bits, target);
        }
        return (a.flags & 2 == b.flags & 2 && a.bytes <= b.bytes) || (a.flags & 2 != 0 && b.flags & 2 == 0 && a.bytes < b.bytes);
    }
    if a.kind == T_FLOAT && b.kind == T_FLOAT { return a.bytes <= b.bytes || v.known; }
    if (a.kind == T_ARRAY || a.kind == T_SLICE) && (b.kind == T_ARRAY || b.kind == T_SLICE) && a.element == b.element {
        if b.kind == T_SLICE { return !v.readonly || b.flags & 1 != 0; }
        if b.flags == 0 { return a.kind == T_ARRAY && a.flags == 0 && a.count == b.count; }
        return true;
    }
    return false;
}
fn require_fit(ctx: Compiler&, n: i64, v: Value, target: i64) {
    if !fits(ctx, v, target) { error_at(ctx, ctx.nodes[n].token, str("incompatible types (type ", v.ty, " kind ", ctx.types[v.ty].kind, " width ", ctx.types[v.ty].bytes, " -> ", target, " kind ", ctx.types[target].kind, " width ", ctx.types[target].bytes, ")")); }
    ctx.nodes[n].target = target;
}
fn merge_numeric(ctx: Compiler&, a: Value, b: Value, token: i64) -> i64 {
    if !numeric(ctx, a.ty) || !numeric(ctx, b.ty) { error_at(ctx, token, "numeric operands required"); }
    if a.ty == b.ty { return a.ty; }
    if a.known && !b.known { if !fits(ctx, a, b.ty) { error_at(ctx, token, "constant operand does not fit the other operand's type"); } return b.ty; }
    if b.known && !a.known { if !fits(ctx, b, a.ty) { error_at(ctx, token, "constant operand does not fit the other operand's type"); } return a.ty; }
    if fits(ctx, a, b.ty) { return b.ty; }
    if fits(ctx, b, a.ty) { return a.ty; }
    error_at(ctx, token, "numeric operands require an explicit conversion"); return 0;
}
recursive fn clone_tree(ctx: Compiler&, n: i64) -> i64 {
    if n < 0 { return -1; }
    let original = ctx.nodes[n];
    let id = node(ctx, original.tag, original.token);
    ctx.nodes[id] = original; ctx.nodes[id].first = -1; ctx.nodes[id].last = -1;
    // Only syntax edges are cloned. Tokens and namespace IDs are immutable.
    ctx.nodes[id].a = clone_tree(ctx, original.a);
    ctx.nodes[id].b = clone_tree(ctx, original.b);
    ctx.nodes[id].c = clone_tree(ctx, original.c);
    var e = original.first;
    while e >= 0 { child(ctx, id, clone_tree(ctx, ctx.edges[e].value)); e = ctx.edges[e].next; }
    return id;
}
fn first_child(ctx: Compiler&, n: i64) -> i64 {
    if n < 0 || ctx.nodes[n].first < 0 { return -1; }
    return ctx.edges[ctx.nodes[n].first].value;
}
fn child_count(ctx: Compiler&, n: i64) -> i64 {
    var count = 0; var e = ctx.nodes[n].first;
    while e >= 0 { count++; e = ctx.edges[e].next; }
    return count;
}

recursive fn check_expression(ctx: Compiler&, n: i64, scope: i64, expected: i64) -> Value {
    let x = ctx.nodes[n];
    let tag = x.tag;
    var v = Value {};
    if tag == N_INT { v.ty = default_integer(ctx); v.known = true; v.bits = ctx.tokens[x.token].integer; if v.bits > 9223372036854775807 { v.ty = integer_type(ctx, 8, true); } }
    else if tag == N_FLOAT { v.ty = builtin_type(ctx, "f64"); v.known = true; }
    else if tag == N_BOOL { v.ty = boolean_type(ctx); v.known = true; if ctx.tokens[x.token].text == "true" { v.bits = 1; } }
    else if tag == N_STRING { v.ty = slice_type(ctx, integer_type(ctx, 1, true), true); v.readonly = true; }
    else if tag == N_NAME {
        let s = lookup(ctx, n, scope, 2);
        if s < 0 { error_at(ctx, x.token, str("unknown variable '", ctx.tokens[x.token].text, "'")); }
        if ctx.symbols[s].state != 2 { error_at(ctx, x.token, "variable read before initialization"); }
        ctx.nodes[n].symbol = s;
        v.ty = ctx.symbols[s].ty; v.place = s; v.root = s;
        v.held = ctx.symbols[s].held;
        v.fields = ctx.symbols[s].fields;
        if ctx.types[v.ty].kind == T_REFERENCE || ctx.types[v.ty].kind == T_SLICE { v.root = ctx.symbols[s].root; }
        v.readonly = ctx.symbols[s].readonly || ctx.symbols[s].flags & 2 != 0 || (ctx.types[v.ty].kind == T_SLICE && ctx.types[v.ty].flags & 1 != 0) || (ctx.types[v.ty].kind == T_REFERENCE && ctx.types[v.ty].flags & 1 != 0);
        v.growshrink = ctx.symbols[s].growshrink;
        v.inexact = ctx.symbols[s].inexact;
        v.known = false; v.bits = ctx.symbols[s].bits;
    } else if tag == N_UNARY {
        let operand = check_expression(ctx, x.a, scope, -1);
        let op = ctx.tokens[x.token].text;
        if op == "&" {
            if operand.place < 0 { error_at(ctx, x.token, "cannot take a reference to a temporary"); }
            v = operand; v.ty = reference_type(ctx, decay(ctx, operand).ty, operand.readonly);
        } else {
            v = decay(ctx, operand);
            if op == "!" { if v.ty != boolean_type(ctx) { error_at(ctx, x.token, "boolean operand required"); } if v.known { v.bits = 0; if operand.bits == 0 { v.bits = 1; } } }
            else if op == "-" || op == "~" {
                if !numeric(ctx, v.ty) { error_at(ctx, x.token, "numeric operand required"); }
                if op == "-" && ctx.types[v.ty].kind == T_INTEGER && ctx.types[v.ty].flags & 2 != 0 && v.bits != 9223372036854775808 { error_at(ctx, x.token, "cannot negate an unsigned value"); }
                if v.known && ctx.types[v.ty].kind == T_INTEGER { if op == "-" { v.bits = 0 - v.bits; v.ty = default_integer(ctx); } else { v.bits = ~v.bits; } }
                v.place = -1;
            }
        }
    } else if tag == N_BINARY {
        let a = decay(ctx, check_expression(ctx, x.a, scope, -1));
        let b = decay(ctx, check_expression(ctx, x.b, scope, -1));
        let op = ctx.tokens[x.token].text;
        if op == "&&" || op == "||" { require_fit(ctx, x.a, a, boolean_type(ctx)); require_fit(ctx, x.b, b, boolean_type(ctx)); v.ty = boolean_type(ctx); }
        else if op == "==" || op == "!=" {
            if numeric(ctx, a.ty) && numeric(ctx, b.ty) { let common = merge_numeric(ctx, a, b, x.token); require_fit(ctx, x.a, a, common); require_fit(ctx, x.b, b, common); }
            else if a.ty != b.ty && !(element_type(ctx, a.ty) >= 0 && element_type(ctx, a.ty) == element_type(ctx, b.ty)) { error_at(ctx, x.token, "equality operands have different types"); }
            v.ty = boolean_type(ctx);
        } else {
            var common = a.ty;
            if op != "<<" && op != ">>" { common = merge_numeric(ctx, a, b, x.token); }
            else if ctx.types[a.ty].kind != T_INTEGER || ctx.types[b.ty].kind != T_INTEGER { error_at(ctx, x.token, "integer shift operands required"); }
            require_fit(ctx, x.a, a, common);
            if op != "<<" && op != ">>" { require_fit(ctx, x.b, b, common); }
            v.ty = common;
            if op == "<" || op == "<=" || op == ">" || op == ">=" { v.ty = boolean_type(ctx); }
        }
    } else if tag == N_CAST {
        let a = decay(ctx, check_expression(ctx, x.a, scope, -1)); v.ty = resolve_type(ctx, x.b, scope);
        if !numeric(ctx, a.ty) || !numeric(ctx, v.ty) { error_at(ctx, x.token, "casts require numeric types"); }
        if ctx.types[a.ty].kind == T_FLOAT && ctx.types[v.ty].kind == T_INTEGER { error_at(ctx, x.token, "not implemented yet: defined wrapping float-to-integer conversion"); }
        v.known = a.known; v.bits = a.bits;
    } else if tag == N_MEMBER {
        let a = decay(ctx, check_expression(ctx, x.a, scope, -1));
        if element_type(ctx, a.ty) >= 0 && (ctx.tokens[x.token].text == "len" || ctx.tokens[x.token].text == "cap") {
            if ctx.tokens[x.token].text == "cap" && (ctx.types[a.ty].kind != T_ARRAY || ctx.types[a.ty].flags != 2) { error_at(ctx, x.token, "capacity exists only on limited arrays"); }
            v.ty = default_integer(ctx);
        } else {
            if ctx.types[a.ty].kind != T_STRUCT { error_at(ctx, x.token, "member access requires a struct"); }
            let f = find_field(ctx, a.ty, x.token);
            if f < 0 { error_at(ctx, x.token, "unknown field"); }
            ctx.nodes[n].symbol = f; v.ty = ctx.fielddefs[f].ty; v.root = a.root; v.place = a.place; v.readonly = a.readonly || ctx.fielddefs[f].flags & 2 != 0;
            v.held = a.held;
            var initializer = a.fields;
            while initializer >= 0 {
                let field = ctx.edges[initializer].value;
                if ctx.nodes[field].symbol == f {
                    let stored = value_of(ctx, ctx.nodes[field].a);
                    v.fields = stored.fields;
                    if ctx.types[v.ty].kind == T_REFERENCE || ctx.types[v.ty].kind == T_SLICE { v.root = stored.root; v.held = stored.held; v.growshrink = stored.growshrink; }
                    break;
                }
                initializer = ctx.edges[initializer].next;
            }
            if ctx.types[v.ty].kind == T_REFERENCE || ctx.types[v.ty].kind == T_SLICE { if initializer < 0 { v.root = a.held; } v.readonly = ctx.types[v.ty].flags & 1 != 0; v.inexact = true; }
            if ctx.types[v.ty].kind == T_REFERENCE && v.root >= 0 { v.held = ctx.symbols[v.root].held; v.fields = ctx.symbols[v.root].fields; }
        }
    } else if tag == N_INDEX || tag == N_SLICE {
        let a = decay(ctx, check_expression(ctx, x.a, scope, -1)); let element = element_type(ctx, a.ty);
        if element < 0 { error_at(ctx, x.token, "indexing requires an array or slice"); }
        if ctx.types[element].size_class != 0 { error_at(ctx, x.token, "variable-size elements require sequential iteration"); }
        if x.b >= 0 { let b = decay(ctx, check_expression(ctx, x.b, scope, -1)); if ctx.types[b.ty].kind != T_INTEGER { error_at(ctx, x.token, "index must be an integer"); } }
        if x.c >= 0 { let c = decay(ctx, check_expression(ctx, x.c, scope, -1)); if ctx.types[c.ty].kind != T_INTEGER { error_at(ctx, x.token, "slice bound must be an integer"); } }
        v.ty = element; v.root = a.root; v.place = a.place; v.readonly = a.readonly;
        v.held = a.held;
        v.growshrink = a.growshrink;
        v.inexact = a.inexact;
        if tag == N_INDEX && (ctx.types[element].kind == T_REFERENCE || ctx.types[element].kind == T_SLICE) { v.root = a.held; v.inexact = true; }
        if tag == N_SLICE { v.ty = slice_type(ctx, element, a.readonly); v.place = -1; }
    } else if tag == N_ARRAY || tag == N_FILL || tag == N_LIMITED { v = check_array(ctx, n, scope, expected); }
    else if tag == N_CONSTRUCT { v = check_construct(ctx, n, scope); }
    else if tag == N_CALL { v = check_call(ctx, n, scope); }
    else if tag == N_BLOCK { v = check_block(ctx, n, scope, expected); }
    else if tag == N_IF {
        let c = decay(ctx, check_expression(ctx, x.a, scope, -1)); require_fit(ctx, x.a, c, boolean_type(ctx));
        let a = check_expression(ctx, x.b, scope, expected);
        if x.c >= 0 {
            let b = check_expression(ctx, x.c, scope, expected);
            v = a;
            if ctx.types[a.ty].kind == T_BOTTOM { v = b; }
            else if ctx.types[b.ty].kind != T_BOTTOM && a.ty != b.ty {
                if fits(ctx, b, a.ty) { require_fit(ctx, x.c, b, a.ty); }
                else if fits(ctx, a, b.ty) { v.ty = b.ty; require_fit(ctx, x.b, a, b.ty); }
                else { error_at(ctx, x.token, "branches have incompatible types"); }
            }
            if root_depth(ctx, b.root) > root_depth(ctx, v.root) { v.root = b.root; }
            if root_depth(ctx, b.held) > root_depth(ctx, v.held) { v.held = b.held; }
            if b.fields != v.fields { v.fields = -1; }
            v.readonly = a.readonly || b.readonly;
            v.growshrink = a.growshrink || b.growshrink;
            v.inexact = a.inexact || b.inexact || a.root != b.root;
            v.place = -1; v.known = false;
        }
    } else if tag == N_WHILE || tag == N_FOR || tag == N_LOOP || tag == N_EARLY { v = check_loop(ctx, n, scope); }
    else if tag == N_RETURN {
        if ctx.checking_function < 0 { error_at(ctx, x.token, "return outside a function"); }
        if x.a >= 0 { error_at(ctx, x.token, "not implemented yet: long-distance returns"); }
        let expected_result = ctx.specializations[ctx.checking_function].result;
        let e = first_child(ctx, n);
        if child_count(ctx, n) > 1 { error_at(ctx, x.token, "not implemented yet: multiple results"); }
        var result = Value {};
        if e >= 0 { result = check_expression(ctx, e, scope, expected_result); }
        check_return(ctx, e, result, ctx.checking_function, x.token);
        v.ty = intern_type(ctx, Type { kind: T_BOTTOM });
    } else if tag == N_BREAK || tag == N_CONTINUE {
        if ctx.loop_depth == 0 { error_at(ctx, x.token, "loop exit outside a loop or block"); }
        if x.first >= 0 { error_at(ctx, x.token, "not implemented yet: value-producing break"); }
        v.ty = intern_type(ctx, Type { kind: T_BOTTOM });
    } else if tag == N_GUARD {
        let c = decay(ctx, check_expression(ctx, x.a, scope, -1)); require_fit(ctx, x.a, c, boolean_type(ctx));
        if x.b >= 0 { let failure = check_expression(ctx, x.b, scope, 0); if ctx.types[failure.ty].kind != T_BOTTOM { error_at(ctx, x.token, "guard else block must diverge"); } }
        else if ctx.loop_depth == 0 && ctx.checking_function >= 0 && ctx.specializations[ctx.checking_function].result > 0 { error_at(ctx, x.token, "bare guard cannot return from a valued function"); }
    } else if tag == N_EXPR_STMT { let inner = check_expression(ctx, x.a, scope, -1); if ctx.types[inner.ty].kind == T_BOTTOM { v = inner; } }
    else if tag == N_BIND { check_binding(ctx, n, scope); }
    else if tag == N_ASSIGN { check_assignment(ctx, n, scope); }
    else if tag == N_FUNCTION {
        let s = ctx.nodes[n].symbol;
        ctx.symbols[s].state = 2; ctx.symbols[s].capture_limit = ctx.symbols.len;
    }
    else { error_at(ctx, x.token, str("not implemented yet: expression tag ", tag)); }
    let function_body = tag == N_BLOCK && ctx.checking_function >= 0 && ctx.specializations[ctx.checking_function].body == n;
    if (tag == N_IF || tag == N_BLOCK) && !function_body && v.ty != 0 && ctx.types[v.ty].kind != T_BOTTOM {
        if expected >= 0 && ctx.types[expected].kind == T_REFERENCE {
            require_fit(ctx, n, v, expected); v.ty = expected;
        } else {
            v = decay(ctx, v);
            if v.place >= 0 && ctx.types[v.ty].size_class > 0 { error_at(ctx, x.token, "nonfixed storage is not copied implicitly by a control expression; use copy or a reference destination"); }
        }
        v.place = -1;
    }
    if (tag == N_ARRAY || tag == N_FILL || tag == N_CONSTRUCT || tag == N_CALL || tag == N_IF || (tag == N_BLOCK && !function_body)) && (ctx.types[v.ty].kind == T_ARRAY || ctx.types[v.ty].kind == T_STRUCT) && v.place < 0 {
        let temporary_scope = scope_new(ctx, scope, ctx.checking_function);
        let temporary_root = ctx.symbols.len;
        ctx.symbols.push(Symbol { token: x.token, ns: -1, kind: 4, decl: n, scope: temporary_scope, ty: v.ty, state: 2 });
        v.root = temporary_root;
        if tag != N_CALL || ctx.nodes[n].symbol == -27 { v.readonly = true; }
    }
    if contains_view(ctx, v.ty) && content_root(ctx, v) >= 0 {
        let owner = ctx.types[decay(ctx, Value { ty: ctx.symbols[content_root(ctx, v)].ty }).ty];
        if owner.kind == T_ARRAY && owner.flags == 4 && holds_view_of(ctx, v.ty, owner.element) { v.growshrink = true; }
    }
    return annotate(ctx, n, v);
}

recursive fn check_array(ctx: Compiler&, n: i64, scope: i64, expected: i64) -> Value {
    let x = ctx.nodes[n];
    var element = -1;
    if expected >= 0 { element = element_type(ctx, expected); }
    var count = 0;
    var root = -1;
    if x.tag == N_LIMITED {
        if element < 0 { error_at(ctx, x.token, "limited literal needs an element type"); }
        require_fit(ctx, x.a, decay(ctx, check_expression(ctx, x.a, scope, -1)), default_integer(ctx));
        return Value { ty: array_type(ctx, element, 2, -1) };
    }
    if x.tag == N_FILL {
        let a = decay(ctx, check_expression(ctx, x.a, scope, element));
        if element < 0 { element = a.ty; } else { require_fit(ctx, x.a, a, element); }
        count = constant_integer(ctx, x.b) as i64; root = a.root;
    } else {
        var e = x.first;
        while e >= 0 {
            let item = ctx.edges[e].value; let a = decay(ctx, check_expression(ctx, item, scope, element));
            if element < 0 { element = a.ty; } else { require_fit(ctx, item, a, element); }
            check_stored_view(ctx, received(ctx, a, element), ctx.nodes[item].token);
            if root_depth(ctx, content_root(ctx, received(ctx, a, element))) >= root_depth(ctx, root) { root = content_root(ctx, received(ctx, a, element)); }
            count++; e = ctx.edges[e].next;
        }
    }
    if element < 0 { error_at(ctx, x.token, "empty array needs an element type"); }
    var mode = 0; if ctx.types[element].size_class > 0 { mode = 1; }
    let ty = array_type(ctx, element, mode, count);
    if ty < 0 { error_at(ctx, x.token, "invalid array element type"); }
    return Value { ty: ty, held: root };
}
recursive fn check_construct(ctx: Compiler&, n: i64, scope: i64) -> Value {
    let name = ctx.nodes[n].a;
    if ctx.nodes[name].tag != N_NAME { error_at(ctx, ctx.nodes[name].token, "not implemented yet: variant constructors"); }
    let symbol = lookup(ctx, name, scope, 1);
    if symbol < 0 { error_at(ctx, ctx.nodes[name].token, "unknown constructor"); }
    let type_name = node(ctx, N_TYPE, ctx.nodes[name].token);
    ctx.nodes[type_name].ns = ctx.nodes[name].ns; ctx.nodes[type_name].flags = ctx.nodes[name].flags;
    ctx.nodes[type_name].first = ctx.nodes[name].first;
    let ty = resolve_type(ctx, type_name, scope);
    if ctx.types[ty].kind != T_STRUCT { error_at(ctx, ctx.nodes[n].token, "constructor needs a struct type"); }
    var f = ctx.types[ty].first; var e = ctx.nodes[n].first;
    ctx.nodes[n].first = -1; ctx.nodes[n].last = -1;
    var root = -1;
    while f >= 0 {
        let field = ctx.edges[f].value;
        var init = -1;
        if e >= 0 {
            let written = ctx.edges[e].value;
            if ctx.nodes[written].flags == 0 || same_name(ctx, ctx.nodes[written].token, ctx.fielddefs[field].token) { init = ctx.nodes[written].a; ctx.nodes[written].symbol = field; e = ctx.edges[e].next; }
        }
        var init_scope = scope;
        if init < 0 {
            if ctx.fielddefs[field].initializer < 0 { error_at(ctx, ctx.nodes[n].token, str("missing field '", ctx.tokens[ctx.fielddefs[field].token].text, "'")); }
            init = clone_tree(ctx, ctx.fielddefs[field].initializer);
            init_scope = ctx.types[ty].scope;
        }
        let added = node(ctx, N_INIT, ctx.fielddefs[field].token); ctx.nodes[added].a = init; ctx.nodes[added].symbol = field; child(ctx, n, added);
        let v = check_expression(ctx, init, init_scope, ctx.fielddefs[field].ty); require_fit(ctx, init, v, ctx.fielddefs[field].ty);
        let stored = received(ctx, v, ctx.fielddefs[field].ty);
        check_stored_view(ctx, stored, ctx.nodes[init].token);
        if contains_view(ctx, stored.ty) && root_depth(ctx, content_root(ctx, stored)) >= root_depth(ctx, root) { root = content_root(ctx, stored); }
        f = ctx.edges[f].next;
    }
    if e >= 0 { error_at(ctx, ctx.nodes[ctx.edges[e].value].token, "unknown or out-of-order field initializer"); }
    return Value { ty: ty, held: root, fields: ctx.nodes[n].first };
}
recursive fn check_block(ctx: Compiler&, n: i64, parent: i64, expected: i64) -> Value {
    let scope = scope_new(ctx, parent, ctx.checking_function);
    ctx.nodes[n].symbol = scope;
    var declaration = ctx.nodes[n].first;
    while declaration >= 0 {
        let d = ctx.edges[declaration].value;
        if ctx.nodes[d].tag == N_FUNCTION { ctx.nodes[d].symbol = register_symbol(ctx, ctx.nodes[d].token, ctx.nodes[d].ns, 3, d, scope, 0); }
        declaration = ctx.edges[declaration].next;
    }
    var e = ctx.nodes[n].first;
    var terminated = false;
    while e >= 0 { let statement = check_expression(ctx, ctx.edges[e].value, scope, -1); if ctx.types[statement.ty].kind == T_BOTTOM { terminated = true; } e = ctx.edges[e].next; }
    var result = Value {};
    if ctx.nodes[n].a >= 0 { result = check_expression(ctx, ctx.nodes[n].a, scope, expected); }
    else if terminated { result.ty = intern_type(ctx, Type { kind: T_BOTTOM }); }
    if contains_view(ctx, result.ty) && content_root(ctx, result) >= 0 && scope_contains(ctx, scope, ctx.symbols[content_root(ctx, result)].scope) { error_at(ctx, ctx.nodes[n].token, "block result refers to a local or temporary that has ended"); }
    return result;
}
recursive fn check_binding(ctx: Compiler&, n: i64, scope: i64) {
    if child_count(ctx, n) != 1 { error_at(ctx, ctx.nodes[n].token, "not implemented yet: multiple bindings"); }
    let name = first_child(ctx, n);
    var s = ctx.nodes[name].symbol;
    if scope != 0 { s = register_symbol(ctx, ctx.nodes[name].token, ctx.nodes[name].ns, 2, n, scope, ctx.nodes[n].flags); ctx.nodes[name].symbol = s; }
    var declared = -1;
    if ctx.nodes[n].a >= 0 { declared = resolve_type(ctx, ctx.nodes[n].a, scope); }
    let init = first_child(ctx, ctx.nodes[n].b);
    if init < 0 {
        if declared < 0 { error_at(ctx, ctx.nodes[n].token, "uninitialized variable needs a type"); }
        ctx.symbols[s].ty = declared; return;
    }
    let raw = check_expression(ctx, init, scope, declared);
    if ctx.types[raw.ty].kind == T_BOTTOM || raw.ty == 0 { error_at(ctx, ctx.nodes[init].token, "initializer has no value"); }
    var v = decay(ctx, raw);
    if declared < 0 {
        declared = v.ty;
        if ctx.nodes[n].flags & 16 != 0 || ((v.place >= 0 || ctx.types[raw.ty].kind == T_REFERENCE) && ctx.types[v.ty].size_class > 0) || (ctx.nodes[init].tag == N_UNARY && ctx.tokens[ctx.nodes[init].token].text == "&") { declared = reference_type(ctx, v.ty, v.readonly); }
    }
    if ctx.types[declared].kind == T_REFERENCE { v = raw; }
    if ctx.types[declared].size_class > 0 && (raw.place >= 0 || ctx.types[raw.ty].kind == T_REFERENCE) { error_at(ctx, ctx.nodes[init].token, "nonfixed storage is not copied implicitly; use copy or bind by reference"); }
    require_fit(ctx, init, v, declared);
    ctx.symbols[s].ty = declared; ctx.symbols[s].state = 2; ctx.symbols[s].root = v.root;
    ctx.symbols[s].held = v.held;
    ctx.symbols[s].fields = raw.fields;
    ctx.symbols[s].growshrink = raw.growshrink;
    ctx.symbols[s].inexact = raw.inexact;
    ctx.symbols[s].readonly = syntax_readonly(ctx, ctx.nodes[n].a, scope) || ((ctx.types[declared].kind == T_REFERENCE || ctx.types[declared].kind == T_SLICE) && raw.readonly);
    if ctx.nodes[n].flags & 1 == 0 && ctx.nodes[init].tag == N_MEMBER && ctx.tokens[ctx.nodes[init].token].text == "len" {
        let receiver = ctx.nodes[init].a;
        let owner = decay(ctx, value_of(ctx, receiver));
        if owner.root >= 0 && ctx.types[owner.ty].kind == T_ARRAY && ctx.types[owner.ty].flags == 4 && stable_array_path(ctx, receiver) { ctx.symbols[s].length_root = owner.root; }
    }
    if ctx.nodes[n].flags & 1 == 0 && v.known { ctx.symbols[s].known = true; ctx.symbols[s].bits = v.bits; }
    store_lifetime(ctx, Value { ty: declared, root: v.root, held: v.held }, s, ctx.nodes[n].token);
}
recursive fn check_assignment(ctx: Compiler&, n: i64, scope: i64) {
    let x = ctx.nodes[n];
    let raw = check_expression(ctx, x.a, scope, -1); let dest = decay(ctx, raw);
    if dest.place < 0 { error_at(ctx, x.token, "assignment requires a stored value"); }
    let direct = ctx.nodes[x.a].tag == N_NAME && ctx.types[raw.ty].kind != T_REFERENCE;
    var slice_slot = direct && ctx.types[raw.ty].kind == T_SLICE;
    if ctx.types[raw.ty].kind == T_SLICE && (ctx.nodes[x.a].tag == N_MEMBER || ctx.nodes[x.a].tag == N_INDEX) { slice_slot = !ctx.nodes[ctx.nodes[x.a].a].readonly; }
    if dest.readonly && !direct && !slice_slot { error_at(ctx, x.token, "cannot write through read-only storage"); }
    if direct && ctx.symbols[dest.place].flags & 1 == 0 { error_at(ctx, x.token, "cannot reassign a let binding"); }
    if ctx.types[dest.ty].size_class == 1 { error_at(ctx, x.token, "cannot replace a variable-size value"); }
    if ctx.tokens[x.token].text == ".=" {
        if ctx.types[raw.ty].kind != T_REFERENCE { error_at(ctx, x.token, ".= rebinds references only"); }
        error_at(ctx, x.token, "not implemented yet: reference rebinding analysis");
    }
    if ctx.types[dest.ty].size_class == 2 { error_at(ctx, x.token, "not implemented yet: whole resizable assignment analysis"); }
    if ctx.symbols[dest.place].flags & 32 != 0 { error_at(ctx, x.token, "cannot write a by-value iteration binding"); }
    if ctx.nodes[x.a].tag == N_MEMBER && ctx.nodes[x.a].symbol >= 0 && ctx.fielddefs[ctx.nodes[x.a].symbol].flags & 1 != 0 { error_at(ctx, x.token, "cannot reassign a let field"); }
    if x.b >= 0 {
        if ctx.nodes[x.a].tag != N_NAME { ctx.nodes[x.b].standalone = false; }
        let rhs = check_expression(ctx, x.b, scope, dest.ty);
        if !direct { check_stored_view(ctx, received(ctx, rhs, dest.ty), x.token); }
        require_fit(ctx, x.b, rhs, dest.ty);
        store_lifetime(ctx, received(ctx, rhs, dest.ty), if direct { dest.place } else { dest.root }, x.token);
        if direct && ctx.types[dest.ty].kind == T_SLICE && root_depth(ctx, rhs.root) >= root_depth(ctx, ctx.symbols[dest.place].root) { ctx.symbols[dest.place].root = rhs.root; }
        if direct { ctx.symbols[dest.place].growshrink = ctx.symbols[dest.place].growshrink || rhs.growshrink; }
    } else if ctx.types[dest.ty].kind != T_INTEGER { error_at(ctx, x.token, "increment requires an integer"); }
    ctx.symbols[dest.place].known = false;
    // Updating an aggregate invalidates a remembered constructor mapping.
    if ctx.types[dest.ty].kind == T_STRUCT { ctx.symbols[dest.place].fields = -1; }
}
recursive fn check_loop(ctx: Compiler&, n: i64, scope: i64) -> Value {
    let x = ctx.nodes[n];
    ctx.loop_depth++;
    if x.tag == N_WHILE { require_fit(ctx, x.a, decay(ctx, check_expression(ctx, x.a, scope, -1)), boolean_type(ctx)); check_expression(ctx, x.b, scope, 0); }
    else if x.tag == N_LOOP || x.tag == N_EARLY { check_expression(ctx, x.a, scope, 0); }
    else {
        let inner = scope_new(ctx, scope, ctx.checking_function);
        var binding_type = default_integer(ctx);
        var root = -1;
        if ctx.nodes[x.b].tag == N_RANGE {
            let a = decay(ctx, check_expression(ctx, ctx.nodes[x.b].a, scope, -1));
            let b = decay(ctx, check_expression(ctx, ctx.nodes[x.b].b, scope, -1));
            binding_type = merge_numeric(ctx, a, b, x.token);
            ctx.nodes[x.b].ty = binding_type;
        } else {
            let iterable = decay(ctx, check_expression(ctx, x.b, scope, -1));
            binding_type = element_type(ctx, iterable.ty); root = iterable.root;
            if binding_type < 0 { binding_type = iterable.ty; if ctx.types[binding_type].kind != T_INTEGER { error_at(ctx, x.token, "for requires an integer or array"); } }
            else if x.flags & 1 != 0 || ctx.types[binding_type].size_class > 0 { binding_type = reference_type(ctx, binding_type, iterable.readonly); }
        }
        let s = register_symbol(ctx, ctx.nodes[x.a].token, -1, 2, n, inner, if ctx.types[binding_type].kind == T_REFERENCE { 0 } else { 32 });
        ctx.symbols[s].ty = binding_type; ctx.symbols[s].root = root; ctx.symbols[s].state = 2; ctx.nodes[x.a].symbol = s;
        if x.first >= 0 {
            let index = ctx.edges[x.first].value; let i = register_symbol(ctx, ctx.nodes[index].token, -1, 2, n, inner, 0);
            ctx.symbols[i].ty = default_integer(ctx); ctx.symbols[i].state = 2; ctx.nodes[index].symbol = i;
        }
        check_expression(ctx, x.c, inner, 0);
    }
    ctx.loop_depth--;
    return Value {};
}
fn check_return(ctx: Compiler&, n: i64, raw: Value, spec: i64, token: i64) {
    var v = raw;
    let own_local = v.place >= 0 && ctx.scopes[ctx.symbols[v.place].scope].function == spec && ctx.types[ctx.symbols[v.place].ty].kind != T_REFERENCE;
    if ctx.specializations[spec].result < 0 {
        let value = decay(ctx, v);
        var inferred = value.ty;
        let explicit_ref = n >= 0 && ctx.nodes[n].tag == N_UNARY && ctx.tokens[ctx.nodes[n].token].text == "&";
        if explicit_ref || (ctx.types[value.ty].size_class > 0 && (ctx.types[v.ty].kind == T_REFERENCE || (v.place >= 0 && !own_local))) { inferred = reference_type(ctx, value.ty, v.readonly); }
        ctx.specializations[spec].result = inferred;
    }
    let target = ctx.specializations[spec].result;
    if ctx.types[target].kind != T_REFERENCE { v = decay(ctx, v); }
    if ctx.types[target].size_class > 0 && v.place >= 0 && !own_local { error_at(ctx, token, "returning nonfixed storage by value requires an explicit copy"); }
    let decl = ctx.symbols[ctx.specializations[spec].symbol].decl;
    if ctx.specializations[spec].in_cycle && ctx.nodes[decl].b < 0 && ctx.types[target].kind == T_REFERENCE { error_at(ctx, token, "a recursive cycle needs an explicit reference result type"); }
    if n >= 0 { require_fit(ctx, n, v, target); }
    else if target != 0 { error_at(ctx, token, "missing return value"); }
    let root = content_root(ctx, received(ctx, v, target));
    if contains_view(ctx, target) && root >= 0 && ctx.scopes[ctx.symbols[root].scope].function == spec { error_at(ctx, token, "returned reference refers to a local"); }
    if contains_view(ctx, target) && ctx.specializations[spec].result_used {
        if root_depth(ctx, root) > root_depth(ctx, ctx.specializations[spec].result_root) || (v.growshrink && !ctx.specializations[spec].result_growshrink) { error_at(ctx, token, "return weakens the provenance already used by a recursive call"); }
        if root != ctx.specializations[spec].result_root && v.inexact && v.place >= 0 && ctx.symbols[v.place].scope != 0 && ctx.scopes[ctx.symbols[v.place].scope].function != spec { error_at(ctx, token, "not implemented yet: recursive result gains a captured inexact reference"); }
    }
    // Each return must be safe on its own. A merged result is bounded by
    // the innermost owner, rather than forcing all paths to one arena.
    if root >= 0 && root_depth(ctx, root) >= root_depth(ctx, ctx.specializations[spec].result_root) { ctx.specializations[spec].result_root = root; }
    if !ctx.specializations[spec].return_seen { ctx.specializations[spec].result_fields = v.fields; }
    else if ctx.specializations[spec].result_fields != v.fields { ctx.specializations[spec].result_fields = -1; }
    ctx.specializations[spec].return_seen = true;
    ctx.specializations[spec].result_readonly = ctx.specializations[spec].result_readonly || v.readonly;
    ctx.specializations[spec].result_growshrink = ctx.specializations[spec].result_growshrink || v.growshrink;
    ctx.specializations[spec].result_inexact = ctx.specializations[spec].result_inexact || v.inexact;
}

recursive fn check_call(ctx: Compiler&, n: i64, scope: i64) -> Value {
    let callee = ctx.nodes[n].a;
    let member = ctx.nodes[callee].tag == N_MEMBER;
    let name = ctx.tokens[ctx.nodes[callee].token].text;
    var receiver = Value {};
    let array_method = member && (name == "push" || name == "append" || name == "resize" || name == "clear" || name == "pop");
    if array_method { receiver = check_expression(ctx, ctx.nodes[callee].a, scope, -1); }
    // Builtin arguments are checked with their construction destination.
    if member && element_type(ctx, receiver.ty) >= 0 {
        let array = decay(ctx, receiver); let element = element_type(ctx, array.ty);
        if name == "push" || name == "append" || name == "resize" || name == "clear" || name == "pop" {
            if (name == "resize" || name == "clear" || name == "pop") && !ctx.nodes[n].standalone { error_at(ctx, ctx.nodes[n].token, "not implemented yet: shrinking within a larger expression"); }
            if array.readonly { error_at(ctx, ctx.nodes[callee].token, "cannot mutate a read-only array"); }
            if ctx.types[array.ty].kind != T_ARRAY || ctx.types[array.ty].flags < 2 { error_at(ctx, ctx.nodes[callee].token, "operation requires a growable array"); }
            var e = ctx.nodes[n].first;
            if name == "push" {
                if child_count(ctx, n) != 1 { error_at(ctx, ctx.nodes[n].token, "push takes one element"); }
                let arg = ctx.edges[e].value; let v = check_expression(ctx, arg, scope, element); require_fit(ctx, arg, v, element); store_in_array(ctx, received(ctx, v, element), array, scope, ctx.nodes[n].token);
                ctx.nodes[n].symbol = -10; return Value { ty: reference_type(ctx, element, false), root: array.root, place: array.place };
            }
            if name == "append" {
                if child_count(ctx, n) != 1 { error_at(ctx, ctx.nodes[n].token, "append takes one array or slice"); }
                let arg = ctx.edges[e].value; let v = decay(ctx, check_expression(ctx, arg, scope, array_type(ctx, element, 3, -1)));
                if element_type(ctx, v.ty) != element { error_at(ctx, ctx.nodes[arg].token, "append element type mismatch"); }
                if ctx.types[v.ty].size_class == 2 && v.place < 0 { error_at(ctx, ctx.nodes[arg].token, "not implemented yet: constructed append result analysis"); }
                store_in_array(ctx, Value { ty: element, root: v.held, held: v.held }, array, scope, ctx.nodes[n].token); ctx.nodes[n].symbol = -11;
            } else if name == "resize" {
                if child_count(ctx, n) < 1 || child_count(ctx, n) > 2 { error_at(ctx, ctx.nodes[n].token, "resize takes a length and optional fill"); }
                let count = ctx.edges[e].value; require_fit(ctx, count, decay(ctx, check_expression(ctx, count, scope, -1)), default_integer(ctx)); e = ctx.edges[e].next;
                var balanced = false;
                if ctx.types[array.ty].flags == 4 && ctx.nodes[count].tag == N_NAME {
                    let mark = ctx.symbols[ctx.nodes[count].symbol];
                    balanced = mark.length_root == array.root && ctx.scopes[mark.scope].function == ctx.checking_function && stable_array_path(ctx, ctx.nodes[callee].a);
                }
                shrink_array(ctx, array, scope, ctx.nodes[n].token, balanced);
                if e >= 0 { let fill = ctx.edges[e].value; require_fit(ctx, fill, check_expression(ctx, fill, scope, element), element); }
                ctx.nodes[n].symbol = -12;
            } else if name == "clear" { shrink_array(ctx, array, scope, ctx.nodes[n].token, false); if child_count(ctx, n) != 0 { error_at(ctx, ctx.nodes[n].token, "clear takes no arguments"); } ctx.nodes[n].symbol = -13; }
            else { shrink_array(ctx, array, scope, ctx.nodes[n].token, false); if child_count(ctx, n) != 0 { error_at(ctx, ctx.nodes[n].token, "pop takes no arguments"); } ctx.nodes[n].symbol = -14; return Value { ty: element }; }
            return Value {};
        }
    }
    if !member && (name == "print" || name == "str" || name == "format") {
        var e = ctx.nodes[n].first;
        if name == "format" {
            if e < 0 { error_at(ctx, ctx.nodes[n].token, "format needs a destination"); }
            let out = decay(ctx, check_expression(ctx, ctx.edges[e].value, scope, -1));
            if element_type(ctx, out.ty) != integer_type(ctx, 1, true) || out.readonly { error_at(ctx, ctx.nodes[n].token, "format needs a writable byte builder"); }
            e = ctx.edges[e].next;
        }
        while e >= 0 { check_expression(ctx, ctx.edges[e].value, scope, -1); e = ctx.edges[e].next; }
        if name == "str" { ctx.nodes[n].symbol = -21; return Value { ty: array_type(ctx, integer_type(ctx, 1, true), 3, -1) }; }
        if name == "print" { ctx.nodes[n].symbol = -20; } else { ctx.nodes[n].symbol = -22; }
        return Value {};
    }
    if !member && (name == "assert" || name == "abort" || name == "exit" || name == "copy" || name == "default") {
        if name == "default" {
            let t = first_child(ctx, callee); if t < 0 { error_at(ctx, ctx.nodes[n].token, "default requires a type argument"); }
            let ty = resolve_type(ctx, t, scope); ctx.nodes[n].symbol = -27; return Value { ty: ty };
        }
        if child_count(ctx, n) != 1 { error_at(ctx, ctx.nodes[n].token, "builtin takes one argument"); }
        let arg = first_child(ctx, n); let raw = check_expression(ctx, arg, scope, -1); let v = decay(ctx, raw);
        if name == "assert" { require_fit(ctx, arg, v, boolean_type(ctx)); ctx.nodes[n].symbol = -23; return Value {}; }
        if name == "copy" { if (v.place < 0 && ctx.types[raw.ty].kind != T_REFERENCE) || ctx.types[v.ty].kind == T_SLICE { error_at(ctx, ctx.nodes[arg].token, "copy requires an owned stored value or its reference"); } ctx.nodes[n].symbol = -26; var result = v; result.place = -1; result.readonly = false; return result; }
        if name == "exit" { require_fit(ctx, arg, v, default_integer(ctx)); ctx.nodes[n].symbol = -25; }
        else { if element_type(ctx, v.ty) != integer_type(ctx, 1, true) { error_at(ctx, ctx.nodes[n].token, "abort requires text"); } ctx.nodes[n].symbol = -24; }
        return Value { ty: intern_type(ctx, Type { kind: T_BOTTOM }) };
    }
    if member {
        // Lower UFCS once to a normal call. The receiver stays first, so both
        // type checking and generated evaluation keep source argument order.
        let function = node(ctx, N_NAME, ctx.nodes[callee].token);
        ctx.nodes[function].ns = ctx.nodes[callee].ns;
        ctx.nodes[function].first = ctx.nodes[callee].first; ctx.nodes[function].last = ctx.nodes[callee].last;
        let edge = ctx.edges.len;
        ctx.edges.push(Edge { value: ctx.nodes[callee].a, next: ctx.nodes[n].first });
        ctx.nodes[n].first = edge; if ctx.nodes[n].last < 0 { ctx.nodes[n].last = edge; }
        ctx.nodes[n].a = function;
        if array_method { ctx.nodes[n].flags |= 256; }
        return check_call(ctx, n, scope);
    }
    let s = lookup(ctx, callee, scope, 3);
    if s < 0 { error_at(ctx, ctx.nodes[callee].token, str("unknown function '", name, "'")); }
    if ctx.symbols[s].scope != 0 && ctx.symbols[s].state != 2 { error_at(ctx, ctx.nodes[callee].token, "nested function called before its declaration is reached"); }
    let concrete = resolve_overload(ctx, s, n, scope);
    let spec = check_function(ctx, concrete, n, scope);
    ctx.checked_calls.push(CheckedCall { node: n, scope: scope, callee: spec });
    var effect = ctx.specializations[spec].shrinks;
    while effect >= 0 {
        let shrink = ctx.shrinks[ctx.edges[effect].value];
        if !shrink.balanced && !ctx.nodes[n].standalone { error_at(ctx, ctx.nodes[n].token, "not implemented yet: call that shrinks within a larger expression"); }
        check_shrink(ctx, shrink.root, element_type(ctx, ctx.symbols[shrink.root].ty), scope, ctx.nodes[n].token, shrink.balanced, true);
        effect = ctx.edges[effect].next;
    }
    ctx.nodes[n].symbol = spec;
    return Value { ty: ctx.specializations[spec].result, root: ctx.specializations[spec].result_root, readonly: ctx.specializations[spec].result_readonly, growshrink: ctx.specializations[spec].result_growshrink, inexact: ctx.specializations[spec].result_inexact, held: ctx.specializations[spec].result_root, fields: ctx.specializations[spec].result_fields };
}
recursive fn check_function(ctx: Compiler&, symbol: i64, call: i64, caller_scope: i64) -> i64 {
    let decl = ctx.symbols[symbol].decl;
    if ctx.nodes[decl].first >= 0 { error_at(ctx, ctx.nodes[decl].token, "not implemented yet: generic functions"); }
    let params = ctx.nodes[decl].a;
    if call >= 0 && child_count(ctx, params) != child_count(ctx, call) { error_at(ctx, ctx.nodes[call].token, "argument count mismatch"); }
    var actual = -1;
    if call >= 0 { actual = ctx.nodes[call].first; }
    var p = ctx.nodes[params].first;
    while p >= 0 {
        let parameter = ctx.edges[p].value;
        if ctx.nodes[parameter].a < 0 { error_at(ctx, ctx.nodes[parameter].token, "not implemented yet: inferred parameter types"); }
        let expected = resolve_type(ctx, ctx.nodes[parameter].a, ctx.symbols[symbol].scope);
        if actual >= 0 {
            let arg = ctx.edges[actual].value;
            let v = if ctx.nodes[call].flags & 512 != 0 || (actual == ctx.nodes[call].first && ctx.nodes[call].flags & 256 != 0) { value_of(ctx, arg) } else { check_expression(ctx, arg, caller_scope, expected) };
            require_fit(ctx, arg, v, expected);
            if ctx.types[expected].size_class > 0 && v.place >= 0 { error_at(ctx, ctx.nodes[arg].token, "nonfixed value parameter needs a fresh value or explicit copy"); }
            actual = ctx.edges[actual].next;
        }
        p = ctx.edges[p].next;
    }
    // A provisional record closes recursion before the body is checked.
    for existing, i in ctx.specializations {
        if existing.symbol == symbol {
            var same = true;
            var pe = ctx.nodes[existing.parameters].first;
            var ae = -1; if call >= 0 { ae = ctx.nodes[call].first; }
            while pe >= 0 && ae >= 0 {
                let ps = ctx.symbols[ctx.nodes[ctx.edges[pe].value].symbol];
                let av = value_of(ctx, ctx.edges[ae].value);
                if ps.growshrink != av.growshrink && !(existing.state == 1 && !av.growshrink) { same = false; }
                if ctx.types[ps.ty].kind == T_REFERENCE || ctx.types[ps.ty].kind == T_SLICE {
                    if ps.root != av.root { same = false; }
                    if ctx.types[ps.ty].flags & 1 == 0 && ps.readonly != av.readonly { same = false; }
                } else if contains_view(ctx, ps.ty) && ps.held != av.held {
                    // A holder passed by value can contain longer-lived views
                    // than the body's conservative bound, including literals.
                    // Array references still retain their storage identity.
                    if existing.state != 1 || !root_outlives(ctx, av.held, ps.held) { same = false; }
                }
                pe = ctx.edges[pe].next; ae = ctx.edges[ae].next;
            }
            if !same && existing.state != 1 { continue; }
            if !same && existing.state == 1 { error_at(ctx, ctx.nodes[call].token, "not implemented yet: recursive arguments broaden reference provenance"); }
            if existing.state == 1 && ctx.nodes[decl].flags & 1 == 0 { error_at(ctx, ctx.nodes[decl].token, "recursive cycle requires recursive fn"); }
            if existing.state == 1 {
                var cycle = ctx.checking_function;
                while cycle >= 0 {
                    ctx.specializations[cycle].in_cycle = true;
                    let cycle_decl = ctx.symbols[ctx.specializations[cycle].symbol].decl;
                    if ctx.nodes[cycle_decl].b < 0 && ctx.specializations[cycle].result >= 0 && ctx.types[ctx.specializations[cycle].result].kind == T_REFERENCE { error_at(ctx, ctx.nodes[cycle_decl].token, "a recursive cycle needs an explicit reference result type"); }
                    if cycle == i { break; } cycle = ctx.specializations[cycle].caller;
                }
            }
            if existing.state == 1 && existing.result < 0 { ctx.specializations[i].result = 0; }
            if existing.state == 1 && contains_view(ctx, ctx.specializations[i].result) {
                if !existing.return_seen { error_at(ctx, ctx.nodes[call].token, "not implemented yet: recursive reference result before a checked return"); }
                ctx.specializations[i].result_used = true;
            }
            return i;
        }
    }
    var result = -1;
    if ctx.nodes[decl].b >= 0 {
        if child_count(ctx, ctx.nodes[decl].b) > 1 { error_at(ctx, ctx.nodes[decl].token, "not implemented yet: multiple return values"); }
        result = resolve_type(ctx, first_child(ctx, ctx.nodes[decl].b), ctx.symbols[symbol].scope);
    }
    let id = ctx.specializations.len;
    let scope = scope_new(ctx, ctx.symbols[symbol].scope, id);
    if ctx.scopes[scope].depth <= ctx.scopes[caller_scope].depth { ctx.scopes[scope].depth = ctx.scopes[caller_scope].depth + 1; }
    let body = clone_tree(ctx, ctx.nodes[decl].c);
    let specialized_params = clone_tree(ctx, params);
    ctx.specializations.push(Specialization { symbol: symbol, body: body, scope: scope, result: result, state: 1, parameters: specialized_params, caller: ctx.checking_function });
    p = ctx.nodes[specialized_params].first; actual = -1; if call >= 0 { actual = ctx.nodes[call].first; }
    while p >= 0 {
        let param = ctx.edges[p].value;
        let s = register_symbol(ctx, ctx.nodes[param].token, -1, 2, param, scope, ctx.nodes[param].flags);
        let ty = resolve_type(ctx, ctx.nodes[param].a, scope); ctx.symbols[s].ty = ty; ctx.symbols[s].state = 2;
        ctx.symbols[s].readonly = syntax_readonly(ctx, ctx.nodes[param].a, scope);
        if actual >= 0 {
            let argument = ctx.nodes[ctx.edges[actual].value];
            ctx.symbols[s].root = argument.root; ctx.symbols[s].held = argument.held; ctx.symbols[s].fields = argument.fields;
            // A by-value holder's fields may differ in another activation.
            if ctx.types[ty].kind != T_REFERENCE && ctx.types[ty].kind != T_SLICE && contains_view(ctx, ty) { ctx.symbols[s].fields = -1; }
            ctx.symbols[s].growshrink = argument.growshrink;
            ctx.symbols[s].inexact = argument.inexact;
            if ctx.types[ty].kind == T_REFERENCE || ctx.types[ty].kind == T_SLICE { ctx.symbols[s].readonly = ctx.symbols[s].readonly || argument.readonly; }
            actual = ctx.edges[actual].next;
        }
        ctx.nodes[param].symbol = s; p = ctx.edges[p].next;
    }
    if ctx.nodes[decl].flags & 2 == 0 {
        let previous = ctx.checking_function; let previous_loop = ctx.loop_depth;
        ctx.checking_function = id; ctx.loop_depth = 0;
        let tail = check_expression(ctx, body, scope, result);
        if ctx.types[tail.ty].kind != T_BOTTOM { check_return(ctx, ctx.nodes[body].a, tail, id, ctx.nodes[decl].token); }
        ctx.checking_function = previous; ctx.loop_depth = previous_loop;
    } else if result < 0 { ctx.specializations[id].result = 0; }
    if ctx.specializations[id].result < 0 { ctx.specializations[id].result = 0; }
    ctx.specializations[id].state = 2;
    return id;
}
fn check_program(ctx: Compiler&) {
    for n in ctx.declarations { if ctx.nodes[n].tag == N_FUNCTION && ctx.tokens[ctx.nodes[n].token].text == "format" { error_at(ctx, ctx.nodes[n].token, "not implemented yet: custom formatting dispatch"); } }
    initialization_order(ctx, 0);
    for n in ctx.global_order { check_binding(ctx, n, 0); }
    var main_symbol = -1;
    for s, i in ctx.symbols { if s.scope == 0 && s.kind == 3 && s.ns < 0 && ctx.tokens[s.token].text == "main" { main_symbol = i; } }
    if main_symbol < 0 { write_stderr("error: no root-file main function\n"); exit(1); }
    check_function(ctx, main_symbol, -1, 0);
    // Fully annotated, non-generic top-level bodies are checked even unused.
    // Generic and nested declarations remain demand-driven.
    let original_symbols = ctx.symbols.len;
    for i in original_symbols {
        let s = ctx.symbols[i];
        if s.scope != 0 { continue; }
        if s.kind == 1 {
            let d = ctx.nodes[s.decl];
            if d.tag == N_ALIAS { resolve_type(ctx, d.a, 0); }
            else if d.tag == N_STRUCT && ctx.nodes[d.a].first < 0 { struct_type(ctx, s.decl, 0); }
        }
        if s.kind == 3 && ctx.nodes[s.decl].first < 0 && ctx.nodes[s.decl].flags & 2 == 0 {
            var typed = true;
            var e = ctx.nodes[ctx.nodes[s.decl].a].first;
            while e >= 0 { if ctx.nodes[ctx.edges[e].value].a < 0 { typed = false; } e = ctx.edges[e].next; }
            if typed { check_function(ctx, i, -1, 0); }
        }
    }
    // Replay after bodies have closed, including recursive back edges whose
    // eventual shrink effects were not known on the first traversal.
    var changed = true;
    while changed {
        let before = ctx.effect_revision;
        for call in ctx.checked_calls {
            ctx.checking_function = ctx.scopes[call.scope].function;
            var effect = ctx.specializations[call.callee].shrinks;
            while effect >= 0 {
                let shrink = ctx.shrinks[ctx.edges[effect].value];
                if !shrink.balanced && !ctx.nodes[call.node].standalone { error_at(ctx, ctx.nodes[call.node].token, "not implemented yet: call that shrinks within a larger expression"); }
                check_shrink(ctx, shrink.root, element_type(ctx, ctx.symbols[shrink.root].ty), call.scope, ctx.nodes[call.node].token, shrink.balanced, true);
                effect = ctx.edges[effect].next;
            }
        }
        changed = ctx.effect_revision != before;
    }
    ctx.checking_function = -1;
    for call in ctx.checked_calls {
        let caller = ctx.scopes[call.scope].function;
        ctx.visit++;
        if caller >= 0 && reaches_function(ctx, call.callee, caller, ctx.visit) {
            for local in ctx.symbols {
                if local.kind == 2 && local.ty >= 0 && ctx.scopes[local.scope].function == caller && ctx.types[local.ty].size_class > 0 && scope_contains(ctx, local.scope, call.scope) { error_at(ctx, ctx.nodes[call.node].token, "call into a recursive cycle while nonfixed local is in scope"); }
            }
        }
    }
}
recursive fn stable_array_path(ctx: Compiler&, n: i64) -> bool {
    let x = ctx.nodes[n];
    if x.tag == N_NAME { let s = ctx.symbols[x.symbol]; return ctx.types[s.ty].kind != T_REFERENCE || s.flags & 1 == 0; }
    if x.tag == N_MEMBER { return ctx.types[x.ty].kind != T_REFERENCE && stable_array_path(ctx, x.a); }
    return false;
}
recursive fn reaches_function(ctx: Compiler&, from: i64, target: i64, visit: i64) -> bool {
    if from == target { return true; }
    if ctx.specializations[from].visit == visit { return false; }
    ctx.specializations[from].visit = visit;
    for call in ctx.checked_calls { if ctx.scopes[call.scope].function == from && reaches_function(ctx, call.callee, target, visit) { return true; } }
    return false;
}
recursive fn initialization_order(ctx: Compiler&, file: i64) {
    if ctx.sources[file].visited != 0 { return; }
    ctx.sources[file].visited = 1;
    for n in ctx.declarations {
        if ctx.tokens[ctx.nodes[n].token].file == file && ctx.nodes[n].tag == N_IMPORT { initialization_order(ctx, ctx.nodes[n].a); }
    }
    for n in ctx.declarations {
        if ctx.tokens[ctx.nodes[n].token].file == file && ctx.nodes[n].tag == N_BIND { ctx.global_order.push(n); }
    }
    ctx.sources[file].visited = 2;
}
