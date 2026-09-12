// Goose compiler — the dump pass: regenerates parseable-looking source from the
// AST. This is the parser's test output; all Dump overrides live together here.
#pragma once

namespace goose {

// Tiny RTTI helpers used by all passes.
template<typename T> const T *Is(const Node *n) { return dynamic_cast<const T *>(n); }
template<typename T> T *Is(Node *n) { return dynamic_cast<T *>(n); }

inline void Indent(string &s, int ind) { s.append((size_t)ind * 4, ' '); }
inline void NL(string &s, int ind) { s += '\n'; Indent(s, ind); }

inline void EscapeString(string &s, string_view v, char quote) {
    s += quote;
    for (auto c : v) {
        switch (c) {
            case '\n': s += "\\n"; break;
            case '\t': s += "\\t"; break;
            case '\r': s += "\\r"; break;
            case 0:    s += "\\0"; break;
            case '\\': s += "\\\\"; break;
            case '"':  s += "\\\""; break;
            case '\'': s += "\\\'"; break;
            default:
                // Other control bytes as \xNN so dumps stay lexable; high bytes
                // (e.g. UTF-8) pass through raw.
                if ((uint8_t)c < 32 || c == 127) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", (uint8_t)c);
                    s += buf;
                } else {
                    s += c;
                }
        }
    }
    s += quote;
}

inline void DumpGenerics(string &s, const vector<GenericParam> &generics) {
    if (generics.empty()) return;
    s += "<";
    for (size_t i = 0; i < generics.size(); i++) {
        if (i) s += ", ";
        s += generics[i].name;
        if (generics[i].bound) { s += ": "; generics[i].bound->Dump(s); }
    }
    s += ">";
}

inline void TypeExpr::Dump(string &s) const {
    // `const` binds to the first reference or slice of the chain, else to
    // the whole type, so a base needs parens when it is const itself or when
    // this node is const and the base has a reference or slice the `const`
    // would otherwise reach first: `(const i64[3])&`, `const (u8[:])&`,
    // `const (u8[:])[3]`. A fn type base needs them to re-parse at all.
    auto chainhasrs = [](const TypeExpr *p) {
        for (; p; p = p->kind == TY_ARRAY ? p->arr->sub : nullptr)
            if (p->kind == TY_REF || p->kind == TY_SLICE) return true;
        return false;
    };
    if (cq) s += "const ";
    auto SubDump = [&](const TypeExpr *inner) {
        auto parens = inner->kind == TY_FN || inner->cq || (cq && chainhasrs(inner));
        if (parens) s += "(";
        inner->Dump(s);
        if (parens) s += ")";
    };
    auto ArgsDump = [&](const vector<TypeExpr *> &ts) {
        if (ts.empty()) return;
        s += "<";
        for (size_t i = 0; i < ts.size(); i++) {
            if (i) s += ", ";
            ts[i]->Dump(s);
        }
        s += ">";
    };
    switch (kind) {
        case TY_INT:  s += IntStorageName(intstorage); break;
        case TY_FLT:  s += FltStorageName(fltstorage); break;
        case TY_BOOL: s += "bool"; break;
        case TY_STRUCT: s += struc->st->qname; ArgsDump(struc->args); break;
        case TY_ENUM:
            s += enu->en->qname;
            ArgsDump(enu->args);
            if (enu->varmode) s += "..";
            break;
        case TY_GENERIC:
        case TY_UNRESOLVED:
            s += named->name;
            ArgsDump(named->args);
            if (named->varmode) s += "..";
            break;
        case TY_FN:
            s += "fn";
            if (fn->has_sig) {
                s += "(";
                for (size_t i = 0; i < fn->args.size(); i++) {
                    if (i) s += ", ";
                    fn->args[i]->Dump(s);
                }
                s += ")";
                if (!fn->rets.empty()) {
                    s += " -> ";
                    if (fn->rets.size() > 1) s += "(";
                    for (size_t i = 0; i < fn->rets.size(); i++) {
                        if (i) s += ", ";
                        fn->rets[i]->Dump(s);
                    }
                    if (fn->rets.size() > 1) s += ")";
                }
            }
            break;
        case TY_ARRAY:
            SubDump(arr->sub);
            switch (arr->akind) {
                case A_FIXED:
                    s += "[";
                    // Types the checker synthesizes (array literals) carry
                    // only the evaluated size.
                    if (arr->sizeexpr) arr->sizeexpr->Dump(s, 0);
                    else s += std::to_string(arr->size);
                    s += "]";
                    break;
                case A_VAR:
                    s += "[";
                    if (arr->lenstorage >= 0) s += IntStorageName(arr->lenstorage);
                    s += "]";
                    break;
                case A_LIMITED:
                    s += "[..";
                    if (arr->sizeexpr) arr->sizeexpr->Dump(s, 0);
                    s += "]";
                    break;
                case A_GROW:       s += "[>..]";  break;
                case A_GROWSHRINK: s += "[>..<]"; break;
            }
            break;
        case TY_SLICE: SubDump(sub); s += "[:]"; break;
        case TY_REF:
            SubDump(ref->sub);
            if (ref->lenstorage >= 0) {
                Append(s, "&<", IntStorageName(ref->lenstorage));
                if (!ref->poolname.empty()) Append(s, " in ", ref->poolname);
                s += ">";
                if (ref->optional) s += "?";
            } else {
                s += ref->optional ? "?" : "&";  // Canonically T?, not T&?.
            }
            break;
        case TY_VARIANT:
            SubDump(var->adt);
            s += ".";
            s += var->name;
            break;
        case TY_VOID: s += "void"; break;
    }
}

