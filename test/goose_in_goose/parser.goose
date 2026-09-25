// Recursive descent plus precedence climbing. Recursive activations own only
// fixed-size state; list cells and syntax nodes live in the outside arenas.

fn at(ctx: Compiler&, s: const u8[:]) -> bool { ctx.tokens[ctx.cursor].kind != TK_STRING && ctx.tokens[ctx.cursor].text == s }
fn ahead(ctx: Compiler&, s: const u8[:]) -> bool { ctx.cursor + 1 < ctx.parse_limit && ctx.tokens[ctx.cursor + 1].kind != TK_STRING && ctx.tokens[ctx.cursor + 1].text == s }
fn take(ctx: Compiler&, s: const u8[:]) -> bool {
    if at(ctx, s) { ctx.cursor++; return true; }
    return false;
}
fn expect(ctx: Compiler&, s: const u8[:]) -> i64 {
    let t = ctx.cursor;
    if !take(ctx, s) { error_at(ctx, ctx.cursor, str("expected '", s, "', got '", ctx.tokens[ctx.cursor].text, "'")); }
    return t;
}
fn reserved(ctx: Compiler&, s: const u8[:]) -> bool {
    for word in ["fn", "thread_fn", "recursive", "extern", "struct", "enum", "type", "import", "namespace", "let", "var", "const", "reusable", "if", "else", "while", "for", "in", "loop", "block", "guard", "match", "return", "break", "continue", "true", "false", "null", "self", "as", "pad"] {
        if s == word { return true; }
    }
    return false;
}
fn identifier(ctx: Compiler&) -> i64 {
    let t = ctx.cursor;
    if ctx.tokens[t].kind != TK_NAME || reserved(ctx, ctx.tokens[t].text) { error_at(ctx, t, "expected identifier"); }
    ctx.cursor++;
    return t;
}
fn name_node(ctx: Compiler&, tag: i64) -> i64 {
    var explicit = false;
    var ns = ctx.current_namespace;
    var t = ctx.cursor;
    if take(ctx, "::") { ns = -1; explicit = true; t = identifier(ctx); }
    else {
        t = identifier(ctx);
        if take(ctx, "::") { ns = t; t = identifier(ctx); explicit = true; }
    }
    let n = node(ctx, tag, t);
    ctx.nodes[n].ns = ns;
    if explicit { ctx.nodes[n].flags = 1; }
    return n;
}

fn close_angle(ctx: Compiler&) {
    if take(ctx, ">") { return; }
    // Split an observed >> in place. The second character remains at cursor.
    if at(ctx, ">>") {
        ctx.tokens[ctx.cursor].text = ">";
        ctx.tokens[ctx.cursor].column++;
        ctx.tokens[ctx.cursor].offset++;
        return;
    }
    error_at(ctx, ctx.cursor, "expected '>'");
}

