// Canonical semantic types and lexical symbols. Syntax type expressions stay
// immutable; nominal declarations are resolved lazily with cycle detection.
const T_VOID = 0;
const T_INTEGER = 1;
const T_FLOAT = 2;
const T_BOOL = 3;
const T_STRUCT = 4;
const T_ARRAY = 5;
const T_SLICE = 6;
const T_REFERENCE = 7;
const T_VARINT = 8;
const T_BOTTOM = 9;

struct Type {
    kind: i64,
    element: i64 = -1,
    count: i64 = 0,
    flags: i64 = 0,
    decl: i64 = -1,
    bytes: i64 = 0,
    size_class: i64 = 0,
    first: i64 = -1,
    state: i64 = 0,
    template: i64 = -1,
    arguments: i64 = -1,
    scope: i64 = 0,
    view_visit: i64 = 0,
}
struct Field {
    token: i64,
    ty: i64,
    initializer: i64,
    flags: i64,
    offset: i64,
}
struct Scope { parent: i64, function: i64, depth: i64 }
struct Symbol {
    token: i64,
    ns: i64,
    kind: i64,
    decl: i64,
    scope: i64,
    ty: i64 = -1,
    state: i64 = 0,
    flags: i64 = 0,
    root: i64 = -1,
    known: bool = false,
    bits: u64 = 0,
    held: i64 = -1,
    fields: i64 = -1,
    length_root: i64 = -1,
    readonly: bool = false,
    growshrink: bool = false,
    inexact: bool = false,
    capture_limit: i64 = -1,
}


