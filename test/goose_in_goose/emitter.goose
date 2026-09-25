// C expressions are lowered to temporaries, preserving Goose's left-to-right
// evaluation. Newly generated snippets are owned by the text arena; existing
// views and static literals are borrowed without copying.
fn emit(ctx: Compiler&, s: const u8[:]) { ctx.c_output.append(s); }
fn save_text(ctx: Compiler&, s: const u8[:]) -> const u8[:] { return own_text(ctx, s); }
fn c_type(ctx: Compiler&, ty: i64) -> const u8[:] {
    let t = ctx.types[ty];
    if t.kind == T_VOID || t.kind == T_BOTTOM { return "void"; }
    if t.kind == T_BOOL { return "uint8_t"; }
    if t.kind == T_INTEGER {
        if t.flags & 2 != 0 { return save_text(ctx, str("uint", t.bytes * 8, "_t")); }
        return save_text(ctx, str("int", t.bytes * 8, "_t"));
    }
    if t.kind == T_FLOAT { if t.bytes == 4 { return "float"; } return "double"; }
    if t.kind == T_SLICE { return "gg_slice"; }
    if t.kind == T_ARRAY && t.flags >= 3 { return "gg_header"; }
    if t.kind == T_REFERENCE && ctx.types[t.element].size_class == 2 { return "gg_reference"; }
    return save_text(ctx, str("gg_type_", ty));
}
fn c_name(ctx: Compiler&, symbol: i64) -> const u8[:] { return save_text(ctx, str("v", symbol)); }
fn c_frame(ctx: Compiler&, target: i64) -> const u8[:] {
    if target == ctx.c_function { return "(&frame)"; }
    var out: u8[>..] = [];
    out.append("frame.parent");
    var current = ctx.scopes[ctx.symbols[ctx.specializations[ctx.c_function].symbol].scope].function;
    while current != target {
        if current < 0 { abort("internal error: missing lexical frame"); }
        out.append("->parent");
        current = ctx.scopes[ctx.symbols[ctx.specializations[current].symbol].scope].function;
    }
    return save_text(ctx, out);
}
fn c_symbol(ctx: Compiler&, symbol: i64) -> const u8[:] {
    if ctx.symbols[symbol].scope == 0 { return save_text(ctx, str("gg_globals.", c_name(ctx, symbol))); }
    let owner = ctx.scopes[ctx.symbols[symbol].scope].function;
    if ctx.return_locals[owner] == symbol { return save_text(ctx, str("(*", c_frame(ctx, owner), "->result.header)")); }
    return save_text(ctx, str(c_frame(ctx, ctx.scopes[ctx.symbols[symbol].scope].function), "->", c_name(ctx, symbol)));
}
fn c_stack(ctx: Compiler&, symbol: i64) -> const u8[:] {
    if ctx.symbols[symbol].scope == 0 { return save_text(ctx, str("gg_get_stack(", ctx.symbol_slots[symbol], ")")); }
    let owner = ctx.scopes[ctx.symbols[symbol].scope].function;
    if ctx.return_locals[owner] == symbol { return save_text(ctx, str(c_frame(ctx, owner), "->result.stack")); }
    return save_text(ctx, str("gg_get_stack(", c_frame(ctx, ctx.scopes[ctx.symbols[symbol].scope].function), "->base + ", ctx.symbol_slots[symbol], ")"));
}
fn temporary(ctx: Compiler&, ty: i64) -> const u8[:] {
    let id = ctx.c_temporary; ctx.c_temporary++;
    let name = save_text(ctx, str("t", id));
    format(ctx.c_declarations, "  ", c_type(ctx, ty), " ", name, ";\n");
    return name;
}
fn raw_temporary(ctx: Compiler&, type_name: const u8[:]) -> const u8[:] {
    let id = ctx.c_temporary; ctx.c_temporary++;
    let name = save_text(ctx, str("t", id));
    format(ctx.c_declarations, "  ", type_name, " ", name, ";\n");
    return name;
}
fn snapshot(ctx: Compiler&, v: CValue) -> CValue {
    if ctx.types[v.ty].kind == T_VOID || ctx.types[v.ty].kind == T_BOTTOM { return v; }
    let name = temporary(ctx, v.ty); emit(ctx, str("  ", name, " = ", v.code, ";\n"));
    return CValue { code: name, ty: v.ty, address: save_text(ctx, str("&", name)), stack: v.stack };
}
fn c_decay(ctx: Compiler&, v: CValue) -> CValue {
    if ctx.types[v.ty].kind != T_REFERENCE { return v; }
    let ty = ctx.types[v.ty].element;
    if ctx.types[ty].size_class == 2 { return CValue { code: save_text(ctx, str("(*(", v.code, ").header)")), ty: ty, address: save_text(ctx, str("(", v.code, ").header")), stack: save_text(ctx, str("(", v.code, ").stack")) }; }
    return CValue { code: save_text(ctx, str("(*(", v.code, "))")), ty: ty, address: v.code, stack: "" };
}
fn c_view(ctx: Compiler&, v: CValue) -> const u8[:] {
    let a = c_decay(ctx, v); let t = ctx.types[a.ty];
    if t.kind == T_SLICE { return a.code; }
    if t.kind != T_ARRAY { abort("internal error: array view expected"); }
    if t.flags == 0 { return save_text(ctx, str("gg_view((", a.code, ").data, ", t.count, ")")); }
    if t.flags >= 3 { return save_text(ctx, str("gg_view((", a.code, ").base, (", a.code, ").len)")); }
    abort("unsupported C array view");
}
fn c_reference(ctx: Compiler&, v: CValue) -> const u8[:] {
    if ctx.types[v.ty].kind == T_REFERENCE { return v.code; }
    if ctx.types[v.ty].size_class == 2 { return save_text(ctx, str("gg_ref(", v.address, ", ", v.stack, ")")); }
    return v.address;
}
fn c_adapt(ctx: Compiler&, v: CValue, target: i64) -> const u8[:] {
    if ctx.types[target].kind == T_REFERENCE { return c_reference(ctx, v); }
    let a = c_decay(ctx, v);
    if ctx.types[target].kind == T_SLICE { return c_view(ctx, a); }
    if numeric(ctx, target) { return save_text(ctx, str("((", c_type(ctx, target), ")(", a.code, "))")); }
    return a.code;
}
fn c_literal(ctx: Compiler&, s: const u8[:]) -> const u8[:] {
    var result: u8[>..] = [];
    result.push('"');
    for c in s {
        // Three octal digits cannot consume following hexadecimal digits.
        result.push('\\'); result.push(('0' + (c >> 6)) as u8);
        result.push(('0' + ((c >> 3) & 7)) as u8); result.push(('0' + (c & 7)) as u8);
    }
    result.push('"'); return save_text(ctx, result);
}
fn c_array_temp(ctx: Compiler&, ty: i64) -> CValue {
    let name = temporary(ctx, ty); let slot = ctx.c_slots; ctx.c_slots++;
    let stack = save_text(ctx, str("gg_get_stack(frame.base + ", slot, ")"));
    emit(ctx, str("  ", name, ".base = ", stack, "->base + ", stack, "->top; ", name, ".len = 0;\n"));
    return CValue { code: name, ty: ty, address: save_text(ctx, str("&", name)), stack: stack };
}