// True for nodes that end in a block and therefore need no ';' as a statement.
inline bool EndsInBlock(const Node *n) {
    if (Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<EarlyBlock>(n) || Is<While>(n) ||
        Is<LoopExpr>(n) || Is<ForLoop>(n) || Is<FnDecl>(n) || Is<Block>(n) ||
        Is<InlineBlock>(n)) return true;
    if (auto g = Is<Guard>(n)) return g->elseb != nullptr;
    if (auto c = Is<Call>(n)) return c->trailing != nullptr;
    return false;
}

// Control expressions need their own parens as operands (§2); the outer
// parens around a binary expression or cast do not group either operand.
// A postfix receiver additionally groups unary expressions so (-x).f()
// keeps its meaning, and literals: 0x10.double() lexes as a malformed hex
// float, and a constant the optimizer folds for --specs can be negative.
// Struct literals need grouping in scrutinee contexts.
inline void DumpOperand(string &s, const Node *n, int ind, bool postfix = false) {
    auto parens = EndsInBlock(n) || Is<StructLit>(n) || Is<Guard>(n) ||
                  Is<Return>(n) || Is<Break>(n) || Is<Continue>(n) ||
                  (postfix && (Is<Unary>(n) || Is<IntLit>(n) || Is<FltLit>(n)));
    if (parens) s += "(";
    n->Dump(s, ind);
    if (parens) s += ")";
}

// The statements and tail of a block, one per line, through the closing
// brace; Block and FunVal share the shape.
inline void DumpBlockBody(string &s, const Block *b, int ind) {
    for (auto st : b->stmts) {
        NL(s, ind + 1);
        st->Dump(s, ind + 1);
        if (!EndsInBlock(st)) s += ";";
    }
    if (b->tail) {
        NL(s, ind + 1);
        b->tail->Dump(s, ind + 1);
    }
    NL(s, ind);
    s += "}";
}

inline void Block::Dump(string &s, int ind) const {
    s += "{";
    DumpBlockBody(s, this, ind);
}

inline void IntLit::Dump(string &s, int) const {
    if (!text.empty()) s += text;
    else if (uns) Append(s, (uint64_t)val);
    else Append(s, val);
}

