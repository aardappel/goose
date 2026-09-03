// Goose compiler — recursive descent parser. One function per grammar construct,
// mirroring the grammar sketch in the spec (Appendix D). The parser builds AST
// nodes and registers top-level symbols; all resolution happens in later phases.
#pragma once

namespace goose {

// An `import a.b.c;` (path a/b/c.goose relative to the main file) or
// `import .a.b;` (relative to the importing file).
struct Import {
    string path;                     // Slash-separated, no extension.
    bool relative = false;           // Leading dot form.
};

struct Parser {
    Ast &ast;
    Lexer lex;
    int fileidx;
    vector<Import> imports;          // Imports encountered, for the driver.

    // Rust-style restriction: no struct literals / trailing blocks directly in
    // if/while/for/match/guard scrutinee position, so `if x {` is unambiguous.
    bool no_struct_lit = false;

    // Statement-level expression state: stmt_level is true only while parsing
    // the spine of a statement expression (cleared inside operands, arguments,
    // and brackets); a block-ended construct there sets stmt_ended, which
    // terminates the statement without a semicolon (the Rust rule) and stops
    // binary/postfix continuation.
    bool stmt_level = false;
    bool stmt_ended = false;

    Parser(Ast &_ast, string_view filename, const char *source, int _fileidx)
        : ast(_ast), lex(filename, source), fileidx(_fileidx) {}

    [[noreturn]] void Error(const string &msg) { lex.Error(msg); }

    Line CurLine() { return { lex.tokline, fileidx }; }

    bool IsNext(TType t) {
        if (lex.tok != t) return false;
        lex.Next();
        return true;
    }

    void Expect(TType t, const char *context) {
        if (lex.tok != t)
            Error(cat("expected \'", TName(t), "\' in ", context, ", found \'", TokStr(), "\'"));
        lex.Next();
    }

    string TokStr() {
        switch (lex.tok) {
            case T_IDENT: case T_INTLIT: case T_FLTLIT: return string(lex.attr);
            case T_STRLIT: return "string literal";
            default: return TName(lex.tok);
        }
    }

    string_view ExpectIdent(const char *context) {
        if (lex.tok != T_IDENT)
            Error(cat("expected identifier in ", context, ", found \'", TokStr(), "\'"));
        auto name = lex.attr;
        lex.Next();
        return name;
    }

    // A `>` that may be the first half of a `>>` closing two nested generic lists.
    void ExpectClosingAngle(const char *context) {
        if (lex.tok == T_SHR) { lex.tok = T_GT; return; }
        Expect(T_GT, context);
    }

    template<typename T, typename... Args> T *New(Args &&...args) {
        return ast.New<T>(std::forward<Args>(args)...);
    }

    TypeExpr *NewUnresolvedType(string_view name, Line line) {
        auto t = ast.NewType(TY_UNRESOLVED, line);
        t->named = ast.NewDetail<TypeName>();
        t->named->name = name;
        return t;
    }

    // ------------------------------------------------------------------
    // Top level.

    void ParseTop() {
        while (lex.tok != T_EOF) ParseTopDecl();
    }