fn same_name(ctx: Compiler&, a: i64, b: i64) -> bool {
    if a < 0 || b < 0 { return a == b; }
    return ctx.tokens[a].text == ctx.tokens[b].text;
}
fn intern_type(ctx: Compiler&, t: Type) -> i64 {
    for existing, i in ctx.types {
        if existing.kind == t.kind && existing.element == t.element && existing.count == t.count && existing.flags == t.flags && existing.decl == t.decl { return i; }
    }
    let id = ctx.types.len; ctx.types.push(t); return id;
}
fn integer_type(ctx: Compiler&, bytes: i64, unsigned: bool) -> i64 {
    var flags = 0; if unsigned { flags = 2; }
    return intern_type(ctx, Type { kind: T_INTEGER, count: bytes, flags: flags, bytes: bytes });
}
fn slice_type(ctx: Compiler&, element: i64, readonly: bool) -> i64 {
    var flags = 0; if readonly { flags = 1; }
    return intern_type(ctx, Type { kind: T_SLICE, element: element, flags: flags, bytes: 16 });
}
fn reference_type(ctx: Compiler&, element: i64, readonly: bool) -> i64 {
    var flags = 0; if readonly { flags = 1; }
    var bytes = 8; if ctx.types[element].size_class == 2 { bytes = 16; }
    return intern_type(ctx, Type { kind: T_REFERENCE, element: element, flags: flags, bytes: bytes });
}
fn array_type(ctx: Compiler&, element: i64, mode: i64, count: i64) -> i64 {
    var size_class = 0;
    var bytes = ctx.types[element].bytes * count;
    if mode == 1 || (mode == 2 && count < 0) { size_class = 1; bytes = -1; }
    if mode >= 3 { size_class = 2; bytes = -1; }
    if mode == 2 && count >= 0 {
        var prefix = 1;
        if count > 255 { prefix = 2; }
        if count > 65535 { prefix = 4; }
        if count > 4294967295 { prefix = 8; }
        bytes += prefix;
    }
    if (mode == 0 || mode == 2 || mode == 4) && ctx.types[element].size_class != 0 { return -1; }
    if ctx.types[element].size_class == 2 { return -1; }
    return intern_type(ctx, Type { kind: T_ARRAY, element: element, count: count, flags: mode, bytes: bytes, size_class: size_class });
}
fn builtin_type(ctx: Compiler&, name: const u8[:]) -> i64 {
    if name == "i8" { return integer_type(ctx, 1, false); }
    if name == "i16" { return integer_type(ctx, 2, false); }
    if name == "i32" { return integer_type(ctx, 4, false); }
    if name == "i64" { return integer_type(ctx, 8, false); }
    if name == "u8" { return integer_type(ctx, 1, true); }
    if name == "u16" { return integer_type(ctx, 2, true); }
    if name == "u32" { return integer_type(ctx, 4, true); }
    if name == "u64" { return integer_type(ctx, 8, true); }
    if name == "f32" { return intern_type(ctx, Type { kind: T_FLOAT, count: 4, bytes: 4 }); }
    if name == "f64" { return intern_type(ctx, Type { kind: T_FLOAT, count: 8, bytes: 8 }); }
    if name == "bool" { return intern_type(ctx, Type { kind: T_BOOL, bytes: 1 }); }
    if name == "varint" { return intern_type(ctx, Type { kind: T_VARINT, bytes: -1, size_class: 1 }); }
    return -1;
}
fn scope_new(ctx: Compiler&, parent: i64, function: i64) -> i64 {
    let id = ctx.scopes.len;
    var depth = 0; if parent >= 0 { depth = ctx.scopes[parent].depth + 1; }
    ctx.scopes.push(Scope { parent: parent, function: function, depth: depth });
    return id;
}
fn find_symbol(ctx: Compiler&, token: i64, ns: i64, scope: i64, kind: i64) -> i64 {
    var i = ctx.symbols.len;
    while i > 0 {
        i--;
        let s = ctx.symbols[i];
        if s.scope == scope && (kind == 0 || s.kind == kind) && same_name(ctx, s.token, token) && (scope != 0 || same_name(ctx, s.ns, ns)) { return i; }
    }
    return -1;
}
fn lookup(ctx: Compiler&, n: i64, scope: i64, kind: i64) -> i64 {
    if ctx.nodes[n].flags & 1 == 0 {
        var s = scope;
        while s > 0 {
            let id = find_symbol(ctx, ctx.nodes[n].token, -1, s, kind);
            if id >= 0 {
                let owner = ctx.scopes[scope].function;
                var visible = true;
                if kind == 2 && owner >= 0 && ctx.scopes[ctx.symbols[id].scope].function != owner {
                    let limit = ctx.symbols[ctx.specializations[owner].symbol].capture_limit;
                    if limit >= 0 && id >= limit { visible = false; }
                }
                if visible { return id; }
            }
            s = ctx.scopes[s].parent;
        }
    }
    let id = find_symbol(ctx, ctx.nodes[n].token, ctx.nodes[n].ns, 0, kind);
    if id >= 0 { return id; }
    if ctx.nodes[n].flags & 1 == 0 && ctx.nodes[n].ns >= 0 { return find_symbol(ctx, ctx.nodes[n].token, -1, 0, kind); }
    return -1;
}
fn register_symbol(ctx: Compiler&, token: i64, ns: i64, kind: i64, decl: i64, scope: i64, flags: i64) -> i64 {
    let previous = find_symbol(ctx, token, ns, scope, kind);
    if previous >= 0 && !(kind == 3 && ctx.symbols[previous].kind == 3) { error_at(ctx, token, "name already declared in this scope"); }
    let id = ctx.symbols.len;
    ctx.symbols.push(Symbol { token: token, ns: ns, kind: kind, decl: decl, scope: scope, flags: flags });
    return id;
}
fn register_declarations(ctx: Compiler&) {
    scope_new(ctx, -1, -1);
    intern_type(ctx, Type { kind: T_VOID });
    for n in ctx.declarations {
        let tag = ctx.nodes[n].tag;
        if tag == N_STRUCT || tag == N_ENUM || tag == N_ALIAS { register_symbol(ctx, ctx.nodes[n].token, ctx.nodes[n].ns, 1, n, 0, 0); }
        if tag == N_FUNCTION {
            let t = ctx.nodes[n].token;
            if !(ctx.tokens[t].file != 0 && ctx.tokens[t].text == "main" && ctx.nodes[n].ns < 0) { register_symbol(ctx, t, ctx.nodes[n].ns, 3, n, 0, 0); }
        }
        if tag == N_BIND {
            var e = ctx.nodes[n].first;
            while e >= 0 {
                let name = ctx.edges[e].value;
                ctx.nodes[name].symbol = register_symbol(ctx, ctx.nodes[name].token, ctx.nodes[name].ns, 2, n, 0, ctx.nodes[n].flags);
                e = ctx.edges[e].next;
            }
        }
    }
}