recursive fn emit_type(ctx: Compiler&, ty: i64) {
    if ctx.c_type_state[ty] == 2 { return; }
    let t = ctx.types[ty];
    if ctx.c_type_state[ty] == 1 { return; }
    ctx.c_type_state[ty] = 1;
    if t.kind == T_STRUCT {
        emit(ctx, str("typedef struct gg_type_", ty, " gg_type_", ty, ";\n"));
        var e = t.first;
        while e >= 0 { emit_type(ctx, ctx.fielddefs[ctx.edges[e].value].ty); e = ctx.edges[e].next; }
        if t.size_class != 0 { error_at(ctx, ctx.nodes[t.decl].token, "not implemented yet: nonfixed struct C layout"); }
        emit(ctx, str("#pragma pack(push, 1)\nstruct gg_type_", ty, " {\n"));
        e = t.first;
        if e < 0 { emit(ctx, "unsigned char empty;\n"); }
        while e >= 0 { let f = ctx.edges[e].value; emit(ctx, str("  ", c_type(ctx, ctx.fielddefs[f].ty), " f", f, ";\n")); e = ctx.edges[e].next; }
        emit(ctx, "};\n#pragma pack(pop)\n");
    } else if t.kind == T_ARRAY && t.flags == 0 {
        emit_type(ctx, t.element);
        var extent = t.count; if extent == 0 { extent = 1; }
        emit(ctx, str("#pragma pack(push, 1)\ntypedef struct { ", c_type(ctx, t.element), " data[", extent, "]; } ", c_type(ctx, ty), ";\n#pragma pack(pop)\n"));
    } else if t.kind == T_REFERENCE && ctx.types[t.element].size_class == 0 {
        emit_type(ctx, t.element); emit(ctx, str("typedef ", c_type(ctx, t.element), " *", c_type(ctx, ty), ";\n"));
    } else if t.kind == T_VARINT || (t.kind == T_ARRAY && (t.flags == 1 || t.flags == 2)) { abort("not implemented yet: packed variable/limited C layout"); }
    ctx.c_type_state[ty] = 2;
}

fn c_binary(ctx: Compiler&, op: const u8[:], a: CValue, b: CValue, result: i64) -> const u8[:] {
    let t = ctx.types[a.ty];
    if op == "==" || op == "!=" {
        if element_type(ctx, a.ty) >= 0 { return save_text(ctx, str(if op == "!=" { "!" } else { "" }, "gg_equal_elements_", element_type(ctx, a.ty), "(", c_view(ctx, a), ", ", c_view(ctx, b), ")")); }
        if t.kind == T_STRUCT { return save_text(ctx, str(if op == "!=" { "!" } else { "" }, "gg_eq_", a.ty, "(", a.code, ",", b.code, ")")); }
    }
    if t.kind == T_INTEGER {
        if op == "/" || op == "%" {
            let name = if t.flags & 2 != 0 { if op == "/" { "gg_udiv" } else { "gg_umod" } } else { if op == "/" { "gg_div" } else { "gg_mod" } };
            return save_text(ctx, str("((", c_type(ctx, result), ")", name, "(", a.code, ", ", b.code, "))"));
        }
        if op == "+" || op == "-" || op == "*" || op == "<<" {
            if op == "<<" { return save_text(ctx, str("((", c_type(ctx, result), ")((uint64_t)(", a.code, ") << ((uint64_t)(", b.code, ") & ", t.bytes * 8 - 1, ")))")); }
            return save_text(ctx, str("((", c_type(ctx, result), ")((uint64_t)(", a.code, ") ", op, " (uint64_t)(", b.code, ")))"));
        }
        if op == ">>" { return save_text(ctx, str("((", a.code, ") >> ((uint64_t)(", b.code, ") & ", t.bytes * 8 - 1, "))")); }
    }
    if t.kind == T_FLOAT && op == "%" { return save_text(ctx, str("fmod(", a.code, ", ", b.code, ")")); }
    return save_text(ctx, str("((", a.code, ") ", op, " (", b.code, "))"));
}