recursive fn parse_type(ctx: Compiler&) -> i64 {
    let qualified = take(ctx, "const");
    var applied = false;
    var n = -1;
    if take(ctx, "(") { n = parse_type(ctx); expect(ctx, ")"); }
    else if take(ctx, "fn") {
        n = node(ctx, N_TYPE, ctx.cursor - 1);
        if take(ctx, "(") {
            if !at(ctx, ")") {
                loop { child(ctx, n, parse_type(ctx)); if !take(ctx, ",") || at(ctx, ")") { break; } }
            }
            expect(ctx, ")");
            if take(ctx, "->") {
                let r = node(ctx, N_BLOCK, ctx.cursor);
                ctx.nodes[n].b = r;
                if take(ctx, "(") {
                    loop { child(ctx, r, parse_type(ctx)); if !take(ctx, ",") || at(ctx, ")") { break; } }
                    expect(ctx, ")");
                } else { child(ctx, r, parse_type(ctx)); }
            }
        }
    } else {
        n = name_node(ctx, N_TYPE);
        let spelling = ctx.tokens[ctx.nodes[n].token].text;
        let primitive = spelling == "bool" || spelling == "varint" || spelling == "i8" || spelling == "i16" || spelling == "i32" || spelling == "i64" || spelling == "u8" || spelling == "u16" || spelling == "u32" || spelling == "u64" || spelling == "f32" || spelling == "f64";
        if !primitive && take(ctx, "<") { parse_type_args(ctx, n); }
    }
    loop {
        let t = ctx.cursor;
        var outer = -1;
        if take(ctx, "[") {
            if take(ctx, ":") { outer = node(ctx, N_TYPE_SLICE, t); }
            else {
                outer = node(ctx, N_TYPE_ARRAY, t);
                if at(ctx, "]") { ctx.nodes[outer].flags = 1; }
                else if take(ctx, ">") {
                    expect(ctx, ".."); ctx.nodes[outer].flags = 3;
                    if take(ctx, "<") { ctx.nodes[outer].flags = 4; }
                } else if take(ctx, "..") {
                    ctx.nodes[outer].flags = 2;
                    if !at(ctx, "]") { ctx.nodes[outer].b = expression(ctx, 0, true, false); }
                } else if (at(ctx, "u8") || at(ctx, "u16") || at(ctx, "u32") || at(ctx, "u64") || at(ctx, "varint")) && ahead(ctx, "]") {
                    ctx.nodes[outer].flags = 1;
                    ctx.nodes[outer].b = node(ctx, N_TYPE, identifier(ctx));
                } else {
                    if (at(ctx, "i8") || at(ctx, "i16") || at(ctx, "i32") || at(ctx, "i64")) && ahead(ctx, "]") { error_at(ctx, ctx.cursor, "array length storage must be unsigned"); }
                    ctx.nodes[outer].b = expression(ctx, 0, true, false);
                }
            }
            expect(ctx, "]");
        } else if take(ctx, "&") {
            outer = node(ctx, N_TYPE_REF, t);
            if take(ctx, "<") {
                ctx.nodes[outer].b = node(ctx, N_TYPE, identifier(ctx));
                if take(ctx, "in") { ctx.nodes[outer].c = name_node(ctx, N_NAME); }
                close_angle(ctx);
            }
        } else if take(ctx, "?") {
            if ctx.nodes[n].tag == N_TYPE_OPTIONAL { error_at(ctx, t, "type is already optional"); }
            outer = node(ctx, N_TYPE_OPTIONAL, t);
        }
        else if take(ctx, "..") { outer = node(ctx, N_TYPE_MODE, t); }
        else if take(ctx, ".") { outer = node(ctx, N_TYPE_MEMBER, identifier(ctx)); }
        else { break; }
        ctx.nodes[outer].a = n;
        n = outer;
        if qualified && !applied && (ctx.nodes[n].tag == N_TYPE_REF || ctx.nodes[n].tag == N_TYPE_SLICE) {
            ctx.nodes[n].flags |= 16; applied = true;
        }
    }
    if qualified && !applied { ctx.nodes[n].flags |= 16; }
    return n;
}
recursive fn parse_type_args(ctx: Compiler&, n: i64) {
    if at(ctx, ">") || at(ctx, ">>") { error_at(ctx, ctx.cursor, "empty type argument list"); }
    loop { child(ctx, n, parse_type(ctx)); if !take(ctx, ",") || at(ctx, ">") || at(ctx, ">>") { break; } }
    close_angle(ctx);
}
fn generic_ahead(ctx: Compiler&) -> bool {
    if !at(ctx, "<") { return false; }
    var p = ctx.cursor;
    var depth = 0;
    var brackets = 0;
    while p < ctx.parse_limit {
        let s = ctx.tokens[p].text;
        if s == "[" { brackets++; }
        if s == "]" { brackets--; }
        if brackets == 0 {
            if s == "<" { depth++; }
            if s == ">" { depth--; }
            if s == ">>" { depth -= 2; }
        }
        if depth <= 0 {
            if p + 1 >= ctx.parse_limit { return false; }
            let after = ctx.tokens[p + 1].text;
            if after == "(" || after == "{" { return true; }
            return after == "." && p + 3 < ctx.parse_limit && ctx.tokens[p + 2].kind == TK_NAME && ctx.tokens[p + 3].text == "{";
        }
        if s == ";" || s == "=" || s == "}" { return false; }
        p++;
    }
    return false;
}
fn precedence(ctx: Compiler&, s: const u8[:]) -> i64 {
    if s == "||" { return 1; }
    if s == "&&" { return 2; }
    if s == "==" || s == "!=" || s == ".==" || s == ".!=" { return 3; }
    if s == "<" || s == "<=" || s == ">" || s == ">=" { return 4; }
    if s == "|" { return 5; }
    if s == "^" { return 6; }
    if s == "&" { return 7; }
    if s == "<<" || s == ">>" { return 8; }
    if s == "+" || s == "-" { return 9; }
    if s == "*" || s == "/" || s == "%" { return 10; }
    return 0;
}
fn ended(ctx: Compiler&, n: i64) -> bool {
    let t = ctx.nodes[n].tag;
    return t == N_BLOCK || t == N_IF || t == N_WHILE || t == N_FOR || t == N_LOOP || t == N_EARLY || t == N_MATCH || t == N_FUNCTION || t == N_FUNVAL || t == N_GUARD || (t == N_CALL && ctx.nodes[n].flags & 128 != 0);
}