recursive fn constant_integer(ctx: Compiler&, n: i64) -> u64 {
    let tag = ctx.nodes[n].tag;
    if tag == N_INT { return ctx.tokens[ctx.nodes[n].token].integer; }
    if tag == N_NAME {
        let s = lookup(ctx, n, 0, 2);
        if s >= 0 && ctx.symbols[s].flags & 1 == 0 {
            if ctx.symbols[s].state == 3 { error_at(ctx, ctx.nodes[n].token, "cyclic constant initializer"); }
            ctx.symbols[s].state = 3;
            let values = ctx.nodes[ctx.symbols[s].decl].b;
            if values >= 0 && ctx.nodes[values].first >= 0 {
                let result = constant_integer(ctx, ctx.edges[ctx.nodes[values].first].value);
                ctx.symbols[s].state = 0; return result;
            }
        }
    }
    if tag == N_UNARY {
        let v = constant_integer(ctx, ctx.nodes[n].a);
        let op = ctx.tokens[ctx.nodes[n].token].text;
        if op == "(" { return v; }
        if op == "-" { return 0 - v; }
        if op == "~" { return ~v; }
    }
    if tag == N_BINARY {
        let a = constant_integer(ctx, ctx.nodes[n].a); let b = constant_integer(ctx, ctx.nodes[n].b);
        let op = ctx.tokens[ctx.nodes[n].token].text;
        if op == "+" { return a + b; }
        if op == "-" { return a - b; }
        if op == "*" { return a * b; }
        if op == "/" || op == "%" {
            if b == 0 { error_at(ctx, ctx.nodes[n].token, "zero divisor in constant"); }
            if a == 9223372036854775808 && b == 18446744073709551615 { error_at(ctx, ctx.nodes[n].token, "constant division overflow"); }
            if op == "/" { return a / b; } return a % b;
        }
        if op == "<<" { return a << b; }
        if op == ">>" { return a >> b; }
        if op == "&" { return a & b; }
        if op == "|" { return a | b; }
        if op == "^" { return a ^ b; }
    }
    error_at(ctx, ctx.nodes[n].token, "expected integer constant expression");
    return 0;
}