fn no_c_value(ctx: Compiler&) -> CValue { return CValue { code: "", ty: 0, address: "", stack: "" }; }
fn symbol_value(ctx: Compiler&, symbol: i64) -> CValue {
    let ty = ctx.symbols[symbol].ty; let name = c_symbol(ctx, symbol);
    var stack = -1; if ctx.types[ty].size_class == 2 { stack = symbol; }
    return CValue { code: name, ty: ty, address: save_text(ctx, str("&(", name, ")")), stack: if stack >= 0 { c_stack(ctx, stack) } else { "" } };
}
fn converted(ctx: Compiler&, v: CValue, target: i64) -> CValue {
    return CValue { code: c_adapt(ctx, v, target), ty: target, address: v.address, stack: v.stack };
}
fn c_render(ctx: Compiler&, out: CValue, v: CValue) {
    let a = c_decay(ctx, v); let t = ctx.types[a.ty]; let dst = c_reference(ctx, out);
    if t.kind == T_INTEGER { emit(ctx, str("  ", if t.flags & 2 != 0 { "gg_unsigned" } else { "gg_integer" }, "(", dst, ", ", a.code, ");\n")); }
    else if t.kind == T_FLOAT { emit(ctx, str("  gg_float(", dst, ", ", a.code, ");\n")); }
    else if t.kind == T_BOOL { emit(ctx, str("  gg_bool(", dst, ", ", a.code, ");\n")); }
    else if element_type(ctx, a.ty) == integer_type(ctx, 1, true) { emit(ctx, str("  gg_text(", dst, ", ", c_view(ctx, a), ");\n")); }
    else { emit(ctx, str("  gg_render_", a.ty, "(", dst, ",", a.code, ");\n")); }
}
recursive fn c_expression(ctx: Compiler&, n: i64) -> CValue {
    let x = ctx.nodes[n];
    let tag = x.tag;
    if tag == N_NAME { return symbol_value(ctx, x.symbol); }
    if tag == N_INT || tag == N_BOOL {
        return CValue { code: save_text(ctx, str("((", c_type(ctx, x.ty), ")UINT64_C(", x.bits, "))")), ty: x.ty, address: "", stack: "" };
    }
    // Keep the validated spelling and let the C library round it exactly.
    // This also handles overflow literals without an invalid C constant.
    if tag == N_FLOAT { return CValue { code: save_text(ctx, str("strtod(\"", ctx.tokens[x.token].text, "\", NULL)")), ty: x.ty, address: "", stack: "" }; }
    if tag == N_STRING { return CValue { code: save_text(ctx, str("gg_view((void *)", c_literal(ctx, ctx.tokens[x.token].text), ", ", ctx.tokens[x.token].text.len, ")")), ty: x.ty, address: "", stack: "" }; }
    if tag == N_UNARY {
        let a = c_expression(ctx, x.a); let op = ctx.tokens[x.token].text;
        if op == "&" { return CValue { code: c_reference(ctx, a), ty: x.ty, address: "", stack: a.stack }; }
        if op == "(" { return a; }
        let v = snapshot(ctx, c_decay(ctx, a));
        if op == "-" && ctx.types[v.ty].kind == T_INTEGER { return CValue { code: save_text(ctx, str("((", c_type(ctx, x.ty), ")((uint64_t)0 - (uint64_t)(", v.code, ")))")), ty: x.ty, address: "", stack: "" }; }
        return CValue { code: save_text(ctx, str("(", op, "(", v.code, "))")), ty: x.ty, address: "", stack: "" };
    }
    if tag == N_BINARY {
        let op = ctx.tokens[x.token].text;
        let a = snapshot(ctx, converted(ctx, c_expression(ctx, x.a), if ctx.nodes[x.a].target >= 0 { ctx.nodes[x.a].target } else { decay(ctx, value_of(ctx, x.a)).ty }));
        if op == "&&" || op == "||" {
            let result = temporary(ctx, x.ty); emit(ctx, str("  ", result, " = ", a.code, ";\n  if (", if op == "||" { "!" } else { "" }, result, ") {\n"));
            let b = c_decay(ctx, c_expression(ctx, x.b)); emit(ctx, str("  ", result, " = ", b.code, ";\n  }\n"));
            return CValue { code: result, ty: x.ty, address: save_text(ctx, str("&", result)), stack: "" };
        }
        let b = snapshot(ctx, converted(ctx, c_expression(ctx, x.b), if ctx.nodes[x.b].target >= 0 { ctx.nodes[x.b].target } else { decay(ctx, value_of(ctx, x.b)).ty }));
        return CValue { code: c_binary(ctx, op, a, b, x.ty), ty: x.ty, address: "", stack: "" };
    }
    if tag == N_CAST { return converted(ctx, c_expression(ctx, x.a), x.ty); }
    if tag == N_MEMBER {
        let a = c_decay(ctx, c_expression(ctx, x.a));
        if element_type(ctx, a.ty) >= 0 {
            return CValue { code: save_text(ctx, str("(", c_view(ctx, a), ").len")), ty: x.ty, address: "", stack: "" };
        }
        let field = save_text(ctx, str("(", a.code, ").f", x.symbol));
        return CValue { code: field, ty: x.ty, address: save_text(ctx, str("&(", field, ")")), stack: "" };
    }
    if tag == N_INDEX || tag == N_SLICE {
        let a = c_expression(ctx, x.a);
        let view = raw_temporary(ctx, "gg_slice"); emit(ctx, str("  ", view, " = ", c_view(ctx, a), ";\n"));
        var lo = CValue { code: "0", ty: default_integer(ctx), address: "", stack: "" };
        if x.b >= 0 { lo = snapshot(ctx, c_decay(ctx, c_expression(ctx, x.b))); }
        let lower = if x.flags & 1 != 0 { save_text(ctx, str(view, ".len - ", lo.code)) } else { lo.code };
        let elem = element_type(ctx, a.ty);
        if tag == N_INDEX {
            let pointer = save_text(ctx, str("((", c_type(ctx, elem), " *)", view, ".data) + gg_index(", lower, ", ", view, ".len)"));
            let sampled = raw_temporary(ctx, save_text(ctx, str(c_type(ctx, elem), " *"))); emit(ctx, str("  ", sampled, " = ", pointer, ";\n"));
            return CValue { code: save_text(ctx, str("(*", sampled, ")")), ty: x.ty, address: sampled, stack: "" };
        }
        var hi = CValue { code: save_text(ctx, str(view, ".len")), ty: default_integer(ctx), address: "", stack: "" };
        if x.c >= 0 { hi = snapshot(ctx, c_decay(ctx, c_expression(ctx, x.c))); }
        let upper = if x.flags & 2 != 0 { save_text(ctx, str(view, ".len - ", hi.code)) } else { hi.code };
        return CValue { code: save_text(ctx, str("gg_sub(", view, ", ", lower, ", ", upper, ", sizeof(", c_type(ctx, elem), "))")), ty: x.ty, address: "", stack: "" };
    }
    if tag == N_CALL { return c_call(ctx, n, no_c_value(ctx)); }
    if tag == N_ARRAY || tag == N_FILL || tag == N_CONSTRUCT {
        let name = temporary(ctx, x.ty);
        let dest = CValue { code: name, ty: x.ty, address: save_text(ctx, str("&", name)), stack: "" };
        c_into(ctx, n, dest); return dest;
    }
    if tag == N_IF || tag == N_BLOCK {
        if x.ty == 0 || ctx.types[x.ty].kind == T_BOTTOM { c_into(ctx, n, no_c_value(ctx)); return no_c_value(ctx); }
        if ctx.types[x.ty].size_class > 0 { let dest = c_array_temp(ctx, x.ty); c_into(ctx, n, dest); return dest; }
        let name = temporary(ctx, x.ty);
        let dest = CValue { code: name, ty: x.ty, address: save_text(ctx, str("&", name)), stack: "" };
        c_into(ctx, n, dest); return dest;
    }
    c_statement(ctx, n); return no_c_value(ctx);
}
recursive fn c_into(ctx: Compiler&, n: i64, dest: CValue) {
    let x = ctx.nodes[n];
    if x.tag == N_IF {
        let condition = snapshot(ctx, c_decay(ctx, c_expression(ctx, x.a))); emit(ctx, str("  if (", condition.code, ") {\n"));
        c_into(ctx, x.b, dest); emit(ctx, "  }");
        if x.c >= 0 { emit(ctx, " else {\n"); c_into(ctx, x.c, dest); emit(ctx, "  }"); }
        emit(ctx, "\n"); return;
    }
    if x.tag == N_BLOCK {
        emit(ctx, "  {\n"); var e = x.first;
        while e >= 0 { c_statement(ctx, ctx.edges[e].value); e = ctx.edges[e].next; }
        if x.a >= 0 { c_into(ctx, x.a, dest); }
        emit(ctx, "  }\n"); return;
    }
    if x.tag == N_CALL && ctx.types[x.ty].size_class == 2 && dest.ty != 0 { c_call(ctx, n, dest); return; }
    if x.tag == N_CONSTRUCT {
        var e = x.first;
        while e >= 0 {
            let init = ctx.edges[e].value; let f = ctx.nodes[init].symbol;
            let field = save_text(ctx, str("(", dest.code, ").f", f));
            c_into(ctx, ctx.nodes[init].a, CValue { code: field, ty: ctx.fielddefs[f].ty, address: save_text(ctx, str("&(", field, ")")), stack: dest.stack });
            e = ctx.edges[e].next;
        }
        return;
    }
    if x.tag == N_ARRAY || x.tag == N_FILL {
        let elem = element_type(ctx, dest.ty);
        if ctx.types[dest.ty].size_class == 2 {
            if x.tag == N_FILL {
                let value = snapshot(ctx, converted(ctx, c_expression(ctx, x.a), elem));
                let index = raw_temporary(ctx, "int64_t");
                emit(ctx, str("  for (", index, "=0; ", index, "<", ctx.types[x.ty].count, "; ", index, "++) gg_append(", c_reference(ctx, dest), ", &", value.code, ", 1, sizeof(", c_type(ctx, elem), "));\n"));
            } else {
                var e = x.first;
                while e >= 0 { let value = snapshot(ctx, converted(ctx, c_expression(ctx, ctx.edges[e].value), elem)); emit(ctx, str("  gg_append(", c_reference(ctx, dest), ", &", value.code, ", 1, sizeof(", c_type(ctx, elem), "));\n")); e = ctx.edges[e].next; }
            }
        } else {
            if x.tag == N_FILL {
                let value = snapshot(ctx, converted(ctx, c_expression(ctx, x.a), elem)); let i = raw_temporary(ctx, "int64_t");
                emit(ctx, str("  for (", i, "=0; ", i, "<", ctx.types[x.ty].count, "; ", i, "++) (", dest.code, ").data[", i, "] = ", value.code, ";\n"));
            } else {
                var e = x.first; var i = 0;
                while e >= 0 { let field = save_text(ctx, str("(", dest.code, ").data[", i, "]")); c_into(ctx, ctx.edges[e].value, CValue { code: field, ty: elem, address: save_text(ctx, str("&", field)), stack: "" }); i++; e = ctx.edges[e].next; }
            }
        }
        return;
    }
    let v = c_expression(ctx, n);
    if dest.ty == 0 || ctx.types[v.ty].kind == T_BOTTOM { if v.code.len > 0 { emit(ctx, str("  (void)(", v.code, ");\n")); } return; }
    if ctx.types[dest.ty].size_class == 2 {
        if ctx.c_function >= 0 && x.tag == N_NAME && x.symbol == ctx.return_locals[ctx.c_function] { return; }
        let view = c_view(ctx, v); emit(ctx, str("  gg_append(", c_reference(ctx, dest), ", (", view, ").data, (", view, ").len, sizeof(", c_type(ctx, element_type(ctx, dest.ty)), "));\n"));
    } else { emit(ctx, str("  ", dest.code, " = ", c_adapt(ctx, v, dest.ty), ";\n")); }
}