recursive fn expression(ctx: Compiler&, minimum: i64, constructs: bool, statement: bool) -> i64 {
    ctx.parse_depth++;
    if ctx.parse_depth > 256 { error_at(ctx, ctx.cursor, "syntax nesting exceeds 256 levels"); }
    var left = primary(ctx, constructs);
    loop {
        if statement && ended(ctx, left) { break; }
        let t = ctx.cursor;
        if take(ctx, "(") {
            let n = node(ctx, N_CALL, t);
            ctx.nodes[n].a = left;
            if !at(ctx, ")") {
                loop { child(ctx, n, expression(ctx, 0, true, false)); if !take(ctx, ",") || at(ctx, ")") { break; } }
            }
            expect(ctx, ")");
            left = n;
            if constructs && at(ctx, "{") { child(ctx, n, function_value(ctx)); ctx.nodes[n].flags |= 128; }
        } else if take(ctx, ".") {
            let n = node(ctx, N_MEMBER, identifier(ctx)); ctx.nodes[n].a = left; left = n;
        } else if take(ctx, "[") {
            let n = node(ctx, N_INDEX, t);
            ctx.nodes[n].a = left;
            if take(ctx, "^") { ctx.nodes[n].flags |= 1; }
            if !at(ctx, "..") { ctx.nodes[n].b = expression(ctx, 0, true, false); }
            if take(ctx, "..") {
                ctx.nodes[n].tag = N_SLICE;
                if take(ctx, "^") { ctx.nodes[n].flags |= 2; }
                if !at(ctx, "]") { ctx.nodes[n].c = expression(ctx, 0, true, false); }
            }
            expect(ctx, "]"); left = n;
        } else if take(ctx, "as") {
            let n = node(ctx, N_CAST, t);
            ctx.nodes[n].a = left;
            if take(ctx, "!") { ctx.nodes[n].flags = 1; }
            ctx.nodes[n].b = parse_type(ctx); left = n;
        } else if (ctx.nodes[left].tag == N_NAME || ctx.nodes[left].tag == N_MEMBER) && generic_ahead(ctx) {
            expect(ctx, "<"); parse_type_args(ctx, left);
        } else if constructs && at(ctx, "{") && (ctx.nodes[left].tag == N_NAME || ctx.nodes[left].tag == N_MEMBER) {
            let n = node(ctx, N_CONSTRUCT, expect(ctx, "{")); ctx.nodes[n].a = left;
            var style = 0;
            if !at(ctx, "}") {
                loop {
                    let item = node(ctx, N_INIT, ctx.cursor);
                    if ctx.tokens[ctx.cursor].kind == TK_NAME && ahead(ctx, ":") {
                        if style == 2 { error_at(ctx, ctx.cursor, "cannot mix named and positional fields"); }
                        style = 1; ctx.nodes[item].flags = 1; identifier(ctx); expect(ctx, ":");
                    } else {
                        if style == 1 { error_at(ctx, ctx.cursor, "cannot mix named and positional fields"); }
                        style = 2;
                    }
                    ctx.nodes[item].a = expression(ctx, 0, true, false); child(ctx, n, item);
                    if !take(ctx, ",") || at(ctx, "}") { break; }
                }
            }
            expect(ctx, "}"); left = n;
        } else {
            let rank = precedence(ctx, ctx.tokens[ctx.cursor].text);
            if rank == 0 || rank < minimum { break; }
            ctx.cursor++;
            let right = expression(ctx, rank + 1, constructs, false);
            let n = node(ctx, N_BINARY, t); ctx.nodes[n].a = left; ctx.nodes[n].b = right; left = n;
        }
    }
    ctx.parse_depth--;
    return left;
}
recursive fn primary(ctx: Compiler&, constructs: bool) -> i64 {
    let t = ctx.cursor;
    let k = ctx.tokens[t].kind;
    if take(ctx, "-") || take(ctx, "!") || take(ctx, "~") || take(ctx, "&") {
        let n = node(ctx, N_UNARY, t); ctx.nodes[n].a = expression(ctx, 11, constructs, false); return n;
    }
    if take(ctx, "(") {
        // A grouping node prevents a block-ended operand from ending its
        // surrounding statement: (if c { 1 } else { 2 }) + 3.
        let n = node(ctx, N_UNARY, t); ctx.nodes[n].a = expression(ctx, 0, true, false); expect(ctx, ")"); return n;
    }
    if at(ctx, "{") { return parse_block(ctx); }
    if take(ctx, "if") {
        let n = node(ctx, N_IF, t); ctx.nodes[n].a = expression(ctx, 0, false, false); ctx.nodes[n].b = parse_block(ctx);
        if take(ctx, "else") { if at(ctx, "if") { ctx.nodes[n].c = primary(ctx, false); } else { ctx.nodes[n].c = parse_block(ctx); } }
        return n;
    }
    if take(ctx, "while") {
        let n = node(ctx, N_WHILE, t); ctx.nodes[n].a = expression(ctx, 0, false, false); ctx.nodes[n].b = parse_block(ctx); return n;
    }
    if take(ctx, "loop") || take(ctx, "block") {
        var tag = N_LOOP; if ctx.tokens[t].text == "block" { tag = N_EARLY; }
        let n = node(ctx, tag, t); ctx.nodes[n].a = parse_block(ctx); return n;
    }
    if take(ctx, "for") {
        let n = node(ctx, N_FOR, t);
        if take(ctx, "&") { ctx.nodes[n].flags = 1; }
        ctx.nodes[n].a = node(ctx, N_NAME, identifier(ctx));
        if take(ctx, ",") { child(ctx, n, node(ctx, N_NAME, identifier(ctx))); }
        expect(ctx, "in"); ctx.nodes[n].b = expression(ctx, 0, false, false);
        if take(ctx, "..") {
            let r = node(ctx, N_RANGE, ctx.cursor - 1); ctx.nodes[r].a = ctx.nodes[n].b; ctx.nodes[r].b = expression(ctx, 0, false, false); ctx.nodes[n].b = r;
        }
        ctx.nodes[n].c = parse_block(ctx); return n;
    }
    if take(ctx, "guard") {
        let n = node(ctx, N_GUARD, t); ctx.nodes[n].a = expression(ctx, 0, false, false);
        if take(ctx, "else") { ctx.nodes[n].b = parse_block(ctx); }
        else { expect(ctx, ";"); }
        return n;
    }
    if take(ctx, "return") || take(ctx, "break") || take(ctx, "continue") {
        var tag = N_RETURN;
        if ctx.tokens[t].text == "break" { tag = N_BREAK; }
        if ctx.tokens[t].text == "continue" { tag = N_CONTINUE; }
        let n = node(ctx, tag, t);
        if tag != N_CONTINUE && !at(ctx, ";") && !at(ctx, "}") && !at(ctx, "from") {
            loop { child(ctx, n, expression(ctx, 0, true, false)); if !take(ctx, ",") { break; } }
        }
        if tag == N_RETURN && take(ctx, "from") { ctx.nodes[n].a = name_node(ctx, N_NAME); }
        return n;
    }
    if take(ctx, "match") {
        let n = node(ctx, N_MATCH, t); ctx.nodes[n].a = expression(ctx, 0, false, false); expect(ctx, "{");
        while !at(ctx, "}") {
            let arm = node(ctx, N_ARM, ctx.cursor);
            if ctx.tokens[ctx.cursor].kind == TK_NAME {
                ctx.nodes[arm].a = name_node(ctx, N_NAME);
                if take(ctx, "&") { ctx.nodes[arm].flags = 1; }
                if !at(ctx, "=>") { ctx.nodes[arm].b = node(ctx, N_NAME, identifier(ctx)); }
            } else {
                ctx.nodes[arm].a = expression(ctx, 5, false, false);
                if take(ctx, "..") {
                    let r = node(ctx, N_RANGE, ctx.cursor - 1); ctx.nodes[r].a = ctx.nodes[arm].a; ctx.nodes[r].b = expression(ctx, 5, false, false); ctx.nodes[arm].a = r;
                }
            }
            expect(ctx, "=>"); ctx.nodes[arm].c = expression(ctx, 0, true, false); child(ctx, n, arm);
            if !take(ctx, ",") { break; }
        }
        expect(ctx, "}"); return n;
    }
    if take(ctx, "[") {
        let n = node(ctx, N_ARRAY, t);
        if take(ctx, "..") { ctx.nodes[n].tag = N_LIMITED; ctx.nodes[n].a = expression(ctx, 0, true, false); }
        else if !at(ctx, "]") {
            let first = expression(ctx, 0, true, false);
            if take(ctx, ";") { ctx.nodes[n].tag = N_FILL; ctx.nodes[n].a = first; ctx.nodes[n].b = expression(ctx, 0, true, false); }
            else {
                child(ctx, n, first);
                while take(ctx, ",") && !at(ctx, "]") { child(ctx, n, expression(ctx, 0, true, false)); }
            }
        }
        expect(ctx, "]"); return n;
    }
    if take(ctx, "true") || take(ctx, "false") { return node(ctx, N_BOOL, t); }
    if take(ctx, "null") { return node(ctx, N_NULL, t); }
    if take(ctx, "self") { return node(ctx, N_SELF, t); }
    if k == TK_INT { ctx.cursor++; return node(ctx, N_INT, t); }
    if k == TK_FLOAT { ctx.cursor++; return node(ctx, N_FLOAT, t); }
    if k == TK_STRING { ctx.cursor++; return node(ctx, N_STRING, t); }
    if k == TK_NAME || at(ctx, "::") { return name_node(ctx, N_NAME); }
    error_at(ctx, t, "expected expression");
    return -1;
}