inline void FltLit::Dump(string &s, int) const {
    auto start = s.size();
    CatOne(s, val);
    // Keep it lexing as a float literal.
    if (s.find_first_of(".e", start) == string::npos) s += ".0";
}

inline void BoolLit::Dump(string &s, int) const { s += val ? "true" : "false"; }
inline void StrLit::Dump(string &s, int) const { EscapeString(s, val, '"'); }
inline void Ident::Dump(string &s, int) const { s += name; }

inline void ArrayLit::Dump(string &s, int ind) const {
    s += "[";
    if (capexpr) {
        s += "..";
        capexpr->Dump(s, ind);
        s += "]";
        return;
    }
    if (fillval) {
        fillval->Dump(s, ind);
        s += "; ";
        fillcount->Dump(s, ind);
    } else {
        for (size_t i = 0; i < elems.size(); i++) {
            if (i) s += ", ";
            elems[i]->Dump(s, ind);
        }
    }
    s += "]";
}

inline void StructLit::Dump(string &s, int ind) const {
    type->Dump(s);
    s += " { ";
    for (size_t i = 0; i < inits.size(); i++) {
        if (i) s += ", ";
        if (!inits[i].name.empty()) Append(s, inits[i].name, ": ");
        inits[i].val->Dump(s, ind);
    }
    s += inits.empty() ? "}" : " }";
}

inline void Unary::Dump(string &s, int ind) const {
    s += TName(op);
    // Parens around a nested unary keep e.g. - -x from dumping as the -- token.
    DumpOperand(s, child, ind, true);
}

inline void Binary::Dump(string &s, int ind) const {
    // Fully parenthesized: dumps double as a precedence test.
    s += "(";
    DumpOperand(s, left, ind);
    Append(s, " ", TName(op), " ");
    DumpOperand(s, right, ind);
    s += ")";
}

inline void Dot::Dump(string &s, int ind) const {
    DumpOperand(s, obj, ind, true);
    Append(s, ".", name);
}

inline void Call::Dump(string &s, int ind) const {
    DumpOperand(s, callee, ind, true);
    if (!tyargs.empty()) {
        s += "<";
        for (size_t i = 0; i < tyargs.size(); i++) {
            if (i) s += ", ";
            tyargs[i]->Dump(s);
        }
        s += ">";
    }
    s += "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i) s += ", ";
        args[i]->Dump(s, ind);
    }
    s += ")";
    if (trailing) {
        s += " ";
        trailing->Dump(s, ind);
    }
}

inline void Index::Dump(string &s, int ind) const {
    DumpOperand(s, obj, ind, true);
    s += "[";
    idx->Dump(s, ind);
    s += "]";
}

inline void SliceExpr::Dump(string &s, int ind) const {
    DumpOperand(s, obj, ind, true);
    s += "[";
    if (lo) {
        if (lo_from_end) s += "^";
        lo->Dump(s, ind);
    }
    s += "..";
    if (hi) {
        if (hi_from_end) s += "^";
        hi->Dump(s, ind);
    }
    s += "]";
}

inline void AsCast::Dump(string &s, int ind) const {
    s += "(";
    DumpOperand(s, child, ind, true);
    s += unchecked ? " as! " : " as ";
    type->Dump(s);
    s += ")";
}

inline void NullLit::Dump(string &s, int) const { s += "null"; }

inline void SelfRef::Dump(string &s, int) const { s += "self"; }

inline void RangeExpr::Dump(string &s, int ind) const {
    DumpOperand(s, lo, ind);
    s += "..";
    DumpOperand(s, hi, ind);
}

inline void IfExpr::Dump(string &s, int ind) const {
    s += "if ";
    DumpOperand(s, cond, ind);
    s += " ";
    thenb->Dump(s, ind);
    if (elseb) {
        s += " else ";
        elseb->Dump(s, ind);
    }
}