recursive fn c_call(ctx: Compiler&, n: i64, destination: CValue) -> CValue {
    let x = ctx.nodes[n];
    let callee = ctx.nodes[x.a];
    let builtin = x.symbol;
    if builtin <= -10 && builtin >= -14 {
        let original = c_expression(ctx, callee.a);
        let receiver = snapshot(ctx, CValue { code: c_reference(ctx, original), ty: reference_type(ctx, decay(ctx, value_of(ctx, callee.a)).ty, false), address: "", stack: "" });
        let array = c_decay(ctx, receiver); let element = element_type(ctx, array.ty);
        let ref = c_reference(ctx, array);
        let arg = first_child(ctx, n);
        if builtin == -10 {
            let v = snapshot(ctx, converted(ctx, c_expression(ctx, arg), element));
            emit(ctx, str("  gg_append(", ref, ", &", v.code, ", 1, sizeof(", c_type(ctx, element), "));\n"));
            return CValue { code: save_text(ctx, str("((", c_type(ctx, element), " *)(", array.code, ").base) + (", array.code, ").len - 1")), ty: x.ty, address: "", stack: "" };
        }
        if builtin == -11 {
            let v = snapshot(ctx, c_expression(ctx, arg)); let view = c_view(ctx, v);
            emit(ctx, str("  gg_append(", ref, ", (", view, ").data, (", view, ").len, sizeof(", c_type(ctx, element), "));\n"));
        } else if builtin == -12 {
            let count = snapshot(ctx, c_decay(ctx, c_expression(ctx, arg))); let e = ctx.edges[ctx.nodes[n].first].next;
            if e >= 0 { let fill = snapshot(ctx, converted(ctx, c_expression(ctx, ctx.edges[e].value), element)); emit(ctx, str("  gg_resize(", ref, ", ", count.code, ", sizeof(", c_type(ctx, element), "), &", fill.code, ");\n")); }
            else { emit(ctx, str("  gg_resize(", ref, ", ", count.code, ", sizeof(", c_type(ctx, element), "), NULL);\n")); }
        } else if builtin == -13 { emit(ctx, str("  gg_resize(", ref, ", 0, sizeof(", c_type(ctx, element), "), NULL);\n")); }
        else {
            let result = temporary(ctx, element);
            emit(ctx, str("  ", result, " = ((", c_type(ctx, element), " *)(", array.code, ").base)[gg_index((", array.code, ").len-1, (", array.code, ").len)];\n  gg_resize(", ref, ", (", array.code, ").len-1, sizeof(", c_type(ctx, element), "), NULL);\n"));
            return CValue { code: result, ty: element, address: save_text(ctx, str("&", result)), stack: "" };
        }
        return no_c_value(ctx);
    }
    if builtin == -20 || builtin == -21 || builtin == -22 {
        var out = destination; var e = x.first;
        if builtin == -22 { out = c_expression(ctx, ctx.edges[e].value); e = ctx.edges[e].next; }
        else if out.ty == 0 { out = c_array_temp(ctx, array_type(ctx, integer_type(ctx, 1, true), 3, -1)); }
        while e >= 0 { c_render(ctx, out, c_expression(ctx, ctx.edges[e].value)); e = ctx.edges[e].next; }
        if builtin == -20 { emit(ctx, str("  gg_append(", c_reference(ctx, out), ", \"\\n\", 1, 1);\n  gg_write(stdout, ", c_view(ctx, out), ");\n")); return no_c_value(ctx); }
        if builtin == -22 { return no_c_value(ctx); }
        return out;
    }
    if builtin == -27 {
        let name = temporary(ctx, x.ty); emit(ctx, str("  memset(&", name, ", 0, sizeof(", name, "));\n"));
        return CValue { code: name, ty: x.ty, address: save_text(ctx, str("&", name)), stack: "" };
    }
    if builtin <= -23 && builtin >= -26 {
        let arg = c_decay(ctx, c_expression(ctx, first_child(ctx, n)));
        if builtin == -26 {
            if ctx.types[arg.ty].size_class == 2 {
                var dest = destination; if dest.ty == 0 { dest = c_array_temp(ctx, arg.ty); }
                let view = c_view(ctx, arg);
                emit(ctx, str("  gg_append(", c_reference(ctx, dest), ", (", view, ").data, (", view, ").len, sizeof(", c_type(ctx, element_type(ctx, arg.ty)), "));\n"));
                return dest;
            }
            return snapshot(ctx, arg);
        }
        if builtin == -23 { emit(ctx, str("  if (!(", arg.code, ")) gg_fail(\"assertion failed\");\n")); }
        if builtin == -24 { emit(ctx, str("  gg_write(stderr, ", c_view(ctx, arg), "); gg_fail(\"abort\");\n")); }
        if builtin == -25 { emit(ctx, str("  exit((int)(", arg.code, "));\n")); }
        return no_c_value(ctx);
    }
    let spec = ctx.specializations[builtin]; let decl = ctx.nodes[ctx.symbols[spec.symbol].decl];
    let params = spec.parameters;
    // Borrow once: the watermark and restore share a stable reference path.
    let arguments = ctx.c_arguments;
    let argument_mark = arguments.len;
    var e = x.first; var p = ctx.nodes[params].first;
    while e >= 0 {
        let expected = ctx.symbols[ctx.nodes[ctx.edges[p].value].symbol].ty;
        let value = snapshot(ctx, converted(ctx, c_expression(ctx, ctx.edges[e].value), expected));
        arguments.push(value.code);
        e = ctx.edges[e].next; p = ctx.edges[p].next;
    }
    var dest = destination;
    if ctx.types[spec.result].size_class > 0 && dest.ty == 0 { dest = c_array_temp(ctx, spec.result); }
    let ret = if spec.result != 0 && ctx.types[spec.result].size_class == 0 { temporary(ctx, spec.result) } else { "" };
    emit(ctx, "  "); if ret.len > 0 { emit(ctx, str(ret, " = ")); }
    if decl.flags & 2 != 0 { emit(ctx, if decl.symbol >= 0 { ctx.tokens[decl.symbol].text } else { ctx.tokens[decl.token].text }); }
    else { emit(ctx, str("gg_function_", builtin)); }
    emit(ctx, "(");
    var comma = false;
    for i in argument_mark..arguments.len {
        if comma { emit(ctx, ", "); } comma = true;
        emit(ctx, arguments[i]);
    }
    arguments.resize(argument_mark);
    if decl.flags & 2 == 0 {
        let parent = ctx.scopes[ctx.symbols[spec.symbol].scope].function;
        if parent >= 0 { if comma { emit(ctx, ", "); } emit(ctx, c_frame(ctx, parent)); comma = true; }
        if ctx.types[spec.result].size_class > 0 { if comma { emit(ctx, ", "); } emit(ctx, c_reference(ctx, dest)); comma = true; }
        if comma { emit(ctx, ", "); } emit(ctx, str("frame.base + ", ctx.c_slots));
    }
    emit(ctx, ");\n");
    if ctx.types[spec.result].size_class > 0 { return dest; }
    return CValue { code: ret, ty: spec.result, address: save_text(ctx, str("&", ret)), stack: "" };
}