    void ParseTopDecl() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_IMPORT: {
                lex.Next();
                (void)line;
                Import imp;
                imp.relative = IsNext(T_DOT);
                imp.path = ExpectIdent("import");
                while (IsNext(T_DOT)) Append(imp.path, "/", ExpectIdent("import"));
                Expect(T_SEMI, "import");
                // Not a topdecl: the driver inlines the imported file's decls,
                // which keeps a dump of the program self-contained.
                imports.push_back(imp);
                return;
            }
            case T_STRUCT: ParseStructDecl(); return;
            case T_ENUM:   ParseEnumDecl();   return;
            case T_TYPE: {
                lex.Next();
                auto al = new SAlias();
                ast.aliases.push_back(al);
                al->name = ExpectIdent("type alias");
                al->line = line;
                CheckFreshTypeName(al->name);
                Expect(T_ASSIGN, "type alias");
                al->type = ParseType();
                Expect(T_SEMI, "type alias");
                ast.aliasmap[al->name] = al;
                ast.topdecls.push_back(New<AliasDecl>(line, al));
                return;
            }
            case T_RECURSIVE: case T_FN: case T_THREADFN: {
                auto fd = ParseFnDecl(false);
                ast.topdecls.push_back(fd);
                return;
            }
            case T_REUSABLE: case T_LET: case T_VAR: {
                auto vd = ParseVarDecl(true);
                Expect(T_SEMI, "global declaration");
                ast.topdecls.push_back(vd);
                return;
            }
            default:
                Error(cat("top-level declaration expected, found \'", TokStr(), "\'"));
        }
    }

    void CheckFreshTypeName(string_view name) {
        if (ast.TypeNameExists(name)) Error(cat("type name already declared: ", name));
    }

    void ParseGenerics(vector<GenericParam> &generics) {
        if (!IsNext(T_LT)) return;
        for (;;) {
            GenericParam g;
            g.name = ExpectIdent("generic parameter list");
            if (IsNext(T_COLON)) g.bound = ParseType();
            generics.push_back(g);
            if (!IsNext(T_COMMA)) break;
            if (lex.tok == T_GT || lex.tok == T_SHR) break;
        }
        ExpectClosingAngle("generic parameter list");
    }

    void ParseStructDecl() {
        auto line = CurLine();
        lex.Next();
        auto st = new SStruct();
        ast.structs.push_back(st);
        st->name = ExpectIdent("struct declaration");
        st->line = line;
        CheckFreshTypeName(st->name);
        ParseGenerics(st->generics);
        Expect(T_LCURLY, "struct declaration");
        ParseFieldList(st->fields, "struct body");
        Expect(T_RCURLY, "struct declaration");
        ast.structmap[st->name] = st;
        ast.topdecls.push_back(New<StructDecl>(line, st));
    }

    void ParseFieldList(vector<Field> &fields, const char *context) {
        while (lex.tok != T_RCURLY) {
            Field f;
            if (IsNext(T_PAD)) {
                f.ispad = true;
                if (lex.tok == T_INTLIT) {
                    f.padsize = lex.ival;
                    lex.Next();
                }
            } else {
                f.isconst = IsNext(T_LET);
                f.name = ExpectIdent(context);
                for (auto &prev : fields)
                    if (!prev.ispad && prev.name == f.name)
                        Error(cat("duplicate field name: ", f.name));
                Expect(T_COLON, context);
                f.type = ParseType();
                if (IsNext(T_ASSIGN)) f.defaultval = ParseExpr();
            }
            fields.push_back(f);
            if (!IsNext(T_COMMA)) break;
        }
    }

    void ParseEnumDecl() {
        auto line = CurLine();
        lex.Next();
        auto en = new SEnum();
        ast.enums.push_back(en);
        en->name = ExpectIdent("enum declaration");
        en->line = line;
        CheckFreshTypeName(en->name);
        ParseGenerics(en->generics);
        Expect(T_LCURLY, "enum declaration");
        while (lex.tok != T_RCURLY) {
            SVariant v;
            v.name = ExpectIdent("enum variant");
            for (auto &prev : en->variants)
                if (prev.name == v.name) Error(cat("duplicate variant name: ", v.name));
            if (IsNext(T_LCURLY)) {
                v.has_payload = true;
                ParseFieldList(v.fields, "variant body");
                Expect(T_RCURLY, "enum variant");
            }
            en->variants.push_back(v);
            if (!IsNext(T_COMMA)) break;
        }
        Expect(T_RCURLY, "enum declaration");
        ast.enummap[en->name] = en;
        ast.topdecls.push_back(New<EnumDecl>(line, en));
    }

    FnDecl *ParseFnDecl(bool nested) {
        auto line = CurLine();
        auto sf = new SFunction();
        ast.functions.push_back(sf);
        sf->line = line;
        sf->isnested = nested;
        sf->isrec = IsNext(T_RECURSIVE);
        if (lex.tok == T_THREADFN) {
            sf->isthread = true;
            if (nested) Error("thread_fn must be declared at top level");
            lex.Next();
        } else {
            Expect(T_FN, "function declaration");
        }
        sf->name = ExpectIdent("function declaration");
        ParseGenerics(sf->generics);
        Expect(T_LPAREN, "function declaration");
        while (lex.tok != T_RPAREN) {
            Param p;
            p.isvar = IsNext(T_VAR);
            p.name = ExpectIdent("parameter list");
            for (auto &prev : sf->params)
                if (prev.name == p.name) Error(cat("duplicate parameter name: ", p.name));
            if (IsNext(T_COLON)) p.type = ParseType();
            sf->params.push_back(p);
            if (!IsNext(T_COMMA)) break;
        }
        Expect(T_RPAREN, "function declaration");
        if (IsNext(T_ARROW)) {
            // Declaration headers take a bare comma-separated return type list.
            sf->has_rets = true;
            for (;;) {
                sf->rets.push_back(ParseType());
                if (!IsNext(T_COMMA)) break;
            }
        }
        sf->body = ParseBlockExpr("function body");
        // Only the root file's `fn main` is the program entry; an imported
        // file's main is ignored entirely (§11.1), letting a runnable file
        // double as an importable library.
        if (!nested && !(sf->name == "main" && fileidx != 0))
            ast.functionmap[sf->name].push_back(sf);
        return New<FnDecl>(line, sf);
    }

    // A call that is a whole statement, initializer or assignment right-hand
    // side: the only positions where a grow-only array may shrink (§5.1).
    static void Standalone(Node *e) {
        if (auto c = Is<Call>(e)) c->standalone = true;
    }

    VarDecl *ParseVarDecl(bool isglobal) {
        auto line = CurLine();
        auto reusable = IsNext(T_REUSABLE);
        bool isvar;
        if (IsNext(T_VAR)) isvar = true;
        else if (IsNext(T_LET)) isvar = false;
        else { Error("let or var expected"); }
        auto vd = New<VarDecl>(line, isvar);
        vd->reusable = reusable;
        vd->isglobal = isglobal;
        for (;;) {
            vd->names.push_back(ExpectIdent("variable declaration"));
            if (!IsNext(T_COMMA)) break;
        }
        if (IsNext(T_COLON)) vd->type = ParseType();
        if (IsNext(T_ASSIGN)) {
            for (;;) {
                auto init = ParseExpr();
                Standalone(init);
                vd->inits.push_back(init);
                if (!IsNext(T_COMMA)) break;
            }
        }
        if (isglobal) {
            if (vd->inits.empty()) Error("global declarations require an initializer");
            if (vd->names.size() != 1) Error("global declarations declare a single name");
            auto name = vd->names[0];
            if (ast.globalmap.count(name)) Error(cat("global name already declared: ", name));
            ast.globalmap[name] = vd;
            ast.globals.push_back(vd);
        }
        return vd;
    }

    // ------------------------------------------------------------------
    // Types.

    TypeExpr *ParseType() {
        auto t = ParseTypePrimary();
        // Postfix type operators, applied left to right.
        for (;;) {
            auto line = CurLine();
            switch (lex.tok) {
                case T_LBRACKET: {
                    lex.Next();
                    if (IsNext(T_COLON)) {
                        Expect(T_RBRACKET, "slice type");
                        auto sl = ast.NewType(TY_SLICE, line);
                        sl->sub = t;
                        t = sl;
                        continue;
                    }
                    auto a = ast.NewType(TY_ARRAY, line);
                    a->arr = ast.NewDetail<TypeArray>();
                    a->arr->sub = t;
                    if (IsNext(T_RBRACKET)) { a->arr->akind = A_VAR; t = a; continue; }
                    if (IsNext(T_DOTDOT)) {
                        a->arr->akind = A_LIMITED;
                        if (lex.tok != T_RBRACKET) a->arr->sizeexpr = ParseTypeSizeExpr();
                    } else if (IsNext(T_GT)) {
                        Expect(T_DOTDOT, "resizable array type");
                        a->arr->akind = IsNext(T_LT) ? A_GROWSHRINK : A_GROW;
                    } else if (IsPrimTypeToken(lex.tok)) {
                        a->arr->akind = A_VAR;
                        a->arr->lenstorage = ParseLengthStorage("array length type");
                    } else {
                        a->arr->akind = A_FIXED;
                        a->arr->sizeexpr = ParseTypeSizeExpr();
                    }
                    Expect(T_RBRACKET, "array type");
                    t = a;
                    continue;
                }
                case T_BITAND: {
                    lex.Next();
                    auto r = ast.NewType(TY_REF, line);
                    r->ref = ast.NewDetail<TypeRef>();
                    r->ref->sub = t;
                    if (IsNext(T_LT)) {
                        r->ref->lenstorage = ParseLengthStorage("relative reference width");
                        ExpectClosingAngle("relative reference width");
                    }
                    t = r;
                    continue;
                }
                case T_QUESTION: {
                    lex.Next();
                    if (t->kind == TY_REF) {
                        // T&? is the same type as T?; the reference becomes nullable.
                        if (t->ref->optional) Error("type is already optional");
                        t->ref->optional = true;
                    } else {
                        auto o = ast.NewType(TY_REF, line);
                        o->ref = ast.NewDetail<TypeRef>();
                        o->ref->sub = t;
                        o->ref->optional = true;
                        t = o;
                    }
                    continue;
                }
                case T_DOTDOT: {
                    lex.Next();
                    // Only ADTs have a variable mode; at parse time those are names.
                    if (t->kind != TY_UNRESOLVED)
                        Error("variable mode (..) applies only to ADT type names");
                    if (t->named->varmode) Error("type is already variable-mode");
                    t->named->varmode = true;
                    continue;
                }
                case T_DOT: {
                    lex.Next();
                    auto v = ast.NewType(TY_VARIANT, line);
                    v->var = ast.NewDetail<TypeVariant>();
                    v->var->adt = t;
                    v->var->name = ExpectIdent("variant type");
                    t = v;
                    continue;
                }
                default:
                    return t;
            }
        }
    }

    // Array length fields and relative reference widths: unsigned storage
    // types and varint only (varint is unsigned-encoded in this position).
    int ParseLengthStorage(const char *context) {
        int s;
        switch (lex.tok) {
            case T_TU8:     s = IS_U8;     break;
            case T_TU16:    s = IS_U16;    break;
            case T_TU32:    s = IS_U32;    break;
            case T_TU64:    s = IS_U64;    break;
            case T_TVARINT: s = IS_VARINT; break;
            default:
                Error(cat("unsigned integer storage type or varint expected in ", context,
                          ", found \'", TokStr(), "\'"));
        }
        lex.Next();
        return s;
    }

    // A constant expression in type position: T[k] sizes, T[..k] capacities.
    Node *ParseTypeSizeExpr() {
        auto save = no_struct_lit;
        auto sub = EnterSub();
        no_struct_lit = false;
        auto e = ParseExpr();
        no_struct_lit = save;
        LeaveSub(sub);
        return e;
    }

    TypeExpr *ParseTypePrimary() {
        auto line = CurLine();
        if (IsPrimTypeToken(lex.tok)) {
            auto t = ast.PrimTypeForToken(lex.tok);
            lex.Next();
            return t;
        }
        switch (lex.tok) {
            case T_IDENT: {
                auto t = NewUnresolvedType(lex.attr, line);
                lex.Next();
                if (IsNext(T_LT)) {
                    for (;;) {
                        t->named->args.push_back(ParseType());
                        if (!IsNext(T_COMMA)) break;
                        if (lex.tok == T_GT || lex.tok == T_SHR) break;
                    }
                    ExpectClosingAngle("type argument list");
                }
                return t;
            }
            case T_FN: {
                lex.Next();
                auto t = ast.NewType(TY_FN, line);
                t->fn = ast.NewDetail<TypeFn>();
                if (IsNext(T_LPAREN)) {
                    t->fn->has_sig = true;
                    while (lex.tok != T_RPAREN) {
                        t->fn->args.push_back(ParseType());
                        if (!IsNext(T_COMMA)) break;
                    }
                    Expect(T_RPAREN, "function type");
                    if (IsNext(T_ARROW)) {
                        // Multiple return types in a function *type* need parens.
                        if (IsNext(T_LPAREN)) {
                            while (lex.tok != T_RPAREN) {
                                t->fn->rets.push_back(ParseType());
                                if (!IsNext(T_COMMA)) break;
                            }
                            Expect(T_RPAREN, "function type return list");
                        } else {
                            t->fn->rets.push_back(ParseType());
                        }
                    }
                }
                return t;
            }
            case T_LPAREN: {
                // Grouping, e.g. (Sexp..&<varint>)[varint].
                lex.Next();
                auto t = ParseType();
                Expect(T_RPAREN, "parenthesized type");
                return t;
            }
            default:
                Error(cat("type expected, found \'", TokStr(), "\'"));
        }
    }

    // ------------------------------------------------------------------
    // Expressions.

    // Sub-expression contexts (operands, arguments, bracketed positions) leave
    // the statement spine; block-ended constructs inside them don't end the
    // statement.
    bool EnterSub() {
        auto save = stmt_level;
        stmt_level = false;
        stmt_ended = false;
        return save;
    }
    void LeaveSub(bool save) { stmt_level = save; }

    Node *ParseExpr() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_IF: {
                auto e = ParseIf();
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_MATCH: {
                auto e = ParseMatch();
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_BLOCK: {
                lex.Next();
                auto e = New<EarlyBlock>(line, ParseBlockExpr("block expression"));
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_WHILE: {
                lex.Next();
                auto cond = ParseScrutinee();
                auto e = New<While>(line, cond, ParseBlockExpr("while body"));
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_LOOP: {
                lex.Next();
                auto e = New<LoopExpr>(line, ParseBlockExpr("loop body"));
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_FOR: {
                auto e = ParseFor();
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_GUARD: {
                lex.Next();
                auto cond = ParseScrutinee();
                auto elseb = IsNext(T_ELSE) ? ParseBlockExpr("guard else") : nullptr;
                if (stmt_level && elseb) stmt_ended = true;
                return New<Guard>(line, cond, elseb);
            }
            case T_RETURN: {
                lex.Next();
                auto sub = EnterSub();
                auto r = New<Return>(line);
                if (lex.tok != T_SEMI && lex.tok != T_FROM && lex.tok != T_RCURLY) {
                    for (;;) {
                        r->vals.push_back(ParseExpr());
                        if (!IsNext(T_COMMA)) break;
                    }
                }
                if (IsNext(T_FROM)) r->from = ExpectIdent("return from");
                LeaveSub(sub);
                return r;
            }
            case T_BREAK: {
                lex.Next();
                auto sub = EnterSub();
                auto val = lex.tok != T_SEMI && lex.tok != T_RCURLY ? ParseExpr() : nullptr;
                LeaveSub(sub);
                return New<Break>(line, val);
            }
            case T_CONTINUE:
                lex.Next();
                return New<Continue>(line);
            default:
                return ParseBinary(1);
        }
    }

    // Scrutinee positions (if/while/for/match/guard headers) disallow struct
    // literals and trailing blocks so the following `{` reads as the body.
    Node *ParseScrutinee() {
        auto save = no_struct_lit;
        auto sub = EnterSub();
        no_struct_lit = true;
        auto e = ParseExpr();
        no_struct_lit = save;
        LeaveSub(sub);
        return e;
    }

    Node *ParseIf() {
        auto line = CurLine();
        lex.Next();
        auto cond = ParseScrutinee();
        auto thenb = ParseBlockExpr("if body");
        Node *elseb = nullptr;
        if (IsNext(T_ELSE))
            elseb = lex.tok == T_IF ? ParseIf() : ParseBlockExpr("else body");
        return New<IfExpr>(line, cond, thenb, elseb);
    }

    Node *ParseFor() {
        auto line = CurLine();
        lex.Next();
        auto byref = IsNext(T_BITAND);
        auto var = ExpectIdent("for binding");
        string_view idxvar;
        if (IsNext(T_COMMA)) idxvar = ExpectIdent("for index binding");
        Expect(T_IN, "for statement");
        auto save = no_struct_lit;
        no_struct_lit = true;
        auto iter = ParseExpr();
        if (IsNext(T_DOTDOT)) iter = New<RangeExpr>(line, iter, ParseExpr());
        no_struct_lit = save;
        return New<ForLoop>(line, byref, var, idxvar, iter, ParseBlockExpr("for body"));
    }

    Node *ParseMatch() {
        auto line = CurLine();
        lex.Next();
        auto m = New<MatchExpr>(line, ParseScrutinee());
        Expect(T_LCURLY, "match expression");
        auto sub = EnterSub();
        while (lex.tok != T_RCURLY) {
            MatchArm arm;
            arm.pat = ParsePattern();
            Expect(T_FATARROW, "match arm");
            arm.body = ParseExpr();
            m->arms.push_back(arm);
            if (!IsNext(T_COMMA)) break;
        }
        LeaveSub(sub);
        Expect(T_RCURLY, "match expression");
        if (m->arms.empty()) Error("match must have at least one arm");
        return m;
    }

    Pattern ParsePattern() {
        Pattern p;
        if (lex.tok == T_IDENT) {
            if (lex.attr == "_") {
                p.kind = P_WILDCARD;
                lex.Next();
            } else {
                p.kind = P_VARIANT;
                p.variant = lex.attr;
                lex.Next();
                if (IsNext(T_BITAND)) {
                    // `Variant &b`: an explicit by-reference payload binding.
                    p.byref = true;
                    p.binder = ExpectIdent("match binding");
                } else if (lex.tok == T_IDENT) {
                    p.binder = lex.attr;
                    lex.Next();
                }
            }
            return p;
        }
        p.kind = P_INT;
        p.lo = ParsePatternInt();
        if (IsNext(T_DOTDOT)) {
            p.kind = P_RANGE;
            p.hi = ParsePatternInt();
        }
        return p;
    }

    Node *ParsePatternInt() {
        auto line = CurLine();
        auto neg = IsNext(T_MINUS);
        if (lex.tok != T_INTLIT)
            Error(cat("integer constant expected in match pattern, found \'", TokStr(), "\'"));
        if (neg && lex.iuns && lex.ival != INT64_MIN)
            Error("negated literal too large for i64");
        auto n = New<IntLit>(line, neg ? -lex.ival : lex.ival,
                             neg ? string_view {} : lex.attr, !neg && lex.iuns);
        lex.Next();
        return n;
    }

    int BinPrec(TType t) {
        switch (t) {
            case T_MUL: case T_DIV: case T_MOD:                return 10;
            case T_PLUS: case T_MINUS:                         return 9;
            case T_SHL: case T_SHR:                            return 8;
            case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:    return 7;
            case T_EQ: case T_NEQ:                             return 6;
            case T_BITAND:                                     return 5;
            case T_XOR:                                        return 4;
            case T_BITOR:                                      return 3;
            case T_ANDAND:                                     return 2;
            case T_OROR:                                       return 1;
            default:                                           return 0;
        }
    }

    Node *ParseBinary(int minprec) {
        auto l = ParseUnary();
        for (;;) {
            if (stmt_ended) return l;  // A statement-ending trailing block: stop.
            auto prec = BinPrec(lex.tok);
            if (prec < minprec || !prec) return l;
            auto op = lex.tok;
            auto line = CurLine();
            lex.Next();
            auto sub = EnterSub();
            auto r = ParseBinary(prec + 1);  // All binary operators left-associate.
            LeaveSub(sub);
            l = New<Binary>(line, op, l, r);
        }
    }

    Node *ParseUnary() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_MINUS: case T_NOT: case T_BITNOT: case T_BITAND: {
                auto op = lex.tok;
                lex.Next();
                auto sub = EnterSub();
                auto e = New<Unary>(line, op, ParseUnary());
                LeaveSub(sub);
                return e;
            }
            default:
                return ParsePostfix();
        }
    }

    Node *ParsePostfix() {
        auto e = ParsePrimary();
        for (;;) {
            if (stmt_ended) return e;  // A statement-ending trailing block: stop.
            auto line = CurLine();
            switch (lex.tok) {
                case T_LPAREN:
                    lex.Next();
                    e = ParseCallRest(e, {}, line);
                    continue;
                case T_LBRACKET:
                    e = ParseIndexOrSlice(e, line);
                    continue;
                case T_DOT: {
                    lex.Next();
                    auto name = ExpectIdent("field access");
                    // `Ident.Ident {` is a variant literal; anything else a Dot
                    // (fields, UFCS, payload-less variant constants).
                    if (lex.tok == T_LCURLY && !no_struct_lit && Is<Ident>(e)) {
                        auto base = NewUnresolvedType(Is<Ident>(e)->name, e->line);
                        auto vt = ast.NewType(TY_VARIANT, line);
                        vt->var = ast.NewDetail<TypeVariant>();
                        vt->var->adt = base;
                        vt->var->name = name;
                        e = ParseStructLitBody(vt, line);
                    } else {
                        e = New<Dot>(line, e, name);
                    }
                    continue;
                }
                case T_AS: {
                    lex.Next();
                    auto unchecked = IsNext(T_NOT);
                    e = New<AsCast>(line, e, ParseType(), unchecked);
                    continue;
                }
                default:
                    return e;
            }
        }
    }

    // Call arguments; the opening paren has been consumed.
    Node *ParseCallRest(Node *callee, vector<TypeExpr *> tyargs, Line line) {
        auto c = New<Call>(line, callee);
        c->tyargs = std::move(tyargs);
        auto save = no_struct_lit;
        auto sub = EnterSub();
        no_struct_lit = false;
        while (lex.tok != T_RPAREN) {
            c->args.push_back(ParseExpr());
            if (!IsNext(T_COMMA)) break;
        }
        Expect(T_RPAREN, "call arguments");
        no_struct_lit = save;
        LeaveSub(sub);
        if (lex.tok == T_LCURLY && !no_struct_lit) {
            c->trailing = ParseFunVal();
            // In statement position a trailing block ends the statement.
            if (stmt_level) stmt_ended = true;
        }
        return c;
    }

    Node *ParseIndexOrSlice(Node *obj, Line line) {
        lex.Next();
        auto save = no_struct_lit;
        auto sub = EnterSub();
        no_struct_lit = false;
        Node *lo = nullptr;
        auto lo_from_end = false;
        auto isslice = lex.tok == T_DOTDOT;
        if (!isslice) {
            lo_from_end = IsNext(T_XOR);
            lo = ParseExpr();
            isslice = lex.tok == T_DOTDOT;
        }
        Node *result;
        if (isslice) {
            lex.Next();
            auto sl = New<SliceExpr>(line, obj);
            sl->lo = lo;
            sl->lo_from_end = lo_from_end;
            if (lex.tok != T_RBRACKET) {
                sl->hi_from_end = IsNext(T_XOR);
                sl->hi = ParseExpr();
            }
            result = sl;
        } else {
            if (lo_from_end) Error("\'^\' bounds are only valid in slices");
            result = New<Index>(line, obj, lo);
        }
        Expect(T_RBRACKET, "indexing");
        no_struct_lit = save;
        LeaveSub(sub);
        return result;
    }

    Node *ParsePrimary() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_INTLIT: {
                auto n = New<IntLit>(line, lex.ival, lex.attr, lex.iuns);
                lex.Next();
                return n;
            }
            case T_FLTLIT: {
                auto n = New<FltLit>(line, lex.fval);
                lex.Next();
                return n;
            }
            case T_STRLIT: {
                auto n = New<StrLit>(line, lex.sval);
                lex.Next();
                return n;
            }
            case T_TRUE:  lex.Next(); return New<BoolLit>(line, true);
            case T_FALSE: lex.Next(); return New<BoolLit>(line, false);
            case T_NULLLIT: lex.Next(); return New<NullLit>(line);
            case T_SELF:  lex.Next(); return New<SelfRef>(line);
            case T_LPAREN: {
                lex.Next();
                auto save = no_struct_lit;
                auto sub = EnterSub();
                no_struct_lit = false;
                auto e = ParseExpr();
                Expect(T_RPAREN, "parenthesized expression");
                no_struct_lit = save;
                LeaveSub(sub);
                return e;
            }
            case T_LBRACKET: return ParseArrayLit(line);
            case T_IDENT: {
                auto name = lex.attr;
                lex.Next();
                if (lex.tok == T_LT) {
                    // Possibly f<T>(...), Name<T> { ... }, or the variant
                    // literal Name<T>.Variant { ... }; backtrack if not.
                    vector<TypeExpr *> tyargs;
                    if (TryParseTyArgs(tyargs)) {
                        if (IsNext(T_LPAREN))
                            return ParseCallRest(New<Ident>(line, name), std::move(tyargs), line);
                        auto t = NewUnresolvedType(name, line);
                        t->named->args = std::move(tyargs);
                        if (IsNext(T_DOT)) {
                            auto vt = ast.NewType(TY_VARIANT, line);
                            vt->var = ast.NewDetail<TypeVariant>();
                            vt->var->adt = t;
                            vt->var->name = ExpectIdent("variant literal");
                            return ParseStructLitBody(vt, line);
                        }
                        return ParseStructLitBody(t, line);
                    }
                }
                if (lex.tok == T_LCURLY && !no_struct_lit) {
                    return ParseStructLitBody(NewUnresolvedType(name, line), line);
                }
                return New<Ident>(line, name);
            }
            default:
                Error(cat("expression expected, found \'", TokStr(), "\'"));
        }
    }

    // Tentatively parse `<` type, ... `>` immediately followed by `(` or `{`;
    // restores the lexer and returns false when it is really a comparison.
    bool TryParseTyArgs(vector<TypeExpr *> &tyargs) {
        auto save = lex;
        lex.Next();  // The '<'.
        try {
            for (;;) {
                tyargs.push_back(ParseType());
                if (!IsNext(T_COMMA)) break;
                if (lex.tok == T_GT || lex.tok == T_SHR) break;
            }
            if (lex.tok == T_SHR) {
                lex.tok = T_GT;
            } else if (lex.tok == T_GT) {
                lex.Next();
            } else {
                lex = save;
                tyargs.clear();
                return false;
            }
            if (lex.tok == T_LPAREN || (lex.tok == T_LCURLY && !no_struct_lit)) return true;
            if (lex.tok == T_DOT && !no_struct_lit) {
                // Name<T>.Variant { ... }: commit only when the full variant
                // literal head is present.
                auto save2 = lex;
                lex.Next();
                if (lex.tok == T_IDENT) {
                    lex.Next();
                    if (lex.tok == T_LCURLY) {
                        lex = save2;  // Leave the '.' for the caller.
                        return true;
                    }
                }
                lex = save2;
            }
        } catch (CompileError &) {}
        lex = save;
        tyargs.clear();
        return false;
    }

    Node *ParseArrayLit(Line line) {
        lex.Next();
        auto a = New<ArrayLit>(line);
        auto save = no_struct_lit;
        auto sub = EnterSub();
        no_struct_lit = false;
        if (lex.tok == T_DOTDOT) {
            // [..cap]: an empty limited array with a construction-time
            // capacity (§5.3).
            lex.Next();
            a->capexpr = ParseExpr();
        } else if (lex.tok != T_RBRACKET) {
            auto first = ParseExpr();
            if (IsNext(T_SEMI)) {
                a->fillval = first;
                a->fillcount = ParseExpr();
            } else {
                a->elems.push_back(first);
                while (IsNext(T_COMMA) && lex.tok != T_RBRACKET)
                    a->elems.push_back(ParseExpr());
            }
        }
        Expect(T_RBRACKET, "array literal");
        no_struct_lit = save;
        LeaveSub(sub);
        return a;
    }

    StructLit *ParseStructLitBody(TypeExpr *type, Line line) {
        Expect(T_LCURLY, "struct literal");
        auto sl = New<StructLit>(line, type);
        auto save = no_struct_lit;
        auto sub = EnterSub();
        no_struct_lit = false;
        while (lex.tok != T_RCURLY) {
            FieldInit fi;
            if (lex.tok == T_IDENT) {
                // Only an `ident :` pair is a named initializer.
                auto save2 = lex;
                auto name = lex.attr;
                lex.Next();
                if (IsNext(T_COLON)) fi.name = name; else lex = save2;
            }
            fi.val = ParseExpr();
            sl->inits.push_back(fi);
            if (!IsNext(T_COMMA)) break;
        }
        Expect(T_RCURLY, "struct literal");
        no_struct_lit = save;
        LeaveSub(sub);
        auto named = 0;
        for (auto &fi : sl->inits) named += !fi.name.empty();
        if (named && named != (int)sl->inits.size())
            Error("struct literal mixes named and positional initializers");
        return sl;
    }

    FunVal *ParseFunVal() {
        auto line = CurLine();
        Expect(T_LCURLY, "function value");
        auto b = New<Block>(line);
        auto fv = New<FunVal>(line, b);
        if (lex.tok == T_IDENT || lex.tok == T_VAR) {
            // Tentative `params =>` prefix; else the body starts right away
            // with an implicit `it` parameter.
            auto save = lex;
            vector<Param> params;
            auto ok = true;
            try {
                for (;;) {
                    Param p;
                    p.isvar = IsNext(T_VAR);
                    if (lex.tok != T_IDENT) { ok = false; break; }
                    p.name = lex.attr;
                    lex.Next();
                    if (IsNext(T_COLON)) p.type = ParseType();
                    params.push_back(p);
                    if (!IsNext(T_COMMA)) break;
                }
                if (!ok || !IsNext(T_FATARROW)) ok = false;
            } catch (CompileError &) { ok = false; }
            if (ok) {
                fv->params = std::move(params);
                fv->explicit_params = true;
            } else {
                lex = save;
            }
        }
        ParseBlockBody(b);
        return fv;
    }

    // ------------------------------------------------------------------
    // Blocks and statements.

    Block *ParseBlockExpr(const char *context) {
        auto line = CurLine();
        Expect(T_LCURLY, context);
        auto b = New<Block>(line);
        ParseBlockBody(b);
        return b;
    }

    // The '{' has been consumed; consumes the closing '}'.
    void ParseBlockBody(Block *b) {
        auto save_nsl = no_struct_lit;
        auto save_sl = stmt_level;
        auto save_se = stmt_ended;
        no_struct_lit = false;
        for (;;) {
            stmt_level = false;
            stmt_ended = false;
            if (IsNext(T_RCURLY)) break;
            switch (lex.tok) {
                case T_LET: case T_VAR: case T_REUSABLE: {
                    auto vd = ParseVarDecl(false);
                    Expect(T_SEMI, "variable declaration");
                    b->stmts.push_back(vd);
                    continue;
                }
                case T_RECURSIVE: case T_FN: case T_THREADFN:
                    b->stmts.push_back(ParseFnDecl(true));
                    continue;
                default: break;
            }
            stmt_level = true;
            auto e = ParseExpr();
            stmt_level = false;
            auto line = CurLine();
            if (!stmt_ended) {
                switch (lex.tok) {
                    case T_ASSIGN: case T_DOTASSIGN: case T_PLUSEQ: case T_MINUSEQ:
                    case T_MULEQ: case T_DIVEQ: case T_MODEQ: case T_ANDEQ: case T_OREQ:
                    case T_XOREQ: {
                        auto op = lex.tok;
                        lex.Next();
                        auto rhs = ParseExpr();
                        Expect(T_SEMI, "assignment statement");
                        if (Is<Ident>(e)) Standalone(rhs);
                        b->stmts.push_back(New<Assign>(line, op, e, rhs));
                        continue;
                    }
                    case T_INC: case T_DEC: {
                        auto op = lex.tok;
                        lex.Next();
                        Expect(T_SEMI, "increment statement");
                        b->stmts.push_back(New<IncDec>(line, op, e));
                        continue;
                    }
                    default: break;
                }
            }
            if (IsNext(T_SEMI)) {  // A ';' is always accepted, even when optional.
                Standalone(e);
                b->stmts.push_back(e);
                continue;
            }
            if (IsNext(T_RCURLY)) {
                Standalone(e);
                b->tail = e;  // Trailing expression: the block's value.
                break;
            }
            // Block-ended constructs stand as statements without a semicolon.
            if (stmt_ended) {
                Standalone(e);
                b->stmts.push_back(e);
                continue;
            }
            Error(cat("\';\' expected after expression, found \'", TokStr(), "\'"));
        }
        no_struct_lit = save_nsl;
        stmt_level = save_sl;
        stmt_ended = save_se;
    }
};

}  // namespace goose
