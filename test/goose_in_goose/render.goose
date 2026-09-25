// Structural equality and formatting are generated per semantic type, rather
// than comparing packed structs as opaque bytes (padding and floats matter).
fn render_field(ctx: Compiler&, ty: i64, code: const u8[:]) {
    if element_type(ctx, ty) == integer_type(ctx, 1, true) {
        emit(ctx, str("gg_quote(out, ", c_view(ctx, CValue { code: code, ty: ty }), ");"));
    } else { emit(ctx, str("gg_render_", ty, "(out, ", code, ");")); }
}
fn c_format_helpers(ctx: Compiler&) {
    emit(ctx, "static void gg_quote(gg_reference out,gg_slice text) { int64_t i; unsigned char c; gg_append(out,\"\\\"\",1,1); for(i=0;i<text.len;i++){c=((unsigned char*)text.data)[i]; if(c=='\\\"'||c=='\\\\'){gg_append(out,\"\\\\\",1,1);gg_append(out,&c,1,1);} else if(c=='\\n')gg_append(out,\"\\\\n\",2,1);else if(c=='\\r')gg_append(out,\"\\\\r\",2,1);else if(c=='\\t')gg_append(out,\"\\\\t\",2,1);else gg_append(out,&c,1,1);}gg_append(out,\"\\\"\",1,1);}\n");
    for t, id in ctx.types {
        if t.kind == T_VOID || t.kind == T_BOTTOM { continue; }
        emit(ctx, str("static uint8_t gg_eq_", id, "(", c_type(ctx, id), " a, ", c_type(ctx, id), " b);\n"));
        emit(ctx, str("static uint8_t gg_equal_elements_", id, "(gg_slice a, gg_slice b);\n"));
        emit(ctx, str("static void gg_render_", id, "(gg_reference out, ", c_type(ctx, id), " value);\n"));
    }
    for t, id in ctx.types {
        if t.kind == T_VOID || t.kind == T_BOTTOM { continue; }
        emit(ctx, str("static uint8_t gg_equal_elements_", id, "(gg_slice a,gg_slice b) {int64_t i; if(a.len!=b.len)return 0;for(i=0;i<a.len;i++)if(!gg_eq_", id, "(((", c_type(ctx, id), "*)a.data)[i],((", c_type(ctx, id), "*)b.data)[i]))return 0;return 1;}\n"));
        emit(ctx, str("static uint8_t gg_eq_", id, "(", c_type(ctx, id), " a, ", c_type(ctx, id), " b) {\n"));
        if t.kind == T_STRUCT {
            var e = t.first;
            while e >= 0 {
                let f = ctx.edges[e].value; let ft = ctx.fielddefs[f].ty;
                if ctx.types[ft].kind == T_SLICE { emit(ctx, str("if(a.f", f, ".data!=b.f", f, ".data || a.f", f, ".len!=b.f", f, ".len)return 0;\n")); }
                else { emit(ctx, str("if(!gg_eq_", ft, "(a.f", f, ",b.f", f, "))return 0;\n")); }
                e = ctx.edges[e].next;
            }
            emit(ctx, "return 1;\n");
        } else if t.kind == T_ARRAY || t.kind == T_SLICE {
            emit(ctx, str("return gg_equal_elements_", t.element, "(", c_view(ctx, CValue { code: "a", ty: id }), ",", c_view(ctx, CValue { code: "b", ty: id }), ");\n"));
        } else if t.kind == T_REFERENCE && ctx.types[t.element].size_class == 2 { emit(ctx, "return a.header == b.header;\n"); }
        else { emit(ctx, "return a == b;\n"); }
        emit(ctx, "}\n");
        emit(ctx, str("static void gg_render_", id, "(gg_reference out, ", c_type(ctx, id), " value) {\n"));
        if t.kind == T_INTEGER { emit(ctx, str(if t.flags & 2 != 0 { "gg_unsigned" } else { "gg_integer" }, "(out,value);\n")); }
        else if t.kind == T_FLOAT { emit(ctx, "gg_float(out,value);\n"); }
        else if t.kind == T_BOOL { emit(ctx, "gg_bool(out,value);\n"); }
        else if t.kind == T_REFERENCE {
            emit(ctx, str("gg_render_", t.element, "(out,", if ctx.types[t.element].size_class == 2 { "*value.header" } else { "*value" }, ");\n"));
        } else if t.kind == T_STRUCT {
            let name = ctx.tokens[ctx.nodes[t.decl].token].text;
            let prefix = save_text(ctx, str(name, " { "));
            emit(ctx, str("gg_append(out,", c_literal(ctx, prefix), ",", prefix.len, ",1);\n"));
            var e = t.first; var first = true;
            while e >= 0 {
                let f = ctx.edges[e].value;
                if !first { emit(ctx, "gg_append(out,\", \",2,1);\n"); } first = false;
                render_field(ctx, ctx.fielddefs[f].ty, save_text(ctx, str("value.f", f))); emit(ctx, "\n"); e = ctx.edges[e].next;
            }
            emit(ctx, "gg_append(out,\" }\",2,1);\n");
        } else if t.kind == T_ARRAY || t.kind == T_SLICE {
            let view = c_view(ctx, CValue { code: "value", ty: id });
            if t.element == integer_type(ctx, 1, true) { emit(ctx, str("gg_text(out,", view, ");\n")); }
            else {
                emit(ctx, str("int64_t i; gg_slice view=", view, ";gg_append(out,\"[\",1,1);for(i=0;i<view.len;i++){if(i)gg_append(out,\", \",2,1);"));
                render_field(ctx, t.element, save_text(ctx, str("((", c_type(ctx, t.element), "*)view.data)[i]")));
                emit(ctx, "}gg_append(out,\"]\",1,1);\n");
            }
        }
        emit(ctx, "}\n");
    }
}