recursive fn c_statement(ctx: Compiler&, n: i64) {
    let x = ctx.nodes[n];
    if x.tag == N_FUNCTION { return; }
    if x.tag == N_BIND {
        let symbol = ctx.nodes[first_child(ctx, n)].symbol;
        let dest = symbol_value(ctx, symbol);
        if ctx.types[dest.ty].size_class == 2 && (ctx.c_function < 0 || ctx.return_locals[ctx.c_function] != symbol) {
            emit(ctx, str("  ", dest.stack, "->top = 0;\n  ", dest.code, ".base = ", dest.stack, "->base; ", dest.code, ".len = 0;\n"));
        }
        let init = first_child(ctx, x.b); if init >= 0 { c_into(ctx, init, dest); }
        return;
    }
    if x.tag == N_RETURN {
        let first = first_child(ctx, n); if first >= 0 { c_into(ctx, first, ctx.c_result); }
        emit(ctx, "  goto gg_done;\n"); return;
    }
    if x.tag == N_BREAK || x.tag == N_CONTINUE {
        var control = ctx.c_controls.len - 1;
        if x.tag == N_CONTINUE { while control >= 0 && ctx.c_controls[control].again < 0 { control--; } }
        if control < 0 { abort("internal error: no loop target"); }
        let label = if x.tag == N_CONTINUE { ctx.c_controls[control].again } else { ctx.c_controls[control].done };
        emit(ctx, str("  goto label", label, ";\n")); return;
    }
    if x.tag == N_ASSIGN {
        let raw = c_expression(ctx, x.a); let dest = c_decay(ctx, raw); let op = ctx.tokens[x.token].text;
        if op == "=" { c_into(ctx, x.b, dest); return; }
        if op == ".=" { let rhs = c_expression(ctx, x.b); emit(ctx, str("  ", raw.code, " = ", c_reference(ctx, rhs), ";\n")); return; }
        let old = snapshot(ctx, dest);
        var right = CValue { code: "1", ty: dest.ty, address: "", stack: "" };
        if x.b >= 0 { right = snapshot(ctx, converted(ctx, c_expression(ctx, x.b), dest.ty)); }
        let operation = if op == "++" { "+" } else if op == "--" { "-" } else { op[..op.len - 1] };
        emit(ctx, str("  ", dest.code, " = ", c_binary(ctx, operation, old, right, dest.ty), ";\n")); return;
    }
    if x.tag == N_WHILE || x.tag == N_LOOP || x.tag == N_EARLY || x.tag == N_FOR {
        let controls = ctx.c_controls;
        let control_mark = controls.len;
        let start = ctx.c_label; let advance = ctx.c_label + 1; let done = ctx.c_label + 2; ctx.c_label += 3;
        controls.push(Control { again: if x.tag == N_EARLY { -1 } else { advance }, done: done });
        if x.tag == N_FOR {
            let index = raw_temporary(ctx, "int64_t");
            var iterable = no_c_value(ctx); var upper = no_c_value(ctx);
            let range = ctx.nodes[x.b].tag == N_RANGE;
            if range {
                let lo = snapshot(ctx, c_decay(ctx, c_expression(ctx, ctx.nodes[x.b].a))); upper = snapshot(ctx, c_decay(ctx, c_expression(ctx, ctx.nodes[x.b].b)));
                emit(ctx, str("  ", index, " = ", lo.code, ";\n"));
            } else {
                iterable = c_decay(ctx, c_expression(ctx, x.b));
                if ctx.types[iterable.ty].kind == T_INTEGER { upper = snapshot(ctx, iterable); }
                emit(ctx, str("  ", index, " = 0;\n"));
            }
            emit(ctx, str("label", start, ":;\n"));
            let symbol = ctx.nodes[x.a].symbol;
            if upper.ty != 0 {
                emit(ctx, str("  if (", index, " >= ", upper.code, ") goto label", done, ";\n  ", c_symbol(ctx, symbol), " = ", index, ";\n"));
            } else {
                let view = c_view(ctx, iterable); let elem = element_type(ctx, iterable.ty);
                emit(ctx, str("  if (", index, " >= (", view, ").len) goto label", done, ";\n  ", c_symbol(ctx, symbol), " = "));
                if x.flags & 1 != 0 || ctx.types[elem].size_class > 0 { emit(ctx, "&"); }
                emit(ctx, str("((", c_type(ctx, elem), " *)(", view, ").data)[", index, "];\n"));
            }
            if x.first >= 0 { emit(ctx, str("  ", c_symbol(ctx, ctx.nodes[ctx.edges[x.first].value].symbol), " = ", index, ";\n")); }
            c_statement(ctx, x.c);
            emit(ctx, str("label", advance, ":;\n  ", index, "++; goto label", start, ";\n"));
        } else if x.tag == N_EARLY { c_statement(ctx, x.a); }
        else {
            emit(ctx, str("label", advance, ":;\n"));
            if x.tag == N_WHILE { let condition = c_decay(ctx, c_expression(ctx, x.a)); emit(ctx, str("  if (!(", condition.code, ")) goto label", done, ";\n")); c_statement(ctx, x.b); }
            else { c_statement(ctx, x.a); }
            emit(ctx, str("  goto label", advance, ";\n"));
        }
        emit(ctx, str("label", done, ":;\n")); controls.resize(control_mark); return;
    }
    if x.tag == N_GUARD {
        let condition = c_decay(ctx, c_expression(ctx, x.a)); emit(ctx, str("  if (!(", condition.code, ")) {\n"));
        if x.b >= 0 { c_statement(ctx, x.b); }
        else if ctx.c_controls.len > 0 { emit(ctx, str("  goto label", ctx.c_controls[ctx.c_controls.len - 1].done, ";\n")); }
        else { emit(ctx, "  goto gg_done;\n"); }
        emit(ctx, "  }\n"); return;
    }
    if x.tag == N_EXPR_STMT { c_into(ctx, x.a, no_c_value(ctx)); return; }
    c_into(ctx, n, no_c_value(ctx));
}
recursive fn returned_local(ctx: Compiler&, n: i64, spec: i64) -> i64 {
    if n < 0 { return -1; }
    let x = ctx.nodes[n];
    if x.tag == N_FUNCTION { return -1; }
    if x.tag == N_RETURN {
        let arg = first_child(ctx, n);
        if arg < 0 || ctx.nodes[arg].tag != N_NAME { return -2; }
        let symbol = ctx.nodes[arg].symbol;
        if symbol < 0 || ctx.scopes[ctx.symbols[symbol].scope].function != spec { return -2; }
        return symbol;
    }
    var result = -1;
    for sub in [x.a, x.b, x.c] {
        let found = returned_local(ctx, sub, spec);
        if found == -2 || (found >= 0 && result >= 0 && found != result) { return -2; }
        if found >= 0 { result = found; }
    }
    var e = x.first;
    while e >= 0 {
        let found = returned_local(ctx, ctx.edges[e].value, spec);
        if found == -2 || (found >= 0 && result >= 0 && found != result) { return -2; }
        if found >= 0 { result = found; }
        e = ctx.edges[e].next;
    }
    return result;
}
fn c_signature(ctx: Compiler&, spec_id: i64) {
    let spec = ctx.specializations[spec_id]; let decl = ctx.nodes[ctx.symbols[spec.symbol].decl];
    emit(ctx, str("static ", if ctx.types[spec.result].size_class > 0 { "void" } else { c_type(ctx, spec.result) }, " gg_function_", spec_id, "("));
    var e = ctx.nodes[spec.parameters].first; var comma = false;
    while e >= 0 {
        let symbol = ctx.nodes[ctx.edges[e].value].symbol;
        if comma { emit(ctx, ", "); } comma = true;
        emit(ctx, str(c_type(ctx, ctx.symbols[symbol].ty), " p", symbol)); e = ctx.edges[e].next;
    }
    let parent = ctx.scopes[ctx.symbols[spec.symbol].scope].function;
    if parent >= 0 { if comma { emit(ctx, ", "); } emit(ctx, str("struct gg_frame_", parent, " *parent")); comma = true; }
    if ctx.types[spec.result].size_class > 0 { if comma { emit(ctx, ", "); } emit(ctx, "gg_reference result"); comma = true; }
    if comma { emit(ctx, ", "); } emit(ctx, "int64_t base)");
}
fn c_frames(ctx: Compiler&) {
    emit(ctx, "static struct {\n  unsigned char dummy;\n");
    for s, i in ctx.symbols { if s.kind == 2 && s.scope == 0 { emit(ctx, str("  ", c_type(ctx, s.ty), " ", c_name(ctx, i), ";\n")); } }
    emit(ctx, "} gg_globals;\n");
    for spec, id in ctx.specializations {
        if ctx.nodes[ctx.symbols[spec.symbol].decl].flags & 2 != 0 { continue; }
        let parent = ctx.scopes[ctx.symbols[spec.symbol].scope].function;
        emit(ctx, str("struct gg_frame_", id, " {\n  int64_t base;\n"));
        if parent >= 0 { emit(ctx, str("  struct gg_frame_", parent, " *parent;\n")); }
        if ctx.types[spec.result].size_class > 0 { emit(ctx, "  gg_reference result;\n"); }
        for s, symbol in ctx.symbols {
            if s.kind == 2 && s.scope != 0 && ctx.scopes[s.scope].function == id && symbol != ctx.return_locals[id] { emit(ctx, str("  ", c_type(ctx, s.ty), " ", c_name(ctx, symbol), ";\n")); }
        }
        emit(ctx, "};\n"); c_signature(ctx, id); emit(ctx, ";\n");
    }
}
fn prepare_emission(ctx: Compiler&) {
    // Rendering itself uses byte types; close that dependency before emitting
    // the helper prototypes rather than discovering it in their bodies.
    integer_type(ctx, 1, true);
    for ty in ctx.types { ctx.c_type_state.push(0); }
    for spec, i in ctx.specializations {
        ctx.function_slots.push(0);
        var returned = -1;
        if ctx.types[spec.result].size_class > 0 { returned = returned_local(ctx, spec.body, i); }
        ctx.return_locals.push(returned);
    }
    for s, i in ctx.symbols {
        var slot = -1;
        if s.kind == 2 && s.ty >= 0 && ctx.types[s.ty].size_class == 2 {
            if s.scope == 0 { slot = ctx.c_global_slots; ctx.c_global_slots++; }
            else { let owner = ctx.scopes[s.scope].function; if ctx.return_locals[owner] != i { slot = ctx.function_slots[owner]; ctx.function_slots[owner]++; } }
        }
        ctx.symbol_slots.push(slot);
    }
}
fn emit_function(ctx: Compiler&, id: i64) {
    let spec = ctx.specializations[id]; let decl = ctx.nodes[ctx.symbols[spec.symbol].decl];
    if decl.flags & 2 != 0 { return; }
    ctx.c_function = id; ctx.c_slots = ctx.function_slots[id];
    let declaration_start = ctx.c_declarations.len;
    let signature_start = ctx.c_output.len;
    c_signature(ctx, id); emit(ctx, " {\n");
    ctx.c_final.append(ctx.c_output[signature_start..]);
    format(ctx.c_final, "  struct gg_frame_", id, " frame;\n");
    let body_start = ctx.c_output.len;
    emit(ctx, "  frame.base = base;\n");
    if ctx.scopes[ctx.symbols[spec.symbol].scope].function >= 0 { emit(ctx, "  frame.parent = parent;\n"); }
    if ctx.types[spec.result].size_class > 0 {
        emit(ctx, "  frame.result = result;\n");
        ctx.c_result = CValue { code: "(*result.header)", ty: spec.result, address: "result.header", stack: "result.stack" };
    } else if spec.result != 0 {
        let name = temporary(ctx, spec.result);
        ctx.c_result = CValue { code: name, ty: spec.result, address: save_text(ctx, str("&", name)), stack: "" };
    } else { ctx.c_result = no_c_value(ctx); }
    var e = ctx.nodes[spec.parameters].first;
    while e >= 0 { let symbol = ctx.nodes[ctx.edges[e].value].symbol; emit(ctx, str("  ", c_symbol(ctx, symbol), " = p", symbol, ";\n")); e = ctx.edges[e].next; }
    c_into(ctx, spec.body, ctx.c_result);
    emit(ctx, "gg_done:;\n");
    if ctx.c_slots > 0 {
        let k = raw_temporary(ctx, "int64_t"); emit(ctx, str("  for (", k, " = 0; ", k, " < ", ctx.c_slots, "; ", k, "++) gg_stacks[base + ", k, "].top = 0;\n"));
    }
    if spec.result != 0 && ctx.types[spec.result].size_class == 0 { emit(ctx, str("  return ", ctx.c_result.code, ";\n")); }
    emit(ctx, "}\n");
    ctx.c_final.append(ctx.c_declarations[declaration_start..]); ctx.c_final.append(ctx.c_output[body_start..]);
}
fn generate_c(ctx: Compiler&) {
    prepare_emission(ctx);
    ctx.c_final.append(runtime_source()); ctx.c_final.push('\n');
    let type_start = ctx.c_output.len;
    for t, id in ctx.types { emit_type(ctx, id); }
    c_format_helpers(ctx); c_frames(ctx); ctx.c_final.append(ctx.c_output[type_start..]);
    for spec, i in ctx.specializations { emit_function(ctx, i); }
    ctx.c_function = -1; ctx.c_slots = 0;
    let declaration_start = ctx.c_declarations.len; let body_start = ctx.c_output.len;
    emit(ctx, str("  struct { int64_t base; } frame;\n  frame.base = ", ctx.c_global_slots, ";\n  gg_argc = argc; gg_argv = argv;\n"));
    for n in ctx.global_order { c_statement(ctx, n); }
    var main_id = -1;
    for spec, i in ctx.specializations {
        let symbol = ctx.symbols[spec.symbol]; if symbol.ns < 0 && symbol.scope == 0 && ctx.tokens[symbol.token].text == "main" { main_id = i; }
    }
    emit(ctx, str("  gg_function_", main_id, "(frame.base + ", ctx.c_slots, ");\n  return 0;\n}\n"));
    ctx.c_final.append("static int gg_program(int argc, char **argv) {\n");
    ctx.c_final.append(ctx.c_declarations[declaration_start..]); ctx.c_final.append(ctx.c_output[body_start..]);
    ctx.c_final.append("int main(int argc, char **argv) {\n#ifdef _MSC_VER\n  __try { return gg_program(argc, argv); }\n  __except (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {\n    _resetstkoflw(); gg_fail(\"native call stack overflow\"); return 1;\n  }\n#else\n  return gg_program(argc, argv);\n#endif\n}\n");
}