inline void MatchExpr::Dump(string &s, int ind) const {
    s += "match ";
    DumpOperand(s, scrutinee, ind);
    s += " {";
    for (auto &arm : arms) {
        NL(s, ind + 1);
        switch (arm.pat.kind) {
            case P_WILDCARD: s += "_"; break;
            case P_VARIANT:
                s += arm.pat.variant;
                if (!arm.pat.binder.empty())
                    Append(s, arm.pat.byref ? " &" : " ", arm.pat.binder);
                break;
            case P_INT: arm.pat.lo->Dump(s, ind); break;
            case P_RANGE:
                arm.pat.lo->Dump(s, ind);
                s += "..";
                arm.pat.hi->Dump(s, ind);
                break;
        }
        s += " => ";
        // The arm's comma would read as a return value or as a break's value.
        // A valueless one has no parenthesized form, so it gets the block a
        // reparse then dumps the same way.
        auto brk = Is<Break>(arm.body);
        auto ret = Is<Return>(arm.body);
        if ((brk && !brk->val) || (ret && ret->vals.empty() && ret->from.empty())) {
            s += "{";
            NL(s, ind + 2);
            arm.body->Dump(s, ind + 2);
            s += ";";
            NL(s, ind + 1);
            s += "}";
        } else if (ret) {
            s += "(";
            arm.body->Dump(s, ind + 1);
            s += ")";
        } else {
            arm.body->Dump(s, ind + 1);
        }
        s += ",";
    }
    NL(s, ind);
    s += "}";
}

inline void EarlyBlock::Dump(string &s, int ind) const {
    s += "block ";
    body->Dump(s, ind);
}

inline void While::Dump(string &s, int ind) const {
    s += "while ";
    DumpOperand(s, cond, ind);
    s += " ";
    body->Dump(s, ind);
}

inline void LoopExpr::Dump(string &s, int ind) const {
    s += "loop ";
    body->Dump(s, ind);
}

inline void ForLoop::Dump(string &s, int ind) const {
    s += "for ";
    if (byref) s += "&";
    s += var;
    if (!idxvar.empty()) Append(s, ", ", idxvar);
    s += " in ";
    DumpOperand(s, iter, ind);
    s += " ";
    body->Dump(s, ind);
}

inline void Guard::Dump(string &s, int ind) const {
    s += "guard ";
    DumpOperand(s, cond, ind);
    if (elseb) {
        s += " else ";
        elseb->Dump(s, ind);
    }
}

inline void Return::Dump(string &s, int ind) const {
    s += "return";
    for (size_t i = 0; i < vals.size(); i++) {
        s += i ? ", " : " ";
        vals[i]->Dump(s, ind);
    }
    if (!from.empty()) Append(s, " from ", from);
}

inline void Break::Dump(string &s, int ind) const {
    s += "break";
    if (val) {
        s += " ";
        // A return would read a following comma, such as a match arm's, as
        // another of its values. A valueless one has no parenthesized form.
        auto ret = Is<Return>(val);
        auto parens = ret && !(ret->vals.empty() && ret->from.empty());
        if (parens) s += "(";
        val->Dump(s, ind);
        if (parens) s += ")";
    }
}

inline void Continue::Dump(string &s, int) const { s += "continue"; }

// Optimizer output only (--specs); this form does not reparse.
inline void InlineBlock::Dump(string &s, int ind) const {
    Append(s, "inline ", sf->qname, "#", spec->id, " ");
    body->Dump(s, ind);
}

inline void FunVal::Dump(string &s, int ind) const {
    s += "{";
    if (explicit_params) {
        for (size_t i = 0; i < params.size(); i++) {
            s += i ? ", " : " ";
            s += params[i].name;
            if (params[i].type) { s += ": "; params[i].type->Dump(s); }
        }
        s += " =>";
    }
    DumpBlockBody(s, body, ind);
}