recursive fn resolve_type(ctx: Compiler&, n: i64, scope: i64) -> i64 {
    return resolve_type_at(ctx, n, scope, false);
}
recursive fn resolve_type_at(ctx: Compiler&, n: i64, scope: i64, reference_target: bool) -> i64 {
    if n < 0 { return 0; }
    let syntax = ctx.nodes[n];
    var ty = -1;
    if syntax.tag == N_TYPE {
        ty = builtin_type(ctx, ctx.tokens[syntax.token].text);
        if ty < 0 {
            let s = lookup(ctx, n, scope, 1);
            if s < 0 { error_at(ctx, syntax.token, str("unknown type '", ctx.tokens[syntax.token].text, "'")); }
            if ctx.symbols[s].decl < 0 {
                if ctx.symbols[s].ty < 0 { error_at(ctx, syntax.token, "cannot infer type parameter"); }
                if syntax.first >= 0 { error_at(ctx, syntax.token, "a type parameter takes no type arguments"); }
                return ctx.symbols[s].ty;
            }
            let d = ctx.symbols[s].decl;
            if ctx.nodes[d].tag == N_ALIAS {
                if ctx.symbols[s].state == 1 { error_at(ctx, syntax.token, "cyclic type alias"); }
                if syntax.first >= 0 { error_at(ctx, syntax.token, "a type alias takes no type arguments"); }
                ctx.symbols[s].state = 1; ty = resolve_type_at(ctx, ctx.nodes[d].a, ctx.symbols[s].scope, reference_target); ctx.symbols[s].state = 2;
            } else {
                if ctx.nodes[d].tag == N_ENUM { error_at(ctx, syntax.token, "not implemented yet: enum type checking"); }
                ty = instantiate_struct(ctx, d, syntax.first, scope, reference_target);
            }
        }
    } else if syntax.tag == N_TYPE_ARRAY {
        if syntax.flags & 15 == 1 || syntax.flags & 15 == 2 { error_at(ctx, syntax.token, "not implemented yet: variable and limited array storage"); }
        let element = resolve_type(ctx, syntax.a, scope);
        var count = -1;
        if syntax.b >= 0 && ctx.nodes[syntax.b].tag != N_TYPE {
            let value = constant_integer(ctx, syntax.b);
            if value > 281474976710656 { error_at(ctx, syntax.token, "array size is negative or exceeds the 48-bit limit"); }
            count = value as i64;
        }
        ty = array_type(ctx, element, syntax.flags & 15, count);
        if ty < 0 { error_at(ctx, syntax.token, "array element size class is not permitted"); }
    } else if syntax.tag == N_TYPE_SLICE {
        ty = slice_type(ctx, resolve_type(ctx, syntax.a, scope), syntax.flags & 16 != 0);
    } else if syntax.tag == N_TYPE_REF || syntax.tag == N_TYPE_OPTIONAL {
        let element = resolve_type_at(ctx, syntax.a, scope, true);
        ty = reference_type(ctx, element, syntax.flags & 16 != 0);
        if syntax.b >= 0 || syntax.tag == N_TYPE_OPTIONAL { error_at(ctx, syntax.token, "not implemented yet: relative and optional references"); }
    } else { error_at(ctx, syntax.token, "not implemented yet: this type form"); }
    ctx.nodes[n].ty = ty;
    return ty;
}
fn same_type_arguments(ctx: Compiler&, a: i64, b: i64) -> bool {
    var left = a; var right = b;
    while left >= 0 && right >= 0 {
        if ctx.edges[left].value != ctx.edges[right].value { return false; }
        left = ctx.edges[left].next; right = ctx.edges[right].next;
    }
    return left == right;
}
recursive fn instantiate_struct(ctx: Compiler&, decl: i64, written: i64, caller_scope: i64, reference_target: bool) -> i64 {
    var param = ctx.nodes[ctx.nodes[decl].a].first;
    if param < 0 {
        if written >= 0 { error_at(ctx, ctx.nodes[decl].token, "non-generic struct takes no type arguments"); }
        let ty = intern_type(ctx, Type { kind: T_STRUCT, decl: decl });
        if reference_target && ctx.types[ty].state == 1 { return ty; }
        return struct_type(ctx, decl, 0);
    }
    var argument = written; var types = -1; var last = -1;
    while param >= 0 && argument >= 0 {
        if ctx.nodes[ctx.edges[param].value].a >= 0 { error_at(ctx, ctx.nodes[decl].token, "not implemented yet: constrained type parameters"); }
        let ty = resolve_type(ctx, ctx.edges[argument].value, caller_scope);
        let edge = ctx.edges.len; ctx.edges.push(Edge { value: ty });
        if last < 0 { types = edge; } else { ctx.edges[last].next = edge; } last = edge;
        param = ctx.edges[param].next; argument = ctx.edges[argument].next;
    }
    if param >= 0 || argument >= 0 { error_at(ctx, ctx.nodes[decl].token, "generic struct requires its type arguments"); }
    for t, i in ctx.types {
        if t.kind == T_STRUCT && t.template == decl && same_type_arguments(ctx, t.arguments, types) {
            if t.state == 1 && !reference_target { error_at(ctx, ctx.nodes[decl].token, "struct contains itself by value"); }
            return i;
        }
    }
    let bindings = scope_new(ctx, 0, -1);
    param = ctx.nodes[ctx.nodes[decl].a].first; argument = types;
    while param >= 0 {
        let symbol = register_symbol(ctx, ctx.nodes[ctx.edges[param].value].token, -1, 1, -1, bindings, 0);
        ctx.symbols[symbol].ty = ctx.edges[argument].value; ctx.symbols[symbol].state = 2;
        param = ctx.edges[param].next; argument = ctx.edges[argument].next;
    }
    let specialized = clone_tree(ctx, decl);
    let ty = intern_type(ctx, Type { kind: T_STRUCT, decl: specialized });
    ctx.types[ty].template = decl; ctx.types[ty].arguments = types; ctx.types[ty].scope = bindings;
    return struct_type(ctx, specialized, bindings);
}
// Const on an owned value qualifies its contents. Its var binding can still
// be replaced as a whole. References and slices also carry type qualifiers.
recursive fn syntax_readonly(ctx: Compiler&, n: i64, scope: i64) -> bool {
    if n < 0 { return false; }
    let syntax = ctx.nodes[n];
    if syntax.flags & 16 != 0 { return true; }
    if syntax.tag == N_TYPE && builtin_type(ctx, ctx.tokens[syntax.token].text) < 0 {
        let s = lookup(ctx, n, scope, 1);
        if s >= 0 && ctx.symbols[s].decl >= 0 && ctx.nodes[ctx.symbols[s].decl].tag == N_ALIAS { return syntax_readonly(ctx, ctx.nodes[ctx.symbols[s].decl].a, scope); }
    }
    return false;
}
recursive fn struct_type(ctx: Compiler&, decl: i64, scope: i64) -> i64 {
    let id = intern_type(ctx, Type { kind: T_STRUCT, decl: decl });
    if ctx.types[id].state == 2 { return id; }
    if ctx.types[id].state == 1 { error_at(ctx, ctx.nodes[decl].token, "struct contains itself by value"); }
    ctx.types[id].state = 1;
    var offset = 0;
    var size_class = 0;
    var e = ctx.nodes[decl].first;
    var last = -1;
    while e >= 0 {
        let n = ctx.edges[e].value;
        if ctx.nodes[n].tag == N_PAD { error_at(ctx, ctx.nodes[n].token, "not implemented yet: explicit padding"); }
        let ft = resolve_type(ctx, ctx.nodes[n].a, scope);
        if ctx.types[ft].size_class > 0 { error_at(ctx, ctx.nodes[n].token, "not implemented yet: nonfixed struct fields"); }
        if size_class == 2 { error_at(ctx, ctx.nodes[n].token, "resizable field must be the final field"); }
        if ctx.types[ft].size_class > size_class { size_class = ctx.types[ft].size_class; }
        let f = ctx.fielddefs.len;
        ctx.fielddefs.push(Field { token: ctx.nodes[n].token, ty: ft, initializer: ctx.nodes[n].b, flags: ctx.nodes[n].flags, offset: offset });
        let edge = ctx.edges.len; ctx.edges.push(Edge { value: f });
        if last < 0 { ctx.types[id].first = edge; } else { ctx.edges[last].next = edge; }
        last = edge;
        offset += ctx.types[ft].bytes;
        e = ctx.edges[e].next;
    }
    if offset == 0 { offset = 1; }
    if size_class > 0 { offset = -1; }
    ctx.types[id].bytes = offset; ctx.types[id].size_class = size_class; ctx.types[id].state = 2;
    return id;
}
fn find_field(ctx: Compiler&, ty: i64, token: i64) -> i64 {
    var e = ctx.types[ty].first;
    while e >= 0 { let f = ctx.edges[e].value; if same_name(ctx, ctx.fielddefs[f].token, token) { return f; } e = ctx.edges[e].next; }
    return -1;
}
fn element_type(ctx: Compiler&, ty: i64) -> i64 {
    if ctx.types[ty].kind == T_REFERENCE { return element_type_plain(ctx, ctx.types[ty].element); }
    return element_type_plain(ctx, ty);
}
fn element_type_plain(ctx: Compiler&, ty: i64) -> i64 {
    if ctx.types[ty].kind == T_ARRAY || ctx.types[ty].kind == T_SLICE { return ctx.types[ty].element; }
    return -1;
}