recursive fn function_value(ctx: Compiler&) -> i64 {
    let n = node(ctx, N_FUNVAL, expect(ctx, "{"));
    let params = node(ctx, N_BLOCK, ctx.cursor); ctx.nodes[n].a = params;
    var p = ctx.cursor;
    while p < ctx.parse_limit && ctx.tokens[p].text != "=>" && ctx.tokens[p].text != ";" && ctx.tokens[p].text != "}" && ctx.tokens[p].text != "{" { p++; }
    if p < ctx.parse_limit && ctx.tokens[p].text == "=>" {
        loop {
            let param = node(ctx, N_PARAM, ctx.cursor);
            if take(ctx, "var") { ctx.nodes[param].flags = 1; }
            ctx.nodes[param].token = identifier(ctx);
            if take(ctx, ":") { ctx.nodes[param].a = parse_type(ctx); }
            child(ctx, params, param);
            if !take(ctx, ",") { break; }
        }
        expect(ctx, "=>");
    }
    ctx.nodes[n].b = block_contents(ctx, ctx.nodes[n].token);
    return n;
}
fn assignment_op(ctx: Compiler&, s: const u8[:]) -> bool {
    for op in ["=", ".=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "++", "--"] { if s == op { return true; } }
    return false;
}
recursive fn binding(ctx: Compiler&) -> i64 {
    let n = node(ctx, N_BIND, ctx.cursor);
    if take(ctx, "reusable") { ctx.nodes[n].flags |= 4; if take(ctx, "[") { expect(ctx, "]"); ctx.nodes[n].flags |= 8; } }
    if take(ctx, "var") { ctx.nodes[n].flags |= 1; }
    else if take(ctx, "const") { ctx.nodes[n].flags |= 2; }
    else { expect(ctx, "let"); }
    loop { child(ctx, n, name_node(ctx, N_NAME)); if !take(ctx, ",") { break; } }
    if take(ctx, ":") { ctx.nodes[n].a = parse_type(ctx); }
    if take(ctx, "=") || take(ctx, ".=") {
        if ctx.tokens[ctx.cursor - 1].text == ".=" { ctx.nodes[n].flags |= 16; }
        let values = node(ctx, N_BLOCK, ctx.cursor); ctx.nodes[n].b = values;
        loop { let value = expression(ctx, 0, true, false); ctx.nodes[value].standalone = true; child(ctx, values, value); if !take(ctx, ",") { break; } }
    }
    expect(ctx, ";"); return n;
}
recursive fn block_contents(ctx: Compiler&, t: i64) -> i64 {
    let n = node(ctx, N_BLOCK, t);
    while !at(ctx, "}") {
        if take(ctx, ";") { continue; }
        if at(ctx, "let") || at(ctx, "var") || at(ctx, "const") || at(ctx, "reusable") { child(ctx, n, binding(ctx)); continue; }
        if at(ctx, "fn") || at(ctx, "recursive") { child(ctx, n, function_decl(ctx, true)); continue; }
        let e = expression(ctx, 0, true, true);
        if assignment_op(ctx, ctx.tokens[ctx.cursor].text) {
            let a = node(ctx, N_ASSIGN, ctx.cursor); ctx.cursor++; ctx.nodes[a].a = e;
            if ctx.tokens[ctx.nodes[a].token].text != "++" && ctx.tokens[ctx.nodes[a].token].text != "--" { ctx.nodes[a].b = expression(ctx, 0, true, false); ctx.nodes[ctx.nodes[a].b].standalone = true; }
            expect(ctx, ";"); child(ctx, n, a);
        } else if take(ctx, ";") { let s = node(ctx, N_EXPR_STMT, ctx.nodes[e].token); ctx.nodes[s].a = e; ctx.nodes[e].standalone = true; child(ctx, n, s); }
        else if at(ctx, "}") { ctx.nodes[n].a = e; break; }
        else if ended(ctx, e) { child(ctx, n, e); }
        else { error_at(ctx, ctx.cursor, "expected ';' after statement"); }
    }
    expect(ctx, "}"); return n;
}
recursive fn parse_block(ctx: Compiler&) -> i64 { let t = expect(ctx, "{"); return block_contents(ctx, t); }

recursive fn generics(ctx: Compiler&, n: i64) {
    if !take(ctx, "<") { return; }
    loop {
        let g = node(ctx, N_GENERIC, identifier(ctx));
        if take(ctx, ":") { ctx.nodes[g].a = parse_type(ctx); }
        child(ctx, n, g);
        if !take(ctx, ",") || at(ctx, ">") || at(ctx, ">>") { break; }
    }
    close_angle(ctx);
}
recursive fn function_decl(ctx: Compiler&, nested: bool) -> i64 {
    var flags = 0;
    var symbol = -1;
    if take(ctx, "extern") { flags |= 2; if ctx.tokens[ctx.cursor].kind == TK_STRING { symbol = ctx.cursor; ctx.cursor++; } }
    if take(ctx, "recursive") { flags |= 1; }
    if take(ctx, "thread_fn") { flags |= 4; } else { expect(ctx, "fn"); }
    let n = name_node(ctx, N_FUNCTION);
    if nested && ctx.nodes[n].flags & 1 != 0 { error_at(ctx, ctx.nodes[n].token, "a nested function cannot name a namespace"); }
    ctx.nodes[n].flags = flags; ctx.nodes[n].symbol = symbol;
    let old_namespace = ctx.current_namespace; ctx.current_namespace = ctx.nodes[n].ns;
    generics(ctx, n);
    let params = node(ctx, N_BLOCK, expect(ctx, "(")); ctx.nodes[n].a = params;
    if !at(ctx, ")") {
        loop {
            var mutable = false; if take(ctx, "var") { mutable = true; }
            let p = node(ctx, N_PARAM, identifier(ctx));
            if mutable { ctx.nodes[p].flags = 1; }
            if take(ctx, ":") { ctx.nodes[p].a = parse_type(ctx); }
            child(ctx, params, p);
            if !take(ctx, ",") || at(ctx, ")") { break; }
        }
    }
    expect(ctx, ")");
    if take(ctx, "->") {
        let results = node(ctx, N_BLOCK, ctx.cursor); ctx.nodes[n].b = results;
        loop { child(ctx, results, parse_type(ctx)); if !take(ctx, ",") { break; } }
    }
    if flags & 2 != 0 { expect(ctx, ";"); } else { ctx.nodes[n].c = parse_block(ctx); }
    ctx.current_namespace = old_namespace;
    return n;
}
recursive fn fields(ctx: Compiler&, n: i64) {
    expect(ctx, "{");
    while !at(ctx, "}") {
        if take(ctx, "pad") {
            let p = node(ctx, N_PAD, ctx.cursor - 1);
            if ctx.tokens[ctx.cursor].kind == TK_INT { ctx.nodes[p].a = node(ctx, N_INT, ctx.cursor); ctx.cursor++; }
            child(ctx, n, p);
        } else {
            var flags = 0;
            if take(ctx, "let") { flags = 1; } else if take(ctx, "const") { flags = 2; }
            let f = node(ctx, N_FIELD, identifier(ctx)); ctx.nodes[f].flags = flags;
            expect(ctx, ":"); ctx.nodes[f].a = parse_type(ctx);
            if take(ctx, "=") { ctx.nodes[f].b = expression(ctx, 0, true, false); }
            child(ctx, n, f);
        }
        if !take(ctx, ",") { break; }
    }
    expect(ctx, "}");
}
fn parse(ctx: Compiler&, file: i64) {
    ctx.cursor = ctx.sources[file].first; ctx.parse_limit = ctx.sources[file].limit; ctx.current_namespace = -1;
    var had_namespace = false;
    var had_declaration = false;
    while ctx.tokens[ctx.cursor].kind != TK_END {
        if take(ctx, ";") { continue; }
        if take(ctx, "namespace") {
            if had_namespace || had_declaration { error_at(ctx, ctx.cursor, "namespace must precede declarations and occur once"); }
            ctx.current_namespace = identifier(ctx); had_namespace = true; expect(ctx, ";"); continue;
        }
        if take(ctx, "import") {
            let n = node(ctx, N_IMPORT, ctx.cursor - 1);
            if take(ctx, ".") { ctx.nodes[n].flags = 1; }
            loop { child(ctx, n, node(ctx, N_NAME, identifier(ctx))); if !take(ctx, ".") { break; } }
            expect(ctx, ";"); ctx.declarations.push(n); continue;
        }
        had_declaration = true;
        if at(ctx, "fn") || at(ctx, "recursive") || at(ctx, "thread_fn") || at(ctx, "extern") { ctx.declarations.push(function_decl(ctx, false)); }
        else if take(ctx, "struct") || take(ctx, "enum") {
            var tag = N_STRUCT; if ctx.tokens[ctx.cursor - 1].text == "enum" { tag = N_ENUM; }
            let n = name_node(ctx, tag);
            let old = ctx.current_namespace; ctx.current_namespace = ctx.nodes[n].ns;
            let gen = node(ctx, N_BLOCK, ctx.cursor); ctx.nodes[n].a = gen; generics(ctx, gen);
            if tag == N_STRUCT { fields(ctx, n); }
            else {
                expect(ctx, "{");
                while !at(ctx, "}") {
                    let v = node(ctx, N_VARIANT, identifier(ctx));
                    if at(ctx, "{") { fields(ctx, v); }
                    child(ctx, n, v);
                    if !take(ctx, ",") { break; }
                }
                expect(ctx, "}");
            }
            ctx.current_namespace = old; ctx.declarations.push(n);
        } else if take(ctx, "type") {
            let n = name_node(ctx, N_ALIAS); expect(ctx, "="); ctx.nodes[n].a = parse_type(ctx); expect(ctx, ";"); ctx.declarations.push(n);
        } else if at(ctx, "let") || at(ctx, "var") || at(ctx, "const") || at(ctx, "reusable") {
            let n = binding(ctx);
            if ctx.nodes[n].b < 0 { error_at(ctx, ctx.nodes[n].token, "global declarations require an initializer"); }
            ctx.declarations.push(n);
        }
        else { error_at(ctx, ctx.cursor, "expected top-level declaration"); }
    }
}