inline void VarDecl::Dump(string &s, int ind) const {
    if (reusable) s += "reusable ";
    s += isconst ? "const " : isvar ? "var " : "let ";
    // A namespaced global dumps with its qualifier: the dump merges every
    // file into one, so declarations carry their namespace themselves.
    if (isglobal && !ns.empty()) Append(s, ns, "::");
    for (size_t i = 0; i < names.size(); i++) {
        if (i) s += ", ";
        s += names[i];
    }
    if (type) { s += ": "; type->Dump(s); }
    for (size_t i = 0; i < inits.size(); i++) {
        s += i ? ", " : byref ? " .= " : " = ";
        inits[i]->Dump(s, ind);
    }
}

inline void Assign::Dump(string &s, int ind) const {
    lval->Dump(s, ind);
    Append(s, " ", TName(op), " ");
    rhs->Dump(s, ind);
}

inline void IncDec::Dump(string &s, int ind) const {
    lval->Dump(s, ind);
    s += TName(op);
}

inline void DumpFields(string &s, const vector<Field> &fields, int ind) {
    for (auto &f : fields) {
        NL(s, ind + 1);
        if (f.ispad) {
            s += "pad";
            if (f.padsize >= 0) Append(s, " ", f.padsize);
        } else {
            // `let f: const T` is written back as its sugar, `const f: T`.
            auto sugar = f.isconst && f.type->cq;
            if (f.isconst) s += sugar ? "const " : "let ";
            Append(s, f.name, ": ");
            if (sugar) {
                TypeExpr plain = *f.type;
                plain.cq = false;
                plain.Dump(s);
            } else {
                f.type->Dump(s);
            }
            if (f.defaultval) {
                s += " = ";
                f.defaultval->Dump(s, ind + 1);
            }
        }
        s += ",";
    }
}

inline void FnDecl::Dump(string &s, int ind) const {
    if (sf->isextern) {
        s += "extern ";
        if (sf->cname != sf->name) { s += "\""; s += sf->cname; s += "\" "; }
    }
    if (sf->isrec) s += "recursive ";
    s += sf->isthread ? "thread_fn " : "fn ";
    s += sf->isnested ? sf->name : sf->qname;   // See VarDecl::Dump.
    DumpGenerics(s, sf->generics);
    s += "(";
    for (size_t i = 0; i < sf->params.size(); i++) {
        if (i) s += ", ";
        if (sf->params[i].isvar) s += "var ";
        s += sf->params[i].name;
        if (sf->params[i].type) { s += ": "; sf->params[i].type->Dump(s); }
    }
    s += ")";
    if (sf->has_rets) {
        s += " -> ";
        for (size_t i = 0; i < sf->rets.size(); i++) {
            if (i) s += ", ";
            sf->rets[i]->Dump(s);
        }
    }
    if (sf->isextern) {
        s += ";";
        return;
    }
    s += " ";
    sf->body->Dump(s, ind);
}

inline void StructDecl::Dump(string &s, int ind) const {
    Append(s, "struct ", st->qname);
    DumpGenerics(s, st->generics);
    s += " {";
    DumpFields(s, st->fields, ind);
    NL(s, ind);
    s += "}";
}

inline void EnumDecl::Dump(string &s, int ind) const {
    Append(s, "enum ", en->qname);
    DumpGenerics(s, en->generics);
    s += " {";
    for (auto &v : en->variants) {
        NL(s, ind + 1);
        s += v.name;
        if (v.has_payload) {
            s += " {";
            DumpFields(s, v.fields, ind + 1);
            NL(s, ind + 1);
            s += "}";
        }
        s += ",";
    }
    NL(s, ind);
    s += "}";
}

inline void AliasDecl::Dump(string &s, int) const {
    Append(s, "type ", al->qname, " = ");
    al->type->Dump(s);
    s += ";";
}

inline void Ast::Dump(string &s) const {
    for (auto d : topdecls) {
        d->Dump(s, 0);
        if (Is<VarDecl>(d)) s += ";";  // Globals; other decl kinds self-terminate.
        s += "\n";
    }
}

}  // namespace goose
